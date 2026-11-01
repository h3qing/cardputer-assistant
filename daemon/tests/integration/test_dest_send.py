"""Sending from this Mac (docs/25 §5.4, §3.4, D14, D21): cpa filing send, cpa dest pending / send / drop / usual,
through main(), over a filing home (tests/support/filing_home) and a fake Mac: MEMO is a new memo without a project
whose text is "周五前给 Alice 发报价单。" and a U+2028, OLD a meeting with only a transcript, MEETING a meeting."""

from __future__ import annotations

import json

import pytest

from cpad.cli.main import main as cpa
from cpad.dest import send
from cpad.events.log import EventLog
from cpad.events.records import iso
from tests.support import archive_tree as at
from tests.support import send_home as sh

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

HEAD = "[Cardputer 语音·可能有错字或别人的话] "


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


# --- cpa filing send ---------------------------------------------------------------------------------


def test_a_memo_goes_to_grok_as_a_draft_with_the_header_and_is_recorded_as_handed(home, mac, capsys):
    fake, board = mac
    sh.grok()
    before = len(fake.opened)
    code, doc = run(capsys, ["filing", "send", at.MEMO.lower(), "url.grok"])
    assert code == 0
    assert doc == {"ok": True, "v": 1, "ref": at.MEMO, "dest": "url.grok", "label": "Grok", "kind": "url",
                   "outcome": "handed", "via": "url", "why": None, "code": None, "leaves_mac": True}  # fmt: skip
    ((kind, link),) = fake.opened[before:]
    assert kind == "url" and link.startswith("grokbot://app/v1/prompt?text=%5BCardputer%20")
    assert "%0A" not in link and "%E2%80%A8" not in link  # the line separator became a space
    decided, handed = events(home)[-2:]
    assert (decided["ev"], decided["origin"], decided["how"], decided["action"], decided["reason"]) == (
        "dest.decided", "mac", "pick", "deliver", "mac")  # fmt: skip
    assert (handed["ev"], handed["via"], handed["by"], handed["item"]) == ("dest.handed", "url", "cli", at.MEMO)
    assert "text" not in handed and "to" in handed and board.texts == []


def test_a_second_draft_to_the_same_bot_goes_to_the_clipboard_and_without_the_clipboard_it_waits(home, mac, capsys):
    fake, board = mac
    sh.grok()
    assert run(capsys, ["filing", "send", at.MEMO, "url.grok"])[1]["via"] == "url"
    code, doc = run(capsys, ["filing", "send", at.MEMO, "url.grok", "--by", "app", "--again"])
    assert (code, doc["via"], doc["why"]) == (0, "clip", "draft_busy")
    assert board.texts == [HEAD + "周五前给 Alice 发报价单。 第二行"] and events(home)[-1]["by"] == "app"
    assert cpa(["dest", "rm", "clip"]) == 0
    code, doc = run(capsys, ["filing", "send", at.MEMO, "url.grok", "--again"])
    assert (code, doc["outcome"], doc["code"]) == (1, "failed", "E_DRAFT_BUSY")
    rows = run(capsys, ["dest", "pending"])[1]["rows"]
    assert [(r["ref"], r["why"], r["st"], r["code"]) for r in rows] == [(at.MEMO, "held", "failed", "E_DRAFT_BUSY")]


def test_an_untested_link_needs_the_device_and_nothing_is_written(home, mac, capsys):
    sh.grok(verified=False)
    before = events(home)
    code, doc = run(capsys, ["filing", "send", at.MEMO, "url.grok"])
    assert (code, doc) == (1, {"ok": False, "v": 1, "error": "E_NEEDS_DEVICE", "field": "dest"})
    assert events(home) == before
    assert cpa(["filing", "send", at.MEMO, "url.grok"]) == 1
    assert "cpa dest test ID --send" in capsys.readouterr().err


@pytest.mark.parametrize(
    ("rec", "dest", "expected"),
    [(at.MEETING, "url.grok", (2, "E_BAD_ARG", "id")), (at.OLD, "clip", (2, "E_BAD_ARG", "id")),
     (at.MEMO, "url.nope", (2, "E_UNKNOWN_DEST", "dest")), (at.MEMO, "daily", (2, "E_BAD_ARG", "dest")),
     (at.MEMO, "queue", (2, "E_BAD_ARG", "dest")), (at.MEMO, "Grok!", (2, "E_BAD_ARG", "dest")),
     ("3QZK1M7-00099-0000", "clip", (1, "E_UNKNOWN_ID", "id")), ("not-an-id!", "clip", (2, "E_BAD_ARG", "id")),
     (at.MEMO, "paste", (1, "E_DEST_OFF", "dest"))],
)  # fmt: skip
def test_refusals_name_a_code_and_write_nothing(home, mac, capsys, rec, dest, expected):
    sh.grok()
    before = events(home)
    code, doc = run(capsys, ["filing", "send", rec, dest])
    assert (code, doc["error"], doc["field"]) == expected and doc["ok"] is False
    assert events(home) == before
    assert cpa(["filing", "send", rec, dest]) == expected[0]
    assert capsys.readouterr().err.startswith("error: ")


