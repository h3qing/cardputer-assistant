"""cpa filing [ls] | show ID | file ID KEY|--none [--other] | done ID | hide ID | reopen ID |
hand ID [KEY] [--spawn] [--other] (docs/23 §3.1-§3.4); `cpa filing triage` is cli.triage, over these same writes.

With --json each prints one document of the contract protocol/filing/v1 (ls, show, result, error); without it, a
few lines for a person. `cpa filing` alone is `cpa filing ls`: the new recordings of this Mac, newest first.
For a person (not in the contract): `--project KEY` alone lists every state of KEY (the default, new, never has a
project), and --from / --to also take today and yesterday, this Mac's local dates.
- reads need no daemon: rows come from events.jsonl and the archive (filing.view); only "segments still on the
  device" is asked of a running cpad, and is null when it is not running;
- argv holds ids, keys and enum words only (filing.args); `--text -` reads the search words from stdin;
- a write is one record or none, decided again under the events lock with whatever was appended meanwhile
  (filing.actions), fsynced before the answer; it prints its undo; the CLI never writes a daily note;
- hand queues the recording's own text, read here from the log and the archive, never from argv; it is refused
  while a cpad older than HAND_API runs or one does not answer within READY_S (that cpad would start the item in
  the project's mode, not plan); with none running it goes ahead, so an older cpad started afterwards would start
  it in the project's mode (accepted: docs/23 §2.2); it is started only from the device board (ADR 0002);
- errors are fixed codes with the argument's name, never its value; exit 2 for a bad argument, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections.abc import Callable, Mapping, Sequence
from datetime import date, timedelta
from typing import Any

from cpad.archive.fs import Root
from cpad.cli import rec
from cpad.config.settings import ConfigError, Paths, load_config, secure_home
from cpad.control import client
from cpad.events.log import EventLog
from cpad.events.records import iso
from cpad.filing import actions, query, view
from cpad.filing.actions import Ask
from cpad.filing.args import E_UNAVAILABLE, FilingError
from cpad.protocol.validate import is_int

STDIN_MAX = 4 * query.TEXT_MAX + 2  # enough to tell an over-long query from a long one
HAND_API = 2  # the first cpad that starts an item handed from a recording in plan mode (control.server.API)
READY_S = 2.0
LS_FLAGS = ("-h", "--help")
DAYS = {"today": 0, "yesterday": 1}
DATES = "dates are YYYY-MM-DD, today or yesterday (to not before from)"
HINTS = {  # for a person only; fixed words, never the input
    ("E_BAD_ARG", "other"): "it asked for another Mac's project: add --other to file it here",
    ("E_BAD_ARG", "id"): "not a recording id, or a meeting (meetings are never handed)",
    ("E_BAD_ARG", "project"): "give one of this Mac's keys, or --none",
    ("E_BAD_ARG", "from"): DATES,
    ("E_BAD_ARG", "to"): DATES,
    ("E_UNKNOWN_ID", "id"): "no archived recording has this id",
    ("E_UNKNOWN_PROJECT", "project"): "not one of this Mac's projects (cpa project ls)",
    ("E_ALREADY", "filing"): "it is not new any more (cpa filing show ID)",
    ("E_ALREADY", "queue"): "its queue item is open, or already started",
    ("E_UNAVAILABLE", None): "config.json or the archive could not be read",
    ("E_UNAVAILABLE", "cpad"): "the running cpad is older than this cpa or did not answer; restart it: "
    "cpa service install",
    ("E_NO_TEXT", "text"): "no usable text yet: file it, or hand it once it is transcribed",
}
NO_PROJECT_YET = "it has no project of this Mac yet: cpa filing hand ID KEY"


def _root(paths: Paths) -> Root:
    return Root.at(load_config(paths.config).notes_path())


def _cpad_ready(paths: Paths) -> None:
    """hand: a running cpad must be one that starts inbox items in plan mode; none running is fine."""
    try:
        doc = client.request(paths.sock, {"cmd": "status"}, timeout=READY_S)
    except client.DaemonNotRunning:
        return
    except (OSError, ValueError):
        raise FilingError(E_UNAVAILABLE, "cpad") from None
    api = doc.get("api") if isinstance(doc, Mapping) else None
    if not is_int(api) or api < HAND_API:
        raise FilingError(E_UNAVAILABLE, "cpad")


def _stdin_text() -> str:
    return sys.stdin.read(STDIN_MAX).removesuffix("\n").removesuffix("\r")


def _day(text: str | None) -> str | None:
    """today / yesterday as this Mac's local date; anything else as typed (filing.query checks it)."""
    return (date.today() - timedelta(days=DAYS[text])).isoformat() if text in DAYS else text


