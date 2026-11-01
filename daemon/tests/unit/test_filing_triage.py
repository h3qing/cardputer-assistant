"""cpa filing triage (docs/23 §3.6, §7.2): the filing list one recording at a time, one key each, driven here by a
fake terminal over a temporary $CPA_HOME and archive (tests/support/filing_home: MEMO new with text, OLD a new
meeting with a transcript, FAILED a meeting that asked for another Mac's project, last). Every key's writes are
the records of `cpa filing file|done|hand|reopen`; refusals are words and stay on the row; afplay gets an
argument array and a confined descriptor."""

from __future__ import annotations

import json
import os
import subprocess
from contextlib import contextmanager
from pathlib import Path

import pytest

from cpad.archive.fs import Root
from cpad.cli import term, triage
from cpad.cli.main import main as cpa
from cpad.config.settings import Paths
from cpad.control import client
from cpad.events import records as queue_records
from cpad.events.log import EventLog
from cpad.filing import view
from cpad.filing.args import FilingError
from cpad.recordings import records as r
from tests.support import archive_tree as at
from tests.support import filing_home
from tests.support.fake_term import FakeSpawn, FakeTerm

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

AT = filing_home.AT
KEYS = (
    "  1-9 归档  0 退回  x 看过了  q 交给AI  s 交给AI（到设备上开）  g 送到…  p 播放  u 撤销  n 跳过  ? 帮助  Esc 退出"
)

SLOTS = "  1 cpa  2 blog"
MEMO_HEAD = "1/3  便条  10-21 15:05  0:24"
OLD_HEAD = "2/3  会议  10-20 10:15  0:24"
FAILED_HEAD = "3/3  会议  10-20 12:00  0:24"
QUIET = "退出：这次什么都没动"
NEW_MEMO = "3QZK1M7-00045-0000"


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths


def _events(paths: Paths) -> list[dict]:
    return [json.loads(line) for line in paths.events.read_bytes().split(b"\n") if line]


def _run(paths: Paths, keys: list, spawn: FakeSpawn | None = None) -> FakeTerm:
    t = FakeTerm(keys)
    assert triage.run(paths, t, spawn=spawn or FakeSpawn()) == 0
    return t


def _written(paths: Paths, keys: list, spawn: FakeSpawn | None = None) -> tuple[FakeTerm, list[dict]]:
    before = len(_events(paths))
    t = _run(paths, keys, spawn)
    return t, _events(paths)[before:]


def _edits(found: list[dict]) -> list[tuple]:
    return [(e["ev"], e.get("rec"), e.get("project", "-"), e.get("done", "-"), e.get("by")) for e in found]


def _after(lines: list[str], line: str) -> str:
    """The line printed right after `line` (blank lines skipped)."""
    rest = [x for x in lines[lines.index(line) + 1 :] if x]
    return rest[0]


def _new_memo(paths: Paths, project: str | None, memo_text: str | None, started_ms: int = at.MEETING_WALL + 9_000_000,
              **over) -> None:  # fmt: skip
    """NEW_MEMO, newer than every other recording: archived (with `over`), and its result when it has text."""
    session = {**r.archived(dev=filing_home.DEV, rec=NEW_MEMO, dir=at.rec_dir("2026-10-21", "1800", NEW_MEMO),
                            mode="memo", project=project, segments=1, dur_ms=5000, started_ms=started_ms,
                            uncertain=False, gaps=[], consent=None, at=AT), **over}  # fmt: skip
    found = (session,) if memo_text is None else (session, r.result(
        rec=NEW_MEMO, state="done", level="summary", title=None, memo_text=memo_text, memo_intent=None, actions_n=0,
        error_code=None, session=session, at=AT))  # fmt: skip
    EventLog(paths.events).append(found)


# --- the screen ----------------------------------------------------------------------------------


def test_the_first_screen_says_what_is_waiting_and_shows_the_newest(home):
    t = _run(home, ["esc"])
    assert t.lines == ["待归档 2 条，别的电脑的项目 1 条（放在最后）", "", MEMO_HEAD, "  报 [31m价",
                       "  周五前给 Alice 发报价单。 第二行", KEYS, SLOTS, QUIET]  # fmt: skip


