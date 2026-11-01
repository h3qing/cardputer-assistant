"""Hook events -> per-session hook state, merged with the agents --json listing (#33, #5)."""

from __future__ import annotations

import pytest

from cpad.agents.base import SessionInfo
from cpad.agents.claude_code import hook_state as hs
from cpad.agents.claude_code.hooks import HookEvent

SID = "8d3e7a19-4c2b-4a6e-9f10-2b7c5d8e3a61"
ASK, PLAN = "AskUserQuestion", "ExitPlanMode"
OTHER = "2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38"


def ev(name: str, **kw) -> HookEvent:
    return HookEvent(ev=name, session_id=kw.pop("sid", SID), **kw)


def info(status: str, session_id: str = SID, kind: str = "tty", waiting_for: str | None = None) -> SessionInfo:
    return SessionInfo(session_id=session_id, sid=session_id[:8], short=None, kind=kind, status=status, state=None,
                       name="架构设计", cwd="/p", waiting_for=waiting_for)  # fmt: skip


def run(*events: HookEvent, start: float = 100.0) -> hs.Book:
    book = hs.EMPTY
    for n, event in enumerate(events):
        book = hs.record(book, event, start + n)
    return book


def state(*events: HookEvent) -> tuple[str, str | None]:
    got = run(*events)[SID]
    return got.phase, got.why


# --- transitions ----------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("events", "expected"),
    [
        ((ev("UserPromptSubmit"),), ("working", None)),
        ((ev("SessionStart", detail="startup"),), ("done", None)),
        ((ev("PermissionRequest", tool="Bash"),), ("needs", "permission")),
        ((ev("PermissionRequest", tool="ExitPlanMode"),), ("needs", "plan")),
        ((ev("PermissionRequest", tool="AskUserQuestion"),), ("needs", "question")),
        ((ev("PreToolUse", tool="AskUserQuestion"),), ("needs", "question")),
        ((ev("PreToolUse", tool="ExitPlanMode"),), ("working", None)),
        ((ev("PermissionRequest", tool=ASK), ev("PostToolUse", tool=ASK)), ("working", None)),
        ((ev("Notification", ntype="permission_prompt"),), ("needs", "permission")),
        ((ev("PreToolUse", tool=ASK), ev("Notification", ntype="permission_prompt")), ("needs", "question")),
        ((ev("PermissionRequest", tool=PLAN), ev("Notification", ntype="permission_prompt")), ("needs", "plan")),
        ((ev("Notification", ntype="elicitation_dialog"),), ("needs", "question")),
        ((ev("Notification", ntype="elicitation_url_dialog"),), ("needs", "question")),
        ((ev("Notification", ntype="quota_auto_resume_stale"),), ("needs", "input")),
        ((ev("Stop"), ev("Notification", ntype="idle_prompt")), ("done", "input")),
        ((ev("PermissionRequest", tool="Bash"), ev("Notification", ntype="idle_prompt")), ("needs", "permission")),
        ((ev("UserPromptSubmit"), ev("Stop")), ("done", None)),
        ((ev("StopFailure", detail="authentication_failed"),), ("needs", "login")),
        ((ev("StopFailure", detail="oauth_org_not_allowed"),), ("needs", "login")),
        ((ev("StopFailure", detail="rate_limit"),), ("done", "error")),
        ((ev("StopFailure", detail="authentication_failed"), ev("Notification", ntype="auth_success")), ("done", None)),
        ((ev("Stop"), ev("Notification", ntype="auth_success")), ("done", None)),
        ((ev("UserPromptSubmit"), ev("PreCompact")), ("working", None)),
        ((ev("UserPromptSubmit"), ev("SessionStart", detail="compact")), ("working", None)),
        ((ev("UserPromptSubmit"), ev("SessionEnd", detail="prompt_input_exit")), ("ended", None)),
        ((ev("UserPromptSubmit"), ev("Notification", ntype="agent_needs_input")), ("working", None)),
    ],
)
def test_transitions(events, expected):
    assert state(*events) == expected


def test_subagent_events_never_change_the_session():
    book = run(ev("UserPromptSubmit"), ev("PreToolUse", tool="AskUserQuestion", sub=True), ev("SubagentStop", sub=True),
               ev("Stop", sub=True))  # fmt: skip
    assert book[SID].phase == "working" and book[SID].at == 100.0


def test_events_that_change_nothing_leave_no_entry():
    assert run(ev("Notification", ntype="auth_success")) == {}
    assert run(ev("SubagentStop", sub=True)) == {}


