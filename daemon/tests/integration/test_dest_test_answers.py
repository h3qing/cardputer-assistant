"""The one-time test (docs/25 §3.1, D12): cpa dest test ID --send asks "appeared?" and "sent by itself?" on a terminal
and records the answer; a missing bot is pasted once and tested again; --json asks nothing (the Mac app asks);
cpa dest map sets a url's bot per project. Against the fake Mac of tests/support/send_home: nothing real opens."""

from __future__ import annotations

import io
import json

import pytest

from cpad.cli.main import main as cpa
from cpad.dest import cli_test
from tests.support import send_home as sh

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = sh.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths


@pytest.fixture
def mac(monkeypatch):
    return sh.install(monkeypatch)


def answering(monkeypatch, *lines: str) -> None:
    monkeypatch.setattr(cli_test, "ASK", lambda: True)
    monkeypatch.setattr("sys.stdin", io.StringIO("".join(f"{line}\n" for line in lines)))


def grok(home) -> dict:
    return {d["id"]: d for d in json.loads((home.home / "destinations.json").read_text())["dests"]}["url.grok"]


def test_appeared_and_not_sent_makes_it_a_draft(home, mac, monkeypatch, capsys):
    sh.grok(verified=False)
    answering(monkeypatch, "y", "n")
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    said = capsys.readouterr().out
    assert "fills a draft only" in said and "Grok 里出现「测试 " in said and "你没按回车，它自己发出去了吗？" in said
    assert "删掉，别按回车" in said
    assert grok(home)["url"]["verified"] == "draft"


def test_sent_by_itself_keeps_it_on_the_device(home, mac, monkeypatch, capsys):
    sh.grok(verified=False)
    answering(monkeypatch, "Y", "y")
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    assert grok(home)["url"]["verified"] == "act"


def test_not_appeared_asks_for_the_bot_keeps_it_and_tests_again(home, mac, monkeypatch, capsys):
    fake, _ = mac
    sh.grok(verified=False)
    link = sh.GROK.focus.replace("{p}", "bot_7")
    answering(monkeypatch, "n", link, "y", "n")
    before = len(fake.opened)
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    first, second = fake.opened[before:]
    assert "&id=" not in first[1] and second[1].endswith("&id=bot_7")
    assert grok(home)["url"]["map"] == {"*": "bot_7"} and grok(home)["url"]["verified"] == "draft"
    assert "存了 Bot" in capsys.readouterr().out


@pytest.mark.parametrize(
    ("lines", "said"),
    [(["n", ""], "没记下"), (["n", "xai-abcdef"], "没存"), ([], "没回答"), (["y"], "没回答"),
     (["n", sh.GROK.focus.replace("{p}", "bot_7"), "n"], "没记下：看看那个 app")],
)  # fmt: skip
def test_no_usable_answer_records_nothing(home, mac, monkeypatch, capsys, lines, said):
    sh.grok(verified=False)
    answering(monkeypatch, *lines)
    assert cpa(["dest", "test", "url.grok", "--send"]) == 1
    assert said in capsys.readouterr().out
    assert grok(home)["url"]["verified"] is None


def test_an_app_only_fills_the_clipboard_and_your_preset_always_acts(home, mac, monkeypatch, capsys, tmp_path):
    assert cpa(["dest", "add", "--app", sh.NOTES, "--local"]) == 0
    answering(monkeypatch, "y")
    assert cpa(["dest", "test", "app.my-notes", "--send"]) == 0
    assert "只放进剪贴板" in capsys.readouterr().out
    doc = {"v": 1, "preset": "my-grok", "name": "mine", "label": "Mine", "kind": "url", "effect": "draft",
           "detect": {"bundle": sh.GROK.bundle}, "template": "grokbot://app/v1/prompt?text={text}",
           "leaves_mac": True, "note": "Mine."}  # fmt: skip
    (tmp_path / "mine.json").write_text(json.dumps(doc), encoding="utf-8")
    assert cpa(["dest", "add", "--preset-file", str(tmp_path / "mine.json"), "--yes", "--key", "y"]) == 0
    answering(monkeypatch, "n")
    assert cpa(["dest", "test", "url.mine", "--send"]) == 1  # no bot to ask for
    answering(monkeypatch, "y", "n")
    assert cpa(["dest", "test", "url.mine", "--send"]) == 0
    assert "这台 Mac 不送它" in capsys.readouterr().out


