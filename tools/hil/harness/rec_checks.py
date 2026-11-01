"""Pure checks for the recording HIL scenarios (docs/12 M3-H, docs/11 §4.4, §5.4, §6.4, §6.7).

- WAV and manifest: a landed segment against its manifest entry (size, canonical 44 B header,
  whole-file SHA-256, samples); contiguous t0_ms.
- Device-clock timeline (G-A4): debug.state key_ms of the start / stop keys and the first segment's
  at.up_ms (millis() right after Mic.begin) against the summed samples.
- Marks: manifest marks in session time against the device time of each star key.
- Tap analysis: per-file transfer rates, file.get offsets per port open (resume), and the E_BUSY
  window of a recording started mid-pull.
- Daily note: the recording lines cpad writes (route.rec_entry), in the Mac's language at that moment (zh or en).
- Health counters (gh #21): debug.state.rec's ring_high, mic_missing, rot_ms_max and heap minima, which the
  test firmware reports for the last recording while idle; G-A2 wants mic_missing == 0.
"""

from __future__ import annotations

import hashlib
import re
import struct
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from harness.report import check
from harness.wire import Rec

RATE = 16000
SAMPLES_PER_MS = RATE // 1000
HEADER = 44
FILE_TYPES = ("file.list", "file.get", "file.del")

# --- WAV and manifest ----------------------------------------------------------------------


def wav_header(data_bytes: int) -> bytes:
    """The standard header the device writes: PCM, 1 channel, 16 kHz, 16 bit."""
    fmt = struct.pack("<HHIIHH", 1, 1, RATE, RATE * 2, 2, 16)
    return b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVEfmt " + struct.pack("<I", 16) + fmt + \
        b"data" + struct.pack("<I", data_bytes)  # fmt: skip


def segment_problems(blob: bytes, seg: Mapping[str, Any]) -> tuple[str, ...]:
    """What is wrong with a landed seg-NNN.wav against its manifest.json entry; () when nothing."""
    out = []
    if len(blob) != seg["bytes"]:
        out.append("size")
    if blob[:HEADER] != wav_header(len(blob) - HEADER):
        out.append("header")
    if hashlib.sha256(blob).hexdigest() != seg["sha256"]:
        out.append("sha256")
    if seg["bytes"] != HEADER + 2 * seg["samples"]:
        out.append("samples")
    return tuple(out)


def contiguous_t0(manifest: Mapping[str, Any]) -> bool:
    """Without gaps every segment starts where the previous one ended (samples / 16 per ms)."""
    total = 0
    for seg in manifest["segments"]:
        if seg["t0_ms"] != total // SAMPLES_PER_MS:
            return False
        total += seg["samples"]
    return not manifest["gaps"]


# --- device-clock timeline -------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Timeline:
    start_latency_ms: int  # start key -> first sample (memo: G0 hold + Mic.begin; meeting: Mic.begin; no cue)
    wall_ms: int  # first sample -> stop key, device clock
    expected_samples: int
    diff_samples: int  # recorded - expected

    def within(self, samples: int) -> bool:
        return abs(self.diff_samples) <= samples


def timeline(*, start_key_ms: int, up0_ms: int, stop_key_ms: int, samples: int) -> Timeline:
    wall = stop_key_ms - up0_ms
    expected = wall * SAMPLES_PER_MS
    return Timeline(up0_ms - start_key_ms, wall, expected, samples - expected)


# --- health counters (gh #21) ---------------------------------------------------------------

HEALTH = ("ring_high", "mic_missing", "rot_ms_max", "heap_free", "heap_block")


def rec_health(rec: Mapping[str, Any]) -> dict[str, Any]:
    """The health counters of a debug.state rec{}; None where the build (or a boot without a recording yet) has none."""
    return {k: rec.get(k) for k in HEALTH}


