"""Session completion and manifest.json v1 (docs/11 §6.4, §7.3; protocol/recording/v1). Pure.

`segs` are a session's rec.seg.stored records in segment order (index.segments).
- complete: segments 0..k all stored and segment k has last:true;
- finalisable with gaps: the device is idle and a full listing shows none of the session's segments;
  the missing indices become gaps, plus k+1 when the last stored segment has last:false;
- t0_ms: cumulative samples while segments are contiguous; after a gap, the up_ms difference to the
  session's first segment (one session never spans a reboot);
- marks: t_ms = t0_ms of the segment + the star's offset in it;
- project: the last stored segment's project / slot, since a meeting's tag may change (or be cleared) at any
  time and the last one wins (docs/11 §3.2, #46); mode, started, lead_ms, consent and device are the first's.
v0.4.1: a memo's destination (dest, segment 0 only) goes to rec.archived, never into manifest.json.
docs/23 §4.2: segment 0's env stamp (stamp.py), when well-formed, goes into manifest.json (an optional field of
contract v1) and rec.archived; without segment 0 or a stamp both stay as before.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from cpad.notes.render import clean
from cpad.recordings import records, stamp
from cpad.recordings.land import seg_name

Segs = tuple[Mapping[str, Any], ...]
HEADER = 44
MS_DIVISOR = 16  # samples per ms at 16 kHz
DURATION_MAX = 14_400_000
LABEL_MAX = 64


def samples(seg: Mapping[str, Any]) -> int:
    return (seg["size"] - HEADER) // 2


def is_complete(segs: Segs) -> bool:
    return bool(segs) and [s["seg"] for s in segs] == list(range(len(segs))) and bool(segs[-1].get("last"))


def gaps(segs: Segs) -> tuple[int, ...]:
    present = {s["seg"] for s in segs}
    top = max(present)
    missing = tuple(i for i in range(top) if i not in present)
    return missing + (() if segs[-1].get("last") else (top + 1,))


def finalisable(segs: Segs, *, listed: frozenset[str]) -> bool:
    """A full listing taken while the device was idle shows none of this session's segments."""
    return bool(segs) and segs[0]["rec"] not in listed


def _up(seg: Mapping[str, Any]) -> int | None:
    at = seg.get("at_dev")
    return at.get("up_ms") if isinstance(at, Mapping) else None


def t0_list(segs: Segs) -> tuple[int, ...]:
    out: list[int] = []
    base_up, anchor, since = _up(segs[0]), 0, 0
    for k, seg in enumerate(segs):
        if k == 0:
            out.append(0)
            continue
        prev = segs[k - 1]
        if seg["seg"] == prev["seg"] + 1:
            since += samples(prev)
        else:
            up = _up(seg)
            fallback = anchor + (since + samples(prev)) // MS_DIVISOR
            anchor = up - base_up if up is not None and base_up is not None and up >= base_up else fallback
            since = 0
        out.append(anchor + since // MS_DIVISOR)
    return tuple(out)


def _project(segs: Segs, names: Mapping[str, str], slots: Mapping[int, str]) -> dict[str, Any] | None:
    """The last stored segment's tag: each segment carries the tag current when it was written (#46)."""
    last = segs[-1]
    key = last.get("project") or slots.get(last.get("slot"))
    if not key:
        return None
    name = clean(names.get(key) or "").strip()[:LABEL_MAX] or None
    return {"key": key, "name": name}


def _segment(seg: Mapping[str, Any], t0: int) -> dict[str, Any]:
    return {"i": seg["seg"], "file": seg_name(seg["seg"]), "id": seg["id"], "t0_ms": t0, "samples": samples(seg),
            "bytes": seg["size"], "sha256": seg["file_sha256"], "recovered": bool(seg.get("rcv")),
            "repaired": bool(seg.get("repaired"))}  # fmt: skip


def _marks(segs: Segs, t0s: tuple[int, ...]) -> list[dict[str, Any]]:
    stars = (t0 + star for seg, t0 in zip(segs, t0s, strict=True) for star in seg.get("stars") or ())
    return [{"t_ms": t, "k": "star"} for t in sorted(stars)]


def build(segs: Segs, *, names: Mapping[str, str], slots: Mapping[int, str], tz_min: int,
          written_at: str) -> dict[str, Any]:  # fmt: skip
    """manifest.json for a complete or finalisable session (attempt fixed to 1 in v0.3.0)."""
    first, t0s = segs[0], t0_list(segs)
    doc: dict[str, Any] = {
        "v": 1, "kind": "cardputer.recording", "session": first["rec"], "attempt": 1, "mode": first["mode"],
        "project": _project(segs, names, slots),
        "started": {"wall_ms": first["wall_ms"], "tz_min": tz_min, "uncertain": bool(first.get("uncertain"))},
        "duration_ms": min(DURATION_MAX, t0s[-1] + samples(segs[-1]) // MS_DIVISOR),
        "lead_ms": (first.get("lead_ms") or 0) if first["seg"] == 0 else 0,
        "audio": {"sample_rate": 16000, "channels": 1, "bits": 16},
        "segments": [_segment(s, t0) for s, t0 in zip(segs, t0s, strict=True)],
        "gaps": list(gaps(segs)), "marks": _marks(segs, t0s),
        "device": {"dev": first["dev"], "fw": first.get("fw") or "cpa-fw/unknown"}, "written_at": written_at,
    }  # fmt: skip
    consent = first.get("consent") if first["seg"] == 0 and first["mode"] == "meeting" else None
    env = env_of(segs)
    return {**doc, **({"consent": consent} if consent == "ack" else {}), **({"env": env} if env else {})}


def env_of(segs: Segs) -> dict[str, Any] | None:
    """Segment 0's env stamp (docs/23 §4.2); None when segment 0 is missing or carries none."""
    return stamp.of_record(segs[0]) if segs and segs[0]["seg"] == 0 else None


def dest_of(segs: Segs) -> str | None:
    """The destination the device gave segment 0 (docs/13 §9.3); None when segment 0 is missing."""
    return segs[0].get("dest") if segs and segs[0]["seg"] == 0 else None


def archived_record(doc: Mapping[str, Any], *, dev: str, dir: str, at: str, dest: str | None = None) -> dict[str, Any]:
    project = doc["project"]["key"] if doc.get("project") else None
    return records.archived(dev=dev, rec=doc["session"], dir=dir, mode=doc["mode"], project=project,
                            segments=len(doc["segments"]), dur_ms=doc["duration_ms"],
                            started_ms=doc["started"]["wall_ms"], uncertain=doc["started"]["uncertain"],
                            gaps=doc["gaps"], consent=doc.get("consent"), at=at, dest=dest,
                            env=doc.get("env"))  # fmt: skip