def test_no_escape_or_line_separator_from_a_title_or_text_reaches_the_terminal(home):
    t = _run(home, ["n", "n", "esc"])
    shown = "\n".join(t.lines)
    assert "\x1b" not in shown and " " not in shown and "‮" not in shown


def test_meetings_and_the_other_mac_group_say_why_q_and_s_do_not_apply(home):
    t = _run(home, ["n", "n", "esc"])
    assert t.lines[t.lines.index(OLD_HEAD) :][:3] == [OLD_HEAD, "  旧会议只有转写。", "  （会议：q、s 不能用）"]
    assert t.lines[t.lines.index(FAILED_HEAD) :][:3] == [FAILED_HEAD, "  录音时按的是 acme（不是这台 Mac 的项目）",
                                                         "  （会议，转写失败：q、s 不能用）"]  # fmt: skip


def test_nothing_waiting_ends_at_once_without_reading_a_key(home):
    for rec, key in ((at.MEMO, "cpa"), (at.OLD, "cpa"), (at.FAILED, "cpa")):
        EventLog(home.events).append((r.edited_project(rec=rec, project=key, at=AT, by="cli"),))
    t = _run(home, [lambda: pytest.fail("read a key")])
    assert t.lines == ["都归好了：没有待归档的录音"]


def test_rows_beyond_one_page_are_all_there(home, monkeypatch):
    monkeypatch.setattr(triage, "PAGE", 1)
    t = _run(home, ["n", "n", "esc"])
    assert FAILED_HEAD in t.lines


def test_a_running_cpad_says_how_many_segments_wait_on_the_device(home, monkeypatch):
    monkeypatch.setattr(
        client, "request", lambda path, payload, **kw: {"ok": True, "api": 2, "recordings": {"on_device": 3}}
    )
    assert _run(home, ["esc"]).lines[0] == "待归档 2 条，别的电脑的项目 1 条（放在最后） · 设备上还有 3 段"


def test_the_header_has_the_mac_side_stamp_and_an_impossible_start_is_a_dash(home):
    _new_memo(home, None, "想法", started_ms=10**18, env={"host": "公司", "link": "here"})
    assert _run(home, ["esc"]).lines[2] == "1/4  便条  -  0:05  录音时：连着这台  取回：公司"


def test_a_mac_without_projects_says_how_to_add_one(home):
    home.projects.write_text(json.dumps({"version": 1, "projects": []}))
    t, found = _written(home, ["1", "esc"])
    assert "  （这台 Mac 还没有项目：cpa project add）" in t.lines and "✗ 1 号没有项目（cpa project ls）" in t.lines
    assert found == []


def test_the_audio_of_an_unknown_recording_is_not_found(home):
    with pytest.raises(FilingError) as err:
        view.segment(home, Root.at(_wav(home, "1405", at.MEMO).parents[3]), "3QZK1M7-0FFFF-0000")
    assert (err.value.code, err.value.field) == ("E_UNKNOWN_ID", "id")


def test_help_and_an_unknown_key_say_something_and_stay(home):
    t, found = _written(home, ["?", "z", "esc"])
    assert found == [] and t.lines.count(MEMO_HEAD) == 2, "the row comes back under the help"
    assert "  1-9  归到这台 Mac 的第 N 个项目（和设备上的 1-9 一样），跳到下一条" in t.lines
    assert "  藏起来不在这里：cpa filing hide ID" in t.lines
    assert _after(t.lines, "  藏起来不在这里：cpa filing hide ID") == MEMO_HEAD
    assert "✗ 不认识这个键（? 帮助）" in t.lines


def test_a_key_from_a_chinese_input_method_says_to_switch_it_off(home):
    t, found = _written(home, ["下", "esc"])
    assert found == [] and "✗ 不认识这个键：像是开着中文输入法，切到英文再按（? 帮助）" in t.lines


# --- file, done, skip ----------------------------------------------------------------------------


