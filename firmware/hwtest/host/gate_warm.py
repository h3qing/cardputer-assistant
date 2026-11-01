"""Issue #31: the ES8311's warm-up after a codec power-on, on the hwtest bench (gate_warm.cpp).

Pure part: the samples of a !warm capture from its warm_pcm lines, and what the capture shows:
the samples that are exactly 0 from the start (lead0), the samples at or under QUIET from the start
(quiet: M5Unified's DC servo carries its offset over from the previous capture, so a muted codec can
start with a few small non-zero values), the first sample over MUTE_END (mute_ms: with the reference
tone playing, where the codec's output comes back whatever the servo left), the 1 kHz reference tone's level per 10 ms
window (Goertzel), the steady level (median of the last 500 ms), the first window within 1 dB of it
(reach_ms, from the first sample), and the peak of the first 50 ms after the quiet lead against the steady
peak (the Mic.begin transient). I/O part: run_warm() sends one command and reads the capture back.
"""

from __future__ import annotations

import base64
import math
import statistics
import struct

RATE = 16000
MS = RATE // 1000
WIN = 10 * MS  # one tone window, 10 ms
FLOOR_DB = -150.0
STEADY_WINDOWS = 50  # the last 500 ms
REACH_TOL_DB = 1.0
ONSET_MS = 50
MUTE_END = 100  # the first |sample| over this ends the codec's mute (the reference tone peaks ~600)
QUIET = 32  # |sample| at or under this is not the reference tone (about -35 dBFS, peak ~600 at kMicGain)


def pcm_from_lines(lines):
    """(samples, ok): ok only when warm_end came and every line 0..lines-1 arrived once, in order."""
    pcm = [ln for ln in lines if ln.get("t") == "warm_pcm"]
    end = next((ln for ln in lines if ln.get("t") == "warm_end"), None)
    ok = end is not None and [ln["seq"] for ln in pcm] == list(range(end["lines"]))
    blob = b"".join(base64.b64decode(ln["b64"]) for ln in pcm)
    return list(struct.unpack(f"<{len(blob) // 2}h", blob[: len(blob) // 2 * 2])), ok


def goertzel_db(x, hz=1000.0):
    """The level of the hz component of x, dBFS (amplitude re 32768); FLOOR_DB when absent."""
    n = len(x)
    if not n:
        return FLOOR_DB
    coeff = 2 * math.cos(2 * math.pi * hz / RATE)
    s1 = s2 = 0.0
    for v in x:
        s1, s2 = v + coeff * s1 - s2, s1
    power = max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)
    amp = 2 * math.sqrt(power) / n / 32768
    return 20 * math.log10(amp) if amp > 1e-7 else FLOOR_DB


def tone_track(x, hz=1000.0):
    return [round(goertzel_db(x[i : i + WIN], hz), 2) for i in range(0, len(x) - WIN + 1, WIN)]


def lead0(x):
    n = 0
    while n < len(x) and x[n] == 0:
        n += 1
    return n


def quiet_lead(x):
    n = 0
    while n < len(x) and abs(x[n]) <= QUIET:
        n += 1
    return n


def mute_end(x):
    """Index of the first sample over MUTE_END, None when there is none."""
    return next((i for i, v in enumerate(x) if abs(v) > MUTE_END), None)


def summarize(x):
    lead, quiet = lead0(x), quiet_lead(x)
    track = tone_track(x)
    steady = statistics.median(track[-STEADY_WINDOWS:]) if track else FLOOR_DB
    reach = next((i for i, v in enumerate(track) if v >= steady - REACH_TOL_DB), None)
    tail = x[-STEADY_WINDOWS * WIN :]
    return {
        "n": len(x),
        "lead0_ms": round(lead / MS, 3),
        "quiet_ms": round(quiet / MS, 3),
        "mute_ms": None if mute_end(x) is None else round(mute_end(x) / MS, 4),
        "zeros_after": sum(1 for v in x[lead:] if v == 0),
        "steady_db": round(steady, 2),
        "reach_ms": reach * 10 if reach is not None and steady > FLOOR_DB + 10 else None,
        "onset_peak": max((abs(v) for v in x[quiet : quiet + ONSET_MS * MS]), default=0),
        "steady_peak": max((abs(v) for v in tail), default=0),
        "track_head": track[: min(len(track), 150)],
    }


# --- I/O ---------------------------------------------------------------------------------------


def run_warm(link, command, timeout=30.0):
    """Send one !warm; (the warm reply, the samples, ok). ok: the reply said ok and every line came."""
    link.send(command)
    evs = link.events(timeout, until=lambda e: e.get("t") == "warm_end" or (e.get("t") == "warm" and not e.get("ok")))
    reply = next((e for e in evs if e.get("t") == "warm"), None)
    x, complete = pcm_from_lines(evs)
    return reply, x, bool(reply and reply.get("ok")) and complete
