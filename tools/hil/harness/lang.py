"""The device's UI language seen from the host (docs/21 §4.3, §7, §9.6): what debug.state reports, the
three presses that switch it on the settings page, and the end of every run, which puts back the language
the device showed when the run started (English, the default, when that is unknown).

A switch is kept in NVS, so a run that ended in the other language would leave the user's device in it.
restore_lang sends debug.lang (an ack and no write when the device already shows it) and never raises:
it runs in the runner's finally, after a failure too.
"""

from __future__ import annotations

import asyncio
from collections.abc import Mapping
from typing import Any, Protocol

from harness import keys as k

RESTORE_TIMEOUT_S = 15.0
FALLBACK_LANG = "en"  # what a run puts back when the language it started in is unknown
SWITCH_KEYS = (k.Press(("opt", "m")), k.Press((".",)), k.ENTER)  # Settings, the Language row, switch


class Requests(Protocol):
    async def request(self, wire: Mapping[str, Any], timeout: float = ...) -> tuple[Any, Any]: ...


def shown_lang(state: Mapping[str, Any]) -> str | None:
    """debug.state's ui.lang ("zh" / "en"); None from a build before docs/21."""
    return dict(state.get("ui") or {}).get("lang")


async def start_lang(dev: Requests) -> str:
    """The language to put back at the end: the one shown now, English when it cannot be read."""
    try:
        _, reply = await asyncio.wait_for(dev.request({"t": "debug.state"}), RESTORE_TIMEOUT_S)
    except Exception:  # an unreadable start is no reason to fail the run
        return FALLBACK_LANG
    return shown_lang(reply.msg) if shown_lang(reply.msg) in ("zh", "en") else FALLBACK_LANG


async def restore_lang(dev: Requests, lang: str = FALLBACK_LANG) -> str | None:
    """debug.lang <lang>; None when acked, else what went wrong (for the run's last lines)."""
    try:
        _, reply = await asyncio.wait_for(dev.request({"t": "debug.lang", "lang": lang}), RESTORE_TIMEOUT_S)
    except Exception as exc:  # the run is ending: say it, never raise
        return f"{type(exc).__name__}: {exc}"
    if reply.msg.get("t") == "ack":
        return None
    return f"debug.lang {lang} answered {reply.msg.get('t')} {reply.msg.get('code')}"