def test_a_digit_files_it_under_that_slot_and_moves_on(home):
    t, found = _written(home, ["2", "esc"])
    assert _edits(found) == [("rec.edited", at.MEMO, "blog", "-", "cli")]
    assert _after(t.lines, "→ 2 blog（u 撤销）") == OLD_HEAD
    assert t.lines[-1] == "退出：这次做了 1 件"


def test_x_is_done_and_n_skips(home):
    t, found = _written(home, ["n", "x", "esc"])
    assert _edits(found) == [("rec.edited", at.OLD, "-", True, "cli")]
    assert _after(t.lines, "→ 跳过") == OLD_HEAD and _after(t.lines, "→ 看过了（u 撤销）") == FAILED_HEAD


def test_a_slot_without_a_project_writes_nothing_and_stays(home):
    t, found = _written(home, ["7", "esc"])
    assert found == [] and "✗ 7 号没有项目（cpa project ls）" in t.lines and OLD_HEAD not in t.lines


def test_zero_on_a_new_row_changes_nothing_and_moves_on(home):
    t, found = _written(home, ["0", "esc"])
    assert found == [] and _after(t.lines, "→ 本来就没有项目") == OLD_HEAD


def test_the_other_mac_group_asks_before_a_digit_and_n_keeps_it(home):
    question = "  录音时按的是 acme（不是这台 Mac 的项目）。归到 1 cpa？y/n"
    t, found = _written(home, ["n", "n", "1", "n", "1", "y", "esc"])
    assert _edits(found) == [("rec.edited", at.FAILED, "cpa", "-", "cli")]
    assert t.lines.count(question) == 2 and "→ 没动" in t.lines and "→ 1 cpa（u 撤销）" in t.lines


def test_zero_on_the_other_mac_group_asks_and_takes_the_asked_key_away(home):
    t, found = _written(home, ["n", "n", "0", "enter", "esc"])
    assert "  录音时按的是 acme（不是这台 Mac 的项目）。不要 acme，当成没项目？y/n" in t.lines
    assert _edits(found) == [("rec.edited", at.FAILED, None, "-", "cli")] and "→ 不再算 acme（u 撤销）" in t.lines


# --- undo ----------------------------------------------------------------------------------------


def test_u_takes_this_run_back_newest_first_and_returns_to_that_row(home):
    t, found = _written(home, ["2", "x", "u", "u", "u", "esc"])
    assert _edits(found) == [
        ("rec.edited", at.MEMO, "blog", "-", "cli"),
        ("rec.edited", at.OLD, "-", True, "cli"),
        ("rec.edited", at.OLD, "-", False, "cli"),
        ("rec.edited", at.MEMO, None, "-", "cli"),
    ]
    backs = [(x, _after(t.lines[i:], x)) for i, x in enumerate(t.lines) if x.startswith("↩ 撤销了")]
    assert backs == [("↩ 撤销了：回到待归档", OLD_HEAD), ("↩ 撤销了：回到待归档", MEMO_HEAD)]
    assert t.lines[-2:] == ["✗ 这次还没有可以撤销的", QUIET]


def test_undo_of_the_other_mac_group_gives_the_asked_key_back(home):
    _, found = _written(home, ["n", "n", "1", "y", "u", "esc"])
    assert [e.get("project") for e in found] == ["cpa", "acme"]


def test_at_the_end_u_goes_back_and_any_other_key_quits(home):
    t, found = _written(home, ["2", "n", "n", "u", "n", "n", "n", "k", lambda: pytest.fail("read past the end")])
    assert [e.get("project") for e in found] == ["blog", None]
    assert t.lines.count("都过完了：u 撤销上一步，别的键退出") == 2 and t.lines[-1] == QUIET


def test_an_undo_that_no_longer_applies_is_said_and_dropped(home):
    def started() -> None:
        EventLog(home.events).append((queue_records.queue_spawned("q1", AT),))

    t, found = _written(home, ["q", "1", started, "u", "u", "esc"])
    assert [e["ev"] for e in found] == ["queue.added", "queue.spawned"]
    assert "✗ 撤不了：排队项已经在设备上开了，撤不回" in t.lines and "✗ 这次还没有可以撤销的" in t.lines


