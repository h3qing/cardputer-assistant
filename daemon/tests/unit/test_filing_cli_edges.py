"""cpa filing at its edges (docs/23 §3.1-§3.3): a broken character in a transcript, an argv argparse refuses, a
person's shortcuts and hints, a cpad that hangs. Over a temporary $CPA_HOME and archive (tests/support/filing_home)."""

from __future__ import annotations

import json
import socket
import time
from datetime import date

import pytest

from cpad.cli.main import main as cpa
from cpad.config.settings import Paths
from cpad.control import client
from cpad.events.log import EventLog
from cpad.filing import view
from cpad.recordings import records as r
from tests.support import archive_tree as at
from tests.support import filing_home

AT = filing_home.AT
HALF_EMOJI = json.loads('"\\ud83d"')  # a transcriber that cut an emoji in two (JS .slice), valid JSON


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    monkeypatch.setenv("TZ", "America/Los_Angeles")
    return paths


def _json(capsys, argv: list[str], code: int = 0) -> dict:
    assert cpa(argv) == code
    return json.loads(capsys.readouterr().out)


def _lines(paths: Paths) -> list[dict]:
    return [json.loads(line) for line in paths.events.read_bytes().split(b"\n") if line]


# --- a lone surrogate --------------------------------------------------------------------------------


def _memo_with_only_a_transcript(paths: Paths, tmp_path, said: str) -> None:
    session = next(e for e in _lines(paths) if e["ev"] == "rec.archived" and e["rec"] == at.MEMO)
    EventLog(paths.events).append((r.result(rec=at.MEMO, state="done", level="transcript", title=None, memo_text=None,
                                            memo_intent=None, actions_n=0, error_code=None, session=session,
                                            at=AT),))  # fmt: skip
    folder = tmp_path / "notes" / at.rec_dir("2026-10-21", "1405", at.MEMO)
    doc = {**at.with_session("transcript_meeting", at.MEMO), "sentences": [{"t_ms": 0, "text": said}]}
    (folder / "transcript.json").write_text(json.dumps(doc), encoding="utf-8")  # ASCII escapes: "\ud83d" stays


def test_half_an_emoji_in_a_transcript_breaks_no_list_show_hand_or_export(home, capsys, tmp_path):
    _memo_with_only_a_transcript(home, tmp_path, f"报价{HALF_EMOJI}单 🙂")
    row = _json(capsys, ["filing", "ls", "--json"])["rows"][0]
    assert (row["id"], row["text"]) == (at.MEMO, "报价 单 🙂"), "a real emoji stays"
    assert _json(capsys, ["filing", "show", at.MEMO, "--json"])["sentences"][0]["text"] == "报价 单 🙂"
    assert cpa(["filing", "show", at.MEMO]) == 0 and "报价 单 🙂" in capsys.readouterr().out
    assert _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"])["filing"] == "handed"
    assert _lines(home)[-1]["text"] == "报价 单 🙂"
    assert cpa(["export", str(tmp_path / "out"), "--no-audio"]) == 0
    rows = (tmp_path / "out" / "data" / "filing.jsonl").read_text(encoding="utf-8")
    assert "报价 单 🙂" in rows


# --- argv that argparse refuses ------------------------------------------------------------------------


@pytest.mark.parametrize(
    "argv",
    [
        ["filing", "ls", "--json", "--net", "x"],
        ["filing", "--json", "--prof", "work"],
        ["filing", "hand", at.MEMO, "cpa", "extra", "--json"],
        ["filing", "nope", "--json"],
        ["project", "ls", "--json", "--path"],
        ["host", "name", "--json", "家", "--clear"],
    ],
)
def test_an_argv_this_cpa_does_not_know_is_still_an_error_document_with_json(home, capsys, argv):
    """An older cpa than the reader expects (a planned flag, a new command): exit 2 and a v1 error, not nothing."""
    assert _json(capsys, argv, 2) == {"ok": False, "v": 1, "error": "E_BAD_ARG", "field": "argv"}


def test_without_json_argparse_says_it_in_its_own_words(home, capsys):
    with pytest.raises(SystemExit) as exit_:
        cpa(["filing", "ls", "--net", "x"])
    assert exit_.value.code == 2 and capsys.readouterr().out == ""
    with pytest.raises(SystemExit) as exit_:
        cpa(["rec", "ls", "--json", "--nope"])
    assert exit_.value.code == 2, "only filing, project and host answer in the filing contract"


def test_help_is_still_help(home, capsys):
    with pytest.raises(SystemExit) as exit_:
        cpa(["filing", "ls", "--json", "--help"])
    assert exit_.value.code == 0 and "--orphans" in capsys.readouterr().out


# --- what a person types -------------------------------------------------------------------------------


