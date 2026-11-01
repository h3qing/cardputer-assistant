"""cpa filing ls | show | file | done | hide | reopen | hand and cpa project ls --json (docs/23 §3.1-§3.4), over a
temporary $CPA_HOME and archive: rows from events.jsonl and the files, filters, pages, display_safe text, one record
per write appended under the events lock, the undo printed, fixed error codes that never echo the input."""

from __future__ import annotations

import hashlib
import io
import json
import os

import pytest

from cpad.cli.main import main as cpa
from cpad.config import host
from cpad.config.settings import Paths
from cpad.control import client
from cpad.control.server import API
from cpad.events import records as queue_records
from cpad.events.log import EventLog
from cpad.filing.rows import device_id
from cpad.queue.fold import fold_queue
from cpad.recordings import records as r
from tests.support import archive_tree as at
from tests.support import filing_home

AT, DEV, SECRET_ROOT = filing_home.AT, filing_home.DEV, filing_home.SECRET_ROOT


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    monkeypatch.setenv("TZ", "America/Los_Angeles")
    return paths


def _json(capsys, argv: list[str], code: int = 0) -> dict:
    assert cpa(argv) == code
    return json.loads(capsys.readouterr().out)


def _ids(doc: dict) -> list[str]:
    return [row["id"] for row in doc["rows"]]


def _lines(paths: Paths) -> list[dict]:
    """Split at newline bytes only, as EventLog does: str.splitlines also splits at a U+2028 in a string."""
    return [json.loads(line) for line in paths.events.read_bytes().split(b"\n") if line]


# --- ls ------------------------------------------------------------------------------------------


def test_filing_alone_lists_new_rows_of_this_mac_newest_first(home, capsys):
    doc = _json(capsys, ["filing", "--json"])
    assert _ids(doc) == [at.MEMO, at.OLD]
    assert doc["counts"] == {"new": 2, "other_mac": 1, "clips_waiting": 0, "on_device": None}
    assert doc["next_cursor"] is None and doc["v"] == 1
    memo, old = doc["rows"]
    assert (memo["title"], memo["intent"], memo["tx"]) == ("报 [31m价", "todo", "done")
    assert memo["text"] == "周五前给 Alice 发报价单。 第二行"
    assert old["text"] == "旧会议只有转写。" and old["at"]["tz_min"] == -420
    assert memo["device"] is None, "cpad has not created id_key yet"


def test_rows_carry_the_short_device_id_once_cpad_made_the_key(home, capsys):
    key = host.id_key(home.id_key)
    assert _json(capsys, ["filing", "ls", "--json"])["rows"][0]["device"] == device_id(key, DEV)


def test_the_other_mac_group_and_hidden_rows_only_when_asked(home, capsys):
    listed = _json(capsys, ["filing", "ls", "--json", "--orphans"])
    assert _ids(listed) == [at.MEMO, at.FAILED, at.OLD]
    orphan = listed["rows"][1]
    assert (orphan["id"], orphan["asked"], orphan["why"], orphan["tx"]) == (at.FAILED, "acme", ["orphan", "no_text"],
                                                                           "failed")  # fmt: skip
    assert _ids(_json(capsys, ["filing", "ls", "--json", "--filing", "hidden"])) == [at.BLOCKED]
    every = _ids(_json(capsys, ["filing", "ls", "--json", "--filing", "all", "--orphans"]))
    assert every == [at.MEMO, at.MEETING, at.FAILED, at.OLD]


@pytest.mark.parametrize(
    ("argv", "want"),
    [
        (["--mode", "meeting", "--from", "2026-10-21"], [at.MEETING]),
        (["--project", "cpa"], [at.MEETING]),
        (["--none", "--to", "2026-10-20"], [at.OLD]),
        (["--tx", "transcript", "--tx", "done"], [at.MEMO, at.MEETING, at.OLD]),
        (["--link", "here"], []),
        (["--text", "PRICING"], [at.MEETING]),
        (["--text", "报价"], [at.MEMO, at.MEETING]),
    ],
)
def test_filters(home, capsys, argv, want):
    assert _ids(_json(capsys, ["filing", "ls", "--json", "--filing", "all", *argv])) == want


def test_the_search_words_can_come_from_stdin(home, capsys, monkeypatch):
    monkeypatch.setattr("sys.stdin", io.StringIO("旧会议\n"))
    assert _ids(_json(capsys, ["filing", "ls", "--json", "--text", "-"])) == [at.OLD]


