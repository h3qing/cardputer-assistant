"""cpa dest and cpa audit (docs/14 §8.2, §9), run through main() against a temporary $CPA_HOME and $HOME."""

from __future__ import annotations

import io
import json
import os
from pathlib import Path

import pytest

from cpad.cli.main import main as cpa
from cpad.dest.adapters import PBCOPY

TEXT = "周五前给 Alice 发报价"


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "cpa"
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


@pytest.fixture
def user_home(tmp_path, monkeypatch) -> Path:
    path = (tmp_path / "user").resolve()
    (path / "Notes" / "sb" / "inbox").mkdir(parents=True)
    monkeypatch.setenv("HOME", str(path))
    return path


def out(capsys) -> str:
    return capsys.readouterr().out


def dests(home: Path) -> dict:
    return {d["id"]: d for d in json.loads((home / "destinations.json").read_text(encoding="utf-8"))["dests"]}


def events(home: Path) -> list[dict]:
    path = home / "events.jsonl"
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()] if path.exists() else []


def add_sb(*extra: str) -> list[str]:
    return ["dest", "add", "file.sb", "--root", "~/Notes/sb", "--path", "inbox/cardputer.md", "--key", "o", *extra]


def test_ls_shows_the_builtins_without_writing(home, capsys):
    assert cpa(["dest", "ls"]) == 0
    listing = out(capsys)
    for line in ("daily", "todo", "queue", "session", "clip", "paste", "not in this version"):
        assert line in listing
    assert not (home / "destinations.json").exists()
    assert cpa(["dest", "ls", "--json"]) == 0
    doc = json.loads(out(capsys))
    assert doc["problem"] is None and [d["state"] for d in doc["dests"]][-1] == "unavailable"


def test_a_destination_that_leaves_the_mac_needs_a_typed_y(home, user_home, capsys, monkeypatch):
    monkeypatch.setattr("sys.stdin", io.StringIO("n\n"))
    assert cpa(add_sb()) == 1
    assert "sends text off this Mac" in out(capsys)
    assert not (home / "destinations.json").exists() and events(home) == []
    monkeypatch.setattr("sys.stdin", io.StringIO(""))
    assert cpa(add_sb()) == 1
    monkeypatch.setattr("sys.stdin", io.StringIO("y\n"))
    assert cpa(add_sb()) == 0
    sb = dests(home)["file.sb"]
    assert sb["leaves_mac"] is True and sb["optin"] and sb["key"] == "o"
    assert oct((home / "destinations.json").stat().st_mode & 0o777) == "0o600"
    (changed,) = events(home)
    assert (changed["ev"], changed["op"], changed["id"], changed["by"]) == ("dest.changed", "add", "file.sb", "cli")


def test_yes_skips_the_question_and_local_needs_none(home, user_home, monkeypatch):
    monkeypatch.setattr("sys.stdin", io.StringIO(""))
    assert cpa(add_sb("--yes")) == 0
    assert dests(home)["file.sb"]["optin"]
    assert cpa(["dest", "add", "file.lan", "--root", str(user_home / "Notes" / "sb"), "--path", "lan.txt",
                "--local"]) == 0  # fmt: skip
    lan = dests(home)["file.lan"]
    assert lan["leaves_mac"] is False and lan["optin"] is None and lan["slot"] == 8


def test_changing_the_file_asks_again(home, user_home, monkeypatch):
    assert cpa(add_sb("--yes")) == 0
    first = dests(home)["file.sb"]["optin"]
    assert cpa(["dest", "add", "file.sb", "--label", "SB"]) == 0  # same file: no question
    assert dests(home)["file.sb"]["optin"] == first and dests(home)["file.sb"]["label"] == "SB"
    monkeypatch.setattr("sys.stdin", io.StringIO("no\n"))
    assert cpa(["dest", "add", "file.sb", "--path", "other.md"]) == 1
    assert dests(home)["file.sb"]["file"]["path"] == "inbox/cardputer.md"


