"""Protocol v0 messages as frozen dataclasses (05 §6.5).

Host-initiated messages get their id from the DeviceLink when sent, so `id` is the last field
and defaults to "". to_wire() renders the wire dict (None fields omitted); from_wire() turns a
decoded, already validated device message into its dataclass.
v0.4.1 (docs/13 §11.1, §12.2): caps.recs / caps.todo, host_id and host_label on the host hello, dest on a
note body; the recordings app's rec.* messages live in rec_messages.py.
"""

from __future__ import annotations

import dataclasses
from collections.abc import Mapping
from dataclasses import dataclass, field
from typing import Any, ClassVar

from cpad.protocol.file_messages import At, Files, parse_file_data, parse_file_items, parse_files, pick
from cpad.protocol.rec_messages import parse_rec_act, parse_rec_items, parse_rec_list

VERSION = 0


@dataclass(frozen=True, slots=True)
class Revs:
    projects: int = 0
    sessions: int = 0
    queue: int = 0


@dataclass(frozen=True, slots=True)
class Caps:
    spawn: bool
    reply: bool
    recs: bool | None = None  # v0.4.1: cpad answers rec.list / rec.act
    todo: bool | None = None  # v0.4.1: cpad writes a note with dest todo as a checkbox


# --- host -> device -------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class HostHello:
    T: ClassVar[str] = "hello"
    app: str
    agent: str
    caps: Caps
    host_id: str | None = None  # v0.4.1 (#19): reserved, random per Mac
    host_label: str | None = None  # v0.4.1: shown on the device's control page
    role: str = "host"
    id: str = ""


@dataclass(frozen=True, slots=True)
class Ping:
    T: ClassVar[str] = "ping"
    id: str = ""


@dataclass(frozen=True, slots=True)
class TimeSet:
    T: ClassVar[str] = "time.set"
    epoch_ms: int
    tz_min: int
    id: str = ""


@dataclass(frozen=True, slots=True)
class ProjectRow:
    slot: int
    key: str
    name: str
    run: int
    need: int
    queued: int
    ok: bool


@dataclass(frozen=True, slots=True)
class Other:
    run: int
    n: int


@dataclass(frozen=True, slots=True)
class ProjectsSnapshot:
    T: ClassVar[str] = "projects.snapshot"
    rev: int
    projects: tuple[ProjectRow, ...]
    other: Other
    id: str = ""


@dataclass(frozen=True, slots=True)
class SessionRow:
    sid: str
    slot: int
    name: str
    kind: str
    st: str
    need: bool
    age_s: int
    short: str | None = None


@dataclass(frozen=True, slots=True)
class SessionsSnapshot:
    T: ClassVar[str] = "sessions.snapshot"
    rev: int
    total: int
    sessions: tuple[SessionRow, ...]
    id: str = ""


@dataclass(frozen=True, slots=True)
class QueueRow:
    qid: str
    slot: int
    text: str
    intent: str
    sid: str | None = None
    uncertain: bool | None = None  # v0.4.1 (docs/20 U25): only true is sent


@dataclass(frozen=True, slots=True)
class QueueSnapshot:
    T: ClassVar[str] = "queue.snapshot"
    rev: int
    items: tuple[QueueRow, ...]
    id: str = ""


@dataclass(frozen=True, slots=True)
class OutboxAck:
    T: ClassVar[str] = "outbox.ack"
    re: str
    status: str
    code: str | None = None
    id: str = ""


@dataclass(frozen=True, slots=True)
class AgentResult:
    T: ClassVar[str] = "agent.result"
    re: str
    ok: bool
    code: str | None = None
    name: str | None = None
    short: str | None = None
    why: str | None = None  # v0.4 (#25), optional: untrusted | login
    id: str = ""


@dataclass(frozen=True, slots=True)
class Ack:
    T: ClassVar[str] = "ack"
    re: str
    id: str = ""


@dataclass(frozen=True, slots=True)
class ErrorMsg:
    T: ClassVar[str] = "error"
    re: str
    code: str
    field: str | None = None
    msg: str | None = None
    id: str = ""


# --- device -> host -------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class DeviceHello:
    T: ClassVar[str] = "hello"
    id: str
    re: str
    fw: str
    dev: str
    boot: str
    up_ms: int
    rst: str
    time: bool
    pending: int
    bat_mv: int
    revs: Revs
    store: str | None = None  # v0.2: "sd" / "int" / "none"; None from a v0.1 device
    launcher: bool | None = None  # v0.2: running under the Launcher
    files: Files | None = None  # v0.3: absent on a device that cannot serve file.*


