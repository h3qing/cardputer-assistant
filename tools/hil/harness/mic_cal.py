"""Pure measurement of one mic_cal capture (mic gain brief §4 HIL, §5, §6); the sweep is harness.mic_decide.

A capture is the device's 16 kHz mono WAV of a meeting that heard the reference: the 1 kHz tone by
Goertzel and its onset, each reference block's level, the room noise in 100 ms windows broadband and
after a 150 Hz high-pass, samples at M5Unified's clamp, the Mic.begin transient (lead_ms, the algorithm
of firmware/hwtest/host/gate_cue.py) and the sample step (GCD: 8 at mag 16, 1 at unity). Also the
sweep's settings, each one a debug.mic {pga, vol, mag}, and kMicGain as mic_gain.h declares it (what a
build records at without debug.mic). Standard library only.
"""

from __future__ import annotations

import math
import re
import statistics
import sys
from array import array
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Any

from harness.rec_checks import HEADER, RATE, wav_header
from harness.stats import percentile

MS = RATE // 1000
FULL_SCALE = 32768.0
FLOOR_DB = -150.0
CLIP_HI, CLIP_LO = 32751, -32752  # M5Unified Mic_Class write_one clamps to INT16_MAX - 16 / INT16_MIN + 16
NEAR_CLIP = 29204  # -1 dBFS: a tone A this hot measures the gain from tone B
TONE_HZ = 1000.0
SETTLE_MS = 1000  # Mic.begin thump and DC servo: never part of a level
NOISE_MS = (1000, 4000)  # the silent pre-roll before afplay
ONSET_WIN_MS, ONSET_RISE_DB, RAMP_MS = 20, 20.0, 10
EDGE_MS = 100  # trimmed from each end of a block
NOISE_WIN_MS = 100
HP_HZ = 150.0
LEAD_SPAN_MS = 2000
VOL_0DB = 0xBF  # ES8311 REG17 at 0 dB


@dataclass(frozen=True, slots=True)
class Setting:
    label: str
    pga: int  # ES8311 REG14 = 0x10 | pga: 3 dB steps
    vol: int  # ES8311 REG17: 0xBF = 0 dB, 0.5 dB steps
    mag: int  # M5Unified magnification at over_sampling 1: x mag/2 after the 16-bit word

    @property
    def nominal_db(self) -> float:
        return 3 * self.pga + (self.vol - VOL_0DB) / 2 + 20 * math.log10(self.mag / 2)

    def wire(self) -> dict[str, Any]:
        return {"t": "debug.mic", "pga": self.pga, "vol": self.vol, "mag": self.mag}


SWEEP = (
    Setting("B1", 0, 0xBF, 16), Setting("U0", 0, 0xBF, 2), Setting("S6", 6, 0xD7, 2), Setting("S7", 7, 0xD1, 2),
    Setting("S8", 8, 0xCB, 2), Setting("S9", 9, 0xC5, 2), Setting("S10", 10, 0xBF, 2), Setting("N27", 9, 0xBF, 2),
    Setting("N33", 10, 0xC5, 2), Setting("N36", 10, 0xCB, 2), Setting("B2", 0, 0xBF, 16),
)  # fmt: skip
BY_LABEL = {s.label: s for s in SWEEP}
BASELINES, UNITY, PREFERRED = ("B1", "B2"), "U0", "S10"
SPLITS = ("S6", "S7", "S8", "S9", "S10")
UNITY_DB = BY_LABEL[UNITY].nominal_db - BY_LABEL["B1"].nominal_db
_K_MIC_GAIN = re.compile(r"constexpr\s+MicGain\s+kMicGain\s*\{([^}]*)\}")
_HEADER_NAMES = {"kMicVolUnity": VOL_0DB}


def shipped_gain(header: str) -> Setting:
    """kMicGain from firmware/app/lib/core/mic_gain.h; ValueError unless it is three values that
    mic_gain_valid and debug.mic would accept (pga 0-10, vol 0-255, mag 1-64)."""
    found = _K_MIC_GAIN.search(header)
    parts = [p.strip() for p in found.group(1).split(",")] if found else []
    if len(parts) != 3:
        raise ValueError("no `constexpr MicGain kMicGain{pga, vol, mag};` in mic_gain.h")
    pga, vol, mag = (_HEADER_NAMES[p] if p in _HEADER_NAMES else int(p, 0) for p in parts)
    if not (0 <= pga <= 10 and 0 <= vol <= 0xFF and 1 <= mag <= 64):
        raise ValueError(f"kMicGain {{{pga}, {vol:#04x}, {mag}}} is out of range")
    return Setting("kMicGain", pga, vol, mag)


