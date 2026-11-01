"""Device.home() on a faked shell: a draft never keeps it from reaching the launcher and is never saved.

The fake follows the firmware where home() depends on it (lib/shell/compose.cpp, shell.cpp dispatch): ` on
a draft with text opens the "discard?" prompt (docs/20 D5; Fn+` types a backtick), where Enter discards and
Fn+` or ` goes back to editing, and debug.state cannot tell the prompt from the editor. Enter at the prompt
within 400 ms of its opening does nothing (docs/20 U18, shell.cpp compose_key_event); the fake keeps the
device's time from each press's hold and gap (lib/shell/debug_inject.h).
"""

import asyncio
from dataclasses import dataclass, replace
from types import SimpleNamespace

import pytest

from harness import keys as k
from harness.device import Device
from harness.ui import MODIFIERS
from harness.wire import Rec

FN_GRAVE, GRAVE, ENTER, BKSP, TAB = ("fn", "`"), ("`",), ("enter",), ("bksp",), ("tab",)
OPT_H, OPT_L = ("opt", "h"), ("opt", "l")
GUARD_MS = 400  # lib/shell/confirm.h kConfirmGuardMs


@dataclass(frozen=True, slots=True)
class Shell:
    mod: str = "board"
    overlay: str = "none"
    text: str = ""
    discard: bool = False
    menu: bool = False
    locked: bool = False
    window: bool = False
    saved: tuple[str, ...] = ()
    clock_ms: int = 0
    asked_ms: int = -GUARD_MS  # when the prompt opened


def held_ms(wire) -> int:
    return wire.get("hold_ms", k.HOLD_DEFAULT_MS) + k.GAP_MS


def closed(s: Shell) -> Shell:
    return replace(s, overlay="none", text="", discard=False)


def compose_key(s: Shell, keys: tuple[str, ...]) -> Shell:
    if s.discard:
        back = keys in (FN_GRAVE, GRAVE)
        guarded = s.clock_ms - s.asked_ms < GUARD_MS
        return (s if guarded else closed(s)) if keys == ENTER else replace(s, discard=False) if back else s
    if keys == GRAVE:
        return replace(s, discard=True, asked_ms=s.clock_ms) if s.text else closed(s)
    if keys == BKSP:
        return replace(s, text=s.text[:-1]) if s.text else closed(s)
    if keys == ENTER:
        return closed(replace(s, saved=(*s.saved, s.text))) if s.text.strip() else s
    if keys == FN_GRAVE:  # docs/20 D5: the backtick, up to the 500-byte limit
        return replace(s, text=s.text + "`") if len(s.text) < k.TEXT_MAX else s
    printable = len(keys) == 1 and len(keys[0]) == 1
    return replace(s, text=s.text + keys[0]) if printable else s  # Opt+H is eaten too


def key(s: Shell, keys: tuple[str, ...]) -> Shell:
    if s.locked:
        unlock = keys == ENTER and s.window
        return replace(s, locked=not unlock, window=False) if unlock else replace(s, window=keys == OPT_L)
    if s.menu:
        return replace(s, menu=keys not in (FN_GRAVE, TAB))
    if keys == TAB:
        return replace(s, menu=True)
    if s.overlay == "compose":
        return compose_key(s, keys)
    return replace(s, mod="home") if keys == OPT_H else s


class FakeDevice(Device):
    def __init__(self, shell: Shell):
        super().__init__(SimpleNamespace(tap=None))
        self.shell = shell

    async def request(self, wire, timeout=0.0):
        if wire["t"] == "debug.key":
            if len(wire["keys"]) == 1 and wire["keys"][0] in MODIFIERS:
                self.shell = replace(self.shell, clock_ms=self.shell.clock_ms + held_ms(wire))  # no key event
            else:
                pressed = key(self.shell, tuple(wire["keys"]))
                self.shell = replace(pressed, clock_ms=pressed.clock_ms + held_ms(wire))
            reply = {"t": "ack"}
        else:
            s = self.shell
            reply = {"t": "debug.state", "mod": s.mod, "overlay": s.overlay, "ui": {"menu": s.menu, "locked": s.locked}}
        return Rec(0.0, "tx", b"", dict(wire)), Rec(0.0, "rx", b"", reply)


@pytest.fixture(autouse=True)
def no_pacing(monkeypatch):
    monkeypatch.setattr(k, "duration_ms", lambda press: 0)


def home(shell: Shell) -> Shell:
    dev = FakeDevice(shell)
    asyncio.run(dev.home())
    return dev.shell


@pytest.mark.parametrize(
    "start",
    [
        Shell(overlay="compose", text="half a note"),
        Shell(overlay="compose", text="half a note", discard=True),  # left at the prompt by an earlier step
        Shell(overlay="compose"),
        Shell(overlay="compose", text="x" * k.TEXT_MAX),
        Shell(overlay="compose", text="locked on a draft", menu=True, locked=True),
    ],
    ids=["editing", "at_the_prompt", "empty", "full", "locked_with_the_menu_open"],
)
def test_home_closes_a_draft_without_saving_it(start):
    end = home(start)
    assert (end.mod, end.overlay, end.menu, end.locked) == ("home", "none", False, False)
    assert end.saved == ()


def test_enter_right_after_the_prompt_opens_is_swallowed():
    """What broke home() after docs/20 U18: Fn+` ` Enter at debug.key pace (80 ms apart) leaves the prompt up."""
    s = Shell(overlay="compose", text="x")
    for keys in (FN_GRAVE, GRAVE, ENTER):
        s = replace(key(s, keys), clock_ms=s.clock_ms + 80)
    assert (s.overlay, s.discard) == ("compose", True)


def test_home_from_rest_only_presses_opt_h():
    assert replace(home(Shell()), clock_ms=0) == Shell(mod="home")
