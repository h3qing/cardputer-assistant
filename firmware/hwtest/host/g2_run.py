"""G2 toolchain spike runner: one scripted measurement pass against the hwtest firmware.

Safe-opens the port (see hwtest_host.open_port; never toggles RTS/DTR otherwise), does the
"!info" handshake with retries, measures N "!info" round trips, then runs the requested
steps and prints one JSON summary (also saved under --out).

    uv run --with pyserial python g2_run.py --label A --steps mic,beep,mic,sdtest
"""

import argparse
import base64
import json
import math
import os
import struct
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hwtest_host import find_port, open_port, save_wav

STEP_REPLY = {"mic": ("!mic", "mic_end", 15), "beep": ("!beep", "beep", 5), "sdtest": ("!sdtest", "sd", 60)}


class Link:
    def __init__(self, ser):
        self.ser, self.buf, self.mic, self.lines = ser, b"", [], []

    def send(self, text):
        self.ser.write((text + "\n").encode())

    def wait_for(self, kind, timeout):
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            self.buf += self.ser.read(4096)
            *lines, self.buf = self.buf.split(b"\n")
            self.lines += lines
            while self.lines:
                ev = self._parse(self.lines.pop(0))
                if ev and ev.get("t") == kind:
                    return ev
        return None

    def drain(self, secs):
        self.wait_for("__none__", secs)

    def _parse(self, raw):
        try:
            ev = json.loads(raw.strip() or b"null")
        except ValueError:
            return None
        if isinstance(ev, dict) and ev.get("t") == "mic_start":
            self.mic = []
        if isinstance(ev, dict) and ev.get("t") == "mic":
            self.mic.append(base64.b64decode(ev["b64"]))
        return ev if isinstance(ev, dict) else None


def handshake(link, tries=3):
    t0 = time.perf_counter()
    for attempt in range(1, tries + 1):
        link.send("!info")
        hello = link.wait_for("hello", 2.0)
        if hello:
            ms = round((time.perf_counter() - t0) * 1000, 1)
            link.drain(0.5)  # swallow duplicate hellos from parked earlier attempts
            return {"ok": True, "attempts": attempt, "ms": ms, "hello": hello}
    return {"ok": False, "attempts": tries, "ms": round((time.perf_counter() - t0) * 1000, 1)}


def percentile(values, pct):
    ordered = sorted(values)
    rank = max(1, math.ceil(pct / 100 * len(ordered)))
    return ordered[rank - 1]


def one_trip(link):
    """One "!info" round trip. A missed reply is classified, not fatal:
    late = arrived by itself within 10 s; nudged = only arrived after another host write; lost."""
    t0 = time.perf_counter()
    link.send("!info")
    hello = link.wait_for("hello", 2.0) or link.wait_for("hello", 8.0)
    ms = round((time.perf_counter() - t0) * 1000, 1)
    if hello:
        return {"ms": ms, "status": "ok" if ms <= 2000 else "late", "up_ms": hello.get("up_ms")}
    link.send("nudge")
    hello = link.wait_for("hello", 2.0) or {}
    return {
        "ms": round((time.perf_counter() - t0) * 1000, 1),
        "status": "nudged" if hello else "lost",
        "up_ms": hello.get("up_ms"),
    }


def round_trips(link, n):
    trips = [one_trip(link) for _ in range(n)]
    samples = [t["ms"] for t in trips]
    bad = [
        dict(t, i=i, prev_up_ms=trips[i - 1]["up_ms"] if i else None)
        for i, t in enumerate(trips)
        if t["status"] != "ok"
    ]
    return {
        "ok": not bad,
        "n": n,
        "bad": bad,
        "samples_ms": samples,
        "p50_ms": percentile(samples, 50),
        "p95_ms": percentile(samples, 95),
        "max_ms": max(samples),
    }


def pcm_summary(pcm):
    vals = struct.unpack(f"<{len(pcm) // 2}h", pcm)
    return {"samples": len(vals), "distinct": len(set(vals)), "p2p": (max(vals) - min(vals)) if vals else 0}


def run_step(link, name, out_dir, label, idx):
    cmd, reply, timeout = STEP_REPLY[name]
    t0 = time.perf_counter()
    link.send(cmd)
    ev = link.wait_for(reply, timeout)
    result = {"step": name, "ms": round((time.perf_counter() - t0) * 1000, 1), "reply": ev}
    if name == "mic" and ev:
        pcm = b"".join(link.mic)
        path = os.path.join(out_dir, f"g2-{label}-{idx:02d}-mic-{datetime.now():%Y%m%d-%H%M%S}.wav")
        save_wav(path, pcm)
        result.update(wav=path, pcm=pcm_summary(pcm))
    return result


def reboot(device):
    """Fresh boot without touching RTS/DTR: ask the firmware to esp_restart(), then reopen."""
    ser = open_port(device)
    try:
        link = Link(ser)
        if not handshake(link)["ok"]:
            sys.exit("reboot: no handshake")
        link.send("!reboot")
        link.wait_for("bye", 2.0)
    finally:
        ser.close()
    time.sleep(3)
    for _ in range(20):
        found = find_port()
        if found:
            return found
        time.sleep(0.5)
    sys.exit("reboot: port did not come back")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--rtt", type=int, default=10)
    ap.add_argument("--steps", default="mic,beep,mic,sdtest")
    ap.add_argument("--reboot", action="store_true", help="esp_restart() the device first, then measure")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "captures"))
    args = ap.parse_args()
    steps = [s for s in args.steps.split(",") if s]
    if any(s not in STEP_REPLY for s in steps):
        sys.exit(f"unknown step in {steps}; known: {sorted(STEP_REPLY)}")
    device = find_port()
    if not device:
        sys.exit("no 303A:1001 device found")
    os.makedirs(args.out, exist_ok=True)
    if args.reboot:
        device = reboot(device)
    ser = open_port(device)
    try:
        link = Link(ser)
        report = {"label": args.label, "port": device, "handshake": handshake(link)}
        if report["handshake"]["ok"]:
            report["rtt"] = round_trips(link, args.rtt)
            report["steps"] = [run_step(link, s, args.out, args.label, i) for i, s in enumerate(steps)]
            report["trip_after"] = one_trip(link)
    finally:
        ser.close()
    path = os.path.join(args.out, f"g2-{args.label}-{datetime.now():%Y%m%d-%H%M%S}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    print(f"saved {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
