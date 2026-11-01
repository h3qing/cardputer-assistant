"""cpa dest presets | add --preset/--app/--preset-file | test --send | verify (docs/25 §3.1, §5.5), through main(),
against a fake Mac and a fake pasteboard: nothing real is opened or copied (tests/conftest.py refuses it)."""

from __future__ import annotations

import io
import json
import plistlib
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from cpad.cli.main import main as cpa
from cpad.dest import cli_presets
from cpad.dest.macos import AppInfo, Signature
from cpad.dest.pasteboard import ClipWriter
from cpad.dest.presets import shipped_presets

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

TEAM = "ABCDE12345"
GROK = next(p for p in shipped_presets() if p.preset == "grok-bot")
OB = next(p for p in shipped_presets() if p.preset == "obsidian")
NOTES = "invalid.example.notes"


@dataclass
class FakeMac:
    apps: dict[str, AppInfo] = field(default_factory=dict)
    opened: list[tuple[str, object]] = field(default_factory=list)

    def app_paths(self, bundle: str) -> tuple[Path, ...]:
        return (self.apps[bundle].path,) if bundle in self.apps else ()

    def info(self, app: Path) -> AppInfo | None:
        return next((a for a in self.apps.values() if a.path == app), None)

    def signature(self, app: Path) -> Signature:
        return Signature(True, TEAM)

    def open_url(self, url: str, app: Path) -> int:
        self.opened.append(("url", url))
        return 0

    def open_app(self, app: Path) -> int:
        self.opened.append(("app", app))
        return 0


class Board:
    def __init__(self) -> None:
        self.texts: list[str] = []

    def write(self, text: str) -> int:
        self.texts.append(text)
        return len(self.texts)

    def change_count(self) -> int:
        return len(self.texts)

    def clear(self) -> None:
        pass


def info(bundle: str, name: str, schemes: frozenset[str] = frozenset()) -> AppInfo:
    return AppInfo(Path(f"/Applications/{name}.app"), bundle, name, "1.0", schemes)


@pytest.fixture
def mac(monkeypatch) -> FakeMac:
    fake = FakeMac({GROK.bundle: info(GROK.bundle, "Grok Bot", frozenset({"grokbot", "sand"})),
                    NOTES: info(NOTES, "My Notes"), OB.bundle: info(OB.bundle, "Vault")})  # fmt: skip
    monkeypatch.setattr(cli_presets, "MAC", lambda: fake)
    return fake


@pytest.fixture
def board(monkeypatch) -> Board:
    fake = Board()
    monkeypatch.setattr(cli_presets, "WRITER", lambda: ClipWriter(fake, schedule=lambda _s, _f: None))
    return fake


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "cpa"
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


@pytest.fixture
def user_home(tmp_path, monkeypatch) -> Path:
    path = (tmp_path / "user").resolve()
    (path / "Notes" / "sb").mkdir(parents=True)
    monkeypatch.setenv("HOME", str(path))
    return path


def out(capsys) -> str:
    return capsys.readouterr().out


def dests(home: Path) -> dict:
    return {d["id"]: d for d in json.loads((home / "destinations.json").read_text(encoding="utf-8"))["dests"]}


def events(home: Path, ev: str | None = None) -> list[dict]:
    path = home / "events.jsonl"
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()] if path.exists() else []
    return [e for e in rows if ev is None or e["ev"] == ev]


def ls(capsys) -> dict:
    out(capsys)
    assert cpa(["dest", "ls", "--json"]) == 0
    return {d["id"]: d for d in json.loads(out(capsys))["dests"]}


# --- presets ------------------------------------------------------------------------------------


def test_presets_lists_shipped_presets_and_whether_their_app_is_here(home, mac, capsys):
    assert cpa(["dest", "presets", "--json"]) == 0
    rows = {r["preset"]: r for r in json.loads(out(capsys))["presets"]}
    assert set(rows) >= {"grok-bot", "cursor", "claude", "codex", "obsidian"}
    assert rows["grok-bot"]["installed"] and not rows["cursor"]["installed"]
    assert (rows["grok-bot"]["id"], rows["grok-bot"]["effect"], rows["grok-bot"]["may_be_draft"]) == (
        "url.grok", "unknown", True)  # fmt: skip
    assert rows["grok-bot"]["takes_map"] and not rows["obsidian"]["takes_map"]
    assert cpa(["dest", "presets", "--installed"]) == 0
    listing = out(capsys)
    assert "grok-bot" in listing and "cursor" not in listing


