"""A Mac send never leaves a memo stuck, never pre-fills it twice by accident (docs/25 §5.4, §7.4): a delivery that
raises or is cut off writes dest.failed{E_INTERRUPTED}; a Mac send still `running` after STALE_RUNNING_S was cut off
too; a memo already handed to a destination needs --again; a held row another process sent meanwhile is gone."""

from __future__ import annotations

import json
import os
import signal
import time

import pytest

from cpad.cli.main import main as cpa
from cpad.dest import send
from cpad.dest.pending import STALE_RUNNING_S
from cpad.events.log import EventLog
from cpad.events.records import iso
from tests.support import archive_tree as at
from tests.support import send_home as sh


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = sh.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths


@pytest.fixture
def mac(monkeypatch):
    return sh.install(monkeypatch)


def events(paths, ev: str | None = None) -> list[dict]:
    found = [json.loads(line) for line in paths.events.read_text(encoding="utf-8").split("\n") if line]
    return [e for e in found if ev is None or e["ev"] == ev]


def run(capsys, argv: list[str]) -> tuple[int, dict]:
    capsys.readouterr()
    code = cpa([*argv, "--json"])
    return code, json.loads(capsys.readouterr().out)


def rows(capsys) -> list[tuple]:
    return [(r["ref"], r["st"], r["code"]) for r in run(capsys, ["dest", "pending"])[1]["rows"]]


def running(rec: str, at_s: float) -> dict:
    return {"ev": "dest.decided", "at": iso(at_s), "item": rec, "src": "voice", "want": "url.grok",
            "dest": "url.grok", "kind": "url", "action": "deliver", "reason": "mac", "how": "pick", "origin": "mac",
            "prof": None, "attempt": 1}  # fmt: skip


# --- a delivery that raises or is cut off ----------------------------------------------------------


def test_a_delivery_that_raises_fails_and_the_memo_can_be_sent_again(home, mac, capsys, monkeypatch):
    fake, _ = mac
    sh.grok()

    def broken(url, app):
        raise OSError("the frameworks did not load")

    works = fake.open_url
    monkeypatch.setattr(fake, "open_url", broken)
    code, doc = run(capsys, ["filing", "send", at.MEMO, "url.grok"])
    assert (code, doc["outcome"], doc["code"]) == (1, "failed", "E_INTERRUPTED")
    assert events(home)[-1]["ev"] == "dest.failed"
    assert rows(capsys) == [(at.MEMO, "failed", "E_INTERRUPTED")]
    monkeypatch.setattr(fake, "open_url", works)
    assert run(capsys, ["dest", "send", at.MEMO])[1]["outcome"] == "handed"


@pytest.mark.parametrize("stop", ["interrupt", "sigterm"])
def test_a_send_cut_off_writes_dest_failed_before_it_ends(home, mac, capsys, monkeypatch, stop):
    fake, _ = mac
    sh.grok()

    def cut(url, app):
        if stop == "sigterm":
            os.kill(os.getpid(), signal.SIGTERM)  # the Mac app's timeout
        raise KeyboardInterrupt

    monkeypatch.setattr(fake, "open_url", cut)
    with pytest.raises((KeyboardInterrupt, SystemExit)):
        cpa(["filing", "send", at.MEMO, "url.grok", "--json"])
    last = events(home)[-1]
    assert (last["ev"], last["code"], last["retryable"]) == ("dest.failed", "E_INTERRUPTED", False)
    assert signal.getsignal(signal.SIGTERM) is signal.SIG_DFL
    assert run(capsys, ["dest", "drop", at.MEMO])[0] == 0


def test_a_mac_send_running_too_long_was_cut_off(home, mac, capsys):
    sh.grok()
    EventLog(home.events).append((running(at.MEMO, send.NOW() - STALE_RUNNING_S - 1),))
    assert rows(capsys) == [(at.MEMO, "failed", "E_INTERRUPTED")]
    code, doc = run(capsys, ["dest", "send", at.MEMO])
    assert (code, doc["outcome"]) == (0, "handed")
    assert events(home, "dest.decided")[-1]["attempt"] == 2


