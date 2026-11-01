"""cpa dest test ID --send and its two questions, and cpa dest map (docs/25 §3.1, D12): the one-time test that lets a
link count as a draft on this Mac.

`test ID --send` hands one marked test text ("test NNNN", or "测试 NNNN" in Chinese) to the app through the same
adapter as a capture, and says to delete it from the input box without pressing Return. On a terminal it then asks,
in the language of the moment (cpad.i18n), as the Mac app does:
- "did it appear?" no: a link preset that takes a bot asks for the bot's link or id, keeps it as map["*"] and sends
  the test once more; otherwise nothing is recorded;
- "was it sent by itself?" no: `verify --draft` (fills a draft only, fl lc); yes: `verify --act` (this Mac does
  not send to it). An app destination only fills the clipboard: nothing to record, and nothing more is asked.
With --json (the Mac app asks its own questions) it prints the `tested` document of protocol/dest-cli/v1 and asks
nothing; the app answers with `cpa dest verify ID --draft|--act` and, for a bot, `cpa dest map ID '*=LINK'`.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Callable
from typing import Any

from cpad.config.settings import Paths
from cpad.dest import cli_presets
from cpad.dest.model import Dest
from cpad.dest.presets import PresetError, load_presets
from cpad.i18n import T

ASK: Callable[[], bool] = lambda: sys.stdin.isatty() and sys.stdout.isatty()  # noqa: E731


def _yes(question: str) -> bool | None:
    try:
        answer = input(question).strip().lower()
    except EOFError:
        return None
    return {"y": True, "n": False}.get(answer)


def _tested(dest: Dest, marker: str, ok: bool, via: str | None, code: str | None) -> dict[str, Any]:
    return {"ok": True, "v": 1, "dest": dest.id, "marker": marker, "handed": ok, "via": via, "code": code}


def _hand(paths: Paths, dest: Dest) -> dict[str, Any]:
    result, marker = cli_presets.hand_test(paths, dest)
    handed = result.ok and result.via is not None
    return _tested(dest, marker, handed, result.via if handed else None, None if handed else result.code)


NO_ANSWER = ("没回答：没记下", "No answer: nothing recorded")
NOT_RECORDED = ("没记下", "Nothing recorded")
CHECK_THE_APP = (
    "没记下：看看那个 app（登录了吗？是不是对的 Bot？），再跑 cpa dest test ID --send",
    "Nothing recorded: check the app (signed in? the right Bot?), then cpa dest test ID --send again",
)
PASTE_BOT = ("把这个 Bot 的链接或 id 粘贴进来（回车跳过）：", "Paste this Bot's link or id (Return skips): ")


def _said_handed(dest: Dest, doc: dict[str, Any]) -> None:
    text = cli_presets.marked_text(doc["marker"])
    where = T("链接", "a link") if doc["via"] == "url" else T("剪贴板", "the clipboard")
    print(
        T(
            f"{dest.id}：交出了测试文字「{text}」（{where}）。",
            f"{dest.id}: handed over the test text “{text}” ({where}).",
        )
    )
    print(T(f"测完把 {dest.label} 输入框里的「{text}」删掉，别按回车。",
            f"Then delete “{text}” from the {dest.label} input box; do not press Return."))  # fmt: skip


def send_test(paths: Paths, dest: Dest, args: argparse.Namespace) -> int:
    doc = _hand(paths, dest)
    if args.json:
        print(json.dumps(doc))
        return 0 if doc["handed"] else 1
    if not doc["handed"]:
        busy = doc["code"] == "E_DRAFT_BUSY"
        why = T(f"：{dest.label} 里还有上一条草稿，先发掉或清空再测",
                f": {dest.label} still holds the last draft; send or clear it, then test") if busy else ""  # fmt: skip
        print(T(f"{dest.id}：没交出去（{doc['code']}）{why}", f"{dest.id}: not handed over ({doc['code']}){why}"))
        return 1
    _said_handed(dest, doc)
    if not ASK():
        print(T(f"  出现了、没自动发：cpa dest verify {dest.id} --draft；自己发出去了：--act",
                f"  It appeared, not sent: cpa dest verify {dest.id} --draft; it sent itself: --act"))  # fmt: skip
        return 0
    return _answers(paths, dest, doc["marker"], again=True)


def _answers(paths: Paths, dest: Dest, marker: str, *, again: bool) -> int:
    text = cli_presets.marked_text(marker)
    seen = _yes(T(f"{dest.label} 里出现「{text}」了吗？[y/n] ", f"Did “{text}” appear in {dest.label}? [y/n] "))
    if seen is None:
        print(T(*NO_ANSWER))
        return 1
    if not seen:
        return _not_seen(paths, dest, again=again)
    if dest.url is None:
        print(T(f"{dest.id}：只放进剪贴板，不用记", f"{dest.id}: the clipboard only, nothing to record"))
        return 0
    sent = _yes(T("你没按回车，它自己发出去了吗？[y/n] ", "You did not press Return: did it send itself? [y/n] "))
    if sent is None:
        print(T(*NO_ANSWER))
        return 1
    if not sent and dest.url.effect != "draft":
        print(
            T(
                f"{dest.id}：你自己的预设总当作会自己发出去；这台 Mac 不送它",
                f"{dest.id}: your own presets always count as sending by themselves; this Mac does not send to it",
            )
        )
        return 0
    return cli_presets.verify(paths, dest.id, draft=not sent)


def _takes_a_bot(paths: Paths, dest: Dest) -> bool:
    preset = load_presets(paths.home).find(dest.url.preset or "") if dest.url else None
    return preset is not None and preset.map_pattern is not None


def _not_seen(paths: Paths, dest: Dest, *, again: bool) -> int:
    if not again or not _takes_a_bot(paths, dest):
        print(T(*CHECK_THE_APP))
        return 1
    try:
        raw = input(T(*PASTE_BOT)).strip()
    except EOFError:
        raw = ""
    if not raw:
        print(T(*NOT_RECORDED))
        return 1
    try:
        changed = cli_presets.set_map(paths, dest.id, [f"*={raw}"])
    except PresetError as exc:
        print(T(f"没存：{exc}", f"Not saved: {exc}"))
        return 1
    doc = _hand(paths, changed)
    if not doc["handed"]:
        print(T(f"{dest.id}：存了 Bot，但测试没交出去（{doc['code']}）",
                f"{dest.id}: saved the Bot, but the test was not handed over ({doc['code']})"))  # fmt: skip
        return 1
    print(T("存了 Bot；又交出了一次。", "Saved the Bot; handed it over once more."))
    _said_handed(changed, doc)
    return _answers(paths, changed, doc["marker"], again=False)


def dest_map(paths: Paths, args: argparse.Namespace) -> int:
    dest = cli_presets.set_map(paths, args.id, list(args.pairs))
    shown = ", ".join(f"{k}={v}" for k, v in dest.url.map) if dest.url else ""
    print(f"{dest.id}: map {shown or '(none)'}")
    return 0


def add_parsers(actions: Any, wrap: Callable[[Any], Any]) -> None:
    mapping = actions.add_parser("map", help="url: the bot (or other {p} value) per project; PROJECT=- forgets one")
    mapping.add_argument("id")
    mapping.add_argument("pairs", nargs="+", metavar="PROJECT=VALUE", help="* for every project; VALUE may be a link")
    mapping.set_defaults(fn=wrap(dest_map))
