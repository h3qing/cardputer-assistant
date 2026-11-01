"""The words of `cpa filing triage` (docs/23 §3.6). Pure.

lines() is one row's screen: where it stands in the list, its kind, start time in its own time zone (~ when the
device clock was unsure), duration, the Mac-side stamp when there is one; the key it asked for when it is in the
other-Mac group; its title and text; why q and s cannot be used on it (and, when its transcription failed or timed
out, to listen first); the keys; this Mac's 1-9. Every text of a row already went through display_safe
(filing.rows), and so does the asked key here. refused() says a fixed error code (filing.args) in words, never the
input; back() says what an undo returned the row to. Every word is a (zh, en) pair that cpad.i18n.T picks when it
is shown.
"""

from __future__ import annotations

from collections.abc import Mapping
from datetime import datetime, timedelta, timezone
from typing import Any

from cpad.filing.args import (
    E_ALREADY,
    E_BAD_ARG,
    E_NO_TEXT,
    E_UNAVAILABLE,
    E_UNKNOWN_ID,
    E_UNKNOWN_PROJECT,
    FilingError,
)
from cpad.filing.fold import Known
from cpad.filing.rows import NO_TEXT
from cpad.filing.text import display_safe
from cpad.i18n import T
from cpad.protocol.validate import is_int

Row = Mapping[str, Any]
Pair = tuple[str, str]  # (zh, en): cpad.i18n.T picks one when it is shown
KIND = {"memo": ("便条", "memo"), "meeting": ("会议", "meeting")}
TX = {"pulling": ("拉取中", "pulling"), "waiting": ("等转写", "waiting to transcribe"),
      "processing": ("转写中", "transcribing"), "blocked": ("转写程序跑不了", "the transcriber cannot run"),
      "transcript": ("只有转写", "transcript only"), "done": ("有纪要", "summarised"), "failed": ("转写失败", "failed"),
      "stale": ("超时没转", "timed out")}  # fmt: skip
KEYS = (
    "  1-9 归档  0 退回  x 看过了  q 交给AI  s 交给AI（到设备上开）  g 送到…  p 播放  u 撤销  n 跳过  ? 帮助  Esc 退出",
    "  1-9 file  0 unfile  x done  q to AI  s to AI (start on device)  g send to…  p play  u undo  n skip  ? help  "
    "Esc quit",
)
HELP = (
    ("  1-9  归到这台 Mac 的第 N 个项目（和设备上的 1-9 一样），跳到下一条",
     "  1-9  file under this Mac's project N (the 1-9 the device shows), then the next one"),
    ("  0    去掉项目，退回待归档", "  0    take the project away: back to unfiled"),
    ("  x    看过了，不需要项目", "  x    done: it needs no project"),
    ("  q    交给 AI 排队：再按 1-9 选项目", "  q    hand to AI as a queue item: then 1-9 for the project"),
    ("  s    交给 AI，排成要开的 session：再按 1-9；之后到设备看板上选中它按 Enter（plan）",
     "  s    hand to AI as a session to start: then 1-9; later select it on the device board and press Enter (plan)"),
    ("  g    送到一个去向：再按它的字母（cpa dest ls 看字母）；会议不行",
     "  g    send to a destination: then its letter (cpa dest ls shows the letters); not a meeting"),
    ("  p    播放 / 停（空格也行）", "  p    play / stop (space too)"),
    ("  u    撤销这次的上一步，可以连着按", "  u    undo the last step of this run; press again for the one before"),
    ("  n    跳过这条", "  n    skip this one"),
    ("  Esc  退出（Ctrl-D、Ctrl-C 也行）", "  Esc  quit (Ctrl-D, Ctrl-C too)"),
    ("  别的电脑的项目：按数字以后再按 y 确认", "  Another computer's project: after the digit, y to confirm"),
    ("  藏起来不在这里：cpa filing hide ID", "  To hide one instead: cpa filing hide ID"),
)  # fmt: skip
BACK = {"new": ("回到待归档", "back to unfiled"), "filed": ("回到已归档", "back to filed"),
        "done": ("回到看过了", "back to done"), "handed": ("回到已交给 AI", "back to handed to AI"),
        "hidden": ("回到藏起来", "back to hidden")}  # fmt: skip
UNDO_STARTED = ("排队项已经在设备上开了，撤不回", "its queue item was already started on the device")
IME = ("✗ 不认识这个键：像是开着中文输入法，切到英文再按（? 帮助）",
       "✗ unknown key: an input method seems to be on; switch it to English and press again (? help)")  # fmt: skip
