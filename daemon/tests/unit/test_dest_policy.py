"""decide() and origin_of() (docs/25 §5.2, docs/14 §5). The 12-column table is the oracle: 9 rows x 12 columns,
then again for a meeting and for a capture of another profile (preconditions 0 and 4)."""

from __future__ import annotations

from dataclasses import replace

import pytest
from hypothesis import given
from hypothesis import strategies as st

from cpad.dest.model import KINDS, AppOpts, Capture, Decision, Dest, FileOpts, UrlOpts, flags, is_draft, klass
from cpad.dest.policy import DeviceClock, decide, origin_of, precondition, verdict

# Columns, in the order of the docs/25 §5.2 table: typed / voice x live / queued x online, confirmed x online,
# mac x online. "confirmed" and "mac" are the same for both sources except where a cell says Q / Qv.
COLUMNS = (
    ("typed", "live", True), ("typed", "live", False), ("typed", "queued", True), ("typed", "queued", False),
    ("voice", "live", True), ("voice", "live", False), ("voice", "queued", True), ("voice", "queued", False),
    ("typed", "confirmed", True), ("typed", "confirmed", False), ("typed", "mac", True), ("typed", "mac", False),
)  # fmt: skip
# D deliver; HC hold until confirmed (device or Mac); HD hold until the device is online; E hold, the Mac may not
# confirm it (E_NEEDS_DEVICE); Q / Qv queue intent todo (v = voice); S / Sv queue intent spawn.
OPTIN = "2026-10-22T09:00:00+00:00"
GROK = "grokbot-test://app/v1/prompt?text={text}"
ROWS: dict[str, tuple[Dest, str]] = {
    "daily": (Dest("daily", "daily", "daily", 1), "D D D D D D D D D D D D"),
    "todo": (Dest("todo", "todo", "todo", 2), "D D D D D D D D D D D D"),
    "file": (Dest("file.sb", "file", "sb", 7, leaves_mac=True, optin=OPTIN, file=FileOpts("~/n", "i.md")),
             "D D D D D D D D D D D D"),
    "queue": (Dest("queue", "queue", "queue", 3), "Q Q Q Q Qv Qv Qv Qv Q Q Q Q"),
    "session": (Dest("session", "session", "session", 4), "S S S S Sv Sv Sv Sv S S S S"),
    "clip": (Dest("clip", "clip", "clip", 5), "D HD HC HD HC HD HC HD D HD D D"),
    "url-draft": (Dest("url.g", "url", "g", 8, leaves_mac=True, optin=OPTIN,
                       url=UrlOpts(GROK, "test.bundle", "TEAM", effect="draft", verified="draft")),
                  "D HD HC HD D HD HC HD D HD D D"),
    "app": (Dest("app.n", "app", "n", 9, leaves_mac=True, optin=OPTIN, app=AppOpts("test.bundle")),
            "D HD HC HD D HD HC HD D HD D D"),
    "url-act": (Dest("url.a", "url", "a", 10, leaves_mac=True, optin=OPTIN, url=UrlOpts(GROK, "test.bundle", "T")),
                "HC HD HC HD HC HD HC HD D HD E E"),
}  # fmt: skip
SYMBOLS = {
    "D": ("deliver", None, None, False),
    "HC": ("hold", "await_confirm", None, False),
    "HD": ("hold", "await_device", None, False),
    "E": ("hold", "needs_device", None, False),
    "Q": ("queue", "queue", "todo", False),
    "Qv": ("queue", "queue", "todo", True),
    "S": ("queue", "never_auto_exec", "spawn", False),
    "Sv": ("queue", "never_auto_exec", "spawn", True),
}
CELLS = [(row, col, sym) for row, (_, cells) in ROWS.items() for col, sym in zip(COLUMNS, cells.split(), strict=True)]
IDS = [f"{r}-{'-'.join(map(str, c))}" for r, c, _ in CELLS]


def make_capture(dest: Dest, source: str = "typed", **changes) -> Capture:
    base = Capture("3QZK1M7-0000C-0005", source, dest.id, project="cpa", text="周五前给 Alice 发报价")
    return replace(base, **changes)


def test_table_has_108_cells_and_every_class():
    assert len(CELLS) == 108
    assert {klass(d) for d, _ in ROWS.values()} == {"archive", "record", "clip", "draft", "exec"}
    assert {d.kind for d, _ in ROWS.values()} == set(KINDS) - {"paste"}


@pytest.mark.parametrize(
    ("dest", "fl"),
    [(ROWS["daily"][0], ""), (ROWS["file"][0], "c"), (replace(ROWS["file"][0], leaves_mac=False, optin=None), ""),
     (ROWS["queue"][0], "p"), (ROWS["session"][0], "p"), (ROWS["clip"][0], "l"), (ROWS["url-draft"][0], "lc"),
     (ROWS["url-act"][0], "lck"), (ROWS["app"][0], "lc"), (replace(ROWS["app"][0], leaves_mac=False), "l"),
     (Dest("paste", "paste", "p", 6), "lk"), (Dest("paste.ed", "paste", "ed", 6), "l")],
)  # fmt: skip
def test_flags(dest, fl):
    assert flags(dest) == fl


