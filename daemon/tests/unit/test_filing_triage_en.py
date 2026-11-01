"""cpa filing triage in English (the default; cpad.i18n): the same runs as test_filing_triage(_send), read in English.
Every fixed word is a (zh, en) pair; the Chinese ones are pinned by those suites."""

from __future__ import annotations

import pytest

from cpad.cli import triage, triage_send
from cpad.cli.main import main as cpa
from cpad.filing import screen
from cpad.filing.args import FilingError
from tests.support import filing_home
from tests.support import send_home as sh
from tests.support.fake_term import FakeSpawn, FakeTerm

KEYS = (
    "  1-9 file  0 unfile  x done  q to AI  s to AI (start on device)  g send to…  p play  u undo  n skip  ? help  "
    "Esc quit"
)
MEMO_HEAD = "1/3  memo  10-21 15:05  0:24"
OLD_HEAD = "2/3  meeting  10-20 10:15  0:24"
FAILED_HEAD = "3/3  meeting  10-20 12:00  0:24"
QUIET = "Quit: nothing changed this run"


def has_han(text: str) -> bool:
    return any("一" <= ch <= "鿿" for ch in text)


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths


def _run(paths, keys: list, spawn: FakeSpawn | None = None) -> list[str]:
    t = FakeTerm(keys)
    assert triage.run(paths, t, spawn=spawn or FakeSpawn()) == 0
    return t.lines


def test_every_fixed_word_is_a_pair_with_an_english_side():
    tables = (screen.KIND, screen.TX, screen.BACK, screen.REFUSED, triage_send.WHY, triage_send.REFUSED)
    pairs = [*(p for t in tables for p in t.values()), *screen.HELP, *triage_send.HELP, screen.KEYS, triage_send.KEYS,
             screen.UNDO_STARTED, screen.IME, screen.NOT_MOVED, screen.UNDO_TAIL, screen.END, triage_send.MEETING,
             triage_send.UNKNOWN, triage_send.SKIPPED, triage.QUIET]  # fmt: skip
    for zh, en in pairs:
        assert zh and en and not has_han(en), (zh, en)


def test_the_first_screen_in_english(home):
    lines = _run(home, ["esc"])
    assert lines == ["2 unfiled, 1 for another computer's projects (last)", "", MEMO_HEAD, "  报 [31m价",
                     "  周五前给 Alice 发报价单。 第二行", KEYS, "  1 cpa  2 blog", QUIET]  # fmt: skip


def test_meetings_and_the_other_mac_group_in_english(home):
    lines = _run(home, ["n", "n", "esc"])
    assert lines[lines.index(OLD_HEAD) :][:3] == [OLD_HEAD, "  旧会议只有转写。", "  (meeting: no q or s)"]
    assert lines[lines.index(FAILED_HEAD) :][:3] == [FAILED_HEAD, "  recorded under acme (not a project of this Mac)",
                                                     "  (meeting, failed: no q or s)"]  # fmt: skip
    assert "→ skipped" in lines


def test_help_unknown_keys_and_an_input_method_in_english(home):
    lines = _run(home, ["?", "z", "下", "esc"])
    assert "  1-9  file under this Mac's project N (the 1-9 the device shows), then the next one" in lines
    assert "  To hide one instead: cpa filing hide ID" in lines
    assert "✗ unknown key (? help)" in lines
    assert "✗ unknown key: an input method seems to be on; switch it to English and press again (? help)" in lines


def test_file_done_undo_and_the_last_line_in_english(home):
    lines = _run(home, ["2", "x", "7", "u", "u", "u", "esc"])
    assert "→ 2 blog (u undo)" in lines and "→ done (u undo)" in lines
    assert "✗ no project in slot 7 (cpa project ls)" in lines
    assert "↩ undone: back to unfiled" in lines and "✗ nothing to undo in this run yet" in lines
    assert lines[-1] == QUIET


