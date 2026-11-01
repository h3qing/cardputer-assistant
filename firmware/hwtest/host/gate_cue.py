"""G-A5: cue timing, lead_ms and I2C concurrency with hwtest !cue
(docs/gates/m3-device-gates.md).

Pure part: run definitions, lead_ms from the 10 ms RMS bins after Mic.begin, grouping,
and the gate verdict (11 §14 / docs/12: loop step <= 50 ms; with the lock 0 bad keys and
0 codec-config failures in 20 runs; memo start <= 1.0 s from pressing G0 (500 ms long
press included), meeting start <= 0.8 s from Enter). I/O part: run_cue().
"""

from __future__ import annotations

import math
import statistics
from dataclasses import dataclass

from gate_stats import summarize

LONG_PRESS_MS = 500  # G0Long threshold (11 §4.1)
MEMO_START_MAX_MS = 1000
MEET_START_MAX_MS = 800
LOOP_STEP_MAX_MS = 50
GATE_RUNS = 20


@dataclass(frozen=True)
class CueRun:
    pattern: str
    tone_ms: int
    vol: int
    hammer: str
    rec_ms: int = 1000

    def __post_init__(self):
        ok = (
            self.pattern in ("memo", "meet", "silent")
            and 20 <= self.tone_ms <= 1000
            and 0 <= self.vol <= 255
            and self.hammer in ("off", "nolock", "lock", "lockyield")
            and 200 <= self.rec_ms <= 1000
        )
        if not ok:
            raise ValueError(f"bad cue run {self!r}")

    def command(self):
        return f"!cue {self.tone_ms} {self.vol} {self.pattern} {self.hammer} {self.rec_ms}"


# Tones of 11 §4.2: memo start 1320 Hz 80 ms; meeting start 660/880/1320 Hz, 100 ms each.
CUE_SWEEP = (
    tuple(CueRun("memo", 80, 64, "off") for _ in range(5))
    + tuple(CueRun("memo", 80, 64, "nolock") for _ in range(GATE_RUNS))
    + tuple(CueRun("memo", 80, 64, "lock") for _ in range(GATE_RUNS))
    + tuple(CueRun("meet", 100, 64, "lock") for _ in range(GATE_RUNS))
    + tuple(CueRun("memo", 80, 64, "lockyield") for _ in range(GATE_RUNS))
    + tuple(CueRun("meet", 100, 64, "lockyield") for _ in range(GATE_RUNS))
    + tuple(CueRun("meet", 100, 64, "nolock") for _ in range(5))
    + tuple(CueRun("memo", 80, v, "lock") for v in (128, 255))
)

# v0.3.1's start (no cue): lead_ms of Mic.begin straight from the idle state, with the
# app's lock (lockyield) and the keyboard read meanwhile.
SILENT_SWEEP = tuple(CueRun("silent", 80, 0, "lockyield") for _ in range(GATE_RUNS))


def lead_ms(rms, bin_ms=10, window_ms=500, quiet_bins=5):
    """Length of the codec transient after Mic.begin: the start of the first run of
    quiet_bins (50 ms) bins at or below max(3 x floor, floor + 8), the floor being the
    median bin from window_ms on. A later sound in the room does not extend it. window_ms
    when the transient never settles; None without >= 10 floor bins."""
    first_floor = window_ms // bin_ms
    floor_bins = rms[first_floor:]
    if len(floor_bins) < 10:
        return None
    floor = statistics.median(floor_bins)
    thr = max(3 * floor, floor + 8)
    quiet = [v <= thr for v in rms[: first_floor + quiet_bins]]
    for i in range(first_floor):
        if all(quiet[i : i + quiet_bins]):
            return i * bin_ms
    return window_ms


def recommend_lead(leads, step=50):
    """Worst run rounded up to the next 50 ms step, never below one step."""
    vals = [v for v in leads if v is not None]
    if not vals:
        return None
    return max(step, math.ceil(max(vals) / step) * step)


def _ms(runs, key):
    return summarize([round(r["cue"][key] / 1000, 1) for r in runs if key in r["cue"]])