def _raw(args: argparse.Namespace) -> dict[str, Any]:
    return {"filing": args.filing or ("all" if args.project else None), "orphans": args.orphans,
            "from": _day(args.from_), "to": _day(args.to), "device": args.device, "link": args.link,
            "mode": args.mode, "project": args.project, "none": args.none, "tx": args.tx,
            "text": _stdin_text() if args.text == "-" else args.text, "limit": args.limit, "cursor": args.cursor,
            "no_text": args.no_text}  # fmt: skip


def _hint(args: argparse.Namespace, err: FilingError) -> str | None:
    unfiled = getattr(args, "action", None) == "hand" and getattr(args, "key", None) is None
    if unfiled and (err.code, err.field) == ("E_UNKNOWN_PROJECT", "project"):
        return NO_PROJECT_YET
    return HINTS.get((err.code, err.field))


def _refuse(args: argparse.Namespace, err: FilingError) -> int:
    if args.json:
        print(json.dumps(err.doc()))
    else:
        hint = _hint(args, err)
        print(f"error: {err}" + (f": {hint}" if hint else ""), file=sys.stderr)
    return err.exit_code


def _run(args: argparse.Namespace, work: Callable[[], dict[str, Any]], say: Callable[[dict[str, Any]], None]) -> int:
    try:
        doc = work()
    except FilingError as err:
        return _refuse(args, err)
    except (ConfigError, OSError):
        return _refuse(args, FilingError(E_UNAVAILABLE))
    if args.json:
        print(json.dumps(doc, ensure_ascii=False))
    else:
        say(doc)
    return 0


# --- what a person reads -------------------------------------------------------------------------


def _cut(text: str | None, width: int) -> str:
    text = text or ""
    return text if len(text) <= width else text[: width - 1] + "…"


def _when(row: Mapping[str, Any]) -> str:
    return rec.when({"started_ms": row["at"]["wall_ms"], "uncertain": row["at"]["uncertain"]})


def _project(row: Mapping[str, Any]) -> str:
    return row["project"] or (f"asked {row['asked']}" if row["asked"] else "-")


def _ls_line(row: Mapping[str, Any]) -> str:
    words = row["title"] or row["text"] or "-"
    return (f"{row['id']:<18} {_when(row):<17} {rec.duration(row):>5}  {row['kind']:<7} {row['filing']:<6} "
            f"{_project(row):<17} {row['tx']:<10} {_cut(words, 48)}")  # fmt: skip


def _say_ls(doc: dict[str, Any]) -> None:
    counts = doc["counts"]
    on_device = "-" if counts["on_device"] is None else counts["on_device"]
    print(f"new {counts['new']} · other Mac {counts['other_mac']} · on device {on_device}")
    print(f"{'ID':<18} {'STARTED':<17} {'DUR':>5}  {'KIND':<7} {'FILING':<6} {'PROJECT':<17} {'TX':<10} TEXT")
    for row in doc["rows"]:
        print(_ls_line(row))
    if doc["next_cursor"]:
        print(f"more: --cursor {doc['next_cursor']}")


def _mmss(t_s: float | None) -> str:
    return "--:--" if t_s is None else f"{int(t_s) // 60:02d}:{int(t_s) % 60:02d}"


def _todo(t: Mapping[str, Any]) -> str:
    who = ", ".join(x for x in (t["owner"], t["due"]) if x)
    return f"{t['n']}. {t['text']}" + (f" ({who})" if who else "")


