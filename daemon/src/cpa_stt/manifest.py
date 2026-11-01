"""manifest.json as untrusted input for the transcriber role (recording contract v1, docs/22 §4.6). Pure.

check() gives the contract's verdict (same order and field paths as check_recording.validate): not an
object -> E_INVALID (root); v not the integer 1 -> E_VERSION v; schema faults; then the segment index rules.
Unknown fields are ignored. runs() splits the segments into runs of contiguous indices (a gap starts a new
run): each run is transcribed as one WAV, from session time t0_ms, skipping lead_ms at the start of
segment 0. order_key() puts voice-capture clips first, then other memos (newest first), then meetings.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from cpad.protocol.validate import (
    NO_CONTROL,
    array,
    boolean,
    format_path,
    integer,
    is_int,
    nullable,
    obj,
    one_of,
    string,
)
from cpad.recordings.contract import ATTEMPT, LABEL, MODE, MONOID, SESSION_MS, SHA256, TIMESTAMP, Verdict

RATE = 16000
MODES = ("memo", "meeting")
HOST_MAX = 24  # common.schema.json#/$defs/hostLabel: code points and UTF-8 bytes
ENV = obj(  # docs/23 §4.2: cpad's stamp, an annotation only; link is an extensible enum
    {"host": nullable(string(NO_CONTROL, min_len=1, max_len=HOST_MAX, max_bytes=HOST_MAX)),
     "link": string(r"^[a-z][a-z0-9_]{0,15}$")},
)  # fmt: skip
SEGMENT = obj(
    {"i": integer(0, 9999), "file": string(r"^seg-[0-9]{3,4}\.wav$"), "id": MONOID, "t0_ms": SESSION_MS,
     "samples": integer(0, 9_600_000), "bytes": integer(44, 19_200_044), "sha256": SHA256,
     "recovered": boolean(), "repaired": boolean()},
)  # fmt: skip
MANIFEST = obj(
    {"kind": one_of("cardputer.recording"), "session": MONOID, "attempt": ATTEMPT, "mode": MODE,
     "project": nullable(obj({"key": string(r"^[a-z0-9][a-z0-9-]{0,11}$")}, {"name": nullable(LABEL)})),
     "started": obj({"wall_ms": integer(1_735_689_600_000, 2**53 - 1), "tz_min": integer(-840, 840),
                     "uncertain": boolean()}),
     "duration_ms": integer(0, 14_400_000), "lead_ms": integer(0, 2000),
     "audio": obj({"sample_rate": integer(RATE, RATE), "channels": integer(1, 1), "bits": integer(16, 16)}),
     "segments": array(SEGMENT, min_items=1, max_items=10000), "gaps": array(integer(0, 9999)),
     "marks": array(obj({"t_ms": SESSION_MS, "k": string(r"^[a-z][a-z0-9_]{0,15}$")})),
     "device": obj({"dev": string(r"^[0-9a-f]{12}$"), "fw": string(r"^cpa-fw/[!-~]{1,24}$")}),
     "written_at": TIMESTAMP},
    {"consent": nullable(one_of("ack")), "cap": MONOID, "env": nullable(ENV)},
)  # fmt: skip


def index_problems(doc: Mapping[str, Any]) -> tuple[str, ...]:
    """The rules beyond the schema (assumes a schema-valid manifest), as in check_recording.index_problems."""
    segments, gaps = doc["segments"], doc["gaps"]
    indices = [s["i"] for s in segments]
    covered = set(indices) | set(gaps)
    out = [f"segments[{k}].i" for k in range(1, len(indices)) if indices[k] <= indices[k - 1]]
    out += [f"gaps[{k}]" for k in range(1, len(gaps)) if gaps[k] <= gaps[k - 1]]
    out += [f"gaps[{k}]" for k, g in enumerate(gaps) if g in indices]
    out += [f"segments[{k}].file" for k, s in enumerate(segments) if s["file"] != f"seg-{s['i']:03d}.wav"]
    out += [f"segments[{k}].bytes" for k, s in enumerate(segments) if s["bytes"] != 44 + 2 * s["samples"]]
    return tuple(out + (["gaps"] if covered != set(range(max(covered) + 1)) else []))


def check(doc: Any) -> Verdict:
    if not isinstance(doc, dict):
        return Verdict("E_INVALID", ("(root)",))
    if not (is_int(doc.get("v")) and doc["v"] == 1):
        return Verdict("E_VERSION", ("v",))
    fields = {format_path(p) for p in MANIFEST(doc, ())} or set(index_problems(doc))
    return Verdict("E_INVALID", tuple(sorted(fields))) if fields else Verdict("ok")


@dataclass(frozen=True, slots=True)
class Seg:
    i: int
    file: str
    bytes: int
    samples: int
    sha256: str


@dataclass(frozen=True, slots=True)
class Run:
    segs: tuple[Seg, ...]
    t0_ms: int
    offset_ms: int

    @property
    def samples(self) -> int:
        return sum(s.samples for s in self.segs)

    @property
    def duration_s(self) -> float:
        return self.samples / RATE


def _seg(s: Mapping[str, Any]) -> Seg:
    return Seg(s["i"], s["file"], s["bytes"], s["samples"], s["sha256"])


def runs(doc: Mapping[str, Any]) -> tuple[Run, ...]:
    """Runs of contiguous segment indices of a valid manifest, in order."""
    groups: list[list[Mapping[str, Any]]] = []
    for s in doc["segments"]:
        if groups and s["i"] == groups[-1][-1]["i"] + 1:
            groups[-1].append(s)
        else:
            groups.append([s])
    lead = doc["lead_ms"]
    return tuple(Run(tuple(_seg(s) for s in g), g[0]["t0_ms"], lead if g[0]["i"] == 0 else 0) for g in groups)


def is_clip(doc: Mapping[str, Any]) -> bool:
    """A voice-capture clip: a memo with cap (a cap on a meeting is ignored, docs/22 §8)."""
    return doc.get("mode") == "memo" and isinstance(doc.get("cap"), str)


def order_key(doc: Mapping[str, Any]) -> tuple[int, int, str]:
    rank = 0 if is_clip(doc) else 1 if doc.get("mode") == "memo" else 2
    return rank, -int(doc["started"]["wall_ms"]), str(doc["session"])
