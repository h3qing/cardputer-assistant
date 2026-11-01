"""v0.4.0 shell UI seen from the host (docs/13 §5-§7): debug.state.ui, the way back to rest, the nav
and menu plans (checked against the firmware's own tables) and the lock scenario's pure parts."""

import importlib
import random
import re
from pathlib import Path

import pytest

from harness import keys as k
from harness.ui import (
    COMPOSE_CLOSE,
    GRAVE,
    MEETING_LEAKS,
    MENU_PLACES,
    MENU_RUNS,
    OPT_H,
    OPT_L,
    OPT_ONLY,
    ROOT,
    TAB,
    UNLOCK,
    View,
    changed,
    is_noise,
    locked,
    meeting_problems,
    menu_open,
    nav_plan,
    noise_presses,
    unwind_step,
    view_text,
)

FIRMWARE = Path(__file__).resolve().parents[3] / "firmware" / "app"


def state(mod=ROOT, overlay="none", ui=None, **extra):
    return {"mod": mod, "overlay": overlay, **({} if ui is None else {"ui": ui}), **extra}


def home_tile_letters() -> set[str]:
    """The letters of the launcher's tiles (modules/home/home_logic.h kHomeTiles)."""
    src = (FIRMWARE / "modules" / "home" / "home_logic.h").read_text(encoding="utf-8")
    block = src[src.index("kHomeTiles[") : src.index("};", src.index("kHomeTiles["))]
    return set(re.findall(r"home_chord\('([a-z])'\)", block))


def global_opt_actions() -> dict[str, str]:
    """Opt+letter -> Action on the Global layer (lib/input/keymap.cpp kKeymap)."""
    src = (FIRMWARE / "lib" / "input" / "keymap.cpp").read_text(encoding="utf-8")
    return dict((key, act) for act, key in re.findall(r"bind\(Layer::Global, Action::(\w+), opt\('(\w)'\)", src))


# --- debug.state.ui ------------------------------------------------------------------------------


@pytest.mark.parametrize("ui", [None, {}, {"menu": False, "locked": False}])
def test_no_ui_or_null_reads_as_neither_menu_nor_locked(ui):
    s = state(ui=ui) if ui is not None else {**state(), "ui": None}
    assert (menu_open(s), locked(s)) == (False, False)


def test_ui_flags_are_read():
    assert menu_open(state(ui={"menu": True, "locked": False}))
    assert locked(state(ui={"menu": False, "locked": True}))


# --- the way back to rest (Device.home) -------------------------------------------------------------


def test_unlock_comes_first_because_a_locked_device_ignores_everything_else():
    s = state(overlay="compose", ui={"menu": True, "locked": True})
    assert unwind_step(s) == UNLOCK == (OPT_L, k.ENTER)


@pytest.mark.parametrize(
    "s",
    [state(ui={"menu": True, "locked": False}), state(overlay="compose", ui={"menu": True}), state(overlay="confirm")],
)
def test_the_menu_and_overlays_close_with_fn_grave(s):
    assert unwind_step(s) == (k.ESCAPE,)


def test_compose_closes_with_fn_grave_grave_enter_whether_editing_or_at_its_discard_prompt():
    """docs/20 D5: ` alone toggles a draft with text between the editor and "discard?" (debug.state shows
    neither): Fn+` puts it in the editor with text, ` then opens the prompt for sure, and Enter there
    discards once the prompt's 400 ms guard is over (docs/20 U18)."""
    assert unwind_step(state(overlay="compose")) == COMPOSE_CLOSE == (k.ESCAPE, GRAVE, k.CONFIRM_GUARD, k.ENTER)


def test_compose_close_outlasts_the_discard_prompts_guard():
    """shell.cpp compose_key_event ignores Enter at the prompt for kConfirmGuardMs after it opened."""
    shell = (FIRMWARE / "lib" / "shell" / "shell.cpp").read_text(encoding="utf-8")
    confirm_h = (FIRMWARE / "lib" / "shell" / "confirm.h").read_text(encoding="utf-8")
    assert "compose_.discard && enter && now_ - discard_ms_ < kConfirmGuardMs" in shell
    guard_ms = int(re.search(r"kConfirmGuardMs = (\d+);", confirm_h).group(1))
    assert k.CONFIRM_GUARD.hold_ms > guard_ms