@dataclass(frozen=True, slots=True)
class Block:
    name: str  # tone_a / tone_b, or a speech block
    start_ms: int  # from the start of tone A
    dur_ms: int
    kind: str  # "tone" | "speech"


# --- one capture -------------------------------------------------------------------------------


def pcm(blob: bytes) -> array:
    """The samples of a landed segment; ValueError unless it has the device's canonical header."""
    body = len(blob) - HEADER
    if body < 0 or body % 2 or blob[:HEADER] != wav_header(body):
        raise ValueError("not the device's 16 kHz mono 16-bit WAV")
    out = array("h")
    out.frombytes(blob[HEADER:])
    if sys.byteorder == "big":
        out.byteswap()
    return out


def dbfs(amplitude: float) -> float:
    return 20 * math.log10(amplitude / FULL_SCALE) if amplitude > 0 else FLOOR_DB


def _goertzel(x: Sequence[float], freq: float, rate: int) -> float:
    """|X| at exactly freq (any length, not only whole bins)."""
    coeff = 2 * math.cos(2 * math.pi * freq / rate)
    s1 = s2 = 0.0
    for v in x:
        s1, s2 = v + coeff * s1 - s2, s1
    return math.sqrt(max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2))


def _amp(x: Sequence[float], freq: float = TONE_HZ, rate: int = RATE) -> float:
    """Amplitude of the freq component, rectangular window."""
    return 2 * _goertzel(x, freq, rate) / len(x) if len(x) else 0.0


def tone_db(x: Sequence[float], freq: float = TONE_HZ, rate: int = RATE) -> float:
    """Level of the freq component re a full-scale sine (Hann window, amplitude-normalised)."""
    n = len(x)
    if not n:
        return FLOOR_DB
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
    return dbfs(2 * _goertzel([v * w for v, w in zip(x, win, strict=True)], freq, rate) / sum(win))


def onset_ms(x: Sequence[float], search_ms: int = NOISE_MS[1], noise_ms: tuple[int, int] = NOISE_MS,
             ramp_ms: int = RAMP_MS) -> int | None:  # fmt: skip
    """Start of the first 1 kHz tone after search_ms: the first 20 ms window ONSET_RISE_DB over the
    median 1 kHz level of the noise window, refined in 1 ms steps to the window holding half the steady
    amplitude (centred on the middle of the ramp). None without a tone or a noise window."""
    n = ONSET_WIN_MS * MS
    floor = [_amp(x[i : i + n]) for i in range(noise_ms[0] * MS, noise_ms[1] * MS - n + 1, n)]
    if not floor:
        return None
    thr = statistics.median(floor) * 10 ** (ONSET_RISE_DB / 20)
    coarse = next((i for i in range(search_ms * MS, len(x) - n + 1, n) if _amp(x[i : i + n]) > thr), None)
    if coarse is None:
        return None
    steady = _amp(x[coarse + 3 * n : coarse + 4 * n])
    half = next((p for p in range(max(0, coarse - 2 * n), coarse + 2 * n, MS) if _amp(x[p : p + n]) >= steady / 2),
                coarse)  # fmt: skip
    return round((half + n / 2) / MS - ramp_ms / 2)


def rms(x: Sequence[float]) -> float:
    return math.sqrt(sum(v * v for v in x) / len(x)) if len(x) else 0.0


def window_rms(x: Sequence[float], n: int) -> list[float]:
    return [rms(x[i : i + n]) for i in range(0, len(x) - n + 1, n)]


def highpass(x: Sequence[float], cutoff: float = HP_HZ, rate: int = RATE) -> list[float]:
    """Second-order Butterworth high-pass (RBJ cookbook biquad, Q = 1/sqrt(2))."""
    w0 = 2 * math.pi * cutoff / rate
    cw, alpha = math.cos(w0), math.sin(w0) / math.sqrt(2)
    a0 = 1 + alpha
    b0, b1, a1, a2 = (1 + cw) / 2 / a0, -(1 + cw) / a0, -2 * cw / a0, (1 - alpha) / a0
    out, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
    for v in x:
        y = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2
        out.append(y)
        x1, x2, y1, y2 = v, x1, y, y1
    return out


@dataclass(frozen=True, slots=True)
class Noise:
    bb: tuple[float, float, float]  # 100 ms window RMS p10 / p50 / p90, counts
    hp: tuple[float, float, float]  # the same after the 150 Hz high-pass

    @property
    def hp_db(self) -> float:
        return dbfs(self.hp[1])


