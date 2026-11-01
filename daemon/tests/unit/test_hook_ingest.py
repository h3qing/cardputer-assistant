"""Claude Code hook input -> compact control request -> HookEvent (#33). Hook input is untrusted JSON."""

from __future__ import annotations

import asyncio
import json

import pytest
from hypothesis import given
from hypothesis import strategies as st

from cpad.agents.claude_code import hooks
from cpad.control import client, server

SID = "8d3e7a19-4c2b-4a6e-9f10-2b7c5d8e3a61"


def base(event: str, **extra) -> dict:
    return {"session_id": SID, "transcript_path": "/Users/u/.claude/projects/x/s.jsonl", "cwd": "/Users/u/proj",
            "hook_event_name": event, "permission_mode": "default", **extra}  # fmt: skip


def line_size(payload: dict) -> int:
    """Bytes the control client puts on the socket (json.dumps default: ASCII escapes) plus the newline."""
    return len(json.dumps(payload).encode()) + 1


# --- client side: compact() -----------------------------------------------------------------


def test_notification_keeps_only_the_allowlist():
    text = "Claude needs your permission to use Bash"
    doc = base("Notification", message=text, notification_type="permission_prompt")
    assert hooks.compact(doc) == {"cmd": "hook", "ev": "Notification", "sid": SID, "ntype": "permission_prompt",
                                  "msg": text}  # fmt: skip


def test_bodies_paths_and_tool_input_are_never_forwarded():
    doc = base("PreToolUse", tool_name="AskUserQuestion", tool_input={"questions": ["secret plan"]},
               tool_use_id="toolu_1", prompt="my prompt", agent_id="a1", agent_type="Explore")  # fmt: skip
    payload = hooks.compact(doc)
    assert payload == {"cmd": "hook", "ev": "PreToolUse", "sid": SID, "tool": "AskUserQuestion", "sub": True}
    assert "secret" not in json.dumps(payload) and "/Users/u" not in json.dumps(payload)


def test_user_prompt_text_is_not_forwarded():
    payload = hooks.compact(base("UserPromptSubmit", prompt="rm -rf ~ please"))
    assert payload == {"cmd": "hook", "ev": "UserPromptSubmit", "sid": SID}


@pytest.mark.parametrize(
    ("event", "field", "value"),
    [("SessionStart", "source", "resume"), ("SessionEnd", "reason", "prompt_input_exit"),
     ("StopFailure", "error", "authentication_failed")],
)  # fmt: skip
def test_detail_comes_from_the_event_specific_field(event, field, value):
    assert hooks.compact(base(event, **{field: value}))["detail"] == value


def test_stop_forwards_a_bounded_clean_excerpt():
    text = "**Done.**\n\n## Summary\nAll `48` tests\tpass ‮evil​ 🎉 " + "很长的总结" * 200
    payload = hooks.compact(base("Stop", last_assistant_message=text, stop_hook_active=False))
    msg = payload["msg"]
    assert msg.startswith("Done. Summary All 48 tests pass evil ")
    assert len(msg.encode()) <= hooks.MSG_MAX and msg.endswith("…")
    assert not any(c in msg for c in "\n\t‮​🎉*`#")


@pytest.mark.parametrize(
    "sid",
    ["served:8d3e7a19-4c2b-4a6e-9f10-2b7c5d8e3a61", "8D3E7A19-4C2B-4A6E-9F10-2B7C5D8E3A61", "8d3e7a19", "", None, 7,
     SID + "\n", "../../etc/passwd"],
)  # fmt: skip
def test_bad_session_ids_drop_the_event(sid):
    assert hooks.compact({**base("Stop"), "session_id": sid}) is None


@pytest.mark.parametrize(
    "doc",
    [[], "Stop", None, {"hook_event_name": "Stop"}, base("ConfigChange"), base(None), base(["Stop"]), base({"a": 1})],
)
def test_unknown_events_and_shapes_are_dropped(doc):
    assert hooks.compact(doc) is None


def test_argv_event_must_agree_with_stdin():
    assert hooks.compact(base("Stop"), "Stop") == {"cmd": "hook", "ev": "Stop", "sid": SID}
    assert hooks.compact(base("Stop"), "SessionEnd") is None