def _say_show(doc: dict[str, Any]) -> None:
    row, hand = doc["row"], doc["row"]["queue"]
    queue = f"{hand['qid']} {hand['intent']} {'open' if hand['open'] else 'closed'}" if hand else "-"
    lines = [("id", row["id"]), ("kind", row["kind"]), ("filing", row["filing"]), ("project", _project(row)),
             ("started", _when(row)), ("duration", rec.duration(row)), ("tx", row["tx"]),
             ("device", row["device"] or "-"), ("title", row["title"] or "-"), ("text", doc["text"] or "-"),
             ("audio", " ".join(doc["audio"]) or "-"), ("fw", doc["fw"] or "-"), ("queue", queue),
             ("queued", doc["queue_text"] or "-")]  # fmt: skip
    print("\n".join(f"{k:<9} {v}" for k, v in lines))
    print("\n".join(f"todo      {_todo(t)}" for t in doc["todos"]) or "todo      -")
    if doc["sentences"] is not None:
        print("transcript")
        print("\n".join(f"  [{_mmss(s['t_s'])}] {s['text']}" for s in doc["sentences"]))


def _say_write(project: str | None) -> Callable[[dict[str, Any]], None]:
    def say(doc: dict[str, Any]) -> None:
        shown = f" · {project}" if project and doc["filing"] != "new" else ""
        print(f"{doc['id']} {doc['filing']}{shown}{'' if doc['changed'] else ' (no change)'}")
        if doc["undo"]:
            print(f"undo: {' '.join(actions.undo_argv(doc['undo']))}")

    return say


def _say_hand(doc: dict[str, Any]) -> None:
    item = doc["queue"]
    print(f"{doc['id']} {doc['filing']} · {item['qid']} {item['intent']}")
    if item["intent"] == "spawn":
        print(f"start it on the device board: select {item['qid']}, Enter (plan mode)")
    print(f"undo: {' '.join(actions.undo_argv(doc['undo']))}")


# --- the commands ----------------------------------------------------------------------------------


def filing_ls(paths: Paths, args: argparse.Namespace) -> int:
    return _run(args, lambda: view.listing(paths, _root(paths), _raw(args), view.on_device(paths)), _say_ls)


def filing_show(paths: Paths, args: argparse.Namespace) -> int:
    return _run(args, lambda: view.detail(paths, _root(paths), args.id), _say_show)


def write(paths: Paths, ask: Ask, at: Callable[[], str] = lambda: iso(time.time()),
          read: Callable[[tuple[dict, ...]], str | None] = lambda events: None,
          ready: Callable[[], None] = lambda: None) -> dict[str, Any]:  # fmt: skip
    """Plan from the log, then plan again under the events lock and append exactly that (one record or none).
    read: what only the archive says (hand: the transcript), once, given the log as loaded; ready: a last check
    once something is to be written."""
    log = EventLog(paths.events)
    events, known = log.load(), view.known(paths.projects)
    transcript = read(events)

    def decide(found: tuple[dict, ...]) -> actions.Plan:
        return actions.plan(ask, found, known, at(), transcript)

    first = decide(events)
    if not first.records:
        return actions.result(first, events, known, changed=False)
    ready()
    secure_home(paths)
    others, written = log.append((), prepare=lambda new: decide((*events, *new)).records)
    final = decide((*events, *others)) if written else first
    return actions.result(final, (*events, *others, *written), known, changed=bool(written))


def _filing_write(paths: Paths, args: argparse.Namespace) -> int:
    ask = Ask(args.action, args.id, project=getattr(args, "key", None), none=getattr(args, "none", False),
              other=getattr(args, "other", False))  # fmt: skip
    return _run(args, lambda: write(paths, ask), _say_write(ask.project))


def hand(paths: Paths, ask: Ask) -> dict[str, Any]:
    """write() for a hand: the recording's transcript read here, and a running cpad that starts it in plan mode."""
    root = _root(paths)
    return write(paths, ask, read=lambda events: view.transcript(root, events, ask.id),
                 ready=lambda: _cpad_ready(paths))  # fmt: skip


def _filing_hand(paths: Paths, args: argparse.Namespace) -> int:
    ask = Ask("hand", args.id, project=args.key, other=args.other, spawn=args.spawn)
    return _run(args, lambda: hand(paths, ask), _say_hand)