def health_checks(health: Mapping[str, Any]) -> list[dict[str, Any]]:
    """Every counter reported (recorded in the report: ring_high, rot_ms_max, heap minima) and mic_missing == 0."""
    missing = [k for k in HEALTH if health.get(k) is None]
    return [
        check("rec_health_reported", not missing, missing=missing, **health),
        check("mic_missing_0", health.get("mic_missing") == 0, mic_missing=health.get("mic_missing")),
    ]


# --- marks ---------------------------------------------------------------------------------


def mark_errors(
    marks: Sequence[Mapping[str, Any]], *, star_key_ms: Sequence[int], up0_ms: int
) -> tuple[int, ...] | None:
    """manifest t_ms minus the star key's session time, per mark; None unless one star per press."""
    stars = [m["t_ms"] for m in marks if m.get("k") == "star"]
    if len(stars) != len(marks) or len(stars) != len(star_key_ms):
        return None
    return tuple(t - (key - up0_ms) for t, key in zip(stars, star_key_ms, strict=True))


# --- tap analysis --------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class FileRate:
    fid: str
    bytes: int  # distinct payload bytes received
    seconds: float  # first file.get sent -> last file.data received
    gets: int
    repeats: int  # file.get for an offset already asked
    first_off: int

    @property
    def kb_s(self) -> float:
        return self.bytes / 1024 / self.seconds if self.seconds > 0 else 0.0


def _is(r: Rec, direction: str, t: str) -> bool:
    return r.dir == direction and r.msg is not None and r.msg.get("t") == t


def file_rates(recs: Sequence[Rec]) -> dict[str, FileRate]:
    gets: dict[str, list[tuple[float, int]]] = {}
    data: dict[str, dict[int, tuple[float, int]]] = {}
    for r in recs:
        if _is(r, "tx", "file.get"):
            gets.setdefault(r.msg["fid"], []).append((r.t, r.msg["off"]))
        elif _is(r, "rx", "file.data") and r.msg.get("n"):
            data.setdefault(r.msg["fid"], {})[r.msg["off"]] = (r.t, r.msg["n"])
    out = {}
    for fid, asked in gets.items():
        got = data.get(fid, {})
        offs = [off for _, off in asked]
        last = max((t for t, _ in got.values()), default=asked[0][0])
        out[fid] = FileRate(fid, sum(n for _, n in got.values()), last - asked[0][0], len(asked),
                            len(offs) - len(set(offs)), offs[0])  # fmt: skip
    return out


def gets_by_connection(recs: Sequence[Rec], fid: str) -> tuple[tuple[int, ...], ...]:
    """file.get offsets for fid, one tuple per port open that asked for it."""
    out: list[list[int]] = [[]]
    for r in recs:
        if r.dir == "open":
            out.append([])
        elif _is(r, "tx", "file.get") and r.msg.get("fid") == fid:
            out[-1].append(r.msg["off"])
    return tuple(tuple(offs) for offs in out if offs)


@dataclass(frozen=True, slots=True)
class BusyWindow:
    start: int | None  # index of the first E_BUSY reply to a file.* request
    end: int | None  # index of the first hello / pong with files.live false after it
    live_seen: bool  # a status with files.live true inside the window (pongs come every 5 s: may miss)
    sent_inside: tuple[str, ...]  # file.* request ids sent inside the window
    non_busy_inside: tuple[str, ...]  # of those, the ones not answered E_BUSY
    data_inside: int  # file.data replies received inside the window
    busy_msgs: tuple[str, ...]  # msg of each E_BUSY reply inside the window
    sent_after_ms: tuple[float, ...] = ()  # per sent_inside: ms after the first E_BUSY reached the tap

    def host_late(self, grace_ms: float) -> tuple[str, ...]:
        """Requests sent more than grace_ms after the first E_BUSY reached the tap. One sent within the
        grace crossed it on the wire: the daemon had not read the E_BUSY yet (docs/11 §5.4 is about
        requests after the host received it)."""
        return tuple(i for i, ms in zip(self.sent_inside, self.sent_after_ms, strict=True) if ms > grace_ms)

    def ok(self, max_late_data: int) -> bool:
        """Opened and cleared, only 'live' refusals, nothing but E_BUSY for requests sent inside, and at
        most max_late_data file.data (the one read the audio task had already taken, docs/11 §4.7)."""
        cleared = self.start is not None and self.end is not None
        busy_live = all(m == "live" for m in self.busy_msgs)
        return cleared and busy_live and not self.non_busy_inside and self.data_inside <= max_late_data