def test_an_undo_that_cannot_be_written_now_is_kept(home, monkeypatch):
    real = triage.cli_filing.write
    t, found = _written(home, ["2", lambda: monkeypatch.setattr(triage.cli_filing, "write", _broken), "u",
                               lambda: monkeypatch.setattr(triage.cli_filing, "write", real), "u", "esc"])  # fmt: skip
    assert [e.get("project") for e in found] == ["blog", None]
    assert "✗ 撤不了：读不了 config.json 或归档" in t.lines and t.lines.count("↩ 撤销了：回到待归档") == 1


def _broken(*args, **kw):
    raise OSError("disk")


# --- hand ----------------------------------------------------------------------------------------


def test_q_then_a_digit_hands_its_own_text_and_u_withdraws_it(home):
    t, found = _written(home, ["q", "1", "u", "esc"])
    added, done = found
    assert (added["ev"], added["origin"], added["from_item"], added["project"], added["intent"], added["text"]) == (
        "queue.added", "inbox", at.MEMO, "cpa", "todo", "周五前给 Alice 发报价单。 第二行")  # fmt: skip
    assert done == {"ev": "queue.done", "at": done["at"], "qid": "q1", "by": "undo"}
    assert "交给 AI：按 1-9 选项目（别的键取消）" in t.lines and "→ 已排队 q1 · 1 cpa（u 撤销）" in t.lines


def test_s_hands_it_to_be_started_on_the_device_board(home):
    t, found = _written(home, ["s", "2", "esc"])
    assert [(e["ev"], e["project"], e["intent"]) for e in found] == [("queue.added", "blog", "spawn")]
    assert "交给 AI（到设备上开）：按 1-9 选项目（别的键取消）" in t.lines
    assert "→ 已排队 q1 · 2 blog · 到设备看板上选中它按 Enter 开（plan）（u 撤销）" in t.lines


def test_q_is_cancelled_by_any_other_key_or_an_empty_slot(home):
    t, found = _written(home, ["q", "x", "s", "esc", "q", "8", "esc"])
    assert found == [] and t.lines.count("→ 没动") == 3 and OLD_HEAD not in t.lines


def test_q_and_s_refuse_a_meeting_and_a_memo_without_text_before_asking_anything(home):
    _new_memo(home, None, None)
    t, found = _written(home, ["q", "s", "n", "n", "q", "esc"])
    assert found == [] and "交给 AI：按 1-9 选项目（别的键取消）" not in t.lines
    assert t.lines.count("✗ 还没有能交的字：先归档，或等转写") == 2 and "✗ 会议不能交给 AI（里面有别人的话）" in t.lines
    assert "  （等转写：q、s 先不能用）" in t.lines


def test_the_other_mac_group_asks_before_handing(home):
    _new_memo(home, "acme", "家里的想法")
    question = "  录音时按的是 acme（不是这台 Mac 的项目）。交给 AI，排到 1 cpa？y/n"
    t, found = _written(home, ["n", "n", "q", "1", "n", "q", "1", "y", "esc"])
    assert t.lines.count(question) == 2 and "→ 没动" in t.lines
    assert [(e["ev"], e["from_item"], e["project"], e["text"]) for e in found] == [
        ("queue.added", NEW_MEMO, "cpa", "家里的想法")
    ]


def test_a_refused_hand_is_said_in_words_and_stays(home, monkeypatch):
    monkeypatch.setattr(client, "request", lambda path, payload, **kw: {"ok": True, "api": 1})
    t, found = _written(home, ["q", "1", "esc"])
    assert found == [] and OLD_HEAD not in t.lines
    assert "✗ cpad 比这个 cpa 旧，或者没回应：先重启 cpad（cpa service install），再按一次" in t.lines


# --- play ----------------------------------------------------------------------------------------


def _fds() -> int:
    return len(os.listdir("/dev/fd"))