def test_a_project_alone_lists_every_state_of_it(home, capsys):
    """`--project KEY` would only ever match filed rows, which the default (new) leaves out."""
    assert [row["id"] for row in _json(capsys, ["filing", "ls", "--json", "--project", "cpa"])["rows"]] == [at.MEETING]
    assert _json(capsys, ["filing", "ls", "--json", "--project", "cpa", "--filing", "new"])["rows"] == []


def test_today_and_yesterday_are_dates(home, capsys, monkeypatch):
    class Day(date):
        @classmethod
        def today(cls) -> date:
            return date(2026, 10, 21)

    monkeypatch.setattr("cpad.cli.filing.date", Day)
    rows = _json(capsys, ["filing", "ls", "--json", "--filing", "all", "--from", "yesterday", "--to", "yesterday"])
    assert [row["id"] for row in rows["rows"]] == [at.OLD]
    today = _json(capsys, ["filing", "ls", "--json", "--filing", "all", "--from", "today"])["rows"]
    assert [row["id"] for row in today] == [at.MEMO, at.MEETING]


@pytest.mark.parametrize(
    ("argv", "hint"),
    [
        (["hand", at.MEMO], "it has no project of this Mac yet: cpa filing hand ID KEY"),
        (["hand", at.MEMO, "acme"], "not one of this Mac's projects"),
        (["hand", at.MEETING, "cpa"], "not a recording id, or a meeting (meetings are never handed)"),
        (["file", at.MEMO, "Cpa"], "give one of this Mac's keys, or --none"),
        (["ls", "--from", "x"], "dates are YYYY-MM-DD, today or yesterday (to not before from)"),
        (["ls", "--to", "x"], "dates are YYYY-MM-DD, today or yesterday (to not before from)"),
        (["show", "3QZK1M7-00099-0000"], "no archived recording has this id"),
        (["done", at.MEETING], "it is not new any more (cpa filing show ID)"),
    ],
)
def test_a_person_gets_a_hint_in_words(home, capsys, argv, hint):
    assert cpa(["filing", *argv]) in (1, 2)
    assert hint in capsys.readouterr().err


def test_an_open_queue_item_hint(home, capsys):
    assert cpa(["filing", "hand", at.MEMO, "cpa"]) == 0
    capsys.readouterr()
    assert cpa(["filing", "file", at.MEMO, "blog"]) == 1
    assert "its queue item is open, or already started" in capsys.readouterr().err


# --- a cpad that hangs ---------------------------------------------------------------------------------


@pytest.fixture
def hung(home):
    """A cpad.sock that accepts and never answers."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.bind(str(home.sock))
    sock.listen(4)
    yield home
    sock.close()


def test_a_hung_cpad_costs_the_list_half_a_second_and_its_count(hung, capsys):
    started = time.monotonic()
    assert view.on_device(hung) is None
    assert time.monotonic() - started < 2.0, "well inside WhisperWoof's 5 s"
    assert _json(capsys, ["filing", "ls", "--json"])["counts"]["on_device"] is None


def test_hand_gives_a_hung_cpad_two_seconds(home, capsys, monkeypatch):
    asked = []

    def request(path, payload, timeout=5.0):
        asked.append(timeout)
        raise TimeoutError

    monkeypatch.setattr(client, "request", request)
    assert _json(capsys, ["filing", "hand", at.MEMO, "cpa", "--json"], 1)["field"] == "cpad"
    assert asked == [2.0]


# --- a symlinked recording folder ----------------------------------------------------------------------

NEW_MEMO = "3QZK1M7-00045-0000"


def test_a_symlinked_day_folder_is_not_followed_for_the_transcription_state(home, capsys, tmp_path):
    """docs/15 §3.8: every read is confined; status.json through a symlink counts as no status."""
    outside = tmp_path / "elsewhere" / f"1800-{NEW_MEMO.lower()}"
    outside.mkdir(parents=True)
    (outside / "status.json").write_text(json.dumps(at.with_session("status_blocked", NEW_MEMO)), encoding="utf-8")
    (tmp_path / "notes" / "recordings" / "2026-10-22").symlink_to(outside.parent, target_is_directory=True)
    session = r.archived(dev=filing_home.DEV, rec=NEW_MEMO, dir=at.rec_dir("2026-10-22", "1800", NEW_MEMO),
                         mode="memo", project=None, segments=1, dur_ms=5000, started_ms=at.MEETING_WALL + 90_000_000,
                         uncertain=False, gaps=[], consent=None, at=AT)  # fmt: skip
    EventLog(home.events).append((session,))
    assert _json(capsys, ["filing", "ls", "--json"])["rows"][0]["tx"] == "waiting"
    assert _json(capsys, ["filing", "show", NEW_MEMO, "--json"])["row"]["tx"] == "waiting"
