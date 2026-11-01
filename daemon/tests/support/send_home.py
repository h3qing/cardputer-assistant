"""A Mac for sending from it (docs/25 §5.4), over tests/support/filing_home: a fake LaunchServices, a fake pasteboard
and the shipped Grok preset, swapped into cpad.dest.cli_presets so nothing real is opened or copied (tests/conftest.py
refuses that anyway). `grok(paths)` adds url.grok as `cpa dest add --preset grok-bot` does and, when verified, runs
the one-time test and answers "appeared, not sent"."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path

from cpad.cli.main import main as cpa
from cpad.config.settings import Paths
from cpad.dest import cli_presets, send
from cpad.dest.macos import AppInfo, Signature
from cpad.dest.pasteboard import ClipWriter
from cpad.dest.presets import shipped_presets
from cpad.events.log import EventLog
from cpad.events.records import iso
from cpad.recordings import records as r
from tests.support import archive_tree as at
from tests.support import filing_home

TEAM = "ABCDE12345"
GROK = next(p for p in shipped_presets() if p.preset == "grok-bot")
NOTES = "invalid.example.notes"
FRESH = "3QZK1M7-00045-0000"  # a memo archived just now (a usual destination offers only the last day's)


@dataclass
class FakeMac:
    apps: dict[str, AppInfo] = field(default_factory=dict)
    opened: list[tuple[str, object]] = field(default_factory=list)

    def app_paths(self, bundle: str) -> tuple[Path, ...]:
        return (self.apps[bundle].path,) if bundle in self.apps else ()

    def info(self, app: Path) -> AppInfo | None:
        return next((a for a in self.apps.values() if a.path == app), None)

    def signature(self, app: Path) -> Signature:
        return Signature(True, TEAM)

    def open_url(self, url: str, app: Path) -> int:
        self.opened.append(("url", url))
        return 0

    def open_app(self, app: Path) -> int:
        self.opened.append(("app", app))
        return 0


class Board:
    def __init__(self) -> None:
        self.texts: list[str] = []

    def write(self, text: str) -> int:
        self.texts.append(text)
        return len(self.texts)

    def change_count(self) -> int:
        return len(self.texts)

    def clear(self) -> None:
        pass


def info(bundle: str, name: str, schemes: frozenset[str] = frozenset()) -> AppInfo:
    return AppInfo(Path(f"/Applications/{name}.app"), bundle, name, "1.0", schemes)


def install(monkeypatch, later_s: float = 3600.0) -> tuple[FakeMac, Board]:
    """The fakes, and a send clock `later_s` ahead (past the draft a one-time test leaves in the app)."""
    mac = FakeMac({GROK.bundle: info(GROK.bundle, "Grok Bot", frozenset({"grokbot", "sand"})),
                   NOTES: info(NOTES, "My Notes")})  # fmt: skip
    board = Board()
    monkeypatch.setattr(cli_presets, "MAC", lambda: mac)
    monkeypatch.setattr(cli_presets, "WRITER", lambda: ClipWriter(board, schedule=lambda _s, _f: None))
    monkeypatch.setattr(send, "NOW", lambda: time.time() + later_s)
    return mac, board


def build(home: Path, notes: Path) -> Paths:
    return filing_home.build(home, notes)


def grok(verified: bool = True) -> None:
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    if verified:
        assert cpa(["dest", "test", "url.grok", "--send"]) == 0
        assert cpa(["dest", "verify", "url.grok", "--draft"]) == 0


def fresh_memo(paths: Paths, project: str | None = "blog", text: str | None = "给 Grok：明天把首页改了",
               rec: str = FRESH, **over) -> str:  # fmt: skip
    """A memo (FRESH by default) archived now with its memo text."""
    now = iso(time.time())
    session = {**r.archived(dev=filing_home.DEV, rec=rec, dir=at.rec_dir("2026-10-22", "0900", rec), mode="memo",
                            project=project, segments=1, dur_ms=8000, started_ms=at.MEETING_WALL + 9_000_000,
                            uncertain=False, gaps=[], consent=None, at=now), **over}  # fmt: skip
    done = r.result(rec=rec, state="done", level="summary", title=None, memo_text=text, memo_intent=None,
                    actions_n=0, error_code=None, session=session, at=now)  # fmt: skip
    EventLog(paths.events).append((session, done))
    return rec