def _pcts(values: Sequence[float]) -> tuple[float, float, float]:
    if not values:
        return (0.0, 0.0, 0.0)
    return (percentile(values, 10), percentile(values, 50), percentile(values, 90))


def noise_stats(x: Sequence[float], span: tuple[int, int] = NOISE_MS) -> Noise:
    """The noise window's 100 ms RMS percentiles; the high-pass settles on the 100 ms before it."""
    a, b, n = span[0] * MS, span[1] * MS, NOISE_WIN_MS * MS
    pre = min(a, n)
    hp = highpass(x[a - pre : b])[pre:]
    return Noise(_pcts(window_rms(x[a:b], n)), _pcts(window_rms(hp, n)))


def clipped(x: Sequence[int]) -> int:
    return sum(1 for v in x if v >= CLIP_HI or v <= CLIP_LO)


def _peak(x: Sequence[int]) -> int:
    return max((abs(v) for v in x), default=0)


def rms_bins(x: Sequence[int], bin_ms: int = 10) -> list[int]:
    """RMS of each whole 10 ms bin, truncated to an integer (hwtest gate_core level_bins)."""
    n = bin_ms * MS
    return [int(math.sqrt(sum(v * v for v in x[i : i + n]) / n)) for i in range(0, len(x) - n + 1, n)]


def lead_ms(rms: Sequence[float], bin_ms: int = 10, window_ms: int = 500, quiet_bins: int = 5) -> int | None:
    """firmware/hwtest/host/gate_cue.py lead_ms, unchanged: the start of the first run of quiet_bins bins at
    or below max(3 x floor, floor + 8), the floor being the median bin from window_ms on; window_ms when
    the transient never settles; None without >= 10 floor bins."""
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


def recommend_lead(leads: Sequence[int | None], step: int = 50) -> int | None:
    """gate_cue.recommend_lead: the worst run rounded up to the next 50 ms step, never below one step."""
    vals = [v for v in leads if v is not None]
    return max(step, math.ceil(max(vals) / step) * step) if vals else None


def lead_of(x: Sequence[int]) -> int | None:
    return lead_ms(rms_bins(x[: LEAD_SPAN_MS * MS]))


def step(x: Sequence[int]) -> int:
    g = 0
    for v in x:
        g = math.gcd(g, v)
        if g == 1:
            break
    return g


@dataclass(frozen=True, slots=True)
class Capture:
    onset_ms: int | None  # start of tone A, ms from the first sample
    tone_a_db: float | None  # 1 kHz level re a full-scale sine over the middle of the block
    tone_b_db: float | None
    tone_a_peak: int  # largest |sample| in tone A's window (the near-clip test)
    speech_db: float | None  # RMS of the speech blocks less the noise power, dBFS
    noise: Noise
    clipped: int  # samples at the M5 clamp after SETTLE_MS
    peak: int  # largest |sample| after SETTLE_MS
    transient_peak: int  # largest |sample| before SETTLE_MS (the Mic.begin thump)
    step: int
    lead_ms: int | None


def _window(x: Sequence[int], onset: int, blk: Block) -> Sequence[int]:
    return x[(onset + blk.start_ms + EDGE_MS) * MS : (onset + blk.start_ms + blk.dur_ms - EDGE_MS) * MS]


def _levels(x: Sequence[int], blocks: Sequence[Block], onset: int, noise_power: float) -> dict[str, Any]:
    if (onset + max(b.start_ms + b.dur_ms for b in blocks)) * MS > len(x):
        return {}  # the capture ended inside the reference
    tones = {b.name: _window(x, onset, b) for b in blocks if b.kind == "tone"}
    speech = [v for b in blocks if b.kind == "speech" for v in _window(x, onset, b)]
    power = rms(speech) ** 2 - noise_power
    return {
        "tone_a": tone_db(tones["tone_a"]),
        "tone_b": tone_db(tones["tone_b"]),
        "tone_a_peak": _peak(tones["tone_a"]),
        "speech": dbfs(math.sqrt(power)) if speech and power > 0 else None,
    }


def measure(x: Sequence[int], blocks: Sequence[Block]) -> Capture:
    settle = SETTLE_MS * MS
    onset = onset_ms(x)
    noise_power = rms(x[NOISE_MS[0] * MS : NOISE_MS[1] * MS]) ** 2
    lv = _levels(x, blocks, onset, noise_power) if onset is not None else {}
    after = x[settle:]
    return Capture(onset, lv.get("tone_a"), lv.get("tone_b"), lv.get("tone_a_peak", 0), lv.get("speech"),
                   noise_stats(x), clipped(after), _peak(after), _peak(x[:settle]), step(x), lead_of(x))  # fmt: skip