def _files_live(r: Rec) -> bool | None:
    if r.dir != "rx" or r.msg is None or r.msg.get("t") not in ("hello", "pong"):
        return None
    files = r.msg.get("files")
    return files.get("live") if isinstance(files, dict) else None


def _is_file_request(r: Rec) -> bool:
    return r.dir == "tx" and r.msg is not None and r.msg.get("t") in FILE_TYPES


def _is_busy(r: Rec) -> bool:
    return _is(r, "rx", "error") and r.msg.get("code") == "E_BUSY"


def _first_busy(recs: Sequence[Rec], file_ids: set[str]) -> int | None:
    return next((i for i, r in enumerate(recs) if _is_busy(r) and r.msg.get("re") in file_ids), None)


def _cleared(recs: Sequence[Rec], start: int) -> int | None:
    return next((i for i in range(start, len(recs)) if _files_live(recs[i]) is False), None)


def _sent_inside(recs: Sequence[Rec], start: int, inside: Sequence[Rec]) -> tuple[tuple[str, ...], tuple[str, ...]]:
    """file.* requests sent inside the window, and those of them not answered E_BUSY."""
    sent = tuple(r.msg["id"] for r in inside if _is_file_request(r))
    replies = {r.msg.get("re"): r.msg for r in recs[start:] if r.dir == "rx" and r.msg}
    return sent, tuple(i for i in sent if replies.get(i, {}).get("code") != "E_BUSY")


def busy_window(recs: Sequence[Rec]) -> BusyWindow:
    """docs/11 §5.4: after an E_BUSY to file.*, no file.* until a status shows live:false (host);
    every file.* meanwhile answered E_BUSY 'live' (device). One read already taken may still answer."""
    start = _first_busy(recs, {r.msg["id"] for r in recs if _is_file_request(r)})
    if start is None:
        return BusyWindow(None, None, False, (), (), 0, ())
    end = _cleared(recs, start)
    inside = recs[start : end if end is not None else len(recs)]
    sent, non_busy = _sent_inside(recs, start, inside)
    after = tuple(round((r.t - recs[start].t) * 1000, 3) for r in inside if _is_file_request(r))
    busy = tuple(r.msg.get("msg") for r in inside if _is_busy(r))
    data = sum(1 for r in inside if _is(r, "rx", "file.data"))
    live = any(_files_live(r) is True for r in inside)
    return BusyWindow(start, end, live, sent, non_busy, data, busy, after)


# --- daily note ----------------------------------------------------------------------------

REC_LINE = re.compile(
    r"^- (?P<box>\[ \] )?(?P<time>~?\d\d:\d\d) (?:#(?P<tag>\S+) )?(?P<body>.*) "
    r"\^cpa-(?P<sid>[0-9a-z]{7}-[0-9a-z]{5}-[0-9a-z]{4})(?P<u>-u)?$"
)


@dataclass(frozen=True, slots=True)
class RecLine:
    todo: bool
    time: str
    tag: str | None
    body: str
    sid: str  # upper case, as on the wire
    unprocessed: bool  # the -u line of a failed or stale session


MEETING_WORDS = (("会议：[", "待办 {n}"), ("Meeting: [", "to-dos: {n}"))  # (zh, en) of route.rec_entry


def meeting_line(body: str, todos: int) -> bool:
    """A finished meeting's daily-note line with `todos` to-dos, in either language."""
    return any(head in body and count.format(n=todos) in body for head, count in MEETING_WORDS)


def parse_rec_line(line: str) -> RecLine | None:
    m = REC_LINE.match(line.rstrip("\n"))
    if m is None:
        return None
    return RecLine(bool(m["box"]), m["time"], m["tag"], m["body"], m["sid"].upper(), bool(m["u"]))
