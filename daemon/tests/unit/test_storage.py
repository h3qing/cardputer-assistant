"""Shell storage: events.jsonl (append + fsync, dedupe index, crash debris), daily notes, config, $CPA_HOME."""

from __future__ import annotations

import json
import os
import stat

import pytest

from cpad.config import settings
from cpad.events import records
from cpad.events.log import EventLog
from cpad.notes.writer import NotesWriter
from cpad.protocol import messages as m
from cpad.queue.fold import assign_qids, fold_queue

AT = "2026-10-19T17:00:00.000+00:00"


def _item(item_id="3QZK1M7-0000C-0003", text="idea"):
    return m.OutboxItem(item_id, "note", m.At(41000, None), m.NoteBody(text, "cpa"))


def _mode(path) -> int:
    return stat.S_IMODE(os.stat(path).st_mode)


# --- events.jsonl ---------------------------------------------------------------------------


def test_append_fsyncs_before_returning_and_creates_a_private_file(tmp_path, monkeypatch):
    calls = []
    real_fsync = os.fsync
    monkeypatch.setattr(os, "fsync", lambda fd: calls.append(fd) or real_fsync(fd))
    log = EventLog(tmp_path / "events.jsonl")
    log.load()
    _, written = log.append((records.item_received("dev", _item(), AT, False),))
    assert calls, "append must fsync"
    assert written[0]["id"] == "3QZK1M7-0000C-0003"
    assert _mode(tmp_path / "events.jsonl") == 0o600
    assert log.is_duplicate("dev", "3QZK1M7-0000C-0003") and not log.is_duplicate("dev2", "3QZK1M7-0000C-0003")


def test_dedupe_index_is_rebuilt_on_load(tmp_path):
    path = tmp_path / "events.jsonl"
    first = EventLog(path)
    first.load()
    first.append((records.item_received("dev", _item(), AT, False),))
    second = EventLog(path)
    events = second.load()
    assert len(events) == 1 and second.is_duplicate("dev", "3QZK1M7-0000C-0003") and second.items_stored == 1


def test_crash_debris_is_skipped_and_terminated(tmp_path):
    path = tmp_path / "events.jsonl"
    path.write_text('{"ev":"queue.added","qid":"q1","project":"a","text":"t","intent":"todo"}\n{"ev":"queue.ad')
    log = EventLog(path)
    assert [e["qid"] for e in log.load()] == ["q1"] and log.skipped == 1
    log.append((records.queue_done("q1", AT),))
    lines = path.read_text().splitlines()
    assert lines[1] == '{"ev":"queue.ad' and json.loads(lines[2])["ev"] == "queue.done"
    assert [e["ev"] for e in EventLog(path).load()] == ["queue.added", "queue.done"]


def test_appends_by_another_process_are_seen_and_qids_stay_unique(tmp_path):
    path = tmp_path / "events.jsonl"
    daemon, cli = EventLog(path), EventLog(path)
    daemon.load()
    cli.load()
    cli.append((records.queue_added(qid="q1", project="a", text="t", intent="todo", origin="cli", at=AT),))
    state = fold_queue(())
    others, written = daemon.append(
        (records.queue_added(qid=None, project="a", text="u", intent="todo", origin="device", at=AT),),
        prepare=lambda new: assign_qids(written_records(), fold_queue(new, state)),
    )
    assert [e["qid"] for e in others] == ["q1"] and written[0]["qid"] == "q2"
    assert daemon.poll() == ()
    cli.append((records.queue_done("q2", AT),))
    assert [e["ev"] for e in daemon.poll()] == ["queue.done"]


def written_records():
    return (records.queue_added(qid=None, project="a", text="u", intent="todo", origin="device", at=AT),)


def test_load_of_a_missing_file_is_empty(tmp_path):
    assert EventLog(tmp_path / "none.jsonl").load() == ()


# --- notes ----------------------------------------------------------------------------------