def test_p_plays_the_first_segment_through_a_confined_descriptor_and_p_stops_it(home):
    spawn, open_before = FakeSpawn(), _fds()
    t = _run(home, ["p", "p", " ", "n", "esc"], spawn)
    (argv, kw, inode), _ = spawn.calls
    assert argv == ["/usr/bin/afplay", f"/dev/fd/{kw['pass_fds'][0]}"] and "shell" not in kw
    assert (kw["stdin"], kw["stdout"], kw["stderr"]) == (subprocess.DEVNULL,) * 3
    assert inode == _wav(home, "1405", at.MEMO).stat().st_ino
    assert [p.stopped for p in spawn.procs] == [True, True], "p stops it; moving on stops the second"
    assert t.lines.count("▶ 播放 seg-000.wav（p 停）") == 2 and "■ 停了" in t.lines
    assert _fds() == open_before


def _wav(paths: Paths, hhmm: str, rec: str) -> Path:
    notes = Path(json.loads(paths.config.read_text())["notes_dir"])
    return notes / at.rec_dir("2026-10-21", hhmm, rec) / "seg-000.wav"


def test_p_without_audio_or_with_a_swapped_file_plays_nothing(home):
    wav = _wav(home, "1405", at.MEMO)
    wav.unlink()
    wav.symlink_to(_wav(home, "1402", at.MEETING))
    spawn = FakeSpawn()
    t = _run(home, ["p", "n", "p", "esc"], spawn)
    assert spawn.calls == [] and t.lines.count("✗ 没有能播的音频") == 2


def test_a_player_that_failed_is_said_at_the_next_key(home):
    spawn = FakeSpawn(exit_code=1)
    t = _run(home, ["p", "z", "esc"], spawn)
    assert t.lines[t.lines.index("▶ 播放 seg-000.wav（p 停）") + 1] == "✗ 播放失败"


def test_a_failed_or_timed_out_memo_says_to_listen_first(home):
    _new_memo(home, None, None)
    EventLog(home.events).append((r.stale(rec=NEW_MEMO, session=_events(home)[-1], at=AT),))
    assert "  （超时没转：q、s 不能用，p 听一遍再归）" in _run(home, ["esc"]).lines


def test_no_afplay_is_said(home):
    def missing(argv, **kw):
        raise FileNotFoundError(argv[0])

    t = FakeTerm(["p", "esc"])
    assert triage.run(home, t, spawn=missing) == 0
    assert "✗ 播放不了（没有 /usr/bin/afplay）" in t.lines


def test_quitting_stops_the_audio_and_ctrl_c_quits(home):
    spawn = FakeSpawn()

    def interrupt() -> None:
        raise KeyboardInterrupt

    t = _run(home, ["p", interrupt], spawn)
    assert spawn.procs[0].stopped and t.lines[-1] == QUIET


# --- the command ---------------------------------------------------------------------------------


def test_not_a_terminal_is_refused_politely_and_nothing_is_read_or_written(home, capsys):
    before = home.events.read_bytes()
    assert cpa(["filing", "triage"]) == 1
    captured = capsys.readouterr()
    assert captured.out == "" and "needs a terminal" in captured.err and "cpa filing ls --json" in captured.err
    assert home.events.read_bytes() == before


@contextmanager
def _fake_raw(keys: list):
    yield FakeTerm(keys)


def test_in_a_terminal_it_runs_in_raw_mode(home, monkeypatch, capsys):
    monkeypatch.setattr(triage, "_is_terminal", lambda: True)
    monkeypatch.setattr(term, "raw", lambda fd, out: _fake_raw(["2", "esc"]))
    assert cpa(["filing", "triage"]) == 0
    assert _events(home)[-1]["project"] == "blog"


def test_an_unreadable_config_is_said_before_the_terminal_changes(home, monkeypatch, capsys):
    monkeypatch.setattr(triage, "_is_terminal", lambda: True)
    monkeypatch.setattr(term, "raw", lambda fd, out: pytest.fail("raw mode"))
    home.config.write_text("{not json")
    assert cpa(["filing", "triage"]) == 1
    assert capsys.readouterr().err.startswith("error: E_UNAVAILABLE")