def test_pages_and_no_text(home, capsys):
    first = _json(capsys, ["filing", "ls", "--json", "--limit", "1", "--no-text"])
    assert _ids(first) == [at.MEMO] and (first["rows"][0]["title"], first["rows"][0]["text"]) == (None, None)
    second = _json(capsys, ["filing", "ls", "--json", "--limit", "1", "--cursor", first["next_cursor"]])
    assert _ids(second) == [at.OLD] and second["next_cursor"] is None


def test_on_device_comes_from_a_running_cpad(home, capsys, monkeypatch):
    monkeypatch.setattr(client, "request", lambda path, payload, **kw: {"ok": True, "recordings": {"on_device": 3}})
    assert _json(capsys, ["filing", "ls", "--json"])["counts"]["on_device"] == 3


def test_the_human_list_has_counts_one_line_per_row_and_no_escape(home, capsys):
    assert cpa(["filing"]) == 0
    out = capsys.readouterr().out
    assert out.splitlines()[0] == "new 2 · other Mac 1 · on device -"
    lines = [line for line in out.splitlines() if line.startswith("3QZK")]
    assert [line.split()[0] for line in lines] == [at.MEMO, at.OLD]
    assert "报 [31m价" in lines[0] and "\x1b" not in out and "\u2028" not in out


@pytest.mark.parametrize(
    ("argv", "code", "field"),
    [
        (["--from", "2026-99-99"], "E_BAD_ARG", "from"),
        (["--project", "acme"], "E_UNKNOWN_PROJECT", "project"),
        (["--filing", "SECRET-WORD"], "E_BAD_ARG", "filing"),
        (["--cursor", "SECRET-WORD"], "E_BAD_ARG", "cursor"),
    ],
)
def test_bad_filters_are_fixed_errors_that_never_echo(home, capsys, argv, code, field):
    assert _json(capsys, ["filing", "ls", "--json", *argv], 2) == {"ok": False, "v": 1, "error": code, "field": field}
    assert cpa(["filing", "ls", *argv]) == 2
    captured = capsys.readouterr()
    assert captured.err.startswith(f"error: {code} ({field})") and "SECRET" not in captured.out + captured.err


@pytest.mark.parametrize(
    ("argv", "hint"),
    [
        (["file", at.FAILED, "cpa"], "add --other"),
        (["file", at.MEMO, "acme"], "cpa project ls"),
        (["ls", "--from", "x"], "dates are YYYY-MM-DD"),
        (["ls", "--cursor", "x"], None),
    ],
)
def test_a_person_gets_a_hint_with_some_errors(home, capsys, argv, hint):
    assert cpa(["filing", *argv]) == 2
    err = capsys.readouterr().err
    assert (hint in err) if hint else err.count(":") == 1, err


def test_an_unreadable_config_is_unavailable(home, capsys):
    home.config.write_text("{", encoding="utf-8")
    assert _json(capsys, ["filing", "ls", "--json"], 1) == {"ok": False, "v": 1, "error": "E_UNAVAILABLE"}


# --- show ----------------------------------------------------------------------------------------


def test_show_has_the_whole_text_the_transcript_audio_and_firmware(home, capsys):
    doc = _json(capsys, ["filing", "show", at.MEMO.lower(), "--json"])
    assert doc["row"]["id"] == at.MEMO and doc["text"] == "周五前给 Alice 发报价单。 第二行"
    assert doc["sentences"] == [{"t_s": 0.0, "text": "待办：周五前给 Alice 发报价单。"}]
    assert (doc["todos"], doc["audio"], doc["fw"], doc["queue_text"]) == ([], ["seg-000.wav"], "cpa-fw/0.4.0", None)
    meeting = _json(capsys, ["filing", "show", at.MEETING, "--json"])
    assert meeting["todos"] == [{"n": 1, "text": "给 Alice 发报价单", "owner": "我", "due": "周五", "t_s": 361.2,
                                 "confident": True}]  # fmt: skip
    assert "recordings/" not in json.dumps(meeting) and SECRET_ROOT not in json.dumps(meeting)


def test_show_without_transcript_and_the_human_view(home, capsys):
    assert _json(capsys, ["filing", "show", at.FAILED, "--json"])["sentences"] is None
    assert cpa(["filing", "show", at.MEETING]) == 0
    out = capsys.readouterr().out
    assert "[00:12] 我们先过一下 pricing 的方案。" in out and "[06:01]" in out and "1. 给 Alice 发报价单" in out


