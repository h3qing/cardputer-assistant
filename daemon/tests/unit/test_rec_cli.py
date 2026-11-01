"""cpa rec ls / show and the recordings block of cpa status (docs/11 §6.9): paths and states, never text."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from cpad.cli.main import main as cpa
from cpad.config import settings
from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.control import client, server
from cpad.events.log import EventLog
from cpad.recordings import records as r
from cpad.recordings import sessions
from cpad.recordings.index import RecIndex
from cpad.recordings.service import notes_warnings

SID = "K3M9Q2T-00042-0A1C"
MEMO = "K3M9Q2T-00042-0A1F"
DIR = f"recordings/2026-10-21/1402-{SID.lower()}"
MEMO_DIR = f"recordings/2026-10-21/1405-{MEMO.lower()}"
AT = "2026-10-21T06:45:10.000+00:00"
TITLE, MEMO_TEXT = "SECRET-TITLE 客户同步", "SECRET-MEMO 周五发报价"


def _archived(rec: str, folder: str, mode: str) -> dict:
    return r.archived(dev="020000000001", rec=rec, dir=folder, mode=mode, project="cpa", segments=1, dur_ms=2520000,
                      started_ms=1792562520000, uncertain=mode == "memo", gaps=[], consent=None, at=AT)  # fmt: skip


def setup_home(tmp_path, monkeypatch, short: Path) -> Paths:
    home = short / "home"  # AF_UNIX paths are short on macOS
    home.mkdir(mode=0o700)
    paths = Paths.at(home)
    notes = tmp_path / "notes"
    write_json(paths.config, {"notes_dir": str(notes)})
    meeting, memo = _archived(SID, DIR, "meeting"), _archived(MEMO, MEMO_DIR, "memo")
    done = r.result(rec=SID, state="done", level="summary", title=TITLE, memo_text=None, memo_intent=None, actions_n=2,
                    error_code=None, session=meeting, at=AT)  # fmt: skip
    EventLog(paths.events).append((meeting, memo, done))
    (notes / DIR).mkdir(parents=True)
    for name in ("seg-000.wav", "manifest.json", "result.json", "summary.md", ".result.json.tmp"):
        (notes / DIR / name).write_text("x")
    (notes / MEMO_DIR).mkdir(parents=True)
    write_json(
        notes / MEMO_DIR / "status.json",
        {
            "v": 1,
            "session": MEMO,
            "attempt": 1,
            "state": "blocked",
            "reason": "model_missing",
            "at": "2026-10-21T14:06:00+08:00",
        },
    )
    monkeypatch.setenv("CPA_HOME", str(home))
    monkeypatch.setenv("TZ", "Asia/Shanghai")
    return paths


def test_rec_ls_offline_lists_every_session_with_its_state(tmp_path, monkeypatch, capsys, short_tmp):
    setup_home(tmp_path, monkeypatch, short_tmp)
    assert cpa(["rec", "ls"]) == 0
    out = capsys.readouterr().out
    lines = [line for line in out.splitlines() if line.startswith("K3M9")]
    assert len(lines) == 2
    assert SID in lines[0] and "meeting" in lines[0] and "42m" in lines[0] and lines[0].endswith("done")
    assert MEMO in lines[1] and "~" in lines[1] and lines[1].endswith("blocked(model_missing)")
    assert TITLE not in out and MEMO_TEXT not in out


def test_rec_ls_json_has_no_text_fields(tmp_path, monkeypatch, capsys, short_tmp):
    setup_home(tmp_path, monkeypatch, short_tmp)
    assert cpa(["rec", "ls", "--json"]) == 0
    rows = json.loads(capsys.readouterr().out)
    assert [row["state"] for row in rows] == ["done", "blocked"]
    assert all("title" not in row and "memo_text" not in row for row in rows)
    assert rows[0]["result"] == {"state": "done", "level": "summary", "error_code": None}


def test_rec_show_prints_paths_and_states_only(tmp_path, monkeypatch, capsys, short_tmp):
    setup_home(tmp_path, monkeypatch, short_tmp)
    assert cpa(["rec", "show", SID.lower()]) == 0
    out = capsys.readouterr().out
    assert f"session   {SID}" in out and "state     done" in out
    assert str(tmp_path / "notes" / DIR) in out
    assert "manifest.json result.json seg-000.wav summary.md" in out and ".result.json.tmp" not in out
    assert TITLE not in out and "SECRET" not in out


def test_rec_show_unknown_session_fails(tmp_path, monkeypatch, capsys, short_tmp):
    setup_home(tmp_path, monkeypatch, short_tmp)
    assert cpa(["rec", "show", "K3M9Q2T-00042-0ZZZ"]) == 1
    assert "no recording session" in capsys.readouterr().err


def test_offline_status_has_a_recordings_block(tmp_path, monkeypatch, capsys, short_tmp):
    setup_home(tmp_path, monkeypatch, short_tmp)
    assert cpa(["status", "--json"]) == 0
    block = json.loads(capsys.readouterr().out)["recordings"]
    assert block["blocked"] == 1 and block["on_device"] is None and block["last_result_at"] == AT
    assert cpa(["status"]) == 0
    assert "recordings: on_device=- pulling=0 waiting=0 processing=0 blocked=1 failed=0" in capsys.readouterr().out


def test_control_accepts_the_rec_command():
    assert server.parse_request(b'{"cmd":"rec"}\n') == {"cmd": "rec"}
    assert isinstance(server.API, int) and server.API >= 1


def test_icloud_notes_dir_is_warned(tmp_path):
    cloud = tmp_path / "Mobile Documents"
    (cloud / "notes").mkdir(parents=True)
    assert notes_warnings(cloud / "notes", icloud=cloud)
    assert notes_warnings(tmp_path / "plain", icloud=cloud) == []


def test_summary_counts_states():
    rows = [{"state": s} for s in ("archived", "processing", "blocked", "failed", "stale", "done")]
    block = sessions.summary(rows, idx=RecIndex(), on_device=3, pulling=True)
    assert block == {"on_device": 3, "pulling": 1, "waiting": 1, "processing": 1, "blocked": 1, "failed": 2,
                     "last_result_at": None}  # fmt: skip


def test_config_has_recording_keys_with_defaults_and_validation():
    cfg = settings.parse_config({})
    assert (cfg.transcribe_wait_h, cfg.rec_pull) == (24.0, True)
    custom = settings.parse_config({"transcribe_wait_h": 2, "rec_pull": False})
    assert settings.parse_config(settings.dump_config(custom)) == custom
    for bad in ({"transcribe_wait_h": 0}, {"transcribe_wait_h": "24"}, {"rec_pull": "yes"}):
        with pytest.raises(settings.ConfigError):
            settings.parse_config(bad)


# --- v0.4.1: ★, project, hidden, time order, unhide (docs/13 §8.2, §8.4) ------------------------------

OLDER = "2B7XW4R-00001-0001"  # an older session from another MonoId epoch: sorts last by time, first by id


def _edit(paths: Paths, *recs: dict) -> None:
    EventLog(paths.events).append(recs)


def _listed(capsys) -> list[str]:
    return [line for line in capsys.readouterr().out.splitlines() if line[:4] in ("K3M9", "2B7X")]


def test_rec_ls_is_newest_first_and_shows_star_and_project(tmp_path, monkeypatch, capsys, short_tmp):
    paths = setup_home(tmp_path, monkeypatch, short_tmp)
    older = r.archived(dev="020000000001", rec=OLDER, dir="recordings/2026-10-20/0900-x", mode="meeting",
                       project=None, segments=1, dur_ms=60000, started_ms=1792462520000, uncertain=False, gaps=[],
                       consent=None, at=AT)  # fmt: skip
    _edit(paths, older, r.edited_star(rec=SID, starred=True, at=AT), r.edited_project(rec=MEMO, project="blog", at=AT))
    assert cpa(["rec", "ls"]) == 0
    out = capsys.readouterr().out
    lines = [line for line in out.splitlines() if line[:4] in ("K3M9", "2B7X")]
    assert [line.split()[0] for line in lines] == [SID, MEMO, OLDER]
    assert "★" in lines[0] and " cpa " in lines[0]
    assert "★" not in lines[1] and " blog " in lines[1] and "★" not in lines[2]
    assert "PROJECT" in out.splitlines()[0] and TITLE not in out and "SECRET" not in out


def test_hidden_sessions_are_left_out_unless_asked_for(tmp_path, monkeypatch, capsys, short_tmp):
    paths = setup_home(tmp_path, monkeypatch, short_tmp)
    _edit(paths, r.hidden(rec=SID, at=AT))
    assert cpa(["rec", "ls"]) == 0
    assert [line.split()[0] for line in _listed(capsys)] == [MEMO]
    assert cpa(["rec", "ls", "--hidden"]) == 0
    lines = _listed(capsys)
    assert [line.split()[0] for line in lines] == [SID, MEMO] and lines[0].endswith("done (hidden)")
    assert cpa(["rec", "ls", "--json"]) == 0
    assert [row["sid"] for row in json.loads(capsys.readouterr().out)] == [MEMO]
    assert cpa(["rec", "ls", "--json", "--hidden"]) == 0
    rows = json.loads(capsys.readouterr().out)
    assert [(row["sid"], row["hidden"]) for row in rows] == [(SID, True), (MEMO, False)]
    assert all("title" not in row for row in rows)


def test_rec_unhide_brings_a_session_back_once(tmp_path, monkeypatch, capsys, short_tmp):
    paths = setup_home(tmp_path, monkeypatch, short_tmp)
    _edit(paths, r.hidden(rec=SID, at=AT))
    assert cpa(["rec", "unhide", SID.lower()]) == 0
    assert capsys.readouterr().out.strip() == f"unhidden {SID}"
    assert cpa(["rec", "ls"]) == 0
    assert [line.split()[0] for line in _listed(capsys)] == [SID, MEMO]
    assert cpa(["rec", "unhide", SID]) == 0
    assert "not hidden" in capsys.readouterr().out
    unhidden = [e for e in EventLog(paths.events).load() if e["ev"] == "rec.unhidden"]
    assert [e["rec"] for e in unhidden] == [SID]


def test_rec_unhide_of_an_unknown_session_fails(tmp_path, monkeypatch, capsys, short_tmp):
    setup_home(tmp_path, monkeypatch, short_tmp)
    assert cpa(["rec", "unhide", "K3M9Q2T-00042-0ZZZ"]) == 1
    assert "no recording session" in capsys.readouterr().err


def test_rec_show_has_the_project_and_flags(tmp_path, monkeypatch, capsys, short_tmp):
    paths = setup_home(tmp_path, monkeypatch, short_tmp)
    _edit(paths, r.edited_star(rec=SID, starred=True, at=AT), r.hidden(rec=SID, at=AT))
    assert cpa(["rec", "show", SID]) == 0
    out = capsys.readouterr().out
    assert "project   cpa" in out and "flags     ★ hidden" in out
    assert cpa(["rec", "show", MEMO]) == 0
    assert "flags     -" in capsys.readouterr().out


def test_rows_from_a_running_cpad_get_the_edits_it_has_not_read_yet(tmp_path, monkeypatch, capsys, short_tmp):
    paths = setup_home(tmp_path, monkeypatch, short_tmp)
    stale = [{"sid": MEMO, "mode": "memo", "started_ms": 1792562520000, "state": "done", "project": "cpa"},
             {"sid": SID, "mode": "meeting", "started_ms": 1792562580000, "state": "done"}]  # fmt: skip
    monkeypatch.setattr(client, "request", lambda path, payload: {"ok": True, "sessions": stale})
    _edit(paths, r.hidden(rec=MEMO, at=AT), r.edited_project(rec=SID, project=None, at=AT))
    assert cpa(["rec", "ls", "--json"]) == 0
    rows = json.loads(capsys.readouterr().out)
    assert [(row["sid"], row["project"], row["hidden"]) for row in rows] == [(SID, None, False)]


@pytest.mark.parametrize(("dur_ms", "shown"), [
    (None, "-"), (0, "0s"), (8400, "8s"), (50688, "51s"), (59499, "59s"), (59500, "1m00s"),
    (61000, "1m01s"), (299000, "4m59s"), (599499, "9m59s"), (599500, "10m"), (2520000, "42m"),
    (14_400_000, "240m"),
])  # fmt: skip
def test_duration_shows_seconds_for_memos(dur_ms, shown) -> None:
    from cpad.cli.rec import duration

    assert duration({"dur_ms": dur_ms}) == shown


# --- docs/23 §1.3, §2.1: the effective project ------------------------------------------------------------------


def test_rows_from_an_older_cpad_without_project_keys_still_show_the_project(tmp_path, monkeypatch, capsys,
                                                                           short_tmp):  # fmt: skip
    setup_home(tmp_path, monkeypatch, short_tmp)
    old = [{"sid": MEMO, "mode": "memo", "started_ms": 1792562520000, "state": "archived"},
           {"sid": SID, "mode": "meeting", "started_ms": 1792562580000, "state": "done"}]  # fmt: skip
    monkeypatch.setattr(client, "request", lambda path, payload: {"ok": True, "sessions": old})
    assert cpa(["rec", "ls", "--json"]) == 0
    rows = json.loads(capsys.readouterr().out)
    assert [(row["sid"], row["project"]) for row in rows] == [(SID, "cpa"), (MEMO, "cpa")]


def test_rec_ls_shows_a_hand_and_a_meetings_last_segment(tmp_path, monkeypatch, capsys, short_tmp):
    paths = setup_home(tmp_path, monkeypatch, short_tmp)
    write_json(paths.projects, {"version": 1, "projects": [{"key": "blog", "name": "Blog", "slot": 2,
                                                            "root": str(tmp_path)}]})  # fmt: skip
    seg = {"ev": "rec.seg.stored", "at": AT, "dev": "020000000001", "id": "K3M9Q2T-00042-0A1E", "rec": SID, "seg": 1,
           "file": f"{DIR}/seg-001.wav", "mode": "meeting", "size": 44, "project": None, "slot": 2}  # fmt: skip
    hand = {"ev": "queue.added", "at": AT, "qid": "q1", "project": "blog", "text": "x", "intent": "todo", "sid": None,
            "origin": "inbox", "from_item": MEMO, "uncertain": False}  # fmt: skip
    _edit(paths, seg, hand)
    assert cpa(["rec", "ls", "--json"]) == 0
    assert [(row["sid"], row["project"]) for row in json.loads(capsys.readouterr().out)] == [(SID, "blog"),
                                                                                            (MEMO, "blog")]  # fmt: skip