def test_message_excerpt_is_kept_with_the_state():
    book = run(ev("PermissionRequest", tool="Bash"), ev("Notification", ntype="permission_prompt", msg="needs Bash"))
    assert book[SID].msg == "needs Bash" and book[SID].tool == "Bash"
    assert run(ev("Stop", msg="All 48 tests pass"))[SID].msg == "All 48 tests pass"
    assert run(ev("Stop", msg="x"), ev("UserPromptSubmit"))[SID].msg is None


def test_record_is_immutable_and_bounded():
    first = run(ev("Stop"))
    second = hs.record(first, ev("UserPromptSubmit"), 200.0)
    assert first[SID].phase == "done" and second[SID].phase == "working"
    many = hs.EMPTY
    for n in range(hs.BOOK_MAX + 5):
        many = hs.record(many, ev("Stop", sid=f"{n:08x}-4c2b-4a6e-9f10-2b7c5d8e3a61"), float(n))
    assert len(many) == hs.BOOK_MAX and "00000000-4c2b-4a6e-9f10-2b7c5d8e3a61" not in many


# --- merge with the listing -----------------------------------------------------------------


def statuses(views) -> dict[str, tuple[str, str | None]]:
    return {v.info.session_id: (v.info.status, v.why) for v in views}


def test_no_hooks_means_the_poll_decides():
    views = hs.merge((info("waiting"), info("idle", OTHER)), hs.EMPTY, 50.0)
    assert statuses(views) == {SID: ("waiting", None), OTHER: ("idle", None)}


def test_a_newer_hook_beats_a_lagging_poll():
    book = run(ev("PermissionRequest", tool="Bash"))
    views = hs.merge((info("busy"), info("idle", OTHER)), book, 101.0)
    assert statuses(views) == {SID: ("waiting", "permission"), OTHER: ("idle", None)}


def test_hook_stays_until_the_poll_has_confirmed_and_then_moved_on():
    book = run(ev("PermissionRequest", tool="Bash"))
    book = hs.observe(book, (info("waiting"),), 103.0)
    assert statuses(hs.merge((info("waiting"),), book, 103.0)) == {SID: ("waiting", "permission")}
    book = hs.observe(book, (info("busy"),), 106.0)
    assert statuses(hs.merge((info("busy"),), book, 106.0)) == {SID: ("busy", None)}


def test_an_unconfirmed_hook_gives_way_after_the_lag():
    book = run(ev("Stop", msg="done"))
    lagging = 100.0 + hs.LAG_S - 0.5
    assert statuses(hs.merge((info("busy"),), hs.observe(book, (info("busy"),), lagging), lagging)) == {
        SID: ("idle", None)
    }
    late = 100.0 + hs.LAG_S + 0.5
    assert statuses(hs.merge((info("busy"),), hs.observe(book, (info("busy"),), late), late)) == {SID: ("busy", None)}


def test_a_poll_older_than_the_hook_does_not_confirm_it():
    book = hs.observe(run(ev("Stop")), (info("idle"),), 99.0)
    assert not book[SID].seen
    assert hs.observe(book, (info("idle"),), 101.0)[SID].seen


def test_login_holds_while_the_session_sits_idle():
    book = run(ev("StopFailure", detail="authentication_failed"))
    book = hs.observe(book, (info("idle"),), 130.0)
    assert statuses(hs.merge((info("idle"),), book, 130.0)) == {SID: ("waiting", "login")}
    book = hs.observe(book, (info("busy"),), 140.0)
    assert statuses(hs.merge((info("busy"),), book, 140.0)) == {SID: ("busy", None)}


def test_ended_sessions_leave_the_board_while_the_poll_lags():
    book = run(ev("SessionEnd", detail="other"))
    assert hs.merge((info("idle"), info("idle", OTHER)), book, 101.0)[0].info.session_id == OTHER
    late = 100.0 + hs.LAG_S + 1
    assert len(hs.merge((info("idle"),), book, late)) == 1


def test_unknown_poll_status_takes_the_hook_but_holds_a_wait_only_for_the_lag():
    book = run(ev("PermissionRequest", tool="Bash"))
    soon, late = 105.0, 100.0 + hs.LAG_S + 1
    book = hs.observe(book, (info("unknown"),), soon)
    assert statuses(hs.merge((info("unknown"),), book, soon)) == {SID: ("waiting", "permission")}
    book = hs.observe(book, (info("unknown"),), late)
    assert statuses(hs.merge((info("unknown"),), book, late)) == {SID: ("unknown", None)}
    done = hs.observe(run(ev("Stop")), (info("unknown"),), 300.0)
    assert statuses(hs.merge((info("unknown"),), done, 300.0)) == {SID: ("idle", None)}


def polls(book: hs.Book, *at_status: tuple[float, str]) -> list[tuple[str, str | None]]:
    """Each poll as the daemon runs it: observe, then merge; the effective (status, waiting_for) per poll."""
    seen = []
    for at, status in at_status:
        book = hs.observe(book, (info(status),), at)
        view = hs.merge((info(status),), book, at)[0]
        seen.append((view.info.status, view.info.waiting_for))
    return seen