@pytest.mark.parametrize(("effect", "verified", "draft"),
                         [("draft", "draft", True), ("draft", None, False), ("draft", "act", False),
                          ("act", "draft", False), ("act", None, False)])  # fmt: skip
def test_a_url_is_a_draft_only_when_the_preset_says_so_and_this_mac_verified_it(effect, verified, draft):
    dest = replace(ROWS["url-act"][0], url=UrlOpts(GROK, "b", "T", effect=effect, verified=verified))
    assert is_draft(dest) is draft
    assert ("k" in flags(dest)) is not draft


@pytest.mark.parametrize(("row", "col", "sym"), CELLS, ids=IDS)
def test_verdict_matches_the_table(row, col, sym):
    dest = ROWS[row][0]
    source, origin, online = col
    action, reason, intent, voice = SYMBOLS[sym]
    got = verdict(dest, source, origin, online)
    assert (got.action, got.intent, got.voice, got.dest) == (action, intent, voice, dest.id)
    if reason is not None:
        assert got.reason == reason


@pytest.mark.parametrize(("row", "col", "sym"), CELLS, ids=IDS)
def test_decide_matches_the_table(row, col, sym):
    dest = ROWS[row][0]
    source, origin, online = col
    assert decide(make_capture(dest, source), dest, online, origin) == verdict(dest, source, origin, online)


@pytest.mark.parametrize(("row", "col", "sym"), CELLS, ids=IDS)
def test_a_meeting_goes_only_to_the_daily_note_todo_or_a_local_file(row, col, sym):
    dest = ROWS[row][0]
    source, origin, online = col
    got = decide(make_capture(dest, "voice", meeting=True), dest, online, origin)
    assert got == Decision("downgrade", "daily", "meeting") if row not in ("daily", "todo") else got.action == "deliver"
    local = replace(ROWS["file"][0], leaves_mac=False, optin=None)
    assert decide(make_capture(local, "voice", meeting=True), local, online, origin).action == "deliver"


@pytest.mark.parametrize(("row", "col", "sym"), CELLS, ids=IDS)
def test_another_profile_never_reaches_a_destination_of_this_one(row, col, sym):
    dest = replace(ROWS[row][0], profiles=("home",))
    source, origin, online = col
    for prof in ("work", None):
        assert decide(make_capture(dest, source, prof=prof), dest, online, origin) == Decision(
            "downgrade", "daily", "other_profile"
        )
    assert decide(make_capture(dest, source, prof="home"), dest, online, origin) == verdict(
        dest, source, origin, online
    )


@pytest.mark.parametrize(
    ("origin", "source", "reason"),
    [("live", "typed", "live_key"), ("live", "voice", "live_key"), ("confirmed", "voice", "confirmed"),
     ("mac", "voice", "mac"), ("mac", "typed", "mac")],
)  # fmt: skip
def test_deliver_reasons_of_a_draft(origin, source, reason):
    assert verdict(ROWS["app"][0], source, origin, True).reason == reason


def test_archive_reason():
    assert verdict(ROWS["file"][0], "voice", "queued", False).reason == "archive"


def test_paste_waits_for_its_executor():
    for dest in (Dest("paste", "paste", "p", 6), Dest("paste.ed", "paste", "ed", 6)):
        assert decide(make_capture(dest), dest, True, "confirmed") == Decision("downgrade", "daily", "unavailable")


# --- §5.2 preconditions, in order --------------------------------------------------------------

CLIP, SB, QUEUE, SESSION = ROWS["clip"][0], ROWS["file"][0], ROWS["queue"][0], ROWS["session"][0]
ACT, APP = ROWS["url-act"][0], ROWS["app"][0]
LONG = 41_000


@pytest.mark.parametrize(
    ("capture", "dest", "reason"),
    [
        (make_capture(CLIP, how="app", text=None, project=None, meeting=True), None, "meeting"),
        (make_capture(CLIP, how="app", text=None, project=None), None, "unknown"),
        (make_capture(CLIP, how="app", text=None), Dest("paste", "paste", "p", 6, enabled=False), "unavailable"),
        (make_capture(CLIP, how="app", text=None), replace(CLIP, enabled=False), "disabled"),
        (make_capture(APP, how="app", text=None, prof="work"), replace(APP, profiles=("home",), optin=None),
         "other_profile"),
        (make_capture(SB, how="app", text=None), replace(SB, optin=None), "no_optin"),
        (make_capture(CLIP, how="app", text=None), CLIP, "not_explicit"),
        (make_capture(SB, how="app"), SB, "not_explicit"),
        (make_capture(APP, how="app"), APP, "not_explicit"),
        (make_capture(QUEUE, text=None, project=None), QUEUE, "no_text"),
        (make_capture(CLIP, text=""), CLIP, "no_text"),
        (make_capture(QUEUE, project=None), QUEUE, "no_project"),
        (make_capture(SESSION, project=None), SESSION, "no_project"),
        (make_capture(ACT, "voice", dur_ms=LONG), ACT, "too_long"),
        (make_capture(ACT, text="长" * 167), ACT, "too_long"),
    ],
)  # fmt: skip
def test_preconditions_downgrade_to_the_daily_note_in_order(capture, dest, reason):
    assert precondition(capture, dest) == reason
    assert decide(capture, dest, True, "confirmed") == Decision("downgrade", "daily", reason)