def with_ls(argv: Sequence[str]) -> list[str]:
    """`cpa filing` and `cpa filing --json ...` mean `cpa filing ls ...`."""
    bare = argv[:1] == ["filing"] and (len(argv) == 1 or (argv[1].startswith("-") and argv[1] not in LS_FLAGS))
    return ["filing", "ls", *argv[1:]] if bare else list(argv)


def _ls_parser(ls: argparse.ArgumentParser) -> None:
    ls.add_argument("--json", action="store_true", help="protocol/filing/v1 ls")
    ls.add_argument("--filing", help="new (default; all with --project), filed, handed, done, hidden, or all "
                    "(all but hidden)")  # fmt: skip
    ls.add_argument("--orphans", action="store_true", help="also the recordings that asked for another Mac's project")
    ls.add_argument("--from", dest="from_", metavar="YYYY-MM-DD", help="or today, yesterday")
    ls.add_argument("--to", metavar="YYYY-MM-DD", help="or today, yesterday")
    ls.add_argument("--device", help="the short device id of a row")
    ls.add_argument("--link", help="here (recorded while linked to this Mac) or unknown")
    ls.add_argument("--mode", help="memo or meeting")
    ls.add_argument("--project", action="append", metavar="KEY", help="one of this Mac's keys; repeatable")
    ls.add_argument("--none", action="store_true", help="only recordings without a project")
    ls.add_argument("--tx", action="append", help="transcription state; repeatable")
    ls.add_argument("--text", help="literal, case-insensitive; '-' reads it from stdin")
    ls.add_argument("--limit", help=f"rows per page, 1-{query.LIMIT_MAX} (default {query.LIMIT_DEFAULT})")
    ls.add_argument("--cursor", help="next_cursor of the previous page")
    ls.add_argument("--no-text", action="store_true", help="leave out titles and text")
    ls.set_defaults(fn=lambda paths, args, deps: filing_ls(paths, args))


def _write_parser(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--json", action="store_true", help="protocol/filing/v1 result or error")
    parser.set_defaults(fn=lambda paths, args, deps: _filing_write(paths, args))


def _hand_parser(hand: argparse.ArgumentParser) -> None:
    hand.add_argument("id")
    hand.add_argument("key", nargs="?", help="one of this Mac's keys (default: the recording's project)")
    hand.add_argument("--spawn", action="store_true", help="to be started as a session, from the device board")
    hand.add_argument("--other", action="store_true", help="yes: it asked for another Mac's project")
    _write_parser(hand)
    hand.set_defaults(fn=lambda paths, args, deps: _filing_hand(paths, args))


def add_parsers(sub: Any) -> Any:
    """`cpa filing` and its subcommands; returns them, for cli.triage to add its own."""
    filing = sub.add_parser("filing", help="recordings to file: ls, show, file, done, hide, reopen, hand, triage "
                            "(docs/23)")  # fmt: skip
    acts = filing.add_subparsers(dest="action", required=True)
    _ls_parser(acts.add_parser("ls", help="new recordings of this Mac by default, newest first"))
    show = acts.add_parser("show", help="the whole text, the transcript, todos, audio file names")
    show.add_argument("id")
    show.add_argument("--json", action="store_true", help="protocol/filing/v1 show or error")
    show.set_defaults(fn=lambda paths, args, deps: filing_show(paths, args))
    file = acts.add_parser("file", help="file under one of this Mac's projects, or --none")
    file.add_argument("id")
    file.add_argument("key", nargs="?")
    file.add_argument("--none", action="store_true", help="no project")
    file.add_argument("--other", action="store_true", help="yes: it asked for another Mac's project")
    _write_parser(file)
    for op, words in (("done", "looked at, needs no project"), ("hide", "hide everywhere; no file is touched"),
                      ("reopen", "back to new: undo done, hide, or a hand not yet started")):  # fmt: skip
        parser = acts.add_parser(op, help=words)
        parser.add_argument("id")
        _write_parser(parser)
    _hand_parser(acts.add_parser("hand", help="queue a memo for AI, its own text; start it on the device board"))
    return acts