def test_the_other_mac_group_asks_in_english(home):
    lines = _run(home, ["n", "n", "1", "n", "0", "y", "esc"])
    assert "  recorded under acme (not a project of this Mac). file under 1 cpa? y/n" in lines
    assert "→ unchanged" in lines and "→ no longer under acme (u undo)" in lines
    assert lines[-1] == "Quit: 1 done this run"


def test_handing_to_ai_in_english(home):
    lines = _run(home, ["s", "2", "esc"])
    assert "To AI (start on the device): 1-9 for the project (other keys cancel)" in lines
    assert "→ queued q1 · 2 blog · select it on the device board, Enter starts it (plan) (u undo)" in lines
    lines = _run(home, ["n", "q", "esc"])
    assert "✗ a meeting cannot go to AI (it holds other people's words)" in lines


def test_playing_in_english(home):
    lines = _run(home, ["p", "p", "esc"])
    assert "▶ playing seg-000.wav (p stops)" in lines and "■ stopped" in lines


def test_nothing_left_in_english(home):
    end = "That was the last one: u undoes the last step, any other key quits"
    assert _run(home, ["2", "2", "2", "y", "z"])[-2:] == [end, "Quit: 3 done this run"]


# --- what waits to be sent ---------------------------------------------------------------------------


@pytest.fixture
def send_home(short_tmp, tmp_path, monkeypatch):
    paths = sh.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths


def test_the_waiting_rows_in_english(send_home, monkeypatch):
    sh.install(monkeypatch)
    sh.grok()
    assert cpa(["dest", "usual", "blog", "url.grok"]) == 0
    sh.fresh_memo(send_home)
    lines = _run(send_home, ["?", "g", "z", "g", "c", "esc"])
    assert lines[:5] == [
        "1 waiting to be sent: ↩ sends to the one picked, g then a letter picks another",
        "",
        "Waiting 1/1  → Grok  #blog  suggested",
        "  给 Grok：明天把首页改了",
        triage_send.KEYS[1],
    ]
    assert "  d    don't send it: its line in today's note stays" in lines
    assert "Send where: the destination's letter (other keys cancel)" in lines
    assert "✗ no destination on Z (cpa dest ls)" in lines and "→ sent to Clipboard" in lines


def _sent(**over) -> dict:
    return {"label": "Grok", "outcome": "handed", "via": "clip", "why": None, "code": None, **over}


@pytest.mark.parametrize(
    ("doc", "line"),
    [(_sent(why="too_long"), "→ too long: the whole text is on the clipboard, ⌘V in Grok"),
     (_sent(why="draft_busy"), "→ Grok still holds the last one: this one is on the clipboard, ⌘V adds it after"),
     (_sent(), "→ on the clipboard · ⌘V in Grok"), (_sent(outcome="delivered", via=None), "→ sent to Grok"),
     (_sent(via="url"), "→ filled into Grok · press Return in Grok to send"),
     (_sent(outcome="failed", code="E_GONE"), "✗ not sent (E_GONE); it keeps waiting")],
)  # fmt: skip
def test_what_each_send_says_in_english(doc, line):
    assert triage_send.said(doc) == line


@pytest.mark.parametrize(
    ("err", "line"),
    [(FilingError("E_BAD_ARG", "dest"), "✗ this destination cannot be sent to from the Mac (cpa dest ls)"),
     (FilingError("E_BAD_ARG", "id"), "✗ a meeting goes only to today's note, todos and files on this Mac"),
     (FilingError("E_NEEDS_DEVICE", "dest"), "✗ this destination may send by itself: confirm it on the device; if it "
                                             "only fills a draft, test it once: cpa dest test url.x --send"),
     (FilingError("E_LATER", "dest"), "✗ E_LATER")],
)  # fmt: skip
def test_what_each_refusal_says_in_english(err, line):
    assert triage_send.refused(err, "url.x") == line