@pytest.mark.parametrize(
    ("field", "value"),
    [("tool_name", "Bash; rm -rf /"), ("tool_name", "x" * 65), ("tool_name", 3), ("notification_type", "Bad Type"),
     ("notification_type", "n" * 33), ("agent_id", "")],
)  # fmt: skip
def test_odd_optional_fields_are_left_out(field, value):
    payload = hooks.compact(base("PermissionRequest", **{field: value}))
    assert payload == {"cmd": "hook", "ev": "PermissionRequest", "sid": SID}


def test_non_string_messages_are_left_out():
    assert "msg" not in hooks.compact(base("Notification", message={"x": 1}, notification_type="idle_prompt"))
    assert "msg" not in hooks.compact(base("Stop", last_assistant_message="  \n\t "))


def test_worst_case_request_fits_the_control_line():
    doc = base("Notification", notification_type="n" * 32, message="😀" * 500, tool_name="T" * 64, agent_id="a")
    payload = {**hooks.compact(doc), "tool": "T" * 64, "detail": "d" * 32}
    assert line_size(payload) <= server.REQUEST_MAX


@given(st.text(max_size=3000))
def test_any_message_fits_and_the_daemon_accepts_it(text):
    payload = hooks.compact(base("Notification", message=text, notification_type="idle_prompt", agent_id="a"))
    assert line_size(payload) <= server.REQUEST_MAX
    event = hooks.parse(json.loads(json.dumps(payload)))
    assert event.msg == payload.get("msg")


# --- daemon side: parse() -------------------------------------------------------------------


def test_parse_builds_a_hook_event():
    event = hooks.parse({"cmd": "hook", "ev": "Notification", "sid": SID, "ntype": "permission_prompt", "msg": "hi"})
    assert event == hooks.HookEvent(ev="Notification", session_id=SID, ntype="permission_prompt", msg="hi")
    assert hooks.parse({"cmd": "hook", "ev": "PreToolUse", "sid": SID, "tool": "AskUserQuestion", "sub": True}).sub


@pytest.mark.parametrize(
    "change",
    [{"ev": "Bogus"}, {"sid": "8d3e7a19"}, {"sid": None}, {"ntype": "Bad Type"}, {"tool": "a b"}, {"detail": "x" * 33},
     {"sub": 1}, {"msg": "x" * 121}, {"msg": "a\nb"}, {"msg": 5}, {"extra": "field"}, {"msg": "‮abc"}],
)  # fmt: skip
def test_parse_rejects_anything_outside_the_allowlist(change):
    with pytest.raises(ValueError):
        hooks.parse({"cmd": "hook", "ev": "Stop", "sid": SID, **change})


def test_control_server_accepts_hook_requests():
    req = server.parse_request(json.dumps({"cmd": "hook", "ev": "Stop", "sid": SID, "msg": "done"}).encode() + b"\n")
    assert req == {"cmd": "hook", "event": hooks.HookEvent(ev="Stop", session_id=SID, msg="done")}
    with pytest.raises(server.BadRequest):
        server.parse_request(json.dumps({"cmd": "hook", "ev": "Stop", "sid": "nope"}).encode())


@pytest.mark.parametrize("ev", [[1], {"Stop": 1}, ["Stop"], 7, None, True])
def test_a_non_string_ev_is_refused_not_crashed(ev):
    doc = {"cmd": "hook", "ev": ev, "sid": SID}
    with pytest.raises(ValueError):
        hooks.parse(doc)
    with pytest.raises(server.BadRequest):
        server.parse_request(json.dumps(doc).encode())


@pytest.mark.parametrize("ev", [[1], {"Stop": 1}])
async def test_the_socket_answers_a_non_string_ev_with_not_ok(short_tmp, ev):
    seen = []

    async def handler(req):
        seen.append(req)
        return {"ok": True}

    srv = server.ControlServer(short_tmp / "cpad.sock", handler)
    await srv.start()
    try:
        reply = await asyncio.to_thread(client.request, srv.path, {"cmd": "hook", "ev": ev, "sid": SID})
        after = await asyncio.to_thread(client.request, srv.path, {"cmd": "status"})
    finally:
        await srv.close()
    assert reply["ok"] is False and "ev" in reply["error"]
    assert after == {"ok": True} and seen == [{"cmd": "status"}]
