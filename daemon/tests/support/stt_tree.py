"""Recording folders for cpa-stt tests: contract-v1 sessions (segments first, manifest.json last) in a temp
root, plus the fake transcriber command and a stand-in for sandbox-exec (CI runs on Linux)."""

from __future__ import annotations

import hashlib
import json
import math
import struct
import sys
from collections.abc import Sequence
from pathlib import Path

SID = "3QZK1M7-00042-0A1F"
FAKE_STT = Path(__file__).with_name("fake_stt.py")
RATE = 16000


def wav_header(data_len: int) -> bytes:
    fmt = struct.pack("<IHHIIHH", 16, 1, 1, RATE, 2 * RATE, 2, 16)
    return b"RIFF" + struct.pack("<I", 36 + data_len) + b"WAVEfmt " + fmt + b"data" + struct.pack("<I", data_len)


def tone(samples: int, amp: int = 3000) -> bytes:
    """A 440 Hz tone: loud enough for the silence gate."""
    pcm = b"".join(struct.pack("<h", int(amp * math.sin(2 * math.pi * 440 * k / RATE))) for k in range(samples))
    return wav_header(len(pcm)) + pcm


def level(samples: int, value: int) -> bytes:
    """Constant-magnitude samples (+value, -value, ...): RMS is exactly `value`."""
    pcm = b"".join(struct.pack("<h", value if k % 2 else -value) for k in range(samples))
    return wav_header(len(pcm)) + pcm


def silence(samples: int) -> bytes:
    return wav_header(2 * samples) + bytes(2 * samples)


def _segment(i: int, blob: bytes, t0_ms: int, sid: str) -> dict:
    samples = (len(blob) - 44) // 2
    return {"i": i, "file": f"seg-{i:03d}.wav", "id": sid if i == 0 else f"{sid[:-4]}{i:04d}", "t0_ms": t0_ms,
            "samples": samples, "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest(), "recovered": False,
            "repaired": False}  # fmt: skip


def manifest(session: str, blobs: Sequence[tuple[int, bytes, int]], *, mode: str = "memo", lead_ms: int = 0,
             gaps: Sequence[int] = (), wall_ms: int = 1792620300000, **extra: object) -> dict:  # fmt: skip
    """blobs: (index, wav bytes, t0_ms) per landed segment."""
    segments = [_segment(i, blob, t0, session) for i, blob, t0 in blobs]
    last = segments[-1]
    doc = {
        "v": 1, "kind": "cardputer.recording", "session": session, "attempt": 1, "mode": mode, "project": None,
        "started": {"wall_ms": wall_ms, "tz_min": -420, "uncertain": False},
        "duration_ms": last["t0_ms"] + last["samples"] // 16, "lead_ms": lead_ms,
        "audio": {"sample_rate": RATE, "channels": 1, "bits": 16}, "segments": segments, "gaps": list(gaps),
        "marks": [], "device": {"dev": "020000000001", "fw": "cpa-fw/0.4.0"},
        "written_at": "2026-10-21T14:50:00.000+08:00",
    }  # fmt: skip
    return {**doc, **extra}


def folder_name(session: str, hhmm: str = "1402") -> str:
    return f"{hhmm}-{session.lower()}"


def make_session(root: Path, session: str = SID, blobs: Sequence[bytes] | None = None, *, date: str = "2026-10-21",
                 indices: Sequence[int] | None = None, doc: dict | None = None, **kw: object) -> Path:  # fmt: skip
    """Write the segments, then manifest.json last (0700 / 0600), like cpad. Returns the session folder."""
    blobs = list(blobs if blobs is not None else [tone(RATE)])
    indices = list(indices if indices is not None else range(len(blobs)))
    folder = root / date / folder_name(session)
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    t0, placed = 0, []
    for i, blob in zip(indices, blobs, strict=True):
        t0 = i * 60000 if indices != list(range(len(blobs))) else t0
        (folder / f"seg-{i:03d}.wav").write_bytes(blob)
        placed.append((i, blob, t0))
        t0 += (len(blob) - 44) // 32
    body = doc if doc is not None else manifest(session, placed, **kw)  # type: ignore[arg-type]
    (folder / "manifest.json").write_text(json.dumps(body), encoding="utf-8")
    for path in folder.iterdir():
        path.chmod(0o600)
    return folder


def fake_argv(*flags: str) -> list[str]:
    """The fake transcriber as a configured command: placeholders are whole argv elements."""
    return [sys.executable, "-I", str(FAKE_STT), *flags, "--offset", "{offset_ms}", "{wav}"]


def fake_sandbox(directory: Path) -> Path:
    """Stand-in for /usr/bin/sandbox-exec: drops `-p PROFILE`, keeps the profile for the test, execs the rest."""
    path = directory / "sandbox-exec"
    path.write_text('#!/bin/sh\nprintf "%s" "$2" > "$0.profile"\nshift 2\nexec "$@"\n', encoding="utf-8")
    path.chmod(0o755)
    return path


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))
