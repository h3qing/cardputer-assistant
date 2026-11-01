"""The Launcher 2.9.1 serial console, as text (pure).

Grammar (bmorcelli/Launcher src/serial_console.cpp; the 2.9.1 binary carries the same strings): one
command per line; replies are lines. `flash firmware <name> <size>` answers "READY <size>", then
"ACK <written>/<size>" per chunk read, then "OK flashed, rebooting" (or "ERR ..."). The boot screen
prints BANNER every 500 ms. `partitions` prints "== Partition table ==", one row per entry, `<free>`
rows and a closing "Flash size: ..." line.
"""

from __future__ import annotations

import enum
import re
from collections.abc import Iterable
from dataclasses import dataclass

BANNER = "Press the button to enter the Launcher!"
NAME_MAX = 20  # the Launcher shows at most 20 characters of an app name
READ_ONLY = frozenset({"version", "partitions", "whoami", "help"})  # never wifi/settings: they print secrets

_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
_READY_RE = re.compile(r"^READY (\d+)$")
_ACK_RE = re.compile(r"^ACK (\d+)/(\d+)$")
_VERSION_RE = re.compile(r"^Launcher (\d+\.\d+\.\d+\S*)$")
_ROW_RE = re.compile(
    r"^(?P<label>\S+)\s+type=(?P<type>\S+)\s+subtype=(?P<subtype>\S+)\s+offset=0x(?P<offset>[0-9A-Fa-f]+)"
    r"\s+size=0x(?P<size>[0-9A-Fa-f]+)\s+\([^)]*\)(?P<boot>\s+<-- BOOT)?$"
)
_FREE_RE = re.compile(r"^<free>\s+offset=0x(?P<offset>[0-9A-Fa-f]+)\s+size=0x(?P<size>[0-9A-Fa-f]+)\s")
_TABLE_HEAD = "== Partition table =="
_TABLE_END = "Flash size:"


class Kind(enum.Enum):
    READY = "ready"
    ACK = "ack"
    OK = "ok"
    ERR = "err"
    BANNER = "banner"
    VERSION = "version"
    MALFORMED = "malformed"
    OTHER = "other"


@dataclass(frozen=True, slots=True)
class Reply:
    kind: Kind
    text: str = ""
    n: int = 0
    total: int = 0


def classify(line: str) -> Reply:
    """What one console line means to a host script."""
    text = line.strip()
    if text.startswith(("READY", "ACK")):
        return _counter(text)
    if text == "OK" or text.startswith("OK "):
        return Reply(Kind.OK, text)
    if text == "ERR" or text.startswith("ERR "):
        return Reply(Kind.ERR, text)
    if text == BANNER:
        return Reply(Kind.BANNER, text)
    version = _VERSION_RE.match(text)
    if version:
        return Reply(Kind.VERSION, version.group(1))
    return Reply(Kind.OTHER, text)


def _counter(text: str) -> Reply:
    ready = _READY_RE.match(text)
    if ready:
        return Reply(Kind.READY, text, int(ready.group(1)))
    ack = _ACK_RE.match(text)
    if ack:
        return Reply(Kind.ACK, text, int(ack.group(1)), int(ack.group(2)))
    return Reply(Kind.MALFORMED, text)


@dataclass(frozen=True, slots=True)
class Partition:
    label: str
    type: str
    subtype: str
    offset: int
    size: int
    boot: bool

    @property
    def is_ota_app(self) -> bool:
        return self.type == "app" and self.subtype.startswith("ota_")


@dataclass(frozen=True, slots=True)
class Table:
    entries: tuple[Partition, ...] = ()
    free: tuple[tuple[int, int], ...] = ()
    complete: bool = False  # the closing "Flash size:" line arrived

    def find(self, label: str) -> Partition | None:
        return next((p for p in self.entries if p.label == label), None)

    def ota_apps(self) -> tuple[Partition, ...]:
        return tuple(p for p in self.entries if p.is_ota_app)

    def boot_app(self) -> Partition | None:
        return next((p for p in self.ota_apps() if p.boot), None)


def parse_partitions(lines: Iterable[str]) -> Table:
    """Rows after the table header; everything else (banners, log noise) is skipped."""
    entries: list[Partition] = []
    free: list[tuple[int, int]] = []
    started = complete = False
    for raw in lines:
        text = raw.strip()
        if text == _TABLE_HEAD:
            started = True
        elif started and text.startswith(_TABLE_END):
            complete = True
            break
        elif started:
            _add_row(text, entries, free)
    return Table(tuple(entries), tuple(free), complete)


def _add_row(text: str, entries: list[Partition], free: list[tuple[int, int]]) -> None:
    row = _ROW_RE.match(text)
    if row:
        entries.append(Partition(row["label"], row["type"], row["subtype"], int(row["offset"], 16),
                                 int(row["size"], 16), row["boot"] is not None))  # fmt: skip
        return
    gap = _FREE_RE.match(text + " ")
    if gap:
        free.append((int(gap["offset"], 16), int(gap["size"], 16)))


def label_base(name: str) -> str:
    """launcherPartitionSanitizedAppLabelBase: first 6 lowercase letters/digits, padded with '0'."""
    base = "".join(c for c in name.lower() if c.isascii() and c.isalnum())[:6]
    return (base or "app").ljust(6, "0")


def check_name(name: str) -> str | None:
    """None when `name` is safe as one console token and as the Launcher's display name, else why not."""
    if not name or len(name) > NAME_MAX:
        return f"name must be 1..{NAME_MAX} characters"
    if not name.isascii() or not _NAME_RE.match(name):
        return "name may use only letters, digits, '.', '_' and '-', and must start with a letter or digit"
    return None


def read_only_command(line: str) -> bool:
    """Commands this tool sends on request: they neither change the device nor print secrets."""
    return line.strip() in READ_ONLY