@pytest.mark.parametrize(
    ("args", "message"),
    [(["dest", "add", "file.x"], "--root"), (["dest", "add", "plugin.todoist"], "unknown destination id"),
     (["dest", "add", "file.x", "--root", "~/Notes", "--path", "../x.md", "--yes"], "outside_root"),
     (["dest", "add", "file.x", "--root", "~/Documents", "--path", "x.md", "--yes"], "denied"),
     (["dest", "add", "file.x", "--root", "~/Notes", "--path", "x.sh", "--yes"], "suffix"),
     (["dest", "add", "mail.x"], "unknown"), (["dest", "add", "clip", "--root", "/tmp"], "only for file"),
     (["dest", "add", "clip", "--key", "a"], "key"), (["dest", "rm", "daily"], "fixed"),
     (["dest", "rm", "file.none"], "no destination"), (["dest", "bind", "daily", "x"], "fixed"),
     (["dest", "bind", "clip", "12"], "one letter")],
)  # fmt: skip
def test_refusals_write_nothing(home, user_home, capsys, args, message):
    assert cpa(args) == 2
    assert message in capsys.readouterr().err
    assert not (home / "destinations.json").exists() and events(home) == []


@pytest.mark.parametrize("where", ["Documents", "Desktop/notes", "Library/Mobile Documents/com~apple~CloudDocs", "cpa"])
def test_a_protected_folder_given_as_an_absolute_path_is_refused(home, user_home, capsys, where):
    """A real shell expands `--root ~/Documents` before cpa sees it; $CPA_HOME is refused the same way."""
    root = str(home) if where == "cpa" else str(user_home / where)
    assert cpa(["dest", "add", "file.doc", "--root", root, "--path", "inbox.md", "--key", "o", "--yes"]) == 2
    assert "denied" in capsys.readouterr().err
    assert not (home / "destinations.json").exists() and events(home) == []


def test_bind_never_steals_a_key(home, user_home, capsys):
    assert cpa(add_sb("--yes")) == 0
    before = (home / "destinations.json").read_bytes()
    assert cpa(["dest", "bind", "clip", "o"]) == 2
    assert "cpa dest bind file.sb -" in capsys.readouterr().err
    assert (home / "destinations.json").read_bytes() == before
    assert cpa(["dest", "bind", "file.sb", "-"]) == 0
    assert cpa(["dest", "bind", "clip", "O"]) == 0
    assert dests(home)["clip"]["key"] == "o" and dests(home)["file.sb"]["key"] is None
    assert [e["op"] for e in events(home)] == ["add", "bind", "bind"]


def test_rm(home, user_home):
    assert cpa(add_sb("--yes")) == 0
    assert cpa(["dest", "rm", "clip"]) == 0
    assert dests(home)["clip"]["enabled"] is False
    assert cpa(["dest", "rm", "file.sb"]) == 0
    assert "file.sb" not in dests(home)
    assert cpa(["dest", "add", "clip"]) == 0
    assert dests(home)["clip"]["enabled"] is True
    assert [(e["op"], e["id"], e["enabled"]) for e in events(home)] == [
        ("add", "file.sb", True), ("rm", "clip", False), ("rm", "file.sb", False), ("add", "clip", True),
    ]  # fmt: skip


def test_dest_test_inspects_only(home, user_home, capsys):
    assert cpa(["dest", "test", "clip"]) == (0 if os.access(PBCOPY, os.X_OK) else 1)
    assert cpa(["dest", "test", "queue"]) == 0 and "project" in out(capsys)
    assert cpa(["dest", "test", "paste"]) == 1 and "not in this version" in out(capsys)
    assert cpa(["dest", "test", "file.none"]) == 1
    assert cpa(add_sb("--yes")) == 0
    out(capsys)
    assert cpa(["dest", "test", "file.sb"]) == 0
    assert "ok" in out(capsys)
    assert not (user_home / "Notes" / "sb" / "inbox" / "cardputer.md").exists()
    assert cpa(["dest", "add", "file.gone", "--root", "~/Notes/none", "--path", "a.md", "--yes"]) == 0
    assert cpa(["dest", "test", "file.gone"]) == 1 and "needs_setup" in out(capsys)
    assert cpa(["dest", "rm", "clip"]) == 0
    assert cpa(["dest", "test", "clip"]) == 1