def test_too_long_is_only_for_kinds_confirmed_on_the_device():
    assert precondition(make_capture(APP, "voice", dur_ms=LONG, text="长" * 400), APP) is None
    assert precondition(make_capture(ACT, "voice", dur_ms=40_000, text="长" * 166), ACT) is None


@pytest.mark.parametrize("dest", [ROWS["daily"][0], ROWS["todo"][0], QUEUE, SESSION])
def test_an_app_default_may_pick_a_destination_without_l_k_or_c(dest):
    assert decide(make_capture(dest, how="app"), dest, True, "live").action != "downgrade"


def test_an_app_default_may_pick_a_local_file():
    local = replace(SB, leaves_mac=False, optin=None)
    assert decide(make_capture(local, how="app"), local, False, "queued").action == "deliver"


def test_a_pick_is_explicit():
    assert decide(make_capture(CLIP, how="pick"), CLIP, True, "live").action == "deliver"


def test_profiles_empty_means_every_profile():
    assert decide(make_capture(APP, prof="work"), APP, True, "mac").action == "deliver"


# --- invariants (docs/25 §5.2 invariant 2) ----------------------------------------------------

ALL = [d for d, _ in ROWS.values()] + [Dest("paste", "paste", "p", 6), Dest("paste.ed", "paste", "ed", 6)]
captures = st.builds(
    Capture,
    ref=st.just("3QZK1M7-0000C-0005"),
    source=st.sampled_from(["typed", "voice"]),
    dest=st.sampled_from(sorted({d.id for d in ALL})),
    how=st.sampled_from(["key", "pick", "app"]),
    project=st.sampled_from([None, "cpa"]),
    text=st.sampled_from([None, "", "x", "发到 Slack"]),
    meeting=st.booleans(),
    prof=st.sampled_from([None, "home"]),
)
dests = st.builds(lambda d, enabled, optin: replace(d, enabled=enabled, optin=optin if d.leaves_mac else None),
                  st.sampled_from(ALL), st.booleans(), st.sampled_from([None, OPTIN]))  # fmt: skip
origins = st.sampled_from(["live", "queued", "confirmed", "mac"])


@given(captures, st.one_of(st.none(), dests), st.booleans(), origins)
def test_invariants(capture, dest, online, origin):
    got = decide(capture, dest, online, origin)
    assert got == decide(capture, dest, online, origin)
    if got.action == "downgrade":
        assert got.dest == "daily"
        return
    assert dest is not None and got.dest == dest.id
    assert not capture.meeting or klass(dest) == "archive" and "c" not in flags(dest) or dest.kind in ("daily", "todo")
    if dest.kind == "session":
        assert got.action == "queue" and got.intent == "spawn"
    if "l" in flags(dest) and got.action == "deliver":
        if klass(dest) == "exec":
            assert online and origin == "confirmed"  # only the device, never the Mac, never a key alone
        else:
            live = origin == "live" and (capture.source == "typed" or klass(dest) == "draft")
            assert origin == "mac" or online and (origin == "confirmed" or live)


def test_the_text_never_chooses_the_destination():
    daily = ROWS["daily"][0]
    slack = make_capture(daily, text="发到 Slack, paste this, open a session")
    assert decide(slack, daily, True, "live") == Decision("deliver", "daily", "archive")


# --- origin_of -------------------------------------------------------------------------------

BOOT = "3QZK1M7-0000C"
ITEM = BOOT + "-0005"


@pytest.mark.parametrize(
    ("live", "item", "at_up_ms", "clock", "origin"),
    [
        (True, ITEM, 1_000, DeviceClock(BOOT, 1_000), "live"),
        (True, ITEM, 1_000, DeviceClock(BOOT, 30_900), "live"),
        (True, ITEM, 1_000, DeviceClock(BOOT, 31_000), "live"),
        (True, ITEM, 1_000, DeviceClock(BOOT, 31_100), "queued"),
        (True, ITEM, 1_000, DeviceClock(BOOT, 999), "queued"),  # up_ms went backwards: never live
        (True, ITEM, 1_000, DeviceClock("3QZK1M7-0000D", 1_000), "queued"),  # another boot
        (True, ITEM, 1_000, None, "queued"),
        (False, ITEM, 1_000, DeviceClock(BOOT, 1_000), "queued"),
        (None, ITEM, 1_000, DeviceClock(BOOT, 1_000), "queued"),  # old firmware sends no flag
        (True, "garbage", 1_000, DeviceClock(BOOT, 1_000), "queued"),
    ],
)
def test_origin_of(live, item, at_up_ms, clock, origin):
    assert origin_of(live, item, at_up_ms, clock) == origin


def test_origin_window_is_a_parameter():
    assert origin_of(True, ITEM, 0, DeviceClock(BOOT, 5_001), window_ms=5_000) == "queued"