def test_a_meeting_and_a_local_file(home, mac, capsys, tmp_path, monkeypatch):
    user = (tmp_path / "user").resolve()
    (user / "Notes" / "sb").mkdir(parents=True)
    monkeypatch.setenv("HOME", str(user))
    assert cpa(["dest", "add", "file.sb", "--root", "~/Notes/sb", "--path", "inbox.md", "--local"]) == 0
    code, doc = run(capsys, ["filing", "send", at.MEMO, "file.sb"])
    assert (code, doc["outcome"], doc["via"]) == (0, "delivered", None)
    assert "周五前给 Alice 发报价单。" in (user / "Notes" / "sb" / "inbox.md").read_text(encoding="utf-8")
    code, doc = run(capsys, ["filing", "send", at.MEETING, "file.sb"])
    assert (code, doc["error"]) == (1, "E_NO_TEXT")  # allowed there, but a meeting has no memo text


def test_any_app_gets_the_clipboard_and_comes_forward(home, mac, capsys):
    fake, board = mac
    assert cpa(["dest", "add", "--app", sh.NOTES, "--local"]) == 0
    code, doc = run(capsys, ["filing", "send", at.MEMO, "app.my-notes"])
    assert (code, doc["outcome"], doc["via"], doc["leaves_mac"]) == (0, "handed", "clip", False)
    assert board.texts == [HEAD + "周五前给 Alice 发报价单。 第二行"] and fake.opened[-1][0] == "app"
    assert cpa(["filing", "send", at.MEMO, "app.my-notes", "--again"]) == 0
    assert "on the clipboard for 2 minutes, My Notes in front" in capsys.readouterr().out


def test_a_failure_waits_and_a_send_from_the_list_tries_once_more(home, mac, capsys):
    fake, _ = mac
    sh.grok()
    app = fake.apps.pop(sh.GROK.bundle)
    assert cpa(["filing", "send", at.MEMO, "url.grok"]) == 1
    assert "not sent (E_DEST_MISSING)" in capsys.readouterr().out
    fake.apps[sh.GROK.bundle] = app
    code, doc = run(capsys, ["dest", "send", at.MEMO.lower()])
    assert (code, doc["outcome"], doc["dest"]) == (0, "handed", "url.grok")
    assert [e["attempt"] for e in events(home, "dest.decided")] == [1, 2]
    assert run(capsys, ["dest", "pending"])[1]["rows"] == []
    assert cpa(["dest", "pending"]) == 0 and "nothing waits" in capsys.readouterr().out


def test_a_send_still_running_is_busy(home, mac, capsys):
    sh.grok()
    running = {"ev": "dest.decided", "at": iso(send.NOW()), "item": at.MEMO, "src": "voice",
               "want": "url.grok", "dest": "url.grok", "kind": "url", "action": "deliver", "reason": "mac",
               "how": "pick", "origin": "mac", "prof": None, "attempt": 1}  # fmt: skip
    EventLog(home.events).append((running,))
    assert run(capsys, ["filing", "send", at.MEMO, "url.grok"])[1]["error"] == "E_BUSY"
    assert run(capsys, ["dest", "drop", at.MEMO])[1]["error"] == "E_BUSY"


# --- the usual destination and the waiting list --------------------------------------------------------


def test_a_usual_destination_preselects_and_a_bare_send_uses_it(home, mac, capsys):
    sh.grok()
    rec = sh.fresh_memo(home)
    assert run(capsys, ["dest", "pending"])[1]["rows"] == []
    assert run(capsys, ["dest", "usual", "blog", "url.grok"]) == (0, {"ok": True, "v": 1, "usual": [
        {"project": "blog", "dest": "url.grok"}]})  # fmt: skip
    (row,) = run(capsys, ["dest", "pending"])[1]["rows"]
    assert {k: row[k] for k in ("ref", "why", "dest", "label", "fl", "project", "src", "text", "st")} == {
        "ref": rec, "why": "usual", "dest": "url.grok", "label": "Grok", "fl": "lc", "project": "blog",
        "src": "voice", "text": "给 Grok：明天把首页改了", "st": None}  # fmt: skip
    assert cpa(["dest", "pending"]) == 0 and "usual" in capsys.readouterr().out
    code, doc = run(capsys, ["dest", "send", rec])
    assert (code, doc["dest"], doc["via"]) == (0, "url.grok", "url")
    assert run(capsys, ["dest", "pending"])[1]["rows"] == []


def test_removing_a_project_forgets_its_usual_destination(home, mac, capsys):
    """Review 2026-10-26: a later project must never inherit a removed one's usual destination."""
    sh.grok()
    assert cpa(["dest", "usual", "blog", "url.grok"]) == 0
    assert run(capsys, ["project", "rm", "blog"])[0] == 0
    assert run(capsys, ["dest", "usual"])[1]["usual"] == []
    forgot = events(home, "dest.changed")[-1]
    assert (forgot["op"], forgot["id"], forgot["project"], forgot["by"]) == ("usual", None, "blog", "cli")


