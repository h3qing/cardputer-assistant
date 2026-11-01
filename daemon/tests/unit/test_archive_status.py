"""get_status data (docs/15 §3.4): one fixed read-only request to cpad.sock, allow-listed fields, file fallback."""

from __future__ import annotations

import json

import pytest

from cpad import __version__
from cpad.archive import status as st
from cpad.archive.fs import Root
from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.control import client
from cpad.events.log import EventLog
from tests.support import archive_tree as at

RUNNING = {
    "ok": True, "running": True, "version": "0.3.1", "link": "ready", "paused_s": 0.0,
    "device": {"serial": "SECRET-SERIAL", "dev": "020000000001", "fw": "cpa-fw/0.3.1", "boot": "3QZK1M7-00042",
               "pending": 0, "store": "ok", "launcher": True},
    "agent": {"status": "ok", "version": "2.1.0", "spawn": True, "reply": False},
    "projects": [{"key": "cpa", "slot": 1, "ok": True}], "projects_error": None, "items_stored": 12,
    "queue_open": 2, "reboots": 0, "last_release": None, "backoff_s": 0.0,
    "recordings": {"on_device": 1, "pulling": 0, "waiting": 0, "processing": 1, "blocked": 0, "failed": 0,
                   "last_result_at": "2026-10-21T22:00:00.000+00:00"},
    "warnings": ["/Users/someone/CardputerNotes is not writable"],
}  # fmt: skip


@pytest.fixture
def home(tmp_path):
    paths = Paths.at(tmp_path / "home")
    paths.home.mkdir()
    write_json(paths.projects, {"version": 1, "projects": [
        {"key": "cpa", "name": "Cardputer", "slot": 1, "root": "/Users/someone/src/SECRET-ROOT"}]})  # fmt: skip
    EventLog(paths.events).append(
        ({"ev": "queue.added", "at": "x", "qid": "q1", "project": "cpa", "text": "a", "intent": "do", "origin": "cli"},)
    )
    return paths, Root.at(at.build(tmp_path / "notes"))


def test_running_daemon_fields_are_allow_listed(home):
    paths, root = home
    sent = []

    def request(path, payload):
        sent.append((path, payload))
        return RUNNING

    doc = st.status(paths, root, request=request)
    assert sent == [(paths.sock, {"cmd": "status"})]
    assert doc == {
        "daemon": "running", "version": "0.3.1", "link": "ready",
        "device": {"fw": "cpa-fw/0.3.1", "pending": 0, "store": "ok", "launcher": True},
        "agent": {"status": "ok", "spawn": True, "reply": False},
        "projects": [{"key": "cpa", "name": "Cardputer", "slot": 1, "ok": True}], "queue_open": 2,
        "recordings": RUNNING["recordings"],
    }  # fmt: skip
    text = json.dumps(doc)
    assert "SECRET" not in text and "020000000001" not in text and "/Users" not in text


@pytest.mark.parametrize("failure", [client.DaemonNotRunning("x"), TimeoutError(), ValueError("garbage")])
def test_offline_status_comes_from_files(home, failure):
    paths, root = home

    def request(path, payload):
        raise failure

    doc = st.status(paths, root, request=request)
    assert (doc["daemon"], doc["version"], doc["link"], doc["device"], doc["agent"]) == (
        "not running", __version__, None, None, None)  # fmt: skip
    assert doc["projects"] == [{"key": "cpa", "name": "Cardputer", "slot": 1, "ok": False}]
    assert doc["queue_open"] == 1
    assert set(doc["recordings"]) == {"on_device", "pulling", "waiting", "processing", "blocked", "failed",
                                      "last_result_at"}  # fmt: skip
    assert "SECRET" not in json.dumps(doc)


def test_garbage_from_the_socket_is_not_passed_through(home):
    paths, root = home
    doc = st.status(paths, root, request=lambda path, payload: {"running": True, "device": "x", "projects": "y"})
    assert doc["device"] == {"fw": None, "pending": None, "store": None, "launcher": None}
    assert doc["projects"] == []


def test_a_broken_projects_file_gives_no_projects(home):
    paths, root = home
    paths.projects.write_text("{")
    doc = st.status(paths, root, request=lambda path, payload: (_ for _ in ()).throw(client.DaemonNotRunning("x")))
    assert doc["projects"] == []


def test_the_request_is_a_constant():
    assert dict(st.STATUS_REQUEST) == {"cmd": "status"}
    with pytest.raises(TypeError):
        st.STATUS_REQUEST["cmd"] = "pause"
