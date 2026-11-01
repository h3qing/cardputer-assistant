"""SD card bring-up runner for the hwtest firmware (docs/gates/sd-card.md).

Safe-opens the port (hwtest_host.open_port), handshakes, runs the requested steps and
prints / saves one JSON report under --out. Replies parked by the core 2.0.17 HWCDC are
released with an empty-line nudge (ADR-0001 §5.2); the firmware answers it with a
harmless {"t":"rx","len":0}.

    uv run --with pyserial python sd_run.py --label probe --steps info,freq
    steps: hz:<kHz> info freq format clear bench outbox rename rec[:secs:buf:nbuf:sync_ms]
           layout tree
"""

import argparse
import base64
import json
import math
import os
import struct
import subprocess
import sys
import time
import zlib
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hwtest_host import find_port, open_port  # noqa: E402

# step -> (command, reply type, replies expected, timeout s)
SIMPLE = {
    "info": ("!sdinfo", "sdinfo", 1, 180),
    "freq": ("!sdfreq", "sdfreq", 5, 600),
    "format": ("!sdformat", "sdformat", 1, 900),
    "clear": ("!sdclear", "sdclear", 1, 900),
    "bench": ("!sdbench", "sdbench", 3, 600),
    "outbox": ("!sdoutbox 200", "sdoutbox", 3, 1800),
    "rename": ("!sdrename", "sdrename", 1, 60),
    "layout": ("!sdlayout", "sdlayout", 1, 120),
    "tree": ("!sdtree", "sdtree", 1, 60),
}
WAV_PATH = "/sd/cpa_hwtest/rec.wav"
CHUNK = 12288


class Link:
    def __init__(self, ser):
        self.ser, self.buf = ser, b""

    def send(self, text):
        self.ser.write((text + "\n").encode())

    def events(self, secs, until=None):
        """JSON objects arriving within secs (stamped with "_rx"); returns early once until(ev)."""
        out, deadline = [], time.perf_counter() + secs
        while time.perf_counter() < deadline:
            self.buf += self.ser.read(max(1, self.ser.in_waiting))  # returns on the first byte
            *lines, self.buf = self.buf.split(b"\n")
            for raw in lines:
                try:
                    ev = json.loads(raw.strip() or b"null")
                except ValueError:
                    continue
                if isinstance(ev, dict):
                    ev["_rx"] = time.perf_counter()
                    out.append(ev)
            if until and any(until(e) for e in out):
                break
        return out

    def collect(self, kind, count, timeout, on_event=None):
        """count replies of one type; nudges with an empty line after 2 s of silence."""
        got, deadline = [], time.perf_counter() + timeout
        while len(got) < count and time.perf_counter() < deadline:
            evs = self.events(2.0, until=lambda e: e.get("t") == kind)
            if not evs:
                self.send("")
            for ev in evs:
                rx = ev.pop("_rx")
                if on_event:
                    on_event(ev, rx)
                if ev.get("t") == kind:
                    got.append(ev)
        return got


def handshake(link, tries=3):
    for attempt in range(1, tries + 1):
        link.send("!info")
        hello = [e for e in link.events(2.0, until=lambda e: e.get("t") == "hello") if e.get("t") == "hello"]
        if hello:
            hello[-1].pop("_rx")
            return {"ok": True, "attempts": attempt, "hello": hello[-1]}
    return {"ok": False, "attempts": tries}


def run_simple(link, name):
    cmd, kind, count, timeout = SIMPLE[name]
    t0 = time.perf_counter()
    link.send(cmd)
    replies = link.collect(kind, count, timeout)
    return {"step": name, "ms": round((time.perf_counter() - t0) * 1000), "replies": replies}


def run_rec(link, args):
    parts = (args + ":60:2048:4:1000").split(":")[:4]
    secs = int(parts[0])
    sent, rtts, prog, final = {}, [], [], []
    t0 = time.perf_counter()
    link.send("!sdrec " + " ".join(parts))
    ping_id, deadline = 0, t0 + secs + 60
    while not final and time.perf_counter() < deadline:
        ping_id += 1
        sent[ping_id] = time.perf_counter()
        link.send(f"!ping {ping_id}")
        for ev in link.events(1.0):
            rx = ev.pop("_rx")
            if ev.get("t") == "pong" and ev.get("id") in sent:
                rtts.append(round((rx - sent[ev["id"]]) * 1000, 1))
            elif ev.get("t") == "sdrec_prog":
                prog.append(ev)
            elif ev.get("t") == "sdrec":
                final.append(ev)
    return {
        "step": "rec",
        "ms": round((time.perf_counter() - t0) * 1000),
        "reply": final[0] if final else None,
        "progress": prog,
        "pings_sent": ping_id,
        "pongs": len(rtts),
        "rtt_ms": summary(rtts),
    }


