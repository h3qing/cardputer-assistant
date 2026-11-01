"""cpa dest pending | send | drop | usual and cpa filing send (docs/25 §5.4, §5.5): the commands over dest.send.

With --json each prints one document of protocol/dest-cli/v1 (pending, sent, dropped, usual, or error); without it,
a few English lines for a person. argv holds ids, destination ids, project keys and enum words only; the text a
send hands over is read from events.jsonl, never from argv. Exit codes: 0 done (a send whose delivery failed: 1, the
row stays in pending), 2 a bad argument (E_BAD_ARG, E_UNKNOWN_DEST), 1 any other refusal.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections.abc import Callable
from typing import Any

from cpad.config.settings import ConfigError, Paths
from cpad.dest import send as sender
from cpad.dest.catalog import Catalog, DestsError, with_usual
from cpad.dest.cli_common import save
from cpad.dest.config import dests_path, load_dests
from cpad.dest.records import dest_usual as usual_record
from cpad.dest.waiting import E_UNAVAILABLE, SendError
from cpad.events.records import iso
from cpad.filing import view
from cpad.filing.args import E_BAD_ARG, E_UNKNOWN_PROJECT, FilingError, parse_key

BY = ("cli", "app")
HINTS = {  # for a person only; fixed words, never the input
    ("E_BAD_ARG", "id"): "not a recording id, or a meeting (meetings only go to the daily note, todo or a local file)",
    ("E_BAD_ARG", "ref"): "not an id from cpa dest pending",
    ("E_BAD_ARG", "dest"): "name a destination (cpa dest ls); the daily note and todo already have it, and "
    "queue / session are cpa filing hand ID [--spawn]",
    ("E_UNKNOWN_ID", "id"): "no archived recording has this id",
    ("E_UNKNOWN_ID", "ref"): "nothing waits under this id (cpa dest pending)",
    ("E_UNKNOWN_DEST", "dest"): "no such destination (cpa dest ls)",
    ("E_DEST_OFF", "dest"): "that destination is off or not allowed yet (cpa dest ls)",
    ("E_OTHER_PROFILE", "dest"): "that destination serves another profile",
    ("E_NO_TEXT", "text"): "no memo text yet: turn transcription on (cpa-stt use) or wait for it",
    ("E_TOO_LONG", "text"): "too long to confirm on the device",
    ("E_NEEDS_DEVICE", "dest"): "it may act by itself, so this Mac cannot send to it; if it only fills a draft, run "
    "cpa dest test ID --send once and answer",
    ("E_BUSY", "ref"): "a send of it is still running",
    ("E_ALREADY", "dest"): "it was already sent there; add --again to send it once more",
    ("E_UNAVAILABLE", "dests"): "destinations.json cannot be used (cpa dest ls says why)",
    ("E_BAD_ARG", "project"): "give one of this Mac's project keys",
    ("E_UNKNOWN_PROJECT", "project"): "not one of this Mac's projects (cpa project ls)",
}


def _refuse(args: argparse.Namespace, err: FilingError) -> int:
    if args.json:
        print(json.dumps(err.doc()))
    else:
        hint = HINTS.get((err.code, err.field))
        print(f"error: {err}" + (f": {hint}" if hint else ""), file=sys.stderr)
    return err.exit_code


def _run(args: argparse.Namespace, work: Callable[[], dict[str, Any]], say: Callable[[dict[str, Any]], int]) -> int:
    try:
        doc = work()
    except FilingError as err:
        return _refuse(args, err)
    except (ConfigError, OSError):
        return _refuse(args, SendError(E_UNAVAILABLE))
    if args.json:
        print(json.dumps(doc, ensure_ascii=False))
        return 1 if doc.get("outcome") == "failed" else 0
    return say(doc)


def _say_sent(doc: dict[str, Any]) -> int:
    label = doc["label"]
    words = {
        ("handed", "url", None): f"filled a draft in {label}: press Return there to send it",
        ("handed", "clip", "too_long"): f"too long for a link: the whole text is on the clipboard for 2 minutes; "
        f"Cmd-V in {label}",
        ("handed", "clip", "draft_busy"): f"{label} still has the previous draft: this one is on the clipboard for 2 "
        "minutes, Cmd-V after it",
        ("handed", "clip", None): f"on the clipboard for 2 minutes, {label} in front: press Cmd-V there",
    }.get((doc["outcome"], doc["via"], doc["why"]))
    if doc["outcome"] == "failed":
        print(f"{doc['ref']} -> {doc['dest']}: not sent ({doc['code']}); it waits in cpa dest pending")
        return 1
    print(f"{doc['ref']} -> {doc['dest']}: {words or 'done'}")
    return 0


def _say_pending(doc: dict[str, Any]) -> int:
    for row in doc["rows"]:
        state = row["code"] or row["st"] or row["why"]
        where = f"#{row['project']}" if row["project"] else "-"
        print(f"{row['ref']:<20} {row['why']:<8} -> {row['label']:<12} {where:<14} {state:<14} {row['text'] or '-'}")
    if not doc["rows"]:
        print("nothing waits to be sent")
    return 0


def _say_dropped(doc: dict[str, Any]) -> int:
    print(f"{doc['ref']}: not sent to {doc['dest']} (its daily-note line stays)")
    return 0


def dest_pending(paths: Paths, args: argparse.Namespace) -> int:
    return _run(args, lambda: sender.pending(paths, expired=args.expired), _say_pending)


def dest_send(paths: Paths, args: argparse.Namespace) -> int:
    return _run(args, lambda: sender.send(paths, args.ref, args.dest, by=args.by, again=args.again), _say_sent)


def dest_drop(paths: Paths, args: argparse.Namespace) -> int:
    return _run(args, lambda: sender.drop(paths, args.ref, by=args.by), _say_dropped)


def filing_send(paths: Paths, args: argparse.Namespace) -> int:
    work = lambda: sender.send(paths, args.id, args.dest, by=args.by, field="id", again=args.again)  # noqa: E731
    return _run(args, work, _say_sent)


def _catalog(paths: Paths) -> Catalog:
    try:
        return load_dests(dests_path(paths))
    except DestsError:
        raise SendError(E_UNAVAILABLE, "dests") from None


def _usual_doc(cat: Catalog) -> dict[str, Any]:
    return {"ok": True, "v": 1, "usual": [{"project": p, "dest": d} for p, d in cat.usual]}


def _set_usual(paths: Paths, project: str, dest: str) -> dict[str, Any]:
    key = parse_key(project)
    if key not in view.known(paths.projects).keys:
        raise SendError(E_UNKNOWN_PROJECT, "project")
    chosen = None if dest == "-" else dest
    try:
        changed = with_usual(_catalog(paths), key, chosen)
    except DestsError:
        raise SendError(E_BAD_ARG, "dest") from None
    save(paths, changed, usual_record(key, chosen, by="cli", at=iso(time.time())))
    return _usual_doc(changed)


def dest_usual(paths: Paths, args: argparse.Namespace) -> int:
    """cpa dest usual [PROJECT [DEST|-]]: list, show one, set or forget a project's usual destination."""
    if args.project is not None and args.dest is not None:
        work: Callable[[], dict[str, Any]] = lambda: _set_usual(paths, args.project, args.dest)  # noqa: E731
    else:
        work = lambda: _usual_doc(_catalog(paths))  # noqa: E731

    def say(doc: dict[str, Any]) -> int:
        shown = [u for u in doc["usual"] if args.project in (None, u["project"])]
        for u in shown:
            print(f"{u['project']:<14} -> {u['dest']}")
        if not shown:
            print("no usual destination" + (f" for {args.project}" if args.project else ""))
        return 0

    return _run(args, work, say)


