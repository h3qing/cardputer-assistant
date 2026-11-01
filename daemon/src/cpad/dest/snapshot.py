"""dests.snapshot: the destinations the device may offer, built from destinations.json (docs/14 §7.2, §7.6).

Rows are the usable destinations (enabled, with an executor here, opted in when they leave the Mac),
sorted by slot. Every row carries its fl (model.flags; docs/25 §2.2), "" included: firmware treats a row
without fl as "lck", the strictest. Nothing else of a destination reaches the wire: no templates, bundle ids,
file paths or opt-in times. rev is a content hash, not a counter: the device caches the snapshot
across reboots and a counter would restart at 1 with cpad, so equal revs must mean equal rows.
The rows are fitted into one line (codec.fit_list) measured with the longest id the link can assign.
"""

from __future__ import annotations

import dataclasses
import json
import zlib
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any, ClassVar

from cpad.dest.catalog import Catalog, usable
from cpad.dest.config import load_catalog
from cpad.dest.model import DESTS_MAX, Dest, flags
from cpad.protocol import codec
from cpad.protocol.limits import LIMITS
from cpad.protocol.messages import to_wire

WORST_ID = "h" + "9" * (LIMITS["id_max"] - 1)


@dataclass(frozen=True, slots=True)
class DestRow:
    id: str
    slot: int
    label: str
    kind: str
    key: str | None = None
    fl: str = ""  # l live, c leaves the Mac, p needs a project, k confirm each on the device


@dataclass(frozen=True, slots=True)
class DestsSnapshot:
    T: ClassVar[str] = "dests.snapshot"
    rev: int
    dests: tuple[DestRow, ...]
    id: str = ""


def _plain(value: Any) -> Any:
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        value = {f.name: getattr(value, f.name) for f in dataclasses.fields(value)}
    if isinstance(value, dict):
        return {k: _plain(v) for k, v in value.items() if v is not None}
    if isinstance(value, tuple | list):
        return [_plain(v) for v in value]
    return value


def content_rev(rows: Sequence[Any]) -> int:
    """CRC-32 of the rows' canonical JSON (sorted keys, compact, None left out); 1 instead of 0."""
    text = json.dumps(_plain(list(rows)), ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return zlib.crc32(text.encode("utf-8")) or 1


def dest_row(dest: Dest) -> DestRow:
    return DestRow(id=dest.id, slot=dest.slot, label=dest.label, kind=dest.kind, key=dest.key, fl=flags(dest))


def dest_rows(cat: Catalog) -> tuple[DestRow, ...]:
    ordered = sorted(usable(cat), key=lambda d: d.slot)
    return tuple(dest_row(d) for d in ordered)[:DESTS_MAX]


def _fit(rows: tuple[DestRow, ...]) -> tuple[DestRow, ...]:
    _, kept = codec.fit_list(to_wire(DestsSnapshot(0xFFFFFFFF, rows, WORST_ID)), "dests")
    return rows[:kept]


def dests_snapshot(cat: Catalog) -> DestsSnapshot:
    rows = _fit(dest_rows(cat))
    return DestsSnapshot(rev=content_rev(rows), dests=rows)


def load_snapshot(path: Path) -> DestsSnapshot:
    """The snapshot for destinations.json at path: the defaults when missing, only the fixed four when broken."""
    return dests_snapshot(load_catalog(path))