def test_a_wait_approved_before_the_first_poll_gives_way_to_fresh_polls():
    """PermissionRequest(Bash) at t=0, approved at the terminal at t=1: no hook says so, only the poll."""
    book = run(ev("PermissionRequest", tool="Bash"))
    assert polls(book, (101.0, "busy")) == [("waiting", "permission")]
    assert polls(book, (102.0, "busy"), (105.0, "busy"), (108.0, "busy")) == [("busy", None)] * 3


@pytest.mark.parametrize("event", [ev("PreToolUse", tool=ASK), ev("PermissionRequest", tool=PLAN),
                                   ev("Notification", ntype="elicitation_dialog")])  # fmt: skip
def test_every_dialog_wait_gets_only_the_short_grace(event):
    assert polls(run(event), (100.0 + hs.NEEDS_GRACE_S, "busy")) == [("busy", None)]


def test_a_wait_the_poll_cannot_show_keeps_the_full_lag():
    book = run(ev("Notification", ntype="quota_auto_resume_stale"))
    assert polls(book, (105.0, "idle")) == [("waiting", "question")]
    assert polls(book, (100.0 + hs.LAG_S, "idle")) == [("idle", None)]


def test_shell_counts_as_working():
    book = hs.observe(run(ev("UserPromptSubmit")), (info("shell"),), 300.0)
    assert statuses(hs.merge((info("shell"),), book, 300.0)) == {SID: ("shell", None)}


def test_merge_keeps_the_excerpt_only_when_the_hook_wins():
    book = run(ev("Stop", msg="All 48 tests pass"))
    assert hs.merge((info("idle"),), book, 101.0)[0].msg == "All 48 tests pass"
    book = hs.observe(book, (info("idle"),), 101.0)
    assert hs.merge((info("busy"),), book, 104.0)[0].msg is None


def test_observe_forgets_sessions_that_left_the_listing():
    book = run(ev("Stop"), ev("Stop", sid=OTHER))
    assert set(hs.observe(book, (info("idle"),), 101.0 + hs.LAG_S - 1)) == {SID, OTHER}
    assert set(hs.observe(book, (info("idle"),), 101.0 + hs.LAG_S + 1)) == {SID}


def test_summary_counts_tracked_sessions():
    assert hs.summary(hs.EMPTY, 10.0) == {"sessions": 0, "last_ago_s": None}
    assert hs.summary(run(ev("Stop"), ev("Stop", sid=OTHER)), 103.5) == {"sessions": 2, "last_ago_s": 2.5}


# --- waiting_for (#5) stays true to the effective status ------------------------------------


def test_poll_only_waits_keep_the_listing_reason():
    views = hs.merge((info("waiting", waiting_for="permission"),), hs.EMPTY, 50.0)
    assert views[0].why == "permission" and views[0].info.waiting_for == "permission"


@pytest.mark.parametrize(
    ("event", "waiting_for"),
    [(ev("PermissionRequest", tool="Bash"), "permission"), (ev("PermissionRequest", tool=PLAN), "permission"),
     (ev("PreToolUse", tool=ASK), "question"), (ev("Notification", ntype="quota_auto_resume_stale"), "question"),
     (ev("StopFailure", detail="authentication_failed"), "dialog")],
)  # fmt: skip
def test_a_hook_wait_fills_waiting_for(event, waiting_for):
    view = hs.merge((info("busy"),), run(event), 101.0)[0]
    assert view.info.status == "waiting" and view.info.waiting_for == waiting_for


def test_a_hook_that_ends_the_wait_clears_waiting_for():
    book = run(ev("PostToolUse", tool=ASK))
    view = hs.merge((info("waiting", waiting_for="question"),), book, 101.0)[0]
    assert view.info.status == "busy" and view.info.waiting_for is None and view.why is None


def test_an_agreeing_poll_keeps_its_own_waiting_for():
    book = run(ev("PermissionRequest", tool="Bash"))
    view = hs.merge((info("waiting", waiting_for="dialog"),), book, 101.0)[0]
    assert view.info.waiting_for == "dialog" and view.why == "permission"


def test_the_late_permission_notification_does_not_reopen_a_confirmed_wait():
    book = hs.observe(run(ev("PermissionRequest", tool="Bash")), (info("waiting"),), 103.0)
    book = hs.record(book, ev("Notification", ntype="permission_prompt", msg="needs Bash"), 106.0)
    assert book[SID].seen and book[SID].msg == "needs Bash"
    assert statuses(hs.merge((info("busy"),), book, 107.0)) == {SID: ("busy", None)}
