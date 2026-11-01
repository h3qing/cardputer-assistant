"""G-A1: HWCDC sustained throughput with hwtest !xfer (docs/gates/m3-device-gates.md).

Pure part: configurations, per-second KB/s, run summaries and the two decisions the gate
feeds (TX ring size; whether v0.3.x adds a binary sub-mode, audio-capture §7.3).
I/O part: run_xfer() drives one !xfer over a GateLink.
"""

from __future__ import annotations

import time
from dataclasses import asdict, dataclass

from gate_stats import kbs, minutes_per_hour, summarize

ENCS = ("b64", "bin")
RINGS = (4, 8, 16)
SRCS = ("ram", "sd", "sdkeep")


@dataclass(frozen=True)
class XferConfig:
    enc: str
    length: int
    depth: int
    ring_kb: int
    src: str
    secs: int = 10

    def __post_init__(self):
        ok = (
            self.enc in ENCS
            and 1 <= self.length <= 4096
            and 0 <= self.depth <= 16
            and self.ring_kb in RINGS
            and self.src in SRCS
            and 1 <= self.secs <= 120
        )
        if not ok:
            raise ValueError(f"bad xfer config {self!r}")

    def command(self):
        return f"!xfer {self.enc} {self.length} {self.depth} {self.ring_kb} {self.src} {self.secs}"

    def label(self):
        return f"{self.enc}-{self.length}-d{self.depth}-r{self.ring_kb}-{self.src}"

    def as_dict(self):
        return asdict(self)


def _sweep():
    rows = []
    for src in ("ram", "sd"):
        for ring in (4, 8):
            rows += [XferConfig("b64", 1380, d, ring, src) for d in (1, 2, 4, 0)]
        rows += [XferConfig("bin", 1380, 4, 4, src)]
        for ring in (8, 16):
            rows += [XferConfig("bin", 4096, d, ring, src) for d in (1, 4, 0)]
    return tuple(rows)


# b64 1380 is the app's file.data (11 §5.3); bin 4096 is the binary sub-mode's frame
# (audio-capture §7.3), which needs an 8 KB ring; bin 1380 isolates the encoding.
SWEEP = _sweep()
SOAK = (XferConfig("b64", 1380, 4, 4, "sd", 60), XferConfig("bin", 4096, 4, 8, "sd", 60))
# Per-request open/seek/read/close against one open file, on a segment-sized file (9.6 MB):
# the read pattern of the app's audio task (11 §4.5, §4.7).
SD_SWEEP = tuple(
    XferConfig(enc, length, 4, ring, src, 20)
    for src in ("sd", "sdkeep")
    for enc, length, ring in (("b64", 1380, 4), ("bin", 4096, 8))
)


def per_second_kbs(rx):
    """KB/s in each whole second after the first frame; rx = [(t_rx, payload_bytes)]."""
    if not rx:
        return []
    t0 = rx[0][0]
    full = int(rx[-1][0] - t0)
    buckets = [0] * full
    for t, n in rx:
        i = int(t - t0)
        if i < full:
            buckets[i] += n
    return [b / 1024 for b in buckets]


def _cpu(xs, key):
    vals = [x[key] for x in xs if key in x]
    if not vals:
        return None
    return {"mean": round(sum(vals) / len(vals), 1), "max": max(vals)}


def summarize_run(run):
    rx, end = run["rx"], run["end"]
    span = rx[-1][0] - rx[0][0] if len(rx) > 1 else 0
    rate = kbs(sum(n for _, n in rx[1:]), span)
    gaps = [b[0] - a[0] for a, b in zip(rx, rx[1:])]
    ok = bool(end) and not end.get("err") and run["crc_bad"] == 0 and run["framing_errors"] == 0 and rate is not None
    return {
        "ok": ok,
        "kbs": round(rate, 1) if rate else None,
        "wire_kbs": round(kbs(run["wire_bytes"], span), 1) if span else None,
        "kbs_per_s": [round(v, 1) for v in per_second_kbs(rx)],
        "min_per_hour": round(minutes_per_hour(rate), 2) if rate else None,
        "frames": len(rx),
        "gap_max_ms": round(max(gaps) * 1000, 1) if gaps else None,
        "rtt": summarize(run["rtt_ms"]),
        "cpu0": _cpu(run["xs"], "cpu0"),
        "cpu1": _cpu(run["xs"], "cpu1"),
        "crc_bad": run["crc_bad"],
        "framing_errors": run["framing_errors"],
        "rearms": (end or {}).get("rearms"),
        "err": (end or {}).get("err", "no xfer_end"),
    }