def test_apps_lists_every_app_for_add_app(home, tmp_path, monkeypatch, capsys):
    app = tmp_path / "Apps" / "Chat.app" / "Contents"
    app.mkdir(parents=True)
    (app / "Info.plist").write_bytes(plistlib.dumps({"CFBundleIdentifier": "invalid.example.chat"}))
    monkeypatch.setattr(cli_presets, "APPS_ROOTS", (tmp_path / "Apps",))
    assert cpa(["dest", "presets", "--apps", "--json"]) == 0
    assert json.loads(out(capsys)) == [{"name": "Chat", "bundle": "invalid.example.chat", "version": ""}]
    assert cpa(["dest", "presets", "--apps"]) == 0 and "invalid.example.chat" in out(capsys)


def test_presets_reports_a_broken_preset_of_yours(home, mac, capsys):
    (home / "presets").mkdir(parents=True)
    (home / "presets" / "x.json").write_text("{", encoding="utf-8")
    assert cpa(["dest", "presets"]) == 0
    assert "x.json" in out(capsys)


# --- add, test --send, verify: the Grok Bot path of docs/25 §3.1 -------------------------------------


def test_grok_bot_from_add_to_a_verified_draft(home, mac, board, capsys, monkeypatch):
    monkeypatch.setattr("sys.stdin", io.StringIO("y\n"))
    assert cpa(["dest", "add", "--preset", "grok-bot"]) == 0
    assert "Grok Bot's cloud computer" in out(capsys)
    grok = dests(home)["url.grok"]
    assert (grok["key"], grok["leaves_mac"], grok["label"]) == ("x", True, "Grok") and grok["optin"]
    url = grok["url"]
    assert (url["bundle"], url["team"], url["effect"], url["verified"], url["preset"]) == (
        GROK.bundle, TEAM, "draft", None, "grok-bot")  # fmt: skip
    (added,) = events(home, "dest.changed")
    assert added["handler"] == {"bundle": GROK.bundle, "team": TEAM} and added["fl"] == "lck"
    assert ls(capsys)["url.grok"]["fl"] == "lck"

    assert cpa(["dest", "verify", "url.grok", "--draft"]) == 2  # no test yet
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    ((kind, link),) = mac.opened
    assert kind == "url" and link.startswith("grokbot://app/v1/prompt?text=%5BCardputer%20") and "&id=" not in link
    (handed,) = events(home, "dest.handed")
    assert handed["item"].startswith("test-") and handed["via"] == "url" and handed["by"] == "cli"
    assert cpa(["dest", "verify", "url.grok", "--draft"]) == 0
    rows = ls(capsys)
    grok_row = rows["url.grok"]
    assert (grok_row["fl"], grok_row["verified"], grok_row["newer_preset"]) == ("lc", "draft", False)
    assert events(home, "dest.changed")[-1]["op"] == "verify"


def test_a_new_add_needs_a_new_test(home, mac, board):
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    assert cpa(["dest", "verify", "url.grok", "--draft"]) == 2
    assert cpa(["dest", "verify", "url.grok", "--act"]) == 0
    assert dests(home)["url.grok"]["url"]["verified"] == "act"


def test_a_bot_per_project_from_a_pasted_link(home, mac):
    link = GROK.focus.replace("{p}", "bot_7")
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes", "--map", f"*={link}", "--map", "cpa=b-2"]) == 0
    assert dests(home)["url.grok"]["url"]["map"] == {"*": "bot_7", "cpa": "b-2"}


@pytest.mark.parametrize(
    ("args", "message"),
    [(["--preset", "grok-bot", "--map", "cpa=xai-abcdef"], "secret"), (["--preset", "grok-bot", "--map", "x"], "--map"),
     (["--preset", "grok-bot", "--local"], "always leaves"), (["--preset", "cursor"], "no app"),
     (["--preset", "nope"], "no preset"), (["--preset", "obsidian"], "--root"),
     (["--app", NOTES, "--map", "*=x"], "only for url"), ([], "name a destination")],
)  # fmt: skip
def test_refusals_write_nothing(home, mac, user_home, capsys, args, message):
    assert cpa(["dest", "add", *args, "--yes"]) == 2
    assert message in capsys.readouterr().err
    assert not (home / "destinations.json").exists()