def test_show_of_a_handed_memo_has_its_queue_text_and_reopen_withdraws_it(home, capsys):
    hand = {"ev": "queue.added", "at": AT, "qid": "q7", "project": "cpa", "text": "发报价\u202e单", "intent": "spawn",
            "sid": None, "origin": "inbox", "from_item": at.MEMO, "uncertain": False}  # fmt: skip
    EventLog(home.events).append((hand,))
    doc = _json(capsys, ["filing", "show", at.MEMO, "--json"])
    assert (doc["row"]["filing"], doc["row"]["queue"], doc["queue_text"]) == (
        "handed", {"qid": "q7", "intent": "spawn", "open": True}, "发报价单")  # fmt: skip
    assert cpa(["filing", "show", at.MEMO]) == 0
    assert "queue     q7 spawn open" in capsys.readouterr().out
    assert _json(capsys, ["filing", "hide", at.MEMO, "--json"], 1)["field"] == "queue"
    withdrawn = _json(capsys, ["filing", "reopen", at.MEMO, "--json"])
    assert (withdrawn["filing"], withdrawn["undo"]) == ("new", None)
    assert _lines(home)[-1] == {"ev": "queue.done", "at": _lines(home)[-1]["at"], "qid": "q7", "by": "undo"}


def test_the_human_list_says_where_the_next_page_starts(home, capsys):
    assert cpa(["filing", "--limit", "1"]) == 0
    assert capsys.readouterr().out.splitlines()[-1].startswith("more: --cursor ")


def test_show_of_an_unknown_or_bad_id(home, capsys):
    assert _json(capsys, ["filing", "show", "3QZK1M7-00099-0000", "--json"], 1)["error"] == "E_UNKNOWN_ID"
    assert _json(capsys, ["filing", "show", "../etc", "--json"], 2) == {"ok": False, "v": 1, "error": "E_BAD_ARG",
                                                                      "field": "id"}  # fmt: skip


# --- writes --------------------------------------------------------------------------------------


def test_file_appends_one_edit_and_prints_its_undo(home, capsys):
    before = len(_lines(home))
    doc = _json(capsys, ["filing", "file", at.MEMO.lower(), "cpa", "--json"])
    assert doc == {"ok": True, "v": 1, "id": at.MEMO, "filing": "filed", "changed": True,
                   "undo": {"op": "file", "id": at.MEMO, "args": {"project": None}}, "queue": None}  # fmt: skip
    written = _lines(home)[before:]
    assert [(e["ev"], e["rec"], e["project"], e["by"]) for e in written] == [("rec.edited", at.MEMO, "cpa", "cli")]
    assert cpa(["filing", "file", at.MEMO, "cpa"]) == 0
    assert capsys.readouterr().out == f"{at.MEMO} filed · cpa (no change)\n"
    assert len(_lines(home)) == before + 1, "already filed there: nothing written"
    assert cpa(["filing", "file", at.MEMO, "--none"]) == 0
    assert capsys.readouterr().out == f"{at.MEMO} new\nundo: cpa filing file {at.MEMO} cpa\n"


def test_done_hide_and_reopen_each_print_their_undo(home, capsys):
    steps = [("done", "done", "reopen"), ("hide", "hidden", "reopen"), ("reopen", "done", "hide"),
             ("reopen", "new", "done")]  # fmt: skip
    for op, filing, undo in steps:
        assert cpa(["filing", op, at.OLD]) == 0
        assert capsys.readouterr().out == f"{at.OLD} {filing}\nundo: cpa filing {undo} {at.OLD}\n"
    assert [e["by"] for e in _lines(home)[-4:]] == ["cli"] * 4


def test_the_other_mac_group_needs_other(home, capsys):
    assert _json(capsys, ["filing", "file", at.FAILED, "cpa", "--json"], 2)["field"] == "other"
    doc = _json(capsys, ["filing", "file", at.FAILED, "cpa", "--other", "--json"])
    assert (doc["filing"], doc["undo"]["args"]) == ("filed", {"project": "acme"})
    assert _json(capsys, ["filing", "file", at.FAILED, "acme", "--json"])["filing"] == "new", "the undo works"