def test_dropping_a_suggestion_writes_why_and_it_is_not_offered_again(home, mac, capsys):
    sh.grok()
    rec = sh.fresh_memo(home)
    assert cpa(["dest", "usual", "blog", "url.grok"]) == 0
    assert run(capsys, ["dest", "drop", rec, "--by", "app"]) == (0, {"ok": True, "v": 1, "ref": rec,
                                                                      "dest": "url.grok"})  # fmt: skip
    dropped = events(home, "dest.dropped")[-1]
    assert (dropped["item"], dropped["by"]) == (rec, "app")
    assert run(capsys, ["dest", "pending"])[1]["rows"] == []
    assert run(capsys, ["dest", "drop", rec])[1]["error"] == "E_UNKNOWN_ID"
    assert cpa(["dest", "drop", "3QZK1M7-00045-0001"]) == 1
    assert "nothing waits" in capsys.readouterr().err


def test_a_memo_without_memo_text_has_nothing_to_send(home, mac, capsys):
    rec = sh.fresh_memo(home, text=None)
    assert run(capsys, ["filing", "send", rec, "clip"])[1]["error"] == "E_NO_TEXT"
    assert cpa(["filing", "send", rec, "clip"]) == 1 and "cpa-stt use" in capsys.readouterr().err


def test_a_bare_send_with_nothing_preselected_needs_a_destination(home, mac, capsys):
    rec = sh.fresh_memo(home)
    assert run(capsys, ["dest", "send", rec])[1] == {"ok": False, "v": 1, "error": "E_BAD_ARG", "field": "dest"}
    assert run(capsys, ["dest", "send", "no such ref!"])[1]["field"] == "ref"
    assert run(capsys, ["dest", "send", "typed-note-1"])[1]["error"] == "E_UNKNOWN_ID"


def test_dropping_a_held_item_and_listing_expired_ones(home, mac, capsys, monkeypatch):
    sh.grok()
    fake, _ = mac
    fake.apps.pop(sh.GROK.bundle)
    assert cpa(["filing", "send", at.MEMO, "url.grok"]) == 1
    assert cpa(["dest", "drop", at.MEMO]) == 0 and "its daily-note line stays" in capsys.readouterr().out
    expired = {"ev": "dest.expired", "at": events(home)[-1]["at"], "item": at.MEMO, "dest": "url.grok"}
    EventLog(home.events).append((expired,))
    assert run(capsys, ["dest", "pending"])[1]["rows"] == []
    (row,) = run(capsys, ["dest", "pending", "--expired"])[1]["rows"]
    assert (row["ref"], row["why"], row["dest"]) == (at.MEMO, "expired", "url.grok")


def test_usual_lists_forgets_and_refuses(home, mac, capsys):
    sh.grok()
    assert cpa(["dest", "usual", "cpa", "url.grok"]) == 0 and "cpa" in capsys.readouterr().out
    assert cpa(["dest", "usual", "cpa"]) == 0 and "url.grok" in capsys.readouterr().out
    assert run(capsys, ["dest", "usual", "cpa", "-"])[1]["usual"] == []
    assert cpa(["dest", "usual"]) == 0 and "no usual destination" in capsys.readouterr().out
    changed = [e for e in events(home, "dest.changed") if e["op"] == "usual"]
    assert [(e["project"], e["id"]) for e in changed] == [("cpa", "url.grok"), ("cpa", None)]
    assert run(capsys, ["dest", "usual", "acme", "url.grok"])[1]["error"] == "E_UNKNOWN_PROJECT"
    assert run(capsys, ["dest", "usual", "Bad!", "url.grok"])[1]["error"] == "E_BAD_ARG"
    assert run(capsys, ["dest", "usual", "cpa", "daily"])[1] == {"ok": False, "v": 1, "error": "E_BAD_ARG",
                                                                 "field": "dest"}  # fmt: skip


def test_a_broken_destinations_file_is_unavailable(home, mac, capsys):
    (home.home / "destinations.json").write_text("{", encoding="utf-8")
    for argv in (["dest", "pending"], ["dest", "usual"], ["filing", "send", at.MEMO, "clip"]):
        assert run(capsys, argv)[1] == {"ok": False, "v": 1, "error": "E_UNAVAILABLE", "field": "dests"}


def test_an_unknown_option_with_json_prints_the_error_document(home, capsys):
    assert cpa(["dest", "send", at.MEMO, "--json", "--net"]) == 2
    assert json.loads(capsys.readouterr().out) == {"ok": False, "v": 1, "error": "E_BAD_ARG", "field": "argv"}


def test_the_send_clock_is_pinned_by_the_fixture(mac):
    assert send.NOW() > 0
