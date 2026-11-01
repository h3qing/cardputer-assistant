"""What cli.py and cli_presets.py share: saving a change with its dest.changed record, and the opt-in question."""

from __future__ import annotations

import time
from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path
from typing import Any, Literal

from cpad.config.settings import Paths, secure_home
from cpad.dest.catalog import Catalog
from cpad.dest.config import dests_path, save_dests
from cpad.dest.model import Dest
from cpad.dest.records import dest_changed
from cpad.events.log import EventLog
from cpad.events.records import iso

STATE_TEXT = {"on": "on", "off": "off", "needs_optin": "needs opt-in", "unavailable": "not in this version"}


def record(paths: Paths, cat: Catalog, op: Literal["add", "rm", "bind", "verify", "map"], dest: Dest) -> None:
    save(paths, cat, dest_changed(op, dest, by="cli", at=iso(time.time())))


def save(paths: Paths, cat: Catalog, changed: Mapping[str, Any]) -> None:
    """destinations.json (atomic, 0600), then the dest.changed record that says what changed."""
    secure_home(paths)
    save_dests(dests_path(paths), cat)
    EventLog(paths.events).append([changed])


def root(text: str) -> str:
    return text if text.startswith(("~", "/")) else str(Path(text).absolute())


def _where(dest: Dest) -> str:
    if dest.file is not None:
        return f"{dest.file.root.rstrip('/')}/{dest.file.path} (this folder may be synced elsewhere)"
    pinned = dest.url or dest.app
    return f"the app {pinned.bundle}, which may send it on (its cloud, its sync)" if pinned else dest.id


def confirm(dest: Dest, yes: bool, note: str | None = None) -> bool:
    """Explains what leaves the Mac and where; True on a typed y (or --yes)."""
    print(f"{dest.id} ({dest.label}) sends text off this Mac.")
    if note:
        print(f"  {note}")
    print(f"  what:  only the text of a capture you send there (key {(dest.key or '-').upper()} or the picker);")
    print("         never audio, meeting transcripts or summaries")
    print(f"  where: {_where(dest)}")
    print("  every delivery is logged with its size and hash, never its text: cpa audit --leaves-mac")
    if yes:
        return True
    try:
        answer = input("Type y to allow it: ")
    except EOFError:
        answer = ""
    return answer.strip().lower() == "y"


def opt_in(dest: Dest, yes: bool, note: str | None = None) -> Dest | None:
    """dest with its optin set now when it leaves the Mac and has none; None when you said no."""
    if not dest.leaves_mac or dest.optin is not None:
        return dest
    if not confirm(dest, yes, note):
        print("not changed")
        return None
    return replace(dest, optin=iso(time.time()))