def summary(values):
    if not values:
        return None
    ordered = sorted(values)

    def rank(p):
        return ordered[max(1, math.ceil(p / 100 * len(ordered))) - 1]

    late = sum(1 for v in values if v > 100)
    return {"n": len(values), "p50": rank(50), "p95": rank(95), "max": ordered[-1], "over_100ms": late, "all": values}


def pull(link, path, dest):
    data, off, size, retries = bytearray(), 0, None, 0
    t0 = time.perf_counter()
    while size is None or off < size:
        link.send(f"!sdget {path} {off} {CHUNK}")
        rep = link.collect("chunk", 1, 10)
        ev = rep[0] if rep else {}
        raw = base64.b64decode(ev.get("b64", "")) if ev.get("n", 0) > 0 else b""
        good = ev.get("off") == off and len(raw) == ev.get("n") and zlib.crc32(raw) == ev.get("crc")
        if not good:
            retries += 1
            if retries > 20:
                return {"ok": False, "error": ev.get("error", "bad chunk"), "off": off, "retries": retries}
            link.events(0.5)  # drop anything stale before retrying
            continue
        size = ev["size"]
        data += raw
        off += len(raw)
    with open(dest, "wb") as f:
        f.write(data)
    secs = time.perf_counter() - t0
    return {
        "ok": True,
        "bytes": len(data),
        "secs": round(secs, 1),
        "kbs": round(len(data) / 1024 / secs),
        "retries": retries,
        "path": dest,
    }


def analyze_wav(path):
    with open(path, "rb") as f:
        blob = f.read()
    riff, riff_size, wave = struct.unpack_from("<4sI4s", blob, 0)
    fmt_tag, ch, rate, byte_rate, align, bits = struct.unpack_from("<HHIIHH", blob, 20)
    data_tag, data_size = struct.unpack_from("<4sI", blob, 36)
    pcm = struct.unpack_from(f"<{data_size // 2}h", blob, 44)
    header_ok = (riff, wave, data_tag) == (b"RIFF", b"WAVE", b"data") and riff_size == len(blob) - 8
    header_ok = header_ok and (fmt_tag, ch, rate, bits) == (1, 1, 16000, 16) and data_size == len(blob) - 44
    skip = rate * 3 // 2  # ES8311 warm-up after Mic.begin()
    body = pcm[skip:]
    mean = sum(body) / len(body)
    per_sec = [rms(body[i : i + rate], mean) for i in range(0, len(body) - rate + 1, rate)]
    return {
        "header_ok": header_ok,
        "secs": round(len(pcm) / rate, 2),
        "rms_after_1_5s": round(rms(body, mean), 1),
        "rms_per_sec_min": round(min(per_sec), 1),
        "rms_per_sec_max": round(max(per_sec), 1),
        "rms_per_sec": [round(v) for v in per_sec],
        "peak": max(abs(v) for v in body),
        "zeros_pct": round(100 * sum(1 for v in body if v == 0) / len(body), 1),
        "distinct": len(set(body)),
        "first_1_5s_zeros_pct": round(100 * sum(1 for v in pcm[:skip] if v == 0) / max(1, skip), 1),
        "afinfo": afinfo(path),
    }


def rms(values, mean):
    return math.sqrt(sum((v - mean) ** 2 for v in values) / len(values))


def afinfo(path):
    try:
        out = subprocess.run(["afinfo", path], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    keep = ("File type", "Data format", "estimated duration", "audio bytes")
    return [ln.strip() for ln in out.splitlines() if ln.strip().startswith(keep)]


def run_step(link, step, out_dir, label):
    name, _, rest = step.partition(":")
    if name == "hz":
        link.send(f"!sdhz {rest}")
        return {"step": step, "replies": link.collect("sdhz", 1, 10)}
    if name == "rec":
        result = run_rec(link, rest)
        if result["reply"] and result["reply"].get("buffers"):
            dest = os.path.join(out_dir, f"sd-{label}-rec-{datetime.now():%Y%m%d-%H%M%S}.wav")
            result["pull"] = pull(link, WAV_PATH, dest)
            if result["pull"]["ok"]:
                result["wav"] = analyze_wav(dest)
        return result
    return run_simple(link, name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--steps", required=True)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "captures"))
    args = ap.parse_args()
    steps = [s for s in args.steps.split(",") if s]
    unknown = [s for s in steps if s.partition(":")[0] not in {*SIMPLE, "hz", "rec"}]
    if unknown:
        sys.exit(f"unknown steps {unknown}")
    device = find_port()
    if not device:
        sys.exit("no 303A:1001 device found")
    os.makedirs(args.out, exist_ok=True)
    ser = open_port(device)
    report = {"label": args.label, "port": device, "steps": []}
    try:
        link = Link(ser)
        report["handshake"] = handshake(link)
        if report["handshake"]["ok"]:
            for step in steps:
                result = run_step(link, step, args.out, args.label)
                report["steps"].append(result)
                print(json.dumps(result), flush=True)
    finally:
        ser.close()
    path = os.path.join(args.out, f"sd-{args.label}-{datetime.now():%Y%m%d-%H%M%S}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    print(f"saved {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
