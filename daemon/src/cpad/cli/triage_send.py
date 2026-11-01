"""cpa filing triage, first part: what waits to be sent (docs/25 §3.3, §3.4, §5.5, D4), and `g` on any memo.

Before the recordings to file, the rows of `cpa dest pending` (held, then those a project's usual destination
preselects), one at a time: Enter sends it to the preselected destination, g then a letter sends it to the
destination bound to that letter, d does not send it (dest.dropped; its daily-note line stays), n skips, ? helps,
Esc / Ctrl-D quits the whole triage. The writes are cpa dest send / drop (dest.send: origin "mac", at most once, a
refusal writes nothing); a send cannot be taken back (the app has it), so u does not apply here. On the recordings
that follow, g then a letter sends that memo the same way (a meeting never: other people's words).
Every word is fixed or from dest.send's documents (display_safe labels and text); the fixed ones are (zh, en)
pairs, shown in the language of the moment (cpad.i18n).
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any, Protocol

from cpad.cli import term
from cpad.config.settings import ConfigError, Paths
from cpad.dest import send as sender
from cpad.dest.catalog import by_key
from cpad.dest.config import dests_path, load_catalog
from cpad.filing.args import FilingError
from cpad.i18n import T

Row = Mapping[str, Any]
QUIT = (term.ESC, term.EOF)
KEYS = ("  ↩ 送到选好的  g 再按字母换去向  d 不送了  n 跳过  ? 帮助  Esc 退出",
        "  ↩ send to the one picked  g then a letter: another  d don't send  n skip  ? help  Esc quit")  # fmt: skip
HELP = (
    ("  ↩    送到选好的去向（Mac 上按一下就算你确认了：只填草稿、剪贴板、本机文件）",
     "  ↩    send to the destination picked (a press on the Mac is your OK: only drafts, the clipboard, files here)"),
    ("  g    再按一个字母：送到那个字母的去向（cpa dest ls 看字母）",
     "  g    then a letter: send to that letter's destination (cpa dest ls shows the letters)"),
    ("  d    不送了：今日笔记里那一行还在", "  d    don't send it: its line in today's note stays"),
    ("  n    跳过这条", "  n    skip this one"),
    ("  Esc  退出", "  Esc  quit"),
)  # fmt: skip
WHY = {"held": ("等你送", "waiting for you"), "usual": ("建议", "suggested"), "expired": ("过期了", "expired")}
REFUSED = {
    "E_NEEDS_DEVICE": ("这个去向可能会自己发出去：要在设备上确认；只填草稿的话先测一次：cpa dest test {dest} --send",
                       "this destination may send by itself: confirm it on the device; if it only fills a draft, "
                       "test it once: cpa dest test {dest} --send"),
    "E_UNKNOWN_DEST": ("没有这个去向（cpa dest ls）", "no such destination (cpa dest ls)"),
    "E_DEST_OFF": ("这个去向关着，或者还没允许（cpa dest ls）",
                   "this destination is off or not allowed yet (cpa dest ls)"),
    "E_OTHER_PROFILE": ("这个去向是另一个档案的", "this destination belongs to another profile"),
    "E_NO_TEXT": ("还没有便条正文（转写开着吗？cpa-stt use）", "no memo text yet (is transcription on? cpa-stt use)"),
    "E_TOO_LONG": ("太长，没法在设备上确认", "too long to confirm on the device"),
    "E_BUSY": ("这条正在送", "this one is being sent"),
    "E_UNKNOWN_ID": ("找不到这条了", "cannot find it any more"),
    "E_UNAVAILABLE": ("destinations.json 用不了（cpa dest ls 看原因）",
                      "destinations.json cannot be used (cpa dest ls says why)"),
}  # fmt: skip
MEETING = ("✗ 会议只进今日笔记、待办和本机文件", "✗ a meeting goes only to today's note, todos and files on this Mac")
UNKNOWN = ("✗ 不认识这个键（? 帮助）", "✗ unknown key (? help)")
SKIPPED = ("→ 跳过", "→ skipped")


class Term(Protocol):
    def key(self) -> str: ...

    def say(self, line: str) -> None: ...

    def drop(self) -> None: ...


def said(doc: Mapping[str, Any]) -> str:
    """One line for a `sent` document."""
    label = doc["label"]
    if doc["outcome"] == "failed":
        return T(f"✗ 没送出（{doc['code']}），留在等你送里", f"✗ not sent ({doc['code']}); it keeps waiting")
    if doc["via"] == "url":
        return T(
            f"→ 已填进 {label} · 到 {label} 里按回车发出", f"→ filled into {label} · press Return in {label} to send"
        )
    if doc["via"] != "clip":
        return T(f"→ 已送到 {label}", f"→ sent to {label}")
    if doc["why"] == "too_long":
        return T(
            f"→ 太长：全文在剪贴板，到 {label} 按 ⌘V", f"→ too long: the whole text is on the clipboard, ⌘V in {label}"
        )
    if doc["why"] == "draft_busy":
        return T(f"→ {label} 里还有上一条：这条在剪贴板，⌘V 接在后面",
                 f"→ {label} still holds the last one: this one is on the clipboard, ⌘V adds it after")  # fmt: skip
    return T(f"→ 在剪贴板 · 到 {label} 按 ⌘V", f"→ on the clipboard · ⌘V in {label}")


def refused(err: FilingError, dest: str | None) -> str:
    if err.code == "E_BAD_ARG":
        if err.field == "id":
            return T(*MEETING)
        return T(
            "✗ 这个去向不能从 Mac 送（cpa dest ls）", "✗ this destination cannot be sent to from the Mac (cpa dest ls)"
        )
    pair = REFUSED.get(err.code)
    return "✗ " + (T(*pair).format(dest=dest or "ID") if pair else err.code)


def send(paths: Paths, t: Term, ref: str, dest: str | None, field: str = "ref") -> str | None:
    """dest.send of one row: "sent", "failed" (tried; it keeps waiting) or None (refused, nothing written)."""
    try:
        doc = sender.send(paths, ref, dest, field=field)
    except FilingError as err:
        t.say(refused(err, dest))
        return None
    except (ConfigError, OSError):
        t.say("✗ " + T(*REFUSED["E_UNAVAILABLE"]))
        return None
    t.say(said(doc))
    return "failed" if doc["outcome"] == "failed" else "sent"


def pick(paths: Paths, t: Term) -> str | None:
    """g, then a letter: the destination bound to it, or None (said why)."""
    t.say(T("送到哪：按去向的字母（别的键取消）", "Send where: the destination's letter (other keys cancel)"))
    key = t.key()
    letter = key if len(key) == 1 and key.isascii() and key.isalpha() else None
    found = by_key(load_catalog(dests_path(paths)), letter) if letter else None
    if found is None and letter:
        t.say(T(f"✗ {letter.upper()} 没绑去向（cpa dest ls）", f"✗ no destination on {letter.upper()} (cpa dest ls)"))
    elif found is None:
        t.say(T("→ 没送", "→ not sent"))
    return found.id if found else None


def _lines(row: Row, n: int, total: int) -> list[str]:
    where = f"#{row['project']}" if row["project"] else T("没项目", "no project")
    state = T(f" · 上次没送出（{row['code']}）", f" · last try failed ({row['code']})") if row["code"] else ""
    why = T(*WHY[row["why"]]) if row["why"] in WHY else row["why"]
    head = f"{T('等你送', 'Waiting')} {n}/{total}  → {row['label']}  {where}  {why}{state}"
    return ["", head, f"  {row['text'] or '-'}", T(*KEYS)]


def _one(paths: Paths, t: Term, row: Row) -> str:
    """Keys for one row until it is done with: "sent", "failed", "next" or "quit"."""
    while True:
        key = t.key()
        done = None
        if key in QUIT:
            return "quit"
        if key in (term.ENTER, "\r", "\n"):
            done = send(paths, t, row["ref"], row["dest"])
        elif key == "g":
            dest = pick(paths, t)
            done = send(paths, t, row["ref"], dest) if dest is not None else None
        elif key == "d":
            return _drop(paths, t, row)
        elif key == "n":
            t.say(T(*SKIPPED))
            return "next"
        elif key == "?":
            for line in HELP:
                t.say(T(*line))
        else:
            t.say(T(*UNKNOWN))
        if done is not None:
            return done


def _drop(paths: Paths, t: Term, row: Row) -> str:
    try:
        sender.drop(paths, row["ref"])
    except FilingError as err:
        t.say(refused(err, row["dest"]))
        return "next"
    t.say(T("→ 不送了（今日笔记里那一行还在）", "→ not sent (its line in today's note stays)"))
    return "next"


def go(paths: Paths, t: Term) -> tuple[bool, int]:
    """The rows waiting to be sent, one at a time: (you quit, so the recordings are not shown; how many were sent)."""
    try:
        rows = tuple(sender.pending(paths)["rows"])
    except (FilingError, ConfigError, OSError):
        return False, 0  # the list is optional here: the recordings still need filing
    if rows:
        n = len(rows)
        t.say(T(f"等你送 {n} 条：↩ 送到选好的，g 再按字母换一个去向",
                f"{n} waiting to be sent: ↩ sends to the one picked, g then a letter picks another"))  # fmt: skip
    sent = 0
    for n, row in enumerate(rows, 1):
        t.drop()
        for line in _lines(row, n, len(rows)):
            t.say(line)
        done = _one(paths, t, row)
        if done == "quit":
            return True, sent
        sent += done == "sent"
    return False, sent
