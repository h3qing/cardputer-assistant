"""`cpa project add|rename|move|folder|rm|ls` (docs/25 §8): projects are named groups made on the Mac, a code folder
is optional. With --json each prints the filing/v1 `projects` document after the change, or its `error`."""

from __future__ import annotations

import json

import pytest

from cpad.cli.main import main as cpa
from cpad.events.log import EventLog


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "home"
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


def _json(capsys, argv: list[str]) -> tuple[int, dict]:
    code = cpa(argv)
    return code, json.loads(capsys.readouterr().out)


def _seen(home, key: str) -> None:
    """A recording filed under `key`, as the events log keeps it after the project is gone."""
    EventLog(home / "events.jsonl").append(({"ev": "rec.edited", "at": "2026-10-26T09:00:00Z", "rec": "R1",
                                             "project": key, "by": "cli"},))  # fmt: skip


def _rows(doc: dict) -> list[tuple]:
    return [(p["slot"], p["key"], p["name"], p["linked"]) for p in doc["projects"]]


def test_add_by_name_only_then_list(home, capsys):
    assert cpa(["project", "add", "--", "博客"]) == 0
    assert "p1" in capsys.readouterr().out
    code, doc = _json(capsys, ["project", "list", "--json"])
    assert code == 0 and doc["v"] == 1
    assert doc["projects"] == [{"key": "p1", "name": "博客", "slot": 1, "ok": True, "linked": False, "folder": None}]


def test_add_with_key_slot_and_folder(home, tmp_path, capsys):
    folder = tmp_path / "my-repo"
    folder.mkdir()
    code, doc = _json(capsys, ["project", "add", "--json", "--key", "web", "--slot", "4", "--folder", str(folder),
                               "--", "Web site"])  # fmt: skip
    assert code == 0 and doc["projects"][0]["folder"] == "my-repo" and doc["projects"][0]["slot"] == 4
    assert str(folder) not in json.dumps(doc)  # the basename only, never the local path


def test_the_older_form_still_adds_a_linked_project(home, tmp_path, capsys):
    assert cpa(["project", "add", "cpa", str(tmp_path), "--slot", "2", "--name", "Cardputer 助手"]) == 0
    capsys.readouterr()
    _, doc = _json(capsys, ["project", "ls", "--json"])
    assert _rows(doc) == [(2, "cpa", "Cardputer 助手", True)]


def test_rename_move_folder_rm(home, tmp_path, capsys):
    for name in ("one", "two"):
        assert cpa(["project", "add", "--", name]) == 0
    capsys.readouterr()
    assert _rows(_json(capsys, ["project", "rename", "--json", "two", "--", "第二"])[1])[1] == (2, "two", "第二", False)
    assert _rows(_json(capsys, ["project", "move", "--json", "two", "1"])[1])[:2] == [
        (1, "two", "第二", False), (2, "one", "one", False)]  # fmt: skip
    assert _rows(_json(capsys, ["project", "folder", "--json", "one", str(tmp_path)])[1])[1][3] is True
    assert _rows(_json(capsys, ["project", "folder", "--json", "one", "--none"])[1])[1][3] is False
    assert _rows(_json(capsys, ["project", "rm", "--json", "two"])[1]) == [(2, "one", "one", False)]


@pytest.mark.parametrize(
    ("argv", "error", "field", "exit_code"),
    [
        (["project", "add", "--json", "--", "-x"], "E_BAD_ARG", "name", 2),
        (["project", "add", "--json", "--key", "first", "--", "again"], "E_ALREADY", "key", 1),
        (["project", "add", "--json", "--slot", "1", "--", "again"], "E_ALREADY", "slot", 1),
        (["project", "add", "--json", "--folder", "/nope/never", "--", "x"], "E_BAD_ARG", "folder", 2),
        (["project", "rename", "--json", "nope", "--", "x"], "E_UNKNOWN_PROJECT", "key", 2),
        (["project", "move", "--json", "first", "10"], "E_BAD_ARG", "slot", 2),
        (["project", "folder", "--json", "first"], "E_BAD_ARG", "argv", 2),
        (["project", "rm", "--json", "nope"], "E_UNKNOWN_PROJECT", "key", 2),
    ],
)
def test_refusals_write_nothing_and_say_which_argument(home, capsys, argv, error, field, exit_code):
    assert cpa(["project", "add", "--key", "first", "--", "First"]) == 0
    capsys.readouterr()
    before = (home / "projects.json").read_bytes()
    code, doc = _json(capsys, argv)
    assert (code, doc) == (exit_code, {"ok": False, "v": 1, "error": error, "field": field})
    assert (home / "projects.json").read_bytes() == before


def test_a_removed_key_is_not_handed_to_the_next_project(home, capsys):
    """Review 2026-10-26: add 家务 (p1), rm p1, add 读书 must not reuse p1 while recordings still carry it."""
    assert cpa(["project", "add", "--", "家务"]) == 0
    _seen(home, "p1")
    assert cpa(["project", "rm", "p1"]) == 0
    capsys.readouterr()
    code, doc = _json(capsys, ["project", "add", "--json", "--", "读书"])
    assert code == 0 and doc["projects"][0]["key"] != "p1"
    code, doc = _json(capsys, ["project", "add", "--json", "--key", "p1", "--", "家务"])
    assert (code, doc["error"], doc["field"]) == (1, "E_ALREADY", "key")
    code, doc = _json(capsys, ["project", "add", "--json", "--key", "p1", "--reuse", "--", "家务"])
    assert code == 0 and "p1" in [p["key"] for p in doc["projects"]]


def test_the_older_form_with_key_or_folder_is_refused(home, tmp_path, capsys):
    code, doc = _json(capsys, ["project", "add", "--json", "--key", "x", "My", "Project"])
    assert (code, doc["error"], doc["field"]) == (2, "E_BAD_ARG", "argv")
    code, doc = _json(capsys, ["project", "add", "--json", "--folder", str(tmp_path), "cpa", str(tmp_path)])
    assert (code, doc["error"], doc["field"]) == (2, "E_BAD_ARG", "argv")
    assert not (home / "projects.json").exists()


def test_an_unreadable_registry_is_unavailable(home, capsys):
    home.mkdir(parents=True)
    (home / "projects.json").write_text("{", encoding="utf-8")
    code, doc = _json(capsys, ["project", "add", "--json", "--", "x"])
    assert (code, doc["error"], doc["field"]) == (1, "E_UNAVAILABLE", "projects")
    assert (home / "projects.json").read_text() == "{"


def test_without_json_a_refusal_is_one_line_on_stderr(home, capsys):
    assert cpa(["project", "rename", "nope", "--", "x"]) == 2
    out = capsys.readouterr()
    assert out.out == "" and out.err.startswith("error: no project with key nope")
