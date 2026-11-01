"""cpa subcommands that edit $CPA_HOME files: queue, device (05 §7.9; US-08 AC4, US-04 AC2; project: cli/projects);
host name (docs/23 §4.2): this Mac's name on the device and in the env stamp of new recordings; with --json the
protocol/filing/v1 host document (WhisperWoof's home / work banner reads and sets it) or its error.

queue reopen: docs/20 U12. lang: config.json's "lang" (cpad.i18n), shown with what decides it now.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

from cpad import i18n
from cpad.config.host import valid_name
from cpad.config.settings import Paths, load_config, merge_config, replace_config, save_config, secure_home
from cpad.events import records
from cpad.events.log import EventLog
from cpad.filing.args import E_BAD_ARG, VERSION, FilingError
from cpad.filing.text import excerpt
from cpad.notes.render import clean
from cpad.projects.registry import ProjectsError, find, load_registry
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import utf8_len
from cpad.queue.fold import assign_qids, closed_item, fold_queue, open_items
from cpad.queue.fold import find as find_item


def _queue_text(words: list[str]) -> str:
    text = clean(" ".join(words)).strip()
    if not text or utf8_len(text) > LIMITS["text_max"]:
        raise ValueError("text must be 1 to 500 UTF-8 bytes")
    return text


def queue_add(paths: Paths, args: argparse.Namespace) -> int:
    if find(load_registry(paths.projects), args.key) is None:
        raise ProjectsError(f"no project with key {args.key}")
    text = _queue_text(args.text)
    secure_home(paths)
    log = EventLog(paths.events)
    state = fold_queue(log.load())
    rec = (records.queue_added(qid=None, project=args.key, text=text, intent="todo", origin="cli",
                               at=records.iso(_now())),)  # fmt: skip
    _, written = log.append(rec, prepare=lambda new: assign_qids(rec, fold_queue(new, state)))
    print(written[0]["qid"])
    return 0


def queue_ls(paths: Paths, args: argparse.Namespace) -> int:
    state = fold_queue(EventLog(paths.events).load())
    for item in open_items(state, args.key):
        print(f"{item.qid:<6} {item.project:<12} {item.intent:<8} {item.text}")
    return 0


def queue_done(paths: Paths, args: argparse.Namespace) -> int:
    log = EventLog(paths.events)
    if find_item(fold_queue(log.load()), args.qid) is None:
        raise ValueError(f"no open queue item {args.qid}")
    log.append((records.queue_done(args.qid, records.iso(_now())),))
    print(f"done {args.qid}")
    return 0


def queue_reopen(paths: Paths, args: argparse.Namespace) -> int:
    """Undo a queue done (or a spawn) by mistake, e.g. Opt+D on the wrong row (docs/20 U12)."""
    log = EventLog(paths.events)
    events = log.load()
    item = closed_item(events, args.qid)
    if item is None:
        why = "is still open" if find_item(fold_queue(events), args.qid) else "was never queued"
        raise ValueError(f"{args.qid} {why}")
    log.append((records.queue_reopened(item, records.iso(_now())),))
    print(f"reopened {args.qid}: {item.text}")
    return 0


def device_forget(paths: Paths, args: argparse.Namespace) -> int:
    secure_home(paths)
    save_config(paths.config, replace_config(load_config(paths.config), device_serial=None))
    print("device binding cleared; the next cpa-fw board that answers hello will be bound", file=sys.stdout)
    return 0


NO_NAME = ("这台 Mac 还没有名字；起一个：cpa host name -- 家", "this Mac has no name; set one: cpa host name -- Home")


def _host_json(label: str | None) -> int:
    print(json.dumps({"v": VERSION, "host": excerpt(label, LIMITS["host_label_max"])}, ensure_ascii=False))
    return 0


def host_name(paths: Paths, args: argparse.Namespace) -> int:
    """Show, set or clear config.json's host_label; a bad name is refused before anything is written, unechoed."""
    as_json, cfg = getattr(args, "json", False), load_config(paths.config)
    if args.label is None and not args.clear:
        if as_json:
            return _host_json(cfg.host_label)
        named = cfg.host_label
        print(i18n.T(f"这台 Mac：{named}", f"this Mac: {named}") if named else i18n.T(*NO_NAME))
        return 0
    if not args.clear and not valid_name(args.label):
        if as_json:
            print(json.dumps(FilingError(E_BAD_ARG, "name").doc()))
            return 2
        raise ValueError("a name is 1-24 UTF-8 bytes without control or direction characters, not starting with '-'")
    secure_home(paths)
    label = None if args.clear else args.label
    save_config(paths.config, replace_config(cfg, host_label=label))
    if as_json:
        return _host_json(label)
    print(
        i18n.T(
            f"这台 Mac：{label or '没有名字'}（新录音马上用；设备下次连上时显示）",
            f"this Mac: {label or 'no name'} (new recordings now; the device shows it from its next connection)",
        )
    )
    return 0


def lang(paths: Paths, args: argparse.Namespace) -> int:
    """Show the language and what decides it; zh or en sets config.json's lang, auto forgets it (the locale)."""
    cfg = load_config(paths.config)  # a broken config.json stops here, before anything is written
    if args.lang is not None:
        secure_home(paths)
        cfg = merge_config(paths.config, lang=None if args.lang == "auto" else args.lang)
    found, source = i18n.resolve(os.environ, lambda: cfg.lang)
    if source == "default":
        print(i18n.T(f"语言：{found}（默认）", f"language: {found} (default)"))
    else:
        print(i18n.T(f"语言：{found}（来自 {source}）", f"language: {found} (from {source})"))
    return 0


def _now() -> float:
    import time  # noqa: PLC0415

    return time.time()