def test_a_broken_file_is_reported_and_never_overwritten(home, user_home, capsys):
    home.mkdir(mode=0o700)
    broken = '{"version": 1, "dests": [{"id": "clip", "token": "secret"}]}'
    (home / "destinations.json").write_text(broken, encoding="utf-8")
    assert cpa(["dest", "ls"]) == 0
    assert "unknown field 'token'" in out(capsys)
    assert cpa(["dest", "ls", "--json"]) == 0
    states = {d["id"]: (d["state"], d["key"]) for d in json.loads(out(capsys))["dests"]}
    assert states["clip"] == ("off", None) and states["daily"] == ("on", "n")
    assert cpa(["dest", "rm", "clip"]) == 2
    assert (home / "destinations.json").read_text(encoding="utf-8") == broken


def _delivery_events() -> list[dict]:
    received = {"ev": "item.received", "at": "2026-10-22T09:14:05.000+00:00", "id": "3QZK1M7-0000C-0005",
                "kind": "note", "body": {"text": TEXT, "dest": "file.sb"}}  # fmt: skip
    delivered = {"ev": "dest.delivered", "at": "2026-10-22T09:14:06.000+00:00", "item": "3QZK1M7-0000C-0005",
                 "dest": "file.sb", "kind": "file", "by": "auto", "bytes": 26, "sha8": "0a1b2c3d", "ref": "^cpa-x",
                 "leaves_mac": True, "attempt": 1}  # fmt: skip
    return [received, delivered]


def test_audit_works_without_the_daemon_and_never_prints_text(home, user_home, capsys):
    assert cpa(["audit"]) == 0
    assert "no destination events" in out(capsys)
    assert cpa(add_sb("--yes")) == 0
    with (home / "events.jsonl").open("a", encoding="utf-8") as f:
        f.writelines(json.dumps(e, ensure_ascii=False) + "\n" for e in _delivery_events())
    out(capsys)
    assert cpa(["audit", "--since", "2026-10-01T00:00:00+00:00"]) == 0
    listing = out(capsys)
    assert "dest.changed" in listing and "dest.delivered" in listing and "sha8=0a1b2c3d" in listing
    assert TEXT not in listing
    assert cpa(["audit", "--since", "2026-10-01T00:00:00+00:00", "--leaves-mac", "--json"]) == 0
    rows = json.loads(out(capsys))
    assert {r["ev"] for r in rows} == {"dest.changed", "dest.delivered"} and all(r["leaves_mac"] for r in rows)
    assert cpa(["audit", "--since", "2026-10-01T00:00:00+00:00", "--dest", "clip"]) == 0
    assert "no destination events" in out(capsys)
    assert cpa(["audit", "--since", "soon"]) == 2


def test_profiles_limit_a_destination_and_ls_shows_fl(home, user_home, capsys):
    assert cpa(add_sb("--yes", "--profile", "home", "--profile", "lab")) == 0
    assert dests(home)["file.sb"]["profiles"] == ["home", "lab"]
    out(capsys)
    assert cpa(["dest", "ls", "--json"]) == 0
    rows = {d["id"]: d for d in json.loads(out(capsys))["dests"]}
    assert (rows["file.sb"]["fl"], rows["file.sb"]["profiles"]) == ("c", ["home", "lab"])
    assert (rows["queue"]["fl"], rows["clip"]["fl"], rows["daily"]["profiles"]) == ("p", "l", [])
    assert cpa(["dest", "ls"]) == 0
    assert "home,lab" in out(capsys)
    assert cpa(["dest", "add", "file.sb", "--label", "SB"]) == 0  # profiles kept when not given
    assert dests(home)["file.sb"]["profiles"] == ["home", "lab"]


def test_a_bad_profile_is_refused(home, user_home, capsys):
    assert cpa(add_sb("--yes", "--profile", "Home")) == 2
    assert "profiles" in capsys.readouterr().err
    assert cpa(["dest", "add", "clip", "--profile", "home"]) == 2
    assert not (home / "destinations.json").exists()
