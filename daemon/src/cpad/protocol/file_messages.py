"""v0.3 recording transfer messages (docs/11 §5.2, §5.3) as frozen dataclasses.

Host requests (FileList, FileGet, FileDel, DebugRec) get their id from the DeviceLink, so `id` is last
and defaults to "". Device replies (FileItems, FileData) are built by parse_* from a decoded message
that codec.decode_line already validated. `At` is shared with outbox items (messages.py re-exports it).
"""

from __future__ import annotations

import base64
import dataclasses
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any, ClassVar


@dataclass(frozen=True, slots=True)
class At:
    up_ms: int
    wall_ms: int | None = None


@dataclass(frozen=True, slots=True)
class Files:
    """files on hello / pong: committed segments on the card, index rev (0 = not ready), recording live."""

    n: int
    rev: int
    live: bool


@dataclass(frozen=True, slots=True)
class FileList:
    T: ClassVar[str] = "file.list"
    after: str | None = None
    max: int | None = None
    id: str = ""


@dataclass(frozen=True, slots=True)
class FileGet:
    T: ClassVar[str] = "file.get"
    fid: str
    off: int
    len: int
    id: str = ""


@dataclass(frozen=True, slots=True)
class FileDel:
    T: ClassVar[str] = "file.del"
    fid: str
    size: int
    sha256: str | None = None
    id: str = ""


@dataclass(frozen=True, slots=True)
class DebugRec:
    """Test firmware only: segment length for the next recordings."""

    T: ClassVar[str] = "debug.rec"
    seg_ms: int
    id: str = ""


@dataclass(frozen=True, slots=True)
class FileItem:
    """One committed segment as listed by file.items (size includes the 44 B WAV header)."""

    id: str
    kind: str
    size: int
    rec: str
    seg: int
    last: bool
    mode: str
    dur_ms: int
    at: At | None
    sha256: str | None = None
    project: str | None = None
    slot: int | None = None
    stars: tuple[int, ...] = ()
    lead_ms: int | None = None
    consent: str | None = None
    rcv: bool | None = None
    dest: str | None = None  # v0.4.1: memo segment 0 only; None = the daily note


@dataclass(frozen=True, slots=True)
class FileItems:
    T: ClassVar[str] = "file.items"
    id: str
    re: str
    items: tuple[FileItem, ...]
    more: bool


@dataclass(frozen=True, slots=True)
class FileData:
    T: ClassVar[str] = "file.data"
    id: str
    re: str
    fid: str
    off: int
    n: int
    size: int
    crc: int
    b64: str | None = None

    def payload(self) -> bytes:
        return base64.b64decode(self.b64) if self.b64 else b""


def pick(cls: type, d: Mapping[str, Any], **override: Any) -> Any:
    """Build a dataclass from the known keys of a wire dict; unknown fields are ignored (05 §6.2)."""
    names = {f.name for f in dataclasses.fields(cls)}
    return cls(**{k: v for k, v in d.items() if k in names and k not in override}, **override)


def parse_files(d: Mapping[str, Any]) -> Files | None:
    files = d.get("files")
    return pick(Files, files) if isinstance(files, Mapping) else None


def _file_item(d: Mapping[str, Any]) -> FileItem:
    at = pick(At, d["at"]) if isinstance(d.get("at"), Mapping) else None
    return pick(FileItem, d, at=at, stars=tuple(d.get("stars") or ()))


def parse_file_items(d: Mapping[str, Any]) -> FileItems:
    return pick(FileItems, d, items=tuple(_file_item(i) for i in d["items"]))


def parse_file_data(d: Mapping[str, Any]) -> FileData:
    return pick(FileData, d)