REFUSED = {
    (E_UNAVAILABLE, "cpad"): ("cpad 比这个 cpa 旧，或者没回应：先重启 cpad（cpa service install），再按一次",
                              "cpad is older than this cpa or did not answer: restart it (cpa service install), "
                              "then press again"),
    (E_UNAVAILABLE, None): ("读不了 config.json 或归档", "cannot read config.json or the archive"),
    (E_UNKNOWN_ID, "id"): ("找不到这条录音", "cannot find this recording"),
    (E_UNKNOWN_PROJECT, "project"): ("不是这台 Mac 的项目（cpa project ls）",
                                     "not a project of this Mac (cpa project ls)"),
    (E_ALREADY, "queue"): ("这条的排队项已经开过了，或者还在排",
                           "its queue item was already started, or is still queued"),
    (E_ALREADY, "filing"): ("它已经不是待归档的了（cpa filing show 看看）",
                            "it is no longer unfiled (see cpa filing show)"),
    (E_NO_TEXT, "text"): ("还没有能交的字：先归档，或等转写",
                          "no text to hand over yet: file it, or wait for the transcript"),
    (E_BAD_ARG, "id"): ("会议不能交给 AI（里面有别人的话）",
                        "a meeting cannot go to AI (it holds other people's words)"),
}  # fmt: skip
NOT_MOVED = ("→ 没动", "→ unchanged")
UNDO_TAIL = ("（u 撤销）", " (u undo)")
END = ("都过完了：u 撤销上一步，别的键退出", "That was the last one: u undoes the last step, any other key quits")


def word(table: Mapping[Any, Pair], key: Any, default: Pair) -> str:
    """The entry of a table of pairs in the language of the moment."""
    return T(*table.get(key, default))


def _at(at: Mapping[str, Any]) -> str:
    wall, tz_min = at.get("wall_ms"), at.get("tz_min")
    tz = timezone(timedelta(minutes=tz_min)) if is_int(tz_min) else None  # filing.rows: within a day
    try:
        when = datetime.fromtimestamp(wall / 1000, tz) if is_int(wall) else None
    except (OverflowError, OSError, ValueError):
        when = None
    return "-" if when is None else f"{'~' if at.get('uncertain') else ''}{when:%m-%d %H:%M}"


def _dur(ms: Any) -> str | None:
    s = (ms + 500) // 1000 if is_int(ms) and ms >= 0 else None
    return None if s is None else f"{s // 60}:{s % 60:02d}"


def _env(env: Any) -> list[str]:
    if not isinstance(env, Mapping):
        return []
    here = [T("录音时：连着这台", "recorded: linked here")] if env.get("link") == "here" else []
    return here + ([T(f"取回：{env['host']}", f"pulled by {env['host']}")] if env.get("host") else [])


def kind(row: Row) -> str:
    return word(KIND, row["kind"], ("录音", "recording"))


def head(row: Row, n: int, total: int) -> str:
    words = [f"{n}/{total}", kind(row), _at(row["at"]), _dur(row["dur_ms"]), *_env(row["env"])]
    return "  ".join(w for w in words if w)


def _hint(row: Row) -> list[str]:
    tx = word(TX, row["tx"], ("没有字", "no text"))
    if row["kind"] != "memo":
        if row["text"]:
            return [T(f"  （{kind(row)}：q、s 不能用）", f"  ({kind(row)}: no q or s)")]
        return [T(f"  （{kind(row)}，{tx}：q、s 不能用）", f"  ({kind(row)}, {tx}: no q or s)")]
    if row["text"]:
        return []
    if row["tx"] in NO_TEXT:
        return [T(f"  （{tx}：q、s 不能用，p 听一遍再归）", f"  ({tx}: no q or s; p to listen before filing)")]
    return [T(f"  （{tx}：q、s 先不能用）", f"  ({tx}: no q or s yet)")]


def asked(row: Row) -> str:
    key = display_safe(row["asked"])
    return T(f"录音时按的是 {key}（不是这台 Mac 的项目）", f"recorded under {key} (not a project of this Mac)")


def slots_line(known: Known) -> str:
    found = sorted(known.slots.items())
    if not found:
        return T("  （这台 Mac 还没有项目：cpa project add）", "  (this Mac has no projects yet: cpa project add)")
    return "  " + "  ".join(f"{slot} {key}" for slot, key in found)


def lines(row: Row, n: int, total: int, known: Known) -> list[str]:
    other = [f"  {asked(row)}"] if row["asked"] else []
    title = [f"  {row['title']}"] if row["title"] and row["title"] != row["text"] else []
    text = [f"  {row['text']}"] if row["text"] else []
    return ["", head(row, n, total), *other, *title, *text, *_hint(row), T(*KEYS), slots_line(known)]


def opening(counts: Mapping[str, Any]) -> str:
    n, left = counts["other_mac"], counts["on_device"]
    other = T(f"，别的电脑的项目 {n} 条（放在最后）", f", {n} for another computer's projects (last)") if n else ""
    device = T(f" · 设备上还有 {left} 段", f" · {left} more on the device") if left else ""
    return T(f"待归档 {counts['new']} 条{other}{device}", f"{counts['new']} unfiled{other}{device}")


def refused(err: FilingError) -> str:
    return word(REFUSED, (err.code, err.field), (f"没做成（{err.code}）", f"not done ({err.code})"))


def back(filing: str) -> str:
    """What an undo returned the row to."""
    return T("↩ 撤销了：", "↩ undone: ") + word(BACK, filing, ("回到之前", "back to before"))
