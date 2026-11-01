"""Key plans for debug.key (protocol README "contract additions" 9; firmware/app/lib/shell/debug_inject.h)."""

import pytest

from harness.keys import (
    CONFIRM_GUARD,
    INJECT_QUEUE_MAX,
    KEY_NAMES,
    Press,
    char_press,
    duration_ms,
    note_presses,
    numbered_texts,
    offline_plan,
    spawn_presses,
    text_presses,
    wire,
)


def test_key_names_come_from_the_protocol_schema():
    assert len(KEY_NAMES) == 57
    assert {"g0", "space", "enter", "bksp", "opt", "shift", "fn", "`", "a", "0"} <= KEY_NAMES


@pytest.mark.parametrize(
    ("ch", "keys"),
    [("a", ("a",)), ("z", ("z",)), ("7", ("7",)), (" ", ("space",)), ("Q", ("shift", "q")), (".", (".",)),
     ("/", ("/",)), ("-", ("-",)), ("`", ("fn", "`"))],
)  # fmt: skip
def test_char_press(ch, keys):
    """docs/20 D5: in the editor ` alone exits (or asks "Discard what you typed?"); Fn+` types the backtick."""
    assert char_press(ch) == Press(keys)


@pytest.mark.parametrize("ch", ["中", "\n", "!", "~", "\t"])
def test_char_press_refuses_what_the_harness_does_not_type(ch):
    with pytest.raises(ValueError):
        char_press(ch)


def test_text_presses_maps_every_character():
    assert text_presses("ab 1") == (Press(("a",)), Press(("b",)), Press(("space",)), Press(("1",)))


def test_note_is_g0_text_enter():
    assert note_presses("hi") == (Press(("g0",)), Press(("h",)), Press(("i",)), Press(("enter",)))


def test_spawn_is_opt_s_then_target_slot_then_text_then_enter():
    assert spawn_presses("go", slot=1) == (
        Press(("opt", "s")),
        Press(("opt", "1")),
        Press(("g",)),
        Press(("o",)),
        Press(("enter",)),
    )


@pytest.mark.parametrize("slot", [0, 10])
def test_spawn_slot_must_be_1_to_9(slot):
    with pytest.raises(ValueError):
        spawn_presses("go", slot=slot)


@pytest.mark.parametrize("text", ["", "-c test", "  -x"])
def test_texts_the_device_refuses_are_refused_here_too(text):
    with pytest.raises(ValueError):
        spawn_presses(text, slot=1)
    if text == "":
        with pytest.raises(ValueError):
            note_presses(text)


def test_confirm_guard_outlasts_the_400ms_confirm_guard_and_presses_nothing():
    assert CONFIRM_GUARD.keys == ("shift",)
    assert CONFIRM_GUARD.hold_ms > 400


def test_wire_omits_hold_when_default():
    assert wire(Press(("g0",))) == {"t": "debug.key", "keys": ["g0"]}
    assert wire(Press(("shift",), 5000)) == {"t": "debug.key", "keys": ["shift"], "hold_ms": 5000}


@pytest.mark.parametrize(
    "press",
    [
        Press(()),
        Press(("a", "b", "c", "d")),
        Press(("a", "a")),
        Press(("f13",)),
        Press(("a",), 5001),
        Press(("a",), -1),
    ],
)
def test_wire_refuses_presses_the_schema_refuses(press):
    with pytest.raises(ValueError):
        wire(press)


def test_duration_is_hold_plus_the_30ms_gap():
    assert duration_ms(Press(("a",))) == 80
    assert duration_ms(Press(("shift",), 600)) == 630


def test_offline_plan_idles_past_the_15s_link_timeout_first():
    body = note_presses("ab")
    plan = offline_plan(body, idle_ms=20000)
    holds = plan[:4]
    assert all(p == Press(("shift",), 5000) for p in holds)
    assert plan[4:] == body
    assert sum(duration_ms(p) for p in holds) >= 20000


def test_offline_plan_rounds_the_idle_up_to_whole_holds():
    assert len(offline_plan((Press(("a",)),), idle_ms=10001)) == 3 + 1


def test_offline_plan_must_fit_the_device_inject_queue():
    assert INJECT_QUEUE_MAX == 16
    three_notes = note_presses("ab") * 3
    assert len(offline_plan(three_notes, idle_ms=20000)) == 16
    with pytest.raises(ValueError, match="16"):
        offline_plan(note_presses("abc") * 3, idle_ms=20000)


def test_numbered_texts_are_unique_typeable_and_zero_padded():
    texts = numbered_texts("mq", 100)
    assert texts[0] == "mq00" and texts[-1] == "mq99"
    assert len(set(texts)) == 100
    assert all(text_presses(t) for t in texts)


def test_numbered_texts_widen_past_99():
    assert numbered_texts("b", 101)[-1] == "b100"
    assert numbered_texts("b", 3) == ("b00", "b01", "b02")


@pytest.mark.parametrize(("prefix", "n"), [("", 3), ("b", 0), ("B!", 2)])
def test_numbered_texts_refuse_bad_input(prefix, n):
    with pytest.raises(ValueError):
        numbered_texts(prefix, n)
