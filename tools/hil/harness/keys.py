"""Key plans for debug.key (protocol README "contract additions" 9; firmware lib/shell/debug_inject.h).

The test firmware presses a request's keys for hold_ms (default 50), releases them, and waits
30 ms before the next request; at most 16 requests wait in its inject queue (more are dropped).
Key names are the protocol's keyName enum (the hwtest 4x14 layout plus g0).
"""

from __future__ import annotations

import json
import math
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

COMMON_SCHEMA = Path(__file__).resolve().parents[3] / "protocol" / "schema" / "v0" / "common.schema.json"
KEY_NAMES = frozenset(json.loads(COMMON_SCHEMA.read_text(encoding="utf-8"))["$defs"]["keyName"]["enum"])
HOLD_DEFAULT_MS = 50
GAP_MS = 30
HOLD_MAX_MS = 5000
KEYS_MAX = 3
INJECT_QUEUE_MAX = 16
TEXT_MAX = 500
BASE_PUNCT = "-=[]\\;',./"


@dataclass(frozen=True, slots=True)
class Press:
    keys: tuple[str, ...]
    hold_ms: int | None = None


G0 = Press(("g0",))
ENTER = Press(("enter",))
ESCAPE = Press(("fn", "`"))
CONFIRM_GUARD = Press(("shift",), 600)  # a lone modifier is no key event; outlasts Confirm's 400 ms guard
IDLE_HOLD = Press(("shift",), HOLD_MAX_MS)


def char_press(ch: str) -> Press:
    if ch == "`":  # docs/20 D5: in the editor ` alone exits (or asks "Discard what you typed?"); Fn+` types it
        return Press(("fn", "`"))
    if len(ch) == 1 and ("a" <= ch <= "z" or "0" <= ch <= "9" or ch in BASE_PUNCT):
        return Press((ch,))
    if ch == " ":
        return Press(("space",))
    if len(ch) == 1 and "A" <= ch <= "Z":
        return Press(("shift", ch.lower()))
    raise ValueError(f"the harness does not type {ch!r}")


def text_presses(text: str) -> tuple[Press, ...]:
    return tuple(char_press(c) for c in text)


def numbered_texts(prefix: str, n: int) -> tuple[str, ...]:
    """n distinct short note texts, prefix + a zero-padded number (prefix: lower-case letters/digits)."""
    if n < 1 or not prefix or not all("a" <= c <= "z" or "0" <= c <= "9" for c in prefix):
        raise ValueError("numbered_texts needs a lower-case alphanumeric prefix and n >= 1")
    width = max(2, len(str(n - 1)))
    return tuple(f"{prefix}{i:0{width}d}" for i in range(n))


def _body(text: str, executable: bool) -> tuple[Press, ...]:
    if not 1 <= len(text.encode()) <= TEXT_MAX:
        raise ValueError("text must be 1 to 500 bytes")
    if executable and text.strip().startswith("-"):
        raise ValueError("spawn text must not start with '-' (the device refuses it)")
    return text_presses(text)


def note_presses(text: str) -> tuple[Press, ...]:
    """G0 (quick note), the text, Enter (save)."""
    return (G0, *_body(text, executable=False), ENTER)


def spawn_presses(text: str, slot: int) -> tuple[Press, ...]:
    """Opt+S (spawn compose), Opt+<slot> (target), the text, Enter (opens Confirm)."""
    if not 1 <= slot <= 9:
        raise ValueError("slot must be 1 to 9")
    return (Press(("opt", "s")), Press(("opt", str(slot))), *_body(text, executable=True), ENTER)


def wire(press: Press) -> dict[str, Any]:
    keys = press.keys
    if not 1 <= len(keys) <= KEYS_MAX or len(set(keys)) != len(keys) or not set(keys) <= KEY_NAMES:
        raise ValueError(f"bad debug.key keys {keys!r}")
    if press.hold_ms is not None and not 0 <= press.hold_ms <= HOLD_MAX_MS:
        raise ValueError("hold_ms must be 0 to 5000")
    msg: dict[str, Any] = {"t": "debug.key", "keys": list(keys)}
    return msg if press.hold_ms is None else {**msg, "hold_ms": press.hold_ms}


def duration_ms(press: Press) -> int:
    return (HOLD_DEFAULT_MS if press.hold_ms is None else press.hold_ms) + GAP_MS


def offline_plan(body: Sequence[Press], idle_ms: int, cap: int = INJECT_QUEUE_MAX) -> tuple[Press, ...]:
    """Lone-modifier holds that keep the device busy for idle_ms, then body.

    Preloaded while the daemon is Ready, the plan outlives the port release: after 15 s without
    host data the device falls back to Attached (offline) and only then reaches the body.
    """
    plan = (IDLE_HOLD,) * math.ceil(idle_ms / HOLD_MAX_MS) + tuple(body)
    if len(plan) > cap:
        raise ValueError(f"{len(plan)} presses do not fit the device inject queue of {cap}")
    return plan