def test_note_write_is_idempotent_and_backfills(tmp_path):
    notes = NotesWriter(tmp_path / "notes")
    rec = records.item_received("dev", _item(), AT, False)
    assert notes.write(rec) is True
    assert notes.write(rec) is False
    (day,) = (tmp_path / "notes").iterdir()
    assert day.read_text().count("^cpa-3qzk1m7-0000c-0003") == 1
    other = records.item_received("dev", _item("3QZK1M7-0000C-0004", "second"), AT, False)
    queue = {**rec, "kind": "queue"}
    assert notes.backfill((rec, other, queue, {"ev": "queue.done"})) == 1
    assert day.read_text().splitlines()[1].endswith("^cpa-3qzk1m7-0000c-0004")


def test_note_write_terminates_a_partial_last_line(tmp_path):
    notes = NotesWriter(tmp_path)
    rec = records.item_received("dev", _item(), AT, False)
    name = notes.target(rec)
    name.write_text("- my own line without newline")
    notes.write(rec)
    assert name.read_text().splitlines()[1].startswith("- ")


def test_note_write_failure_is_reported_not_raised(tmp_path):
    blocker = tmp_path / "file"
    blocker.write_text("x")
    assert NotesWriter(blocker / "sub").write(records.item_received("dev", _item(), AT, False)) is False


def test_a_bad_note_record_is_skipped_not_raised(tmp_path):
    notes = NotesWriter(tmp_path / "notes")
    good = records.item_received("dev", _item(), AT, False)
    bad_time = {**records.item_received("dev", _item("3QZK1M7-0000C-0004", "x"), "not a time", False), "at_dev": {}}
    no_id = {k: v for k, v in good.items() if k != "id"}
    wrong_body = {**good, "id": "3QZK1M7-0000C-0005", "body": "not a mapping"}
    for bad in (bad_time, no_id, wrong_body):
        assert notes.write(bad) is False
    assert notes.backfill((bad_time, no_id, wrong_body, good)) == 1, "a bad record never aborts the backfill"


# --- config and $CPA_HOME -------------------------------------------------------------------


def test_config_defaults_and_round_trip(tmp_path):
    path = tmp_path / "config.json"
    cfg = settings.load_config(path)
    assert (cfg.poll_s, cfg.inbox_idle_s, cfg.experimental_bg_reply, cfg.device_serial) == (3.0, 60.0, False, None)
    cfg = settings.replace_config(cfg, device_serial="28:84", spawn_env={"PATH": "/usr/bin"}, claude_bin="/x/claude")
    settings.save_config(path, cfg)
    assert _mode(path) == 0o600
    again = settings.load_config(path)
    assert again == cfg and json.loads(path.read_text())["device"] == {"serial": "28:84"}


@pytest.mark.parametrize(
    "doc",
    [{"poll_s": "3"}, {"poll_s": 0}, {"inbox_idle_s": -1}, {"spawn_env": {"PATH": 1}}, {"experimental_bg_reply": 1},
     {"claude_bin": "relative/claude"}, {"device": {"serial": 5}}, [], {"notes_dir": 3}],
)  # fmt: skip
def test_invalid_config_is_rejected(tmp_path, doc):
    path = tmp_path / "config.json"
    path.write_text(json.dumps(doc))
    with pytest.raises(settings.ConfigError):
        settings.load_config(path)


def test_home_from_env_and_default(monkeypatch, tmp_path):
    monkeypatch.setenv("CPA_HOME", str(tmp_path / "h"))
    assert settings.cpa_home() == tmp_path / "h"
    monkeypatch.delenv("CPA_HOME")
    assert settings.cpa_home().name == "CardputerAssistant"


def test_secure_home_creates_0700_and_tightens_loose_files(tmp_path):
    paths = settings.Paths.at(tmp_path / "home")
    assert settings.secure_home(paths) == ()
    assert _mode(paths.home) == 0o700 and _mode(paths.logs) == 0o700
    paths.config.write_text("{}")
    os.chmod(paths.config, 0o644)
    os.chmod(paths.home, 0o755)
    fixed = settings.secure_home(paths)
    assert set(fixed) == {str(paths.home), str(paths.config)}
    assert _mode(paths.home) == 0o700 and _mode(paths.config) == 0o600


def test_notes_dir_expands_user(tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    assert settings.Config().notes_path() == tmp_path / "CardputerNotes"