def test_a_mac_send_running_just_now_is_still_busy(home, mac, capsys):
    sh.grok()
    EventLog(home.events).append((running(at.MEMO, send.NOW() - 5),))
    assert rows(capsys) == []
    assert run(capsys, ["filing", "send", at.MEMO, "url.grok"])[1]["error"] == "E_BUSY"


# --- never twice by accident -----------------------------------------------------------------------


def test_a_memo_already_handed_there_needs_again(home, mac, capsys):
    _, board = mac
    sh.grok()
    assert run(capsys, ["filing", "send", at.MEMO, "url.grok"])[1]["via"] == "url"
    before = events(home)
    code, doc = run(capsys, ["filing", "send", at.MEMO, "url.grok"])
    assert (code, doc) == (1, {"ok": False, "v": 1, "error": "E_ALREADY", "field": "dest"})
    assert events(home) == before and board.texts == []
    assert cpa(["filing", "send", at.MEMO, "url.grok"]) == 1 and "--again" in capsys.readouterr().err
    code, doc = run(capsys, ["filing", "send", at.MEMO, "url.grok", "--again"])
    assert (code, doc["via"], doc["why"]) == (0, "clip", "draft_busy")
    assert run(capsys, ["filing", "send", at.MEMO, "clip"])[1]["outcome"] == "delivered"  # another destination


def test_a_held_row_sent_by_another_process_meanwhile_is_not_sent_again(home, mac, capsys, monkeypatch):
    fake, _ = mac
    sh.grok()
    app = fake.apps.pop(sh.GROK.bundle)
    assert cpa(["filing", "send", at.MEMO, "url.grok"]) == 1
    fake.apps[sh.GROK.bundle] = app
    stale = send.open_desk(home)
    assert run(capsys, ["dest", "send", at.MEMO])[1]["outcome"] == "handed"  # the other process
    monkeypatch.setattr(send, "open_desk", lambda _paths: stale)
    opened = len(fake.opened)
    assert run(capsys, ["dest", "send", at.MEMO, "--again"])[1]["error"] == "E_UNKNOWN_ID"
    assert len(fake.opened) == opened


def test_dropping_a_suggestion_keeps_its_profile(home, mac, capsys):
    sh.grok()
    rec = sh.fresh_memo(home, prof="work")
    assert cpa(["dest", "usual", "blog", "url.grok"]) == 0
    assert run(capsys, ["dest", "drop", rec])[0] == 0
    assert events(home, "dest.dropped")[-1]["prof"] == "work"


def test_a_lone_surrogate_in_a_memo_is_sent_as_a_replacement_character(home, mac, capsys):
    fake, _ = mac
    sh.grok()
    rec = sh.fresh_memo(home, text="ok")
    raw = home.events.read_text(encoding="utf-8").replace('"memo_text":"ok"', '"memo_text":"a\\ud800b"')
    home.events.write_text(raw, encoding="utf-8")
    code, doc = run(capsys, ["filing", "send", rec, "url.grok"])
    assert (code, doc["outcome"]) == (0, "handed")
    assert fake.opened[-1][1].endswith("a%EF%BF%BDb")


def test_the_stale_limit_outlasts_the_mac_app_s_timeout():
    assert STALE_RUNNING_S > 30 and time.time() > 0


def test_off_the_main_thread_nothing_is_installed_for_sigterm():
    import threading

    seen: list[object] = []

    def body() -> None:
        with send._terminable():
            seen.append(signal.getsignal(signal.SIGTERM))

    worker = threading.Thread(target=body)
    worker.start()
    worker.join()
    assert seen == [signal.SIG_DFL]


def test_a_running_item_with_an_unreadable_time_is_left_running():
    from cpad.dest.pending import fold_pending, settled

    state = fold_pending([running(at.MEMO, 0.0) | {"at": "nope"}])
    assert settled(state, time.time()).items[0].st == "running"