@pytest.mark.parametrize(
    ("argv", "code", "error", "field"),
    [
        (["file", at.MEMO, "acme"], 2, "E_UNKNOWN_PROJECT", "project"),
        (["file", at.MEMO], 2, "E_BAD_ARG", "project"),
        (["reopen", at.MEETING], 1, "E_ALREADY", "filing"),
        (["done", at.MEETING], 1, "E_ALREADY", "filing"),
        (["hide", "3QZK1M7-00099-0000"], 1, "E_UNKNOWN_ID", "id"),
    ],
)
def test_refused_writes_write_nothing(home, capsys, argv, code, error, field):
    before = home.events.read_bytes()
    assert _json(capsys, ["filing", *argv, "--json"], code) == {"ok": False, "v": 1, "error": error, "field": field}
    assert home.events.read_bytes() == before


def test_a_write_without_any_log_creates_nothing(short_tmp, monkeypatch, capsys):
    paths = Paths.at(short_tmp / "empty")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    assert _json(capsys, ["filing", "done", at.MEMO, "--json"], 1)["error"] == "E_UNKNOWN_ID"
    assert not paths.events.exists()


def test_the_decision_is_made_again_under_the_lock(home, capsys, monkeypatch):
    """Another process hides the memo between this one's read and its append: nothing more is written."""
    real = EventLog.append

    def racing(self, recs, prepare=None):
        real(EventLog(self.path), (r.hidden(rec=at.MEMO, at=AT),))
        monkeypatch.setattr(EventLog, "append", real)
        return real(self, recs, prepare)

    monkeypatch.setattr(EventLog, "append", racing)
    doc = _json(capsys, ["filing", "hide", at.MEMO, "--json"])
    assert (doc["changed"], doc["undo"], doc["filing"]) == (False, None, "hidden")
    assert sum(1 for e in _lines(home) if e["ev"] == "rec.hidden" and e["rec"] == at.MEMO) == 1


# --- hand ----------------------------------------------------------------------------------------

MEMO_TEXT = "周五前给 Alice 发报价单。 第二行"  # the memo text through queue_text: its U+2028 is a space


def _error(code: str, field: str) -> dict:
    return {"ok": False, "v": 1, "error": code, "field": field}


def test_hand_queues_the_memo_text_cpa_reads_itself(home, capsys):
    before = len(_lines(home))
    doc = _json(capsys, ["filing", "hand", at.MEMO.lower(), "cpa", "--json"])
    assert doc == {"ok": True, "v": 1, "id": at.MEMO, "filing": "handed", "changed": True,
                   "undo": {"op": "reopen", "id": at.MEMO, "args": {}},
                   "queue": {"qid": "q1", "intent": "todo", "open": True}}  # fmt: skip
    (added,) = _lines(home)[before:]
    body = MEMO_TEXT.encode()
    assert added == {"ev": "queue.added", "at": added["at"], "qid": "q1", "project": "cpa", "text": MEMO_TEXT,
                     "intent": "todo", "sid": None, "origin": "inbox", "from_item": at.MEMO, "uncertain": False,
                     "voice": True, "bytes": len(body), "sha8": hashlib.sha256(body).hexdigest()[:8]}  # fmt: skip
    assert _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"], 1) == _error("E_ALREADY", "queue")
    assert _json(capsys, ["filing", "show", at.MEMO, "--json"])["queue_text"] == MEMO_TEXT


def test_hand_takes_no_text_from_argv(home):
    with pytest.raises(SystemExit):
        cpa(["filing", "hand", at.MEMO, "cpa", "words to queue"])


def test_hand_prints_the_item_and_spawn_says_where_it_starts(home, capsys):
    assert cpa(["filing", "hand", at.MEMO, "cpa", "--spawn"]) == 0
    assert capsys.readouterr().out == (f"{at.MEMO} handed · q1 spawn\n"
                                       "start it on the device board: select q1, Enter (plan mode)\n"
                                       f"undo: cpa filing reopen {at.MEMO}\n")  # fmt: skip
    assert cpa(["filing", "reopen", at.MEMO]) == 0
    assert capsys.readouterr().out == f"{at.MEMO} new\n"
    assert fold_queue(_lines(home)).items == (), "withdrawn: off the device board"
    assert cpa(["filing", "hand", at.MEMO, "cpa"]) == 0
    assert capsys.readouterr().out.splitlines()[0] == f"{at.MEMO} handed · q2 todo"


