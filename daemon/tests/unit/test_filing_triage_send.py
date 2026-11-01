"""cpa filing triage over what waits to be sent (docs/25 §3.3, §5.5, D4): the rows of cpa dest pending come first,
Enter sends to the preselected destination, g then a letter to another, d drops, n skips; on the recordings after
them g then a letter sends a memo, never a meeting. A fake terminal and the fake Mac of tests/support/send_home."""

from __future__ import annotations

import json

import pytest

from cpad.cli import triage, triage_send
from cpad.cli.main import main as cpa
from cpad.filing.args import FilingError
from tests.support import archive_tree as at
from tests.support import send_home as sh
from tests.support.fake_term import FakeSpawn, FakeTerm

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

ROW_KEYS = "  ↩ 送到选好的  g 再按字母换去向  d 不送了  n 跳过  ? 帮助  Esc 退出"
FILLED = "→ 已填进 Grok · 到 Grok 里按回车发出"


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = sh.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths


@pytest.fixture
def mac(monkeypatch):
    return sh.install(monkeypatch)


@pytest.fixture
def suggested(home, mac):
    """url.grok (tested, a draft), the usual destination of blog, and a fresh blog memo: one row waiting to be sent."""
    sh.grok()
    assert cpa(["dest", "usual", "blog", "url.grok"]) == 0
    return sh.fresh_memo(home)


def run(paths, keys: list) -> list[str]:
    t = FakeTerm(keys)
    assert triage.run(paths, t, spawn=FakeSpawn()) == 0
    return t.lines


def evs(paths, ev: str) -> list[dict]:
    found = [json.loads(line) for line in paths.events.read_text(encoding="utf-8").split("\n") if line]
    return [e for e in found if e["ev"] == ev]


def test_the_waiting_rows_come_first_and_enter_fills_the_draft(home, mac, suggested):
    fake, _ = mac
    lines = run(home, ["enter", "esc"])
    assert lines[:5] == ["等你送 1 条：↩ 送到选好的，g 再按字母换一个去向", "", "等你送 1/1  → Grok  #blog  建议",
                         "  给 Grok：明天把首页改了", ROW_KEYS]  # fmt: skip
    assert lines[5] == FILLED and lines[-1] == "退出：这次什么都没动"
    assert fake.opened[-1][1].startswith("grokbot://app/v1/prompt?text=%5BCardputer%20")
    assert evs(home, "dest.handed")[-1]["item"] == suggested
    assert "1/3  便条" in "\n".join(lines)  # then the recordings to file


def test_g_then_a_letter_sends_elsewhere_and_an_unbound_letter_says_so(home, mac, suggested):
    _, board = mac
    lines = run(home, ["g", "z", "g", "enter", "?", "!", "g", "c", "esc"])
    assert "✗ Z 没绑去向（cpa dest ls）" in lines and "→ 没送" in lines
    assert "✗ 不认识这个键（? 帮助）" in lines and any(line.startswith("  g    再按一个字母") for line in lines)
    assert "→ 在剪贴板 · 到 剪贴板 按 ⌘V" not in lines and "→ 已送到 剪贴板" in lines
    assert board.texts == ["给 Grok：明天把首页改了"]


def test_d_drops_and_n_skips_and_esc_quits_before_the_recordings(home, mac, suggested):
    lines = run(home, ["d"])
    assert "→ 不送了（今日笔记里那一行还在）" in lines
    assert evs(home, "dest.dropped")[-1]["item"] == suggested
    assert "等你送" not in "\n".join(run(home, ["esc"]))
    sh.fresh_memo(home)  # the same id again: still decided, never offered twice
    assert run(home, ["esc"])[-1] == "退出：这次什么都没动"


def test_quitting_in_the_middle_counts_what_was_sent(home, mac, suggested):
    second = sh.fresh_memo(home, rec="3QZK1M7-00045-0001")
    assert run(home, ["enter", "esc"])[-1] == "退出：这次送了 1 条"
    assert [e["item"] for e in evs(home, "dest.handed") if not e["item"].startswith("test-")] == [suggested]
    lines = run(home, ["esc"])
    assert "等你送 1 条：↩ 送到选好的，g 再按字母换一个去向" in lines  # only the second one waits now
    assert second not in {e["item"] for e in evs(home, "dest.handed")}