def _codec_fail(c):
    spk_bad = c.get("pattern") != "silent" and (not c.get("spk_ok") or c.get("spk_rb_bad"))
    return int(bool(spk_bad or not c.get("mic_ok") or c.get("mic_rb_bad") or c.get("rb_err")))


def _group(runs):
    cues = [r["cue"] for r in runs]
    leads = [lead_ms(r["bins"]["rms"]) for r in runs if r.get("bins")]
    return {
        "runs": len(runs),
        "done": sum(c.get("done", 0) for c in cues),
        "bad_keys": sum(c.get("bad_keys", 0) for c in cues),
        "bad_reads": sum(c.get("bad_reads", 0) for c in cues),
        "i2c_err": sum(c.get("i2c_err", 0) for c in cues),
        "codec_fail": sum(_codec_fail(c) for c in cues),
        "loop_max_ms": round(max(c.get("loop_max_us", 0) for c in cues) / 1000, 1),
        "lock_max_ms": round(max(c.get("lock_max_us", 0) for c in cues) / 1000, 1),
        "lock_wait_ms": _ms(runs, "lock_wait_us"),
        "lock_wait_max_ms": round(max(c.get("lock_wait_max_us", 0) for c in cues) / 1000, 1),
        "spk_begin_ms": _ms(runs, "spk_begin_us"),
        "tone_ms": _ms(runs, "tone_us"),
        "spk_end_ms": _ms(runs, "spk_end_us"),
        "mic_begin_ms": _ms(runs, "mic_begin_us"),
        "first_buf_ms": _ms(runs, "first_buf_us"),
        "start_ms": _ms(runs, "start_us"),
        "lead_ms": summarize([v for v in leads if v is not None]),
        "leads": leads,
    }


def summarize_cue(runs):
    groups = {}
    for r in runs:
        c = r["cue"]
        groups.setdefault(f"{c['pattern']}/{c['hammer']}", []).append(r)
    return {k: _group(v) for k, v in sorted(groups.items())}


def gate_verdict(summary, lock="lock"):
    """Verdict for the runs made with one lock mode ("lock" or "lockyield")."""
    why = []
    memo, meet = summary.get(f"memo/{lock}"), summary.get(f"meet/{lock}")
    if not memo or not meet or memo["runs"] < GATE_RUNS or meet["runs"] < GATE_RUNS:
        return {"pass": False, "why": [f"needs >= {GATE_RUNS} locked runs of memo and meet"]}
    for name, g in (("memo", memo), ("meet", meet)):
        if g["loop_max_ms"] > LOOP_STEP_MAX_MS:
            why.append(f"{name}: loop step {g['loop_max_ms']} ms > {LOOP_STEP_MAX_MS}")
        if g["bad_keys"] or g["bad_reads"] or g["i2c_err"] or g["codec_fail"] or g["done"] < g["runs"]:
            why.append(f"{name}: errors with the lock")
        if g["lock_wait_max_ms"] > LOOP_STEP_MAX_MS:
            why.append(f"{name}: lock wait {g['lock_wait_max_ms']} ms > {LOOP_STEP_MAX_MS} (starved)")
    memo_start = LONG_PRESS_MS + memo["start_ms"]["max"]
    meet_start = meet["start_ms"]["max"]
    if memo_start > MEMO_START_MAX_MS:
        why.append(f"memo start {memo_start} ms > {MEMO_START_MAX_MS}")
    if meet_start > MEET_START_MAX_MS:
        why.append(f"meeting start {meet_start} ms > {MEET_START_MAX_MS}")
    lead = recommend_lead(memo["leads"] + meet["leads"])
    return {"lock": lock, "pass": not why, "why": why, "memo_start_ms": memo_start, "meet_start_ms": meet_start,
            "lead_ms": lead}


# ---- I/O ------------------------------------------------------------------------------


def run_cue(link, run):
    link.send(run.command())
    cue = link.wait_for("cue", 15.0)
    bins = link.wait_for("cue_bins", 5.0) if cue and "error" not in cue else None
    strip = lambda ev: {k: v for k, v in (ev or {}).items() if not k.startswith("_")}  # noqa: E731
    return {"cue": strip(cue), "bins": strip(bins) if bins else None}