def add_parsers(actions: Any, wrap: Callable[[Any], Any]) -> None:
    """Registers `cpa dest {pending,send,drop,usual}` under `cpa dest`."""
    pend = actions.add_parser("pending", help="what waits to be sent from this Mac")
    pend.add_argument("--expired", action="store_true", help="also what expired in the last week")
    pend.add_argument("--json", action="store_true", help="protocol/dest-cli/v1 pending")
    pend.set_defaults(fn=wrap(dest_pending))
    send = actions.add_parser("send", help="send a waiting capture (or a recording) to its preselected destination")
    send.add_argument("ref")
    send.add_argument("dest", nargs="?", help="another destination id (default: the preselected one)")
    _again(send)
    _by_json(send, "sent")
    send.set_defaults(fn=wrap(dest_send))
    drop = actions.add_parser("drop", help="do not send it (its daily-note line stays)")
    drop.add_argument("ref")
    _by_json(drop, "dropped")
    drop.set_defaults(fn=wrap(dest_drop))
    usual = actions.add_parser("usual", help="a project's usual destination: preselected for its memos, never sent")
    usual.add_argument("project", nargs="?")
    usual.add_argument("dest", nargs="?", help="a destination id, or - to forget it")
    usual.add_argument("--json", action="store_true", help="protocol/dest-cli/v1 usual")
    usual.set_defaults(fn=wrap(dest_usual))


def _again(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--again", action="store_true", help="send it once more although it was already sent there")


def _by_json(parser: argparse.ArgumentParser, doc: str) -> None:
    parser.add_argument("--by", choices=BY, default="cli", help="who chose it: cli (default) or app (the Mac app)")
    parser.add_argument("--json", action="store_true", help=f"protocol/dest-cli/v1 {doc} or error")


def add_filing_parser(acts: Any) -> None:
    """`cpa filing send ID DEST` (docs/23 P1-G): a recording to any destination, as from the waiting list."""
    send = acts.add_parser("send", help="send a memo's text to a destination (url.*, app.*, clip, file.*)")
    send.add_argument("id")
    send.add_argument("dest")
    _again(send)
    _by_json(send, "sent")
    send.set_defaults(fn=lambda paths, args, deps: filing_send(paths, args))
