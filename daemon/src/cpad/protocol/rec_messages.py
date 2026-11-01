"""v0.4.1 recordings app messages (docs/13 §8, §12.2) as frozen dataclasses.

The device asks for one page of sessions (RecList) or acts on one session (RecAct); cpad answers with
RecItems, Ack or ErrorMsg. Device requests are built by parse_* from a decoded message that
codec.decode_line already validated (unknown fields ignored). RecItems is a host reply: the DeviceLink
gives it its id, so `id` is last and defaults to "". Optional row fields left None are omitted on the wire.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any, ClassVar

from cpad.protocol.file_messages import pick


@dataclass(frozen=True, slots=True)
class RecList:
    """filter: all / starred / memo / meeting; cursor: the `next` of the previous page, opaque to the device."""

    T: ClassVar[str] = "rec.list"
    id: str
    filter: str
    max: int
    cursor: str | None = None


@dataclass(frozen=True, slots=True)
class RecAct:
    """op: star / unstar / tag / hide. With tag, project None means "no project"."""

    T: ClassVar[str] = "rec.act"
    id: str
    rec: str
    op: str
    project: str | None = None


@dataclass(frozen=True, slots=True)
class RecRow:
    """One session as the recordings app lists it; st is a sessions.state_of value."""

    rec: str
    mode: str
    st: str
    wall_ms: int | None = None
    dur_ms: int | None = None
    project: str | None = None
    starred: bool | None = None
    marks: int | None = None
    title: str | None = None
    todo_n: int | None = None


@dataclass(frozen=True, slots=True)
class RecItems:
    T: ClassVar[str] = "rec.items"
    re: str
    items: tuple[RecRow, ...]
    next: str | None = None
    id: str = ""


def parse_rec_list(d: Mapping[str, Any]) -> RecList:
    return pick(RecList, d)


def parse_rec_act(d: Mapping[str, Any]) -> RecAct:
    return pick(RecAct, d)


def parse_rec_items(d: Mapping[str, Any]) -> RecItems:
    return pick(RecItems, d, items=tuple(pick(RecRow, row) for row in d["items"]))