def test_hand_takes_the_transcript_when_the_memo_has_no_text(home, capsys, tmp_path):
    session = next(e for e in _lines(home) if e["ev"] == "rec.archived" and e["rec"] == at.MEMO)
    EventLog(home.events).append((r.result(rec=at.MEMO, state="done", level="transcript", title=None, memo_text=None,
                                           memo_intent=None, actions_n=0, error_code=None, session=session,
                                           at=AT),))  # fmt: skip
    folder = tmp_path / "notes" / at.rec_dir("2026-10-21", "1405", at.MEMO)
    (folder / "transcript.json").rename(folder / "transcript.json.away")
    assert _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"], 1) == _error("E_NO_TEXT", "text")
    (folder / "transcript.json.away").rename(folder / "transcript.json")
    _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"])
    assert _lines(home)[-1]["text"] == "待办：周五前给 Alice 发报价单。"


@pytest.mark.parametrize(
    ("argv", "code", "error", "field"),
    [
        (["hand", at.MEETING, "cpa"], 2, "E_BAD_ARG", "id"),
        (["hand", at.MEMO], 2, "E_UNKNOWN_PROJECT", "project"),
        (["hand", at.MEMO, "acme"], 2, "E_UNKNOWN_PROJECT", "project"),
        (["hand", at.MEMO, "Cpa"], 2, "E_BAD_ARG", "project"),
        (["hand", "3QZK1M7-00099-0000", "cpa"], 1, "E_UNKNOWN_ID", "id"),
    ],
)
def test_refused_hands_write_nothing(home, capsys, argv, code, error, field):
    before = home.events.read_bytes()
    assert _json(capsys, ["filing", *argv, "--json"], code) == _error(error, field)
    assert home.events.read_bytes() == before


@pytest.mark.parametrize("reply", [{"ok": True}, {"ok": True, "api": API - 1}, {"ok": True, "api": "2"}, OSError()])
def test_hand_waits_for_cpad_to_be_restarted_when_an_older_or_silent_one_runs(home, capsys, monkeypatch, reply):
    """An older cpad would start an inbox item with the project's own mode, not plan (docs/23 §2.2)."""

    def request(path, payload, **kw):
        if isinstance(reply, Exception):
            raise reply
        return reply

    monkeypatch.setattr(client, "request", request)
    before = home.events.read_bytes()
    assert _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"], 1) == _error("E_UNAVAILABLE", "cpad")
    assert cpa(["filing", "hand", at.MEMO, "cpa"]) == 1
    assert "cpa service install" in capsys.readouterr().err
    assert home.events.read_bytes() == before


def test_hand_with_a_current_cpad_running(home, capsys, monkeypatch):
    monkeypatch.setattr(client, "request", lambda path, payload, **kw: {"ok": True, "api": API})
    assert _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"])["filing"] == "handed"


def test_the_queue_id_is_given_under_the_lock(home, capsys, monkeypatch):
    """The device queues q1 between this process's read and its append: the hand becomes q2."""
    real = EventLog.append
    typed = queue_records.queue_added(qid="q1", project="blog", text="typed", intent="todo", origin="device", at=AT)

    def racing(self, recs, prepare=None):
        real(EventLog(self.path), (typed,))
        monkeypatch.setattr(EventLog, "append", real)
        return real(self, recs, prepare)

    monkeypatch.setattr(EventLog, "append", racing)
    assert _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"])["queue"]["qid"] == "q2"
    assert [e["qid"] for e in _lines(home) if e["ev"] == "queue.added"] == ["q1", "q2"]


# --- cpa project ls --json -----------------------------------------------------------------------


def test_project_ls_json_has_no_local_path(home, capsys):
    doc = _json(capsys, ["project", "ls", "--json"])
    folder = os.path.basename(json.loads(home.projects.read_text())["projects"][1]["root"])
    assert doc == {"v": 1, "projects": [
        {"key": "cpa", "name": "Cardputer", "slot": 1, "ok": False, "linked": True, "folder": "SECRET-ROOT"},
        {"key": "blog", "name": "Blog", "slot": 2, "ok": True, "linked": True, "folder": folder}]}  # fmt: skip
    assert SECRET_ROOT not in json.dumps(doc)  # the last component only, never the path
    home.projects.write_text("{", encoding="utf-8")
    assert _json(capsys, ["project", "ls", "--json"], 1) == {"ok": False, "v": 1, "error": "E_UNAVAILABLE",
                                                             "field": "projects"}  # fmt: skip
