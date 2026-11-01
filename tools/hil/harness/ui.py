"""The v0.4.0 shell seen from the host (docs/13 §5-§7): pure parts of the nav, menu and lock scenarios.

debug.state.ui {menu, locked} says whether the Tab menu is open and whether the device is locked; overlay
still reports the layer under the menu (§12.1). A build before v0.4.0 has no ui: both read as false.
The home launcher is the root page (§5.5): a tile letter presses Opt+letter for the user, Enter the
selected tile, 1-9 Opt+1-9. Locked, only Opt+L (lights the card, opens a 3 s window), Enter in that
window (unlocks) and G0 get through the lock gate (§7.1); everything else is swallowed.
"""

from __future__ import annotations

import random
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from harness import keys as k

ROOT = "home"
TAB = k.Press(("tab",))
GRAVE = k.Press(("`",))
SPACE = k.Press(("space",))
SLASH = k.Press(("/",))
OPT_H = k.Press(("opt", "h"))
OPT_P = k.Press(("opt", "p"))
OPT_I = k.Press(("opt", "i"))
OPT_M = k.Press(("opt", "m"))
OPT_L = k.Press(("opt", "l"))
OPT_R = k.Press(("opt", "r"))
UNLOCK = (OPT_L, k.ENTER)
UNLOCK_WINDOW_S = 3.0
CARD_S = 3.0  # the lock card stays lit this long after Opt+L or G0
# docs/20 D5: Compose with text answers ` with its "discard?" prompt and a second ` goes back to editing
# (an empty draft just closes), Fn+` types a backtick, and debug.state cannot tell the prompt from the
# editor: Fn+` (at the prompt: back to editing; editing: one more character, so never empty), then `
# (the prompt, for sure) and Enter (discards) close a draft from either without saving it. The prompt
# ignores Enter for its first 400 ms (docs/20 U18), so CONFIRM_GUARD waits that out before Enter.
COMPOSE_CLOSE = (k.ESCAPE, GRAVE, k.CONFIRM_GUARD, k.ENTER)
OPT_ONLY = frozenset("rl")  # docs/20 D1: tiles whose letter alone does nothing on home (Opt+R, Opt+L)
MODIFIERS = ("fn", "shift", "ctrl", "opt", "alt")
LOCK_KEYS = frozenset({"g0", "enter"})  # with Opt+L: what the lock gate may let through


def ui_of(state: Mapping[str, Any]) -> dict[str, Any]:
    return dict(state.get("ui") or {})


def menu_open(state: Mapping[str, Any]) -> bool:
    return bool(ui_of(state).get("menu"))


def locked(state: Mapping[str, Any]) -> bool:
    return bool(ui_of(state).get("locked"))


def unwind_step(state: Mapping[str, Any]) -> tuple[k.Press, ...]:
    """What to press to peel one layer off on the way back to rest: unlock first (a locked device ignores
    everything else), then Fn+` for the menu or an overlay, COMPOSE_CLOSE for Compose; () at rest."""
    if locked(state):
        return UNLOCK
    if not menu_open(state) and state.get("overlay") == "compose":
        return COMPOSE_CLOSE
    if menu_open(state) or state.get("overlay", "none") != "none":
        return (k.ESCAPE,)
    return ()


@dataclass(frozen=True, slots=True)
class View:
    """What debug.state should show: the page (None: any), the overlay, the menu and the lock."""

    mod: str | None = ROOT
    overlay: str = "none"
    menu: bool = False
    locked: bool = False

    def met(self, state: Mapping[str, Any]) -> bool:
        return (
            (self.mod is None or state.get("mod") == self.mod)
            and state.get("overlay") == self.overlay
            and menu_open(state) == self.menu
            and locked(state) == self.locked
        )

    def text(self) -> str:
        return _text(self.mod or "*", self.overlay, self.menu, self.locked)


def _text(mod: Any, overlay: Any, menu: bool, lock: bool) -> str:
    return f"{mod}/{overlay}" + ("+menu" if menu else "") + ("+locked" if lock else "")


def view_text(state: Mapping[str, Any]) -> str:
    return _text(state.get("mod"), state.get("overlay"), menu_open(state), locked(state))


# --- nav -----------------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class NavStep:
    """From ROOT at rest: press `before`, then `press`, expect `want`; then `back` must lead to ROOT."""

    name: str
    press: k.Press
    want: View
    back: tuple[k.Press, ...] = ()
    before: tuple[k.Press, ...] = ()


def _tile(letter: str, want: View, back: tuple[k.Press, ...]) -> NavStep:
    return NavStep(f"tile_{letter}", k.Press((letter,)), want, back)