def test_a_refused_send_stays_on_its_row(home, mac):
    sh.grok(verified=False)
    assert cpa(["dest", "usual", "blog", "url.grok"]) == 0
    sh.fresh_memo(home)
    lines = run(home, ["enter", "n", "esc"])
    refusal = "✗ 这个去向可能会自己发出去：要在设备上确认；只填草稿的话先测一次：cpa dest test url.grok --send"
    assert lines[lines.index(refusal) + 1] == "→ 跳过"


def test_a_failed_send_moves_on_and_keeps_waiting(home, mac, suggested):
    fake, _ = mac
    fake.apps.pop(sh.GROK.bundle)
    lines = run(home, ["enter", "esc"])
    assert "✗ 没送出（E_DEST_MISSING），留在等你送里" in lines
    assert "等你送 1/1  → Grok  #blog  等你送 · 上次没送出（E_DEST_MISSING）" in run(home, ["esc"])


def test_g_on_a_memo_to_file_sends_it_and_a_meeting_is_refused(home, mac):
    sh.grok()
    lines = run(home, ["g", "x", "g", "esc"])
    assert lines[lines.index(FILLED) + 2].startswith("2/3  会议")
    assert "✗ 会议只进今日笔记、待办和本机文件" in lines
    assert evs(home, "dest.handed")[-1]["item"] == at.MEMO


def test_g_with_a_refusal_on_a_memo_stays(home, mac):
    sh.grok(verified=False)
    lines = run(home, ["g", "x", "esc"])
    assert any(line.startswith("✗ 这个去向可能会自己发出去") for line in lines)
    assert lines[-1] == "退出：这次什么都没动"


def test_a_broken_destinations_file_skips_the_list(home, mac):
    (home.home / "destinations.json").write_text("{", encoding="utf-8")
    lines = run(home, ["g", "x", "esc"])
    assert lines[0].startswith("待归档") and "✗ X 没绑去向（cpa dest ls）" in lines


# --- the words of every outcome and refusal --------------------------------------------------------


def _sent(**over) -> dict:
    return {"label": "Grok", "outcome": "handed", "via": "clip", "why": None, "code": None, **over}


@pytest.mark.parametrize(
    ("doc", "line"),
    [(_sent(why="too_long"), "→ 太长：全文在剪贴板，到 Grok 按 ⌘V"),
     (_sent(why="draft_busy"), "→ Grok 里还有上一条：这条在剪贴板，⌘V 接在后面"),
     (_sent(), "→ 在剪贴板 · 到 Grok 按 ⌘V"), (_sent(outcome="delivered", via=None), "→ 已送到 Grok")],
)  # fmt: skip
def test_what_each_send_says(doc, line):
    assert triage_send.said(doc) == line


@pytest.mark.parametrize(
    ("err", "line"),
    [(FilingError("E_BAD_ARG", "dest"), "✗ 这个去向不能从 Mac 送（cpa dest ls）"),
     (FilingError("E_LATER", "dest"), "✗ E_LATER"), (FilingError("E_BUSY", "ref"), "✗ 这条正在送")],
)  # fmt: skip
def test_what_each_refusal_says(err, line):
    assert triage_send.refused(err, None) == line


def test_an_unreadable_log_is_said_and_a_drop_refused_stays_quiet(home, mac, suggested, monkeypatch):
    t = FakeTerm([])

    def broken(*_a, **_k):
        raise OSError("disk")

    monkeypatch.setattr(triage_send.sender, "send", broken)
    assert triage_send.send(home, t, suggested, "url.grok") is None
    assert t.lines == ["✗ destinations.json 用不了（cpa dest ls 看原因）"]

    def busy(*_a, **_k):
        raise FilingError("E_BUSY", "ref")

    monkeypatch.setattr(triage_send.sender, "drop", busy)
    assert triage_send._drop(home, t, {"ref": suggested, "dest": "url.grok"}) == "next"
    assert t.lines[-1] == "✗ 这条正在送"