def test_a_scheme_the_app_does_not_register_is_refused(home, mac, capsys):
    mac.apps[GROK.bundle] = info(GROK.bundle, "Grok Bot")
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 2
    assert "does not handle grokbot://" in capsys.readouterr().err


def test_saying_no_changes_nothing(home, mac, monkeypatch, capsys):
    monkeypatch.setattr("sys.stdin", io.StringIO("n\n"))
    assert cpa(["dest", "add", "--preset", "grok-bot"]) == 1
    assert not (home / "destinations.json").exists()


# --- any app, your preset, a file preset ----------------------------------------------------------


def test_any_app_by_bundle_id(home, mac, board, capsys):
    assert cpa(["dest", "add", "--app", NOTES, "--local", "--key", "u"]) == 0
    notes = dests(home)["app.my-notes"]
    assert (notes["app"], notes["leaves_mac"], notes["label"], notes["key"]) == (
        {"bundle": NOTES, "team": TEAM}, False, "My Notes", "u")  # fmt: skip
    assert ls(capsys)["app.my-notes"]["fl"] == "l"
    assert cpa(["dest", "test", "app.my-notes"]) == 0 and "ok" in out(capsys)
    assert cpa(["dest", "test", "app.my-notes", "--send"]) == 0
    assert board.texts[0].startswith("[Cardputer 打字] 测试 ") and mac.opened[-1][0] == "app"
    assert cpa(["dest", "verify", "app.my-notes", "--draft"]) == 2


def test_your_preset_file_always_acts(home, mac, board, tmp_path, capsys):
    doc = {"v": 1, "preset": "my-grok", "name": "mine", "label": "Mine", "kind": "url", "effect": "draft",
           "detect": {"bundle": GROK.bundle}, "template": "grokbot://app/v1/prompt?text={text}", "leaves_mac": True,
           "note": "Mine."}  # fmt: skip
    path = tmp_path / "mine.json"
    path.write_text(json.dumps(doc), encoding="utf-8")
    assert cpa(["dest", "add", "--preset-file", str(path), "--yes", "--key", "y"]) == 0
    assert dests(home)["url.mine"]["url"]["effect"] == "act"
    assert cpa(["dest", "test", "url.mine", "--send"]) == 0
    assert cpa(["dest", "verify", "url.mine", "--draft"]) == 2
    assert "never be a draft" in capsys.readouterr().err
    (tmp_path / "bad.json").write_text("{", encoding="utf-8")
    assert cpa(["dest", "add", "--preset-file", str(tmp_path / "bad.json")]) == 2


def test_a_file_preset_writes_into_the_root_you_give(home, mac, user_home):
    assert cpa(["dest", "add", "--preset", "obsidian", "--root", "~/Notes/sb", "--yes"]) == 0
    ob = dests(home)["file.ob"]
    assert ob["file"] == {"root": "~/Notes/sb", "path": "Cardputer.md"} and ob["key"] == "o"


def test_send_and_verify_refusals(home, mac, board, capsys):
    assert cpa(["dest", "test", "clip", "--send"]) == 2
    assert cpa(["dest", "verify", "clip", "--draft"]) == 2
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    mac.apps.pop(GROK.bundle)
    assert cpa(["dest", "test", "url.grok", "--send"]) == 1 and "E_DEST_MISSING" in out(capsys)
    assert cpa(["dest", "test", "url.grok"]) == 1 and "missing" in out(capsys)


def test_ls_says_what_a_url_does_now_and_whether_it_takes_a_bot(home, mac, board, capsys):
    assert cpa(["dest", "add", "--preset", "grok-bot", "--yes"]) == 0
    assert cpa(["dest", "add", "--app", NOTES, "--local", "--yes"]) == 0
    rows = ls(capsys)
    assert (rows["url.grok"]["takes_map"], rows["url.grok"]["effect"]) == (True, "draft")
    assert "takes_map" not in rows["app.my-notes"]
    assert cpa(["dest", "ls"]) == 0
    assert "untested" in next(line for line in out(capsys).splitlines() if "url.grok" in line)
    assert cpa(["dest", "verify", "url.grok", "--act"]) == 0
    assert "this Mac does not send to it" in out(capsys)
    assert cpa(["dest", "ls"]) == 0
    assert "acts" in next(line for line in out(capsys).splitlines() if "url.grok" in line)
