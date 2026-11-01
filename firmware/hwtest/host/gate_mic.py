"""G-A10: speech level against WhisperWoof's silence gates with hwtest !mic
(docs/gates/m3-device-gates.md; needs the user speaking).

WhisperWoof skips a whole Upload-Audio file whose float RMS is < 0.001 and a meeting
piece (<= 20 s) whose RMS is < 0.003 (docs/gates/whisperwoof.md §2). levels() reports
peak / RMS as int16 and float, the 20 s pieces under 0.003, clipping, and the spread of
100 ms window RMS (p10 ~ noise floor, p90 ~ speech level). No audio content is logged.
"""

from __future__ import annotations

import math
import struct
from array import array
from dataclasses import dataclass

from gate_stats import percentile

FULL_SCALE = 32768
WHOLE_FILE_GATE = 0.001
PIECE_GATE = 0.003
PIECE_SECS = 20


@dataclass(frozen=True)
class MicRun:
    mag: int
    pga: int
    secs: int

    def __post_init__(self):
        if not (1 <= self.mag <= 64 and 0 <= self.pga <= 10 and 1 <= self.secs <= 300):
            raise ValueError(f"bad mic run {self!r}")

    def command(self):
        return f"!mic {self.mag} {self.pga} {self.secs}"


def parse_wav(blob):
    """(rate, int16 samples) of a 16-bit mono PCM WAV with the 44 B header hwtest writes."""
    riff, _, wave = struct.unpack_from("<4sI4s", blob, 0)
    fmt_tag, ch, rate, _, _, bits = struct.unpack_from("<HHIIHH", blob, 20)
    data_tag, size = struct.unpack_from("<4sI", blob, 36)
    if (riff, wave, data_tag) != (b"RIFF", b"WAVE", b"data") or (fmt_tag, ch, bits) != (1, 1, 16):
        raise ValueError("not a 16-bit mono PCM WAV")
    pcm = array("h")
    pcm.frombytes(blob[44 : 44 + size - size % 2])
    return rate, pcm


def _rms(samples):
    if not samples:
        return 0.0
    return math.sqrt(sum(v * v for v in samples) / len(samples))


def levels(samples, rate):
    rms = _rms(samples)
    win = rate // 10
    wins = [_rms(samples[i : i + win]) for i in range(0, len(samples) - win + 1, win)]
    piece = rate * PIECE_SECS
    pieces = [_rms(samples[i : i + piece]) / FULL_SCALE for i in range(0, len(samples), piece)]
    return {
        "secs": round(len(samples) / rate, 2),
        "peak": max((abs(v) for v in samples), default=0),
        "clipped": sum(1 for v in samples if v >= 32767 or v <= -32767),
        "rms_int16": round(rms, 1),
        "rms_float": round(rms / FULL_SCALE, 6),
        "rms_dbfs": round(20 * math.log10(rms / FULL_SCALE), 1) if rms else None,
        "whole_file_pass": rms / FULL_SCALE >= WHOLE_FILE_GATE,
        "pieces": {"n": len(pieces), "below_0_003": sum(1 for p in pieces if p < PIECE_GATE)},
        "win100_rms": {p: round(percentile(wins, n), 1) if wins else None for p, n in (("p10", 10), ("p50", 50), ("p90", 90))},
    }


def suggest_silence_rms(noise_rms, speech_rms):
    """CARDPUTER_SILENCE_RMS candidate: halfway in dB between the noise floor and the
    speech level (their geometric mean), as float RMS. None without both."""
    if noise_rms <= 0 or speech_rms <= 0:
        return None
    return math.sqrt(noise_rms * speech_rms) / FULL_SCALE


# ---- I/O ------------------------------------------------------------------------------


def run_mic(link, run, dest, pull):
    """Record on the device, pull the WAV to dest with pull(link, path, dest), analyse it."""
    link.send(run.command())
    rec = link.wait_for("sdrec", run.secs + 40.0)
    done = link.wait_for("mic_done", 10.0)
    out = {"run": {"mag": run.mag, "pga": run.pga, "secs": run.secs}, "sdrec": _clean(rec), "mic_done": _clean(done)}
    if not done or "path" not in done:
        return out
    out["pull"] = pull(link, done["path"], dest)
    if out["pull"].get("ok"):
        with open(dest, "rb") as f:
            rate, pcm = parse_wav(f.read())
        out["levels"] = levels(pcm, rate)
    return out


def _clean(ev):
    return {k: v for k, v in (ev or {}).items() if not k.startswith("_")}
