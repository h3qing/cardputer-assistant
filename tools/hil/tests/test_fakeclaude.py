"""The harness's view of daemon/tests/fixtures/bin/fake-claude's state directory."""

import json

from harness.fakeclaude import FakeClaudeDir, bg_session, tty_session


def test_tty_session_has_the_interactive_shape():
    s = tty_session("aaaa0001", "看板测试", "/p", "waiting")
    assert s == {"pid": 2, "cwd": "/p", "kind": "interactive", "name": "看板测试", "startedAt": 1792455000000,
                 "sessionId": "aaaa0001-4c2b-4a6e-9f10-2b7c5d8e3a61", "status": "waiting"}  # fmt: skip


def test_bg_session_has_the_background_shape():
    s = bg_session("bbbb0002", "cpa/x", "/p", "busy", "running")
    assert s["kind"] == "background"
    assert s["id"] == "bbbb0002"
    assert s["sessionId"].startswith("bbbb0002-")
    assert (s["status"], s["state"]) == ("busy", "running")


def test_init_writes_an_empty_state_and_the_env_points_at_the_dir(tmp_path):
    fake = FakeClaudeDir.create(tmp_path / "fc")
    assert fake.state() == {"sessions": []}
    assert fake.env("/usr/bin:/bin")["FAKE_CLAUDE_DIR"] == str(tmp_path / "fc")
    assert fake.env("/usr/bin:/bin")["PATH"].endswith("/usr/bin:/bin")


def test_set_sessions_keeps_other_keys(tmp_path):
    fake = FakeClaudeDir.create(tmp_path / "fc")
    fake.write_state({"sessions": [], "seq": 4, "spawn_mode": "ok"})
    fake.set_sessions([tty_session("aaaa0001", "n", "/p")])
    state = fake.state()
    assert state["seq"] == 4 and state["spawn_mode"] == "ok"
    assert [s["name"] for s in state["sessions"]] == ["n"]


def test_write_state_is_atomic_and_utf8(tmp_path):
    fake = FakeClaudeDir.create(tmp_path / "fc")
    fake.set_sessions([tty_session("aaaa0001", "中文", "/p")])
    assert "中文" in (tmp_path / "fc" / "state.json").read_text(encoding="utf-8")
    assert not list((tmp_path / "fc").glob("*.tmp"))


def test_calls_and_spawns_read_calls_jsonl(tmp_path):
    fake = FakeClaudeDir.create(tmp_path / "fc")
    assert fake.calls() == ()
    lines = [
        {"argv": ["/bin/fake-claude", "agents", "--json"], "cwd": "/", "env": {}},
        {"argv": ["/bin/fake-claude", "--bg", "--name", "cpa/x", "--", "t"], "cwd": "/p", "env": {}},
        {"argv": ["/bin/fake-claude", "--bg", "--resume", "id", "--", "t"], "cwd": "/p", "env": {}},
    ]
    (tmp_path / "fc" / "calls.jsonl").write_text("".join(json.dumps(x) + "\n" for x in lines), encoding="utf-8")
    assert len(fake.calls()) == 3
    assert [c["cwd"] for c in fake.spawns()] == ["/p"]


def test_env_carries_the_corefoundation_encoding(tmp_path, monkeypatch):
    monkeypatch.delenv("__CF_USER_TEXT_ENCODING", raising=False)
    fake = FakeClaudeDir.create(tmp_path / "fc")
    assert fake.env("/usr/bin:/bin")["__CF_USER_TEXT_ENCODING"].startswith("0x")