def test_the_firmware_types_fn_grave_in_compose_and_takes_both_as_back_at_the_prompt():
    """COMPOSE_CLOSE relies on these (lib/input/keymap.cpp, lib/shell/compose.cpp discard_prompt): docs/20
    D5, ` cancels and Fn+` types the backtick in Compose; at the prompt Enter discards, ` and Fn+` go back."""
    keymap = (FIRMWARE / "lib" / "input" / "keymap.cpp").read_text(encoding="utf-8")
    compose = (FIRMWARE / "lib" / "shell" / "compose.cpp").read_text(encoding="utf-8")
    assert "bind(Layer::Compose, Action::Cancel, chr('`'))" in keymap
    assert "bind(Layer::Compose, Action::Backtick, nav(Nav::Back))" in keymap
    assert "case Action::Backtick: return type_char(s, '`');" in compose
    prompt = compose[compose.index("ComposeStep discard_prompt") :]
    assert "Nav::Enter) return step(s, ComposeResult::Cancelled)" in prompt and "ev.ch == '`'" in prompt
    assert "ev.nav == Nav::Back" in prompt


def test_latency_closes_its_empty_editor_with_grave():
    """docs/20 D5: the latency scenario's G0 taps open an empty editor; ` alone closes it (Fn+` would type a
    backtick and leave it open, so the next tap's wait for "none" would time out)."""
    latency = importlib.import_module("scenarios.latency")  # the package exports the function by that name
    assert latency.COMPOSE_CLOSE_EMPTY == GRAVE != k.ESCAPE
    keymap = (FIRMWARE / "lib" / "input" / "keymap.cpp").read_text(encoding="utf-8")
    assert "bind(Layer::Compose, Action::Cancel, chr('`'))" in keymap
    source = Path(latency.__file__).read_text(encoding="utf-8")
    assert "k.ESCAPE" not in source


def test_nothing_to_unwind_at_rest_even_off_the_root_page():
    assert unwind_step(state(mod="board", ui={"menu": False, "locked": False})) == ()


# --- View ------------------------------------------------------------------------------------------


def test_view_matches_page_overlay_menu_and_lock():
    s = state(mod="board", ui={"menu": True, "locked": False})
    assert View("board", menu=True).met(s)
    assert not View("board").met(s)
    assert not View(ROOT, menu=True).met(s)
    assert View(None, menu=True).met(s)  # any page
    assert View(ROOT, locked=True).met(state(ui={"menu": False, "locked": True}))
    assert not View(ROOT, "compose").met(state())


def test_view_text_names_what_differs_from_rest():
    assert view_text(state(mod="inbox")) == "inbox/none"
    assert view_text(state(overlay="compose", ui={"menu": True, "locked": True})) == "home/compose+menu+locked"


def test_a_view_reads_like_the_state_it_wants():
    assert View(ROOT, "compose", menu=True, locked=True).text() == "home/compose+menu+locked"
    assert View(None, menu=True).text() == "*/none+menu"


# --- nav plan ------------------------------------------------------------------------------------


def test_nav_covers_every_launcher_tile_letter_in_the_firmware():
    planned = {s.name.removeprefix("tile_") for s in nav_plan(rec=True) if s.name.startswith("tile_")}
    assert planned == home_tile_letters() == set("pirnqsml")


def test_nav_reaches_the_opt_only_tiles_by_enter_and_their_letters_stay_home():
    """docs/20 D1 (modules/home/home_logic.cpp opt_only): r and l alone press nothing on home."""
    src = (FIRMWARE / "modules" / "home" / "home_logic.cpp").read_text(encoding="utf-8")
    body = src[src.index("bool opt_only(char c)") :].split("\n", 1)[0]
    assert set(re.findall(r"c == '([a-z])'", body)) == set(OPT_ONLY) == {"r", "l"}
    steps = {s.name: s for s in nav_plan(rec=True)}
    for letter in OPT_ONLY:
        assert steps[f"tile_{letter}"].press == k.ENTER and steps[f"tile_{letter}"].before, letter
        assert steps[f"letter_{letter}"].want == View(ROOT) and steps[f"letter_{letter}"].back == (), letter


@pytest.mark.parametrize("rec", [True, False])
def test_nav_opt_keys_open_what_the_global_layer_binds(rec):
    modules = {"Home": ROOT, "Projects": "board", "Inbox": "inbox", "System": "system"}
    want = {f"opt_{key}": modules[act] for key, act in global_opt_actions().items() if act in modules}
    assert set(want) == {"opt_h", "opt_p", "opt_i", "opt_m"}
    steps = {s.name: s for s in nav_plan(rec)}
    assert {name: steps[name].want.mod for name in want} == want