def test_json_asks_nothing_and_says_what_was_handed(home, mac, monkeypatch, capsys):
    fake, _ = mac
    sh.grok(verified=False)
    answering(monkeypatch)
    capsys.readouterr()
    assert cpa(["dest", "test", "url.grok", "--send", "--json"]) == 0
    doc = json.loads(capsys.readouterr().out)
    assert {k: doc[k] for k in ("ok", "v", "dest", "handed", "via", "code")} == {
        "ok": True, "v": 1, "dest": "url.grok", "handed": True, "via": "url", "code": None}  # fmt: skip
    assert doc["marker"].isdigit() and len(doc["marker"]) == 4
    fake.apps.pop(sh.GROK.bundle)
    assert cpa(["dest", "test", "url.grok", "--send", "--json"]) == 1
    assert json.loads(capsys.readouterr().out)["code"] == "E_DEST_MISSING"


def test_a_second_try_whose_test_is_not_handed(home, mac, monkeypatch, capsys):
    fake, _ = mac
    sh.grok(verified=False)
    monkeypatch.setattr(cli_test, "ASK", lambda: True)

    class Gone(io.StringIO):
        def readline(self, *a):
            line = super().readline(*a)
            if line.startswith("grokbot"):
                fake.apps.pop(sh.GROK.bundle)
            return line

    monkeypatch.setattr("sys.stdin", Gone(f"n\n{sh.GROK.focus.replace('{p}', 'bot_7')}\n"))
    assert cpa(["dest", "test", "url.grok", "--send"]) == 1
    assert "存了 Bot，但测试没交出去（E_DEST_MISSING）" in capsys.readouterr().out


def test_map_sets_and_forgets_a_bot_per_project(home, mac, capsys):
    sh.grok(verified=False)
    assert cpa(["dest", "map", "url.grok", "cpa=b-2", f"*={sh.GROK.focus.replace('{p}', 'bot_7')}"]) == 0
    assert grok(home)["url"]["map"] == {"*": "bot_7", "cpa": "b-2"}
    assert cpa(["dest", "map", "url.grok", "cpa=-"]) == 0 and "*=bot_7" in capsys.readouterr().out
    assert grok(home)["url"]["map"] == {"*": "bot_7"}
    assert cpa(["dest", "map", "url.grok", "cpa=xai-abcdef"]) == 2
    assert cpa(["dest", "map", "clip", "*=x"]) == 2
    assert cpa(["dest", "map", "url.grok", "*=-"]) == 0 and "(none)" in capsys.readouterr().out


# --- English (the default) -----------------------------------------------------------------------------


def test_the_questions_and_the_marked_text_in_english(home, mac, monkeypatch, capsys, en):
    _, board = mac
    sh.grok(verified=False)
    answering(monkeypatch, "y", "n")
    assert cpa(["dest", "test", "url.grok", "--send"]) == 0
    said = capsys.readouterr().out
    marker = said.split("“test ", 1)[1][:4]
    assert f"url.grok: handed over the test text “test {marker}” (a link)." in said
    assert f"Then delete “test {marker}” from the Grok input box; do not press Return." in said
    assert f"Did “test {marker}” appear in Grok? [y/n] " in said
    assert "You did not press Return: did it send itself? [y/n] " in said
    assert grok(home)["url"]["verified"] == "draft"


@pytest.mark.parametrize(
    ("lines", "said"),
    [(["n", ""], "Nothing recorded"), (["n", "xai-abcdef"], "Not saved: "), ([], "No answer: nothing recorded"),
     (["n", sh.GROK.focus.replace("{p}", "bot_7"), "n"], "Nothing recorded: check the app")],
)  # fmt: skip
def test_no_usable_answer_in_english(home, mac, monkeypatch, capsys, lines, said, en):
    sh.grok(verified=False)
    answering(monkeypatch, *lines)
    assert cpa(["dest", "test", "url.grok", "--send"]) == 1
    out = capsys.readouterr().out
    assert said in out and not any("一" <= ch <= "鿿" for ch in out)