def nav_plan(rec: bool) -> tuple[NavStep, ...]:
    """Every launcher tile, Enter on a tile, a digit and Opt+H/P/I/M. rec: the card is there, so R opens
    the meeting confirm (its 400 ms guard is waited out before ` cancels it); without it R is refused.
    docs/20 D1: the R and L tiles by Enter on them (their letters alone stay on home, at rest); D5: ` leaves
    an empty editor (Fn+` would type a backtick)."""
    meeting = (View(ROOT, "confirm"), (k.CONFIRM_GUARD, GRAVE)) if rec else (View(ROOT), ())
    compose = (View(ROOT, "compose"), (GRAVE,))
    up = k.Press((";",))
    return (
        _tile("p", View("board"), (GRAVE,)),
        _tile("i", View("inbox"), (GRAVE,)),
        NavStep("tile_r", k.ENTER, *meeting, before=(SLASH, SLASH)),
        _tile("n", *compose),
        _tile("q", *compose),
        _tile("s", *compose),
        _tile("m", View("system"), (GRAVE,)),
        NavStep("tile_l", k.ENTER, View(ROOT, locked=True), UNLOCK, before=(up, SLASH)),
        NavStep("letter_r", k.Press(("r",)), View(ROOT)),
        NavStep("letter_l", k.Press(("l",)), View(ROOT)),
        NavStep("enter_first_tile", k.ENTER, View("board"), (GRAVE,)),
        NavStep("enter_after_move", k.ENTER, View("inbox"), (GRAVE,), before=(SLASH,)),
        NavStep("digit_1", k.Press(("1",)), View("board"), (GRAVE,)),
        NavStep("opt_p", OPT_P, View("board"), (GRAVE,)),
        NavStep("opt_i", OPT_I, View("inbox"), (GRAVE,)),
        NavStep("opt_m", OPT_M, View("system"), (GRAVE,)),
        NavStep("opt_h", OPT_H, View(ROOT), (GRAVE,), before=(OPT_P, OPT_I)),  # clears the stack
    )


# --- menu ----------------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class MenuPlace:
    name: str
    opener: tuple[k.Press, ...]  # from ROOT at rest
    under: View  # what the page shows with the menu closed


@dataclass(frozen=True, slots=True)
class MenuRun:
    """From ROOT at rest: `before`, then `keys` (Tab first) must run a row: the menu closes on `want`."""

    name: str
    before: tuple[k.Press, ...]
    keys: tuple[k.Press, ...]
    want: View


MENU_PLACES = (
    MenuPlace("home", (), View(ROOT)),
    MenuPlace("board", (OPT_P,), View("board")),
    MenuPlace("inbox", (OPT_I,), View("inbox")),
    MenuPlace("system", (OPT_M,), View("system")),
    MenuPlace("compose", (k.G0,), View(ROOT, "compose")),
)
MENU_RUNS = (
    MenuRun("here_first_row_on_home", (), (TAB, k.ENTER), View("board")),  # Here row 1: P Projects
    MenuRun("goto_first_row", (OPT_P,), (TAB, SLASH, k.ENTER), View(ROOT)),  # Go row 1: Opt+H Home
    MenuRun("goto_letter", (OPT_P,), (TAB, SLASH, k.Press(("i",))), View("inbox")),  # a letter = Opt+letter
    MenuRun("control_first_row", (), (TAB, SLASH, SLASH, k.ENTER), View(ROOT, locked=True)),  # Opt+L
)
# The meeting page's own keys (★, a project tag, untag): with the menu open they must not reach the page.
# None closes the menu or turns its page (on Control a letter would replay Opt+letter). Not Opt+R: the menu
# shows it (End meeting…), and docs/20 D9 runs a chord the menu shows (the menu closes, the end box opens).
MEETING_LEAKS = (SPACE, k.Press(("1",)), k.Press(("0",)))


def meeting_problems(manifest: Mapping[str, Any], stars: int) -> list[str]:
    """A meeting from ROOT (no project) where exactly `stars` ★ reached the page."""
    got = sum(1 for m in manifest.get("marks") or [] if m.get("k") == "star")
    out = [] if got == stars else [f"stars {got} != {stars}"]
    project = manifest.get("project")
    return out + ([f"tagged {project.get('key')}"] if project else [])


# --- lock ----------------------------------------------------------------------------------------


def is_noise(press: k.Press) -> bool:
    """A press the lock gate must swallow: no G0, no Enter, not Opt+L."""
    keys = set(press.keys)
    return not keys & LOCK_KEYS and not {"opt", "l"} <= keys


def noise_presses(rng: random.Random, n: int) -> tuple[k.Press, ...]:
    """n random single keys and modifier chords, none of them one the lock gate lets through."""
    plain = sorted(k.KEY_NAMES - LOCK_KEYS - set(MODIFIERS))
    out: list[k.Press] = []
    while len(out) < n:
        key = rng.choice(plain)
        press = k.Press((key,)) if rng.random() < 0.5 else k.Press((rng.choice(MODIFIERS), key))
        if is_noise(press):
            out.append(press)
    return tuple(out)


def _facts(state: Mapping[str, Any]) -> dict[str, Any]:
    outbox, rec = dict(state.get("outbox") or {}), dict(state.get("rec") or {})
    return {
        "mod": state.get("mod"),
        "overlay": state.get("overlay"),
        "menu": menu_open(state),
        "locked": locked(state),
        "pending": outbox.get("pending"),
        "bad": outbox.get("bad"),
        "live": rec.get("live"),
        "mode": rec.get("mode"),
        "files_n": rec.get("files_n"),
    }


def changed(before: Mapping[str, Any], after: Mapping[str, Any]) -> tuple[str, ...]:
    """The facts a swallowed key must not move (page, overlay, menu, lock, outbox, recording) that moved."""
    a, b = _facts(before), _facts(after)
    return tuple(key for key in a if a[key] != b[key])
