"""Wait for the Launcher menu (the user presses RST, then Enter on the 5-second boot screen).

The console answers `version` both on the boot screen and in the menu, but on the boot screen the
Launcher boots the selected app after 5 s unless Enter is pressed, which would cut an install short.
The boot screen prints BANNER every 500 ms, so: ready = a `version` reply at least QUIET_S after the
last banner and after the port was opened (so a banner just about to come is not missed).
Presence is pure; wait_for_launcher is the polling shell.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, replace

from cpalauncher.console import Kind, classify
from cpalauncher.link import Console, PortLost

QUIET_S = 2.0  # four banner periods
POLL_S = 0.6  # one `version` query per window
RETRY_S = 0.5  # between attempts to open the port


@dataclass(frozen=True, slots=True)
class Presence:
    opened_at: float
    version: str | None = None
    version_at: float | None = None
    banner_at: float | None = None


def observe(p: Presence, line: str, now: float) -> tuple[Presence, str | None]:
    """The new presence and, the first time something is seen, a notice for the user."""
    reply = classify(line)
    if reply.kind is Kind.BANNER:
        notice = None if p.banner_at is not None else "Launcher boot screen: press Enter on the Cardputer now"
        return replace(p, banner_at=now), notice
    if reply.kind is Kind.VERSION:
        notice = None if p.version is not None else f"Launcher {reply.text} is answering"
        return replace(p, version=reply.text, version_at=now), notice
    return p, None


def ready(p: Presence) -> bool:
    if p.version_at is None:
        return False
    quiet_since = max(p.opened_at, p.banner_at if p.banner_at is not None else p.opened_at)
    return p.version_at - quiet_since >= QUIET_S


def wait_for_launcher(open_console: Callable[[], Console | None], clock: Callable[[], float],
                      sleep: Callable[[float], None], timeout_s: float,
                      say: Callable[[str], None]) -> tuple[Console, str] | None:  # fmt: skip
    """An open console in the Launcher menu and its version, or None after `timeout_s`."""
    deadline = clock() + timeout_s
    while clock() < deadline:
        console = open_console()
        if console is None:
            sleep(RETRY_S)
            continue
        version = _watch(console, clock, deadline, say)
        if version is not None:
            return console, version
        console.close()
        sleep(RETRY_S)
    return None


def _watch(console: Console, clock: Callable[[], float], deadline: float, say: Callable[[str], None]) -> str | None:
    presence = Presence(opened_at=clock())
    try:
        while clock() < deadline:
            console.send("version")
            for line in console.collect(POLL_S):
                presence, notice = observe(presence, line, clock())
                if notice:
                    say(notice)
            if ready(presence):
                return presence.version
    except PortLost:
        return None
    return None
