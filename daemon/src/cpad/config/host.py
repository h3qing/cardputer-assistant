"""This Mac's identity on the host hello (docs/13 §11.1, #19): host_id and host_label.

host_id is random, created once in $CPA_HOME/host_id (0600) and then only read. It is reserved: the device
decides nothing with it before v0.8. It is never written to a log. host_label is an optional name from
config.json, shown on the device's control page: 1-24 UTF-8 bytes without control characters. docs/23 §4.2: it also
names this Mac in the env stamp of new recordings; `cpa host name` sets it (valid_name: never starting with '-', and
none of the characters filing rows strip (filing.text.display_safe): C1, U+2028 / U+2029, bidirectional controls,
so the name stored in config.json, manifests and events is the name every reader shows).
id_key (docs/23 §3.2) is 32 random bytes in $CPA_HOME/id_key (0600, hex), the HMAC key behind the short device
ids of `cpa filing` and the export: one per Mac, so two Macs' ids cannot be joined. cpad creates it at start (like
host_id); readers that must not write to $CPA_HOME (the export) only read it. Never exported or logged.
"""

from __future__ import annotations

import contextlib
import logging
import os
import re
import secrets
from pathlib import Path
from typing import Any

from cpad.config.files import atomic_write
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import NO_CONTROL, matches, utf8_len

log = logging.getLogger("cpad.config")
KEY_HEX = re.compile(r"[0-9a-f]{64}\Z")
UNSAFE = re.compile("[\u0080-\u009f\u2028\u2029\u202a-\u202e\u2066-\u2069]")  # beyond NO_CONTROL (C0, DEL)


def _stored(path: Path) -> str | None:
    try:
        return path.read_bytes().decode("ascii").strip()
    except FileNotFoundError:
        return None
    except UnicodeDecodeError:
        return ""


def host_id(path: Path) -> str:
    """The id stored at `path`; a missing file gets a fresh one, a malformed one is replaced (logged, no value)."""
    stored = _stored(path)
    if matches(LIMITS["host_id_pattern"], stored):
        return stored
    fresh = secrets.token_hex(8)
    atomic_write(path, (fresh + "\n").encode("ascii"))
    if stored is not None:
        log.warning("host_id was malformed; replaced")
    return fresh


def _encodable(text: str) -> bool:
    """Strict UTF-8, as codec.serialize writes it: a lone surrogate would crash every host hello."""
    try:
        text.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return True


def valid_label(value: Any) -> bool:
    return (isinstance(value, str) and _encodable(value) and 1 <= utf8_len(value) <= LIMITS["host_label_max"]
            and matches(NO_CONTROL, value))  # fmt: skip


def _create_once(path: Path, data: bytes) -> None:
    """Create `path` with `data` unless it exists: link(2) of a finished temp file never clobbers a concurrent
    writer's key, and a reader never sees a half-written one."""
    tmp = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    atomic_write(tmp, data)
    try:
        os.link(tmp, path)
    except FileExistsError:
        pass
    finally:
        with contextlib.suppress(FileNotFoundError):
            tmp.unlink()


def stored_id_key(path: Path) -> bytes | None:
    """The key stored at `path`, or None when it is missing or malformed; never writes."""
    stored = _stored(path)
    return bytes.fromhex(stored) if stored is not None and KEY_HEX.match(stored) else None


def id_key(path: Path) -> bytes:
    """The key stored at `path`; a missing file gets a fresh one, a malformed one is replaced (logged, no value)."""
    if _stored(path) is None:
        _create_once(path, (secrets.token_hex(32) + "\n").encode("ascii"))
    stored = stored_id_key(path)
    if stored is not None:
        return stored
    fresh = secrets.token_hex(32)
    atomic_write(path, (fresh + "\n").encode("ascii"))
    log.warning("id_key was malformed; replaced")
    return bytes.fromhex(fresh)


def valid_name(value: Any) -> bool:
    """A name `cpa host name` accepts: a label the device takes on the hello, never one that reads like an option."""
    return valid_label(value) and not value.startswith("-") and not UNSAFE.search(value)