@dataclass(frozen=True, slots=True)
class Pong:
    T: ClassVar[str] = "pong"
    id: str
    re: str
    up_ms: int
    pending: int
    revs: Revs
    files: Files | None = None


@dataclass(frozen=True, slots=True)
class NoteBody:
    text: str
    project: str | None = None
    dest: str | None = None  # v0.4.1: None = the daily note


@dataclass(frozen=True, slots=True)
class QueueBody:
    text: str
    project: str
    intent: str
    sid: str | None = None
    uncertain: bool | None = None


@dataclass(frozen=True, slots=True)
class OutboxItem:
    T: ClassVar[str] = "outbox.item"
    id: str
    kind: str
    at: At
    body: NoteBody | QueueBody


@dataclass(frozen=True, slots=True)
class AgentSpawn:
    T: ClassVar[str] = "agent.spawn"
    id: str
    project: str
    text: str
    qid: str | None = None


@dataclass(frozen=True, slots=True)
class AgentReply:
    T: ClassVar[str] = "agent.reply"
    id: str
    sid: str
    text: str


@dataclass(frozen=True, slots=True)
class QueueDone:
    T: ClassVar[str] = "queue.done"
    id: str
    qid: str


@dataclass(frozen=True, slots=True)
class InboxDismiss:
    T: ClassVar[str] = "inbox.dismiss"
    id: str
    sid: str


@dataclass(frozen=True, slots=True)
class Unhandled:
    """A valid message the daemon has no dataclass for (e.g. a debug.state summary)."""

    t: str
    id: str
    re: str | None = None
    raw: Mapping[str, Any] = field(default_factory=dict)


def _plain(value: Any) -> Any:
    if dataclasses.is_dataclass(value):
        pairs = ((f.name, getattr(value, f.name)) for f in dataclasses.fields(value))
        return {name: _plain(v) for name, v in pairs if v is not None}
    if isinstance(value, tuple | list):
        return [_plain(v) for v in value]
    return value


def to_wire(msg: Any) -> dict[str, Any]:
    head = {"v": VERSION, "t": msg.T, "id": msg.id}
    re = {"re": msg.re} if getattr(msg, "re", None) is not None else {}
    pairs = ((f.name, getattr(msg, f.name)) for f in dataclasses.fields(msg) if f.name not in ("id", "re"))
    return {**head, **re, **{name: _plain(value) for name, value in pairs if value is not None}}


def _revs(d: Mapping[str, Any]) -> Revs:
    return Revs(d["projects"], d["sessions"], d["queue"])


_pick = pick


def _hello(d: Mapping[str, Any]) -> Any:
    if d.get("role") != "device":
        return Unhandled(d["t"], d["id"], d.get("re"), d)
    return _pick(DeviceHello, d, revs=_revs(d["revs"]), files=parse_files(d))


def _outbox_item(d: Mapping[str, Any]) -> OutboxItem:
    body_cls = NoteBody if d["kind"] == "note" else QueueBody
    return _pick(OutboxItem, d, at=_pick(At, d["at"]), body=_pick(body_cls, d["body"]))


_PARSERS = {
    "hello": _hello,
    "pong": lambda d: _pick(Pong, d, revs=_revs(d["revs"]), files=parse_files(d)),
    "ack": lambda d: _pick(Ack, d),
    "error": lambda d: _pick(ErrorMsg, d),
    "outbox.item": _outbox_item,
    "agent.spawn": lambda d: _pick(AgentSpawn, d),
    "agent.reply": lambda d: _pick(AgentReply, d),
    "queue.done": lambda d: _pick(QueueDone, d),
    "inbox.dismiss": lambda d: _pick(InboxDismiss, d),
    "file.items": parse_file_items,
    "file.data": parse_file_data,
    "rec.list": parse_rec_list,
    "rec.act": parse_rec_act,
    "rec.items": parse_rec_items,
}


def from_wire(d: Mapping[str, Any]) -> Any:
    """Dataclass for a decoded device message (validated by codec.decode_line first)."""
    parser = _PARSERS.get(d["t"])
    return parser(d) if parser else Unhandled(d["t"], d["id"], d.get("re"), d)