def test_nav_letters_and_apps_come_back_home_with_the_grave_key():
    steps = {s.name: s for s in nav_plan(rec=True)}
    for name in ("tile_p", "tile_i", "tile_m", "opt_p", "opt_i", "opt_m", "digit_1", "enter_first_tile"):
        assert steps[name].back == (GRAVE,), name
    assert steps["tile_l"].want == View(ROOT, locked=True) and steps["tile_l"].back == UNLOCK
    assert steps["opt_h"].before and steps["opt_h"].press == OPT_H


def test_nav_r_opens_the_meeting_confirm_only_with_the_card():
    with_card = {s.name: s for s in nav_plan(rec=True)}["tile_r"]
    without = {s.name: s for s in nav_plan(rec=False)}["tile_r"]
    assert with_card.want == View(ROOT, "confirm") and with_card.back == (k.CONFIRM_GUARD, GRAVE)
    assert without.want == View(ROOT) and without.back == ()


def test_every_planned_press_is_a_valid_debug_key():
    presses = [p for s in nav_plan(rec=True) for p in (*s.before, s.press, *s.back)]
    presses += [p for place in MENU_PLACES for p in place.opener]
    presses += [p for run in MENU_RUNS for p in (*run.before, *run.keys)]
    presses += [*MEETING_LEAKS, TAB, *UNLOCK]
    for p in presses:
        k.wire(p)


# --- menu plan -----------------------------------------------------------------------------------


def test_menu_is_opened_over_the_launcher_each_app_and_compose():
    assert [p.name for p in MENU_PLACES] == ["home", "board", "inbox", "system", "compose"]
    assert MENU_PLACES[-1].under == View(ROOT, "compose")


def test_menu_runs_end_with_the_menu_closed():
    assert MENU_RUNS and all(not run.want.menu and run.keys[0] == TAB for run in MENU_RUNS)


def test_meeting_leak_keys_neither_close_the_menu_nor_reach_its_other_pages():
    closing = {("tab",), ("`",), ("bksp",), ("fn", "`"), ("g0",), ("enter",), (",",), ("/",)}
    assert not {p.keys for p in MEETING_LEAKS} & closing
    assert all(len(p.keys) == 1 for p in MEETING_LEAKS)  # docs/20 D9: a chord the menu shows runs


@pytest.mark.parametrize(
    ("manifest", "stars", "problems"),
    [
        ({"marks": [{"k": "star", "t_ms": 1}, {"k": "star", "t_ms": 2}], "project": None}, 2, []),
        ({"marks": [{"k": "star", "t_ms": 1}], "project": None}, 2, ["stars 1 != 2"]),
        ({"marks": [], "project": {"key": "cpa"}}, 0, ["tagged cpa"]),
    ],
)
def test_meeting_problems(manifest, stars, problems):
    assert meeting_problems(manifest, stars) == problems


# --- lock: noise and what must not change ----------------------------------------------------------


def test_noise_never_presses_the_keys_the_lock_lets_through():
    noise = noise_presses(random.Random(7), 200)  # noqa: S311 - reproducible test noise
    assert len(noise) == 200 and all(is_noise(p) for p in noise)
    assert not any("g0" in p.keys or "enter" in p.keys or {"opt", "l"} <= set(p.keys) for p in noise)
    assert any(len(p.keys) == 1 for p in noise) and any(len(p.keys) == 2 for p in noise)
    for p in noise:
        k.wire(p)


def test_noise_is_reproducible_from_its_seed():
    a, b = random.Random(42), random.Random(42)  # noqa: S311 - reproducible test noise
    assert noise_presses(a, 50) == noise_presses(b, 50)


@pytest.mark.parametrize("press", [k.G0, k.ENTER, OPT_L, k.Press(("l", "opt"))])
def test_the_lock_keys_are_not_noise(press):
    assert not is_noise(press)


def test_changed_lists_the_fields_that_moved():
    rec = {"live": False, "mode": None, "files_n": 0}
    before = state(ui={"menu": False, "locked": True}, outbox={"pending": 0, "bad": 0}, rec=rec)
    assert changed(before, before) == ()
    after = {**before, "mod": "board", "outbox": {"pending": 1, "bad": 0}, "rec": {**rec, "live": True}}
    assert changed(before, after) == ("mod", "pending", "live")
    assert changed(before, {**before, "ui": {"menu": False, "locked": False}}) == ("locked",)