def _best(rows, enc):
    ok = [r for r in rows if r["config"]["enc"] == enc and r["summary"]["ok"] and r["summary"]["kbs"]]
    sd = [r for r in ok if r["config"]["src"] == "sd"]
    pool = sd or ok
    return max((r["summary"]["kbs"] for r in pool), default=None)


def decide_encoding(rows):
    """audio-capture §7.3: b64 >= 320 KB/s -> b64 only; else bin >= 1.3x b64 -> add bin;
    else both < 190 KB/s -> look at ADPCM; else b64 only. Best SD-sourced runs compared."""
    b64, binr = _best(rows, "b64"), _best(rows, "bin")
    if b64 is not None and b64 >= 320:
        choice, why = "b64", "b64 >= 320 KB/s"
    elif b64 and binr and binr >= 1.3 * b64:
        choice, why = "bin", "bin >= 1.3 x b64"
    elif (b64 or 0) < 190 and (binr or 0) < 190:
        choice, why = "adpcm?", "both < 190 KB/s"
    else:
        choice, why = "b64", "bin gain < 1.3 x"
    return {"choice": choice, "why": why, "b64_kbs": b64, "bin_kbs": binr}


def choose_ring(rows, gain=1.10):
    """App frame (b64 1380, 4 in flight): 8 KB only when it is >= 10% faster than 4 KB."""
    best = {}
    for r in rows:
        c, s = r["config"], r["summary"]
        if c["enc"] == "b64" and c["length"] == 1380 and c["depth"] == 4 and s["ok"] and s["kbs"]:
            best[c["ring_kb"]] = max(best.get(c["ring_kb"], 0), s["kbs"])
    if 4 not in best or 8 not in best:
        return {"ring_kb": None, "kbs": best}
    return {"ring_kb": 8 if best[8] >= gain * best[4] else 4, "kbs": best}


# ---- I/O ------------------------------------------------------------------------------


class _Pinger:
    def __init__(self, link, every=1.0):
        self.link, self.every, self.sent, self.rtts, self.next_id = link, every, {}, [], 1
        self.next_at = time.perf_counter() + 0.5

    def tick(self, now):
        if now >= self.next_at:
            self.sent[self.next_id] = now
            self.link.send(f"!ping {self.next_id}")
            self.next_id += 1
            self.next_at = now + self.every

    def pong(self, ev):
        t = self.sent.pop(ev.get("id"), None)
        if t is not None:
            self.rtts.append(round((ev["_rx"] - t) * 1000, 1))


def _absorb(evs, cfg, run, pinger):
    credits = 0
    for ev in evs:
        kind = ev.get("t")
        if kind == "file.data":
            run["rx"].append((ev["_rx"], ev["_n"] if ev["_ok"] else 0))
            credits += 1
        elif kind == "pong":
            pinger.pong(ev)
        elif kind == "xs":
            run["xs"].append({k: v for k, v in ev.items() if not k.startswith("_")})
        elif kind == "xfer_end":
            run["end"] = {k: v for k, v in ev.items() if not k.startswith("_")}
    return credits if cfg.depth else 0


def run_xfer(link, cfg):
    reader = link.reset_reader()
    link.send(cfg.command())
    start = link.wait_for("xfer_start", 8.0)
    run = {"start": start, "rx": [], "xs": [], "end": None, "rtt_ms": []}
    if start and not start.get("err") and cfg.depth:
        link.send_raw(b"!g\n" * cfg.depth)
    pinger, deadline, quiet_since = _Pinger(link), time.perf_counter() + cfg.secs + 12, time.perf_counter()
    while run["end"] is None and start and time.perf_counter() < deadline:
        evs = link.poll()
        now = time.perf_counter()
        quiet_since = now if evs else quiet_since
        credits = _absorb(evs, cfg, run, pinger)
        if credits:
            link.send_raw(b"!g\n" * credits)
        if run["end"] is None and now - quiet_since > 2.0:
            link.send("")  # nudge a parked reply (ADR-0001 §5.2)
            quiet_since = now
        if run["end"] is None:
            pinger.tick(now)
    run.update(rtt_ms=pinger.rtts, crc_bad=reader.crc_bad, framing_errors=reader.framing_errors,
               wire_bytes=reader.wire_bytes)
    return run
