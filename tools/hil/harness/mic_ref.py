"""The mic_cal reference and the Mac's speakers (mic gain brief §6.2).

The reference is built once per run in the rig's temp dir, 16 kHz int16: 300 ms of silence, tone A
(1 kHz, -20 dBFS, 1 s), tone B (-32 dBFS), an English and a Chinese `say` sentence each normalised to a
-14 dBFS peak, 500 ms apart. Block offsets count from the start of tone A, which the analysis finds
in each capture (harness.mic_cal.onset_ms).

Pure: tone synthesis, normalisation, layout, voice choice, parsing the output check. I/O: `say`, the
read-only output check (osascript reads the volume settings, system_profiler the default output; the
system volume is never changed) and afplay at the file's own level.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import math
import re
import subprocess
import sys
import time
import wave
from array import array
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from harness.mic_cal import MS, RAMP_MS, RATE, TONE_HZ, Block

INT16_MAX = 32767
LEAD_IN_MS, GAP_MS, TAIL_MS = 300, 500, 300
TONE_MS, TONE_A_DBFS, TONE_B_DBFS = 1000, -20.0, -32.0
SPEECH_PEAK_DBFS = -14.0
HEADROOM_DBFS = -1.0  # a rescaled reference keeps its loudest sample this far under full scale
EN_TEXT = "This is a calibration recording. Seven red boxes arrive on Tuesday at nine."
ZH_TEXT = "这是一段校准录音。下午三点开会，讨论定价和交付。"
EN_VOICES = ("Samantha", "Alex", "Daniel", "Karen", "Moira")
ZH_VOICES = ("Tingting", "Meijia", "Sinji")
VOICE_LINE = re.compile(r"^(?P<name>.+?)\s+(?P<locale>[a-z]{2,3}_[A-Za-z0-9]+)\s+#")
BUILTIN = "coreaudio_device_type_builtin"


# --- synthesis ---------------------------------------------------------------------------------


def tone(freq: float, dbfs: float, ms: int, ramp_ms: int = RAMP_MS) -> list[int]:
    """A sine at dbfs (re INT16_MAX) with raised-cosine ramps of ramp_ms at both ends."""
    amp, n, r = 10 ** (dbfs / 20) * INT16_MAX, ms * MS, ramp_ms * MS

    def env(i: int) -> float:
        k = min(i, n - 1 - i)
        return 0.5 - 0.5 * math.cos(math.pi * k / r) if k < r else 1.0

    return [round(amp * env(i) * math.sin(2 * math.pi * freq * i / RATE)) for i in range(n)]


def normalise(x: Sequence[int], peak_dbfs: float) -> list[int]:
    peak = max((abs(v) for v in x), default=0)
    if not peak:
        return list(x)
    k = 10 ** (peak_dbfs / 20) * INT16_MAX / peak
    return [round(v * k) for v in x]


def trim_silence(x: Sequence[int], rel: float = 0.01) -> list[int]:
    """Drop the head and tail quieter than rel x the peak (say's own silence)."""
    thr = max((abs(v) for v in x), default=0) * rel
    loud = [i for i, v in enumerate(x) if abs(v) > thr]
    return list(x[loud[0] : loud[-1] + 1]) if loud else []


def scaled(x: Sequence[int], gain_db: float) -> list[int]:
    k = 10 ** (gain_db / 20)
    return [max(-INT16_MAX - 1, min(INT16_MAX, round(v * k))) for v in x]


def max_gain_db(x: Sequence[int]) -> float:
    """The largest gain that keeps the loudest sample HEADROOM_DBFS under full scale."""
    peak = max((abs(v) for v in x), default=0)
    return HEADROOM_DBFS - 20 * math.log10(peak / INT16_MAX) if peak else 0.0


def assemble(tone_a: Sequence[int], tone_b: Sequence[int],
             speech: Sequence[tuple[str, Sequence[int]]]) -> tuple[list[int], tuple[Block, ...]]:  # fmt: skip
    """The reference's samples and its blocks (offsets from the start of tone A, whole ms)."""
    parts = [("tone_a", tone_a, "tone"), ("tone_b", tone_b, "tone"), *((n, s, "speech") for n, s in speech)]
    padded = [list(p) + [0] * (-len(p) % MS) for _, p, _ in parts]
    durs = [len(p) // MS for p in padded]
    starts = [sum(durs[:i]) + GAP_MS * i for i in range(len(parts))]
    blocks = tuple(Block(n, st, d, k) for (n, _, k), st, d in zip(parts, starts, durs, strict=True))
    gap = [0] * (GAP_MS * MS)
    body = [v for i, p in enumerate(padded) for v in (gap if i else []) + p]
    return [0] * (LEAD_IN_MS * MS) + body + [0] * (TAIL_MS * MS), blocks


# --- voices and the output check (pure parsing) --------------------------------------------------


def parse_voices(text: str) -> tuple[tuple[str, str], ...]:
    """(name, locale) of each `say -v '?'` line."""
    return tuple((m["name"], m["locale"]) for m in map(VOICE_LINE.match, text.splitlines()) if m)


def pick_voice(voices: Sequence[tuple[str, str]], preferred: Sequence[str], lang: str) -> str | None:
    names = {n for n, _ in voices}
    named = next((p for p in preferred if p in names), None)
    return named or next((n for n, loc in voices if loc.startswith(lang + "_")), None)


def choose_voices(voices: Sequence[tuple[str, str]]) -> dict[str, str]:
    """{"en": ..., "zh": ...}, each only when a voice of that language is installed."""
    picked = {"en": pick_voice(voices, EN_VOICES, "en"), "zh": pick_voice(voices, ZH_VOICES, "zh")}
    return {lang: v for lang, v in picked.items() if v}


def _default_output(profiler: str) -> dict[str, Any] | None:
    items = [d for group in json.loads(profiler).get("SPAudioDataType", []) for d in group.get("_items", [])]
    return next((d for d in items if d.get("coreaudio_default_audio_output_device") == "spaudio_yes"), None)


def output_check(muted: str, volume: str, profiler: str) -> tuple[str | None, dict[str, Any]]:
    """Why the reference would not be heard as calibrated (None: unmuted built-in speakers), and what
    was read: osascript's output muted / output volume, system_profiler SPAudioDataType -json."""
    m, v = muted.strip(), volume.strip()
    try:
        dev = _default_output(profiler)
    except (ValueError, AttributeError, TypeError):
        return "cannot read system_profiler SPAudioDataType -json", {"muted": m != "false", "volume": None}
    name = (dev or {}).get("_name")
    info = {"muted": m != "false", "volume": int(v) if v.isdigit() else None, "device": name}
    speakers = dev is not None and dev.get("coreaudio_device_transport") == BUILTIN and "Speakers" in (name or "")
    problems = (
        (m != "false", f"output is muted or has no device (output muted = {m!r})"),
        (not info["volume"], f"output volume is {v!r}"),
        (dev is None, "no default output device"),
        (not speakers, f"default output is {name!r}, not the built-in speakers"),
    )
    return next((why for bad, why in problems if bad), None), info


# --- I/O ---------------------------------------------------------------------------------------


def read_wav(path: Path) -> list[int]:
    """16-bit 16 kHz samples of a WAV with any extra chunks (say writes FLLR); stereo is averaged."""
    with wave.open(str(path), "rb") as w:
        ch, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.readframes(w.getnframes())
    if width != 2 or rate != RATE:
        raise ValueError(f"{path.name}: want 16-bit {RATE} Hz, got {8 * width}-bit {rate} Hz")
    pcm = array("h", frames)
    if sys.byteorder == "big":
        pcm.byteswap()
    return [sum(pcm[i : i + ch]) // ch for i in range(0, len(pcm), ch)]


def write_wav(path: Path, x: Sequence[int]) -> None:
    pcm = array("h", x)
    if sys.byteorder == "big":
        pcm.byteswap()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


@dataclass(frozen=True, slots=True)
class Reference:
    path: Path
    blocks: tuple[Block, ...]
    voices: Mapping[str, str]
    gain_db: float  # applied to base when written
    max_gain_db: float
    base: tuple[int, ...]  # the reference before any gain
    sha256: str
    ms: int

    def doc(self) -> dict[str, Any]:
        """For the report: no samples."""
        blocks = [{"name": b.name, "start_ms": b.start_ms, "dur_ms": b.dur_ms} for b in self.blocks]
        gains = {"gain_db": round(self.gain_db, 2), "max_gain_db": round(self.max_gain_db, 2)}
        return {"voices": dict(self.voices), **gains, "ms": self.ms, "sha256": self.sha256, "blocks": blocks}


def installed_voices() -> tuple[tuple[str, str], ...]:
    done = subprocess.run(["say", "-v", "?"], capture_output=True, text=True, check=False, timeout=30)
    return parse_voices(done.stdout)


def _say(voice: str, text: str, out: Path) -> list[int]:
    cmd = ["say", "-v", voice, "-o", str(out), "--file-format=WAVE", "--data-format=LEI16@16000", text]
    subprocess.run(cmd, capture_output=True, check=True, timeout=60)
    return read_wav(out)


def _write(path: Path, base: tuple[int, ...], blocks: tuple[Block, ...], voices: Mapping[str, str],
           gain_db: float) -> Reference:  # fmt: skip
    write_wav(path, scaled(base, gain_db))
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    return Reference(path, blocks, dict(voices), gain_db, max_gain_db(base), base, sha, len(base) // MS)


def build(folder: Path, gain_db: float = 0.0) -> Reference:
    """Synthesise the reference into folder (the rig's temp dir); RuntimeError without an English voice."""
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    voices = choose_voices(installed_voices())
    if "en" not in voices:
        raise RuntimeError("no English `say` voice installed")
    texts = {"en": EN_TEXT, "zh": ZH_TEXT}
    speech = tuple((lang, normalise(trim_silence(_say(v, texts[lang], folder / f"{lang}.wav")), SPEECH_PEAK_DBFS))
                   for lang, v in voices.items())  # fmt: skip
    samples, blocks = assemble(tone(TONE_HZ, TONE_A_DBFS, TONE_MS), tone(TONE_HZ, TONE_B_DBFS, TONE_MS), speech)
    return _write(folder / "ref.wav", tuple(samples), blocks, voices, gain_db)


def rescale(ref: Reference, gain_db: float) -> Reference:
    return _write(ref.path, ref.base, ref.blocks, ref.voices, gain_db)


def output_state() -> tuple[str | None, dict[str, Any]]:
    """Read-only check of the Mac's output (output_check); nothing here changes a setting."""

    def run(*cmd: str) -> str:
        return subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=60).stdout

    return output_check(run("osascript", "-e", "output muted of (get volume settings)"),
                        run("osascript", "-e", "output volume of (get volume settings)"),
                        run("system_profiler", "SPAudioDataType", "-json"))  # fmt: skip


async def play(path: Path, timeout: float) -> float:
    """afplay the file at its own level (no -v, the system volume untouched); the seconds it took."""
    started = time.monotonic()
    proc = await asyncio.create_subprocess_exec(
        "afplay", str(path), stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE
    )
    try:
        _, err = await asyncio.wait_for(proc.communicate(), timeout)
    finally:
        if proc.returncode is None:
            proc.kill()
    if proc.returncode:
        raise RuntimeError(f"afplay exited {proc.returncode}: {err.decode(errors='replace').strip()[:200]}")
    return time.monotonic() - started
