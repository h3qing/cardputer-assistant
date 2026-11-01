"""cpa dest ls | add | rm | bind | test, and cpa audit (docs/14 §8.2, §9). English output, never a capture's text.
Presets, url and app destinations, test --send and verify are in cli_presets.py (docs/25 §5.5).

The CLI is the only writer of destinations.json (atomic, 0600); the daemon reloads it by mtime. Every
change appends a dest.changed record. Enabling anything that leaves the Mac first explains what will be
sent and where, and needs a typed `y` (or --yes); the time of that answer is stored as `optin`.
Edits load the file strictly, so a broken destinations.json is reported, never overwritten by defaults.
"""

from __future__ import annotations

import argparse
import json
import time
from dataclasses import asdict, replace
from pathlib import Path
from typing import Any

from cpad.config.settings import Paths
from cpad.dest import cli_presets, cli_send, cli_test
from cpad.dest.adapters import make_adapters, probe
from cpad.dest.audit import audit_rows, format_rows, parse_since
from cpad.dest.catalog import Catalog, DestsError, bind, find, free_slot, put, remove, state_of
from cpad.dest.cli_common import STATE_TEXT, opt_in, record
from cpad.dest.cli_common import root as root_
from cpad.dest.config import dests_path, load_catalog, load_dests
from cpad.dest.model import BUILTIN_IDS, Dest, FileOpts, flags, kind_of
from cpad.dest.paths import check_place
from cpad.events.log import EventLog


def _ls_row(d: Dest) -> dict[str, Any]:
    target = f"{d.file.root.rstrip('/')}/{d.file.path}" if d.file else None
    return {"slot": d.slot, "key": d.key, "id": d.id, "kind": d.kind, "label": d.label, "state": state_of(d),
            "leaves_mac": d.leaves_mac, "fl": flags(d), "profiles": list(d.profiles), "file": target}  # fmt: skip


def dest_ls(paths: Paths, args: argparse.Namespace) -> int:
    cat = load_catalog(dests_path(paths))
    shipped = {p.preset: p for p in cli_presets.load_presets(None).presets}
    rows = [{**_ls_row(d), **cli_presets.ls_fields(d, shipped)} for d in cat.dests]
    if args.json:
        print(json.dumps({"problem": cat.problem, "dests": rows}, ensure_ascii=False))
        return 0
    if cat.problem:
        print(f"destinations.json: {cat.problem}; only daily, todo, queue and session work until it is fixed")
    for row in rows:
        where = "leaves Mac" if row["leaves_mac"] else "local"
        key, fl = (row["key"] or "-").upper(), row["fl"] or "-"
        head = f"{row['slot']:>2}  {key}  {row['id']:<16} {row['kind']:<8} {fl:<4} "
        tail = f"  {row['file']}" if row["file"] else ""
        profs = f"  [{','.join(row['profiles'])}]" if row["profiles"] else ""
        profs += f"  ({cli_presets.does_now(row)})" if row["kind"] == "url" else ""
        print(f"{head}{STATE_TEXT[row['state']]:<20} {where:<10} {row['label']}{profs}{tail}")
    return 0


def _file_opts(old: FileOpts | None, args: argparse.Namespace, cpa_home: Path) -> FileOpts:
    root = args.root or (old.root if old else None)
    path = args.path or (old.path if old else None)
    if not root or not path:
        raise DestsError("a file destination needs --root DIR and --path FILE")
    opts = FileOpts(root_(root), path)
    check_place(opts, Path.home(), denied=(cpa_home,))
    return opts


def _file_draft(cat: Catalog, before: Dest | None, dest_id: str, args: argparse.Namespace, cpa_home: Path) -> Dest:
    """A new or changed file.<name>; its opt-in survives only when neither the file nor --local changed."""
    base = before or Dest(dest_id, "file", dest_id.partition(".")[2], free_slot(cat), leaves_mac=True)
    opts = _file_opts(base.file, args, cpa_home)
    leaves = not args.local
    same = base.leaves_mac and leaves and base.file == opts
    return replace(base, label=args.label or base.label, slot=args.slot or base.slot, enabled=True,
                   leaves_mac=leaves, optin=base.optin if same else None, file=opts)  # fmt: skip


def _with_profiles(dest: Dest, args: argparse.Namespace) -> Dest:
    return replace(dest, profiles=tuple(args.profile)) if args.profile else dest


def _draft(cat: Catalog, dest_id: str, args: argparse.Namespace, cpa_home: Path) -> Dest:
    kind = kind_of(dest_id)
    if kind is None:
        raise DestsError(f"unknown destination id {dest_id!r}: use {', '.join(BUILTIN_IDS)} or file.<name>")
    if kind in ("url", "app"):
        raise DestsError(f"{kind} destinations are added with --preset or --app")
    before = find(cat, dest_id)
    if kind == "file":
        return _file_draft(cat, before, dest_id, args, cpa_home)
    if before is None or args.root or args.path or args.local:
        raise DestsError("--root, --path and --local are only for file.<name>")
    return replace(before, label=args.label or before.label, slot=args.slot or before.slot, enabled=True)


def dest_add(paths: Paths, args: argparse.Namespace) -> int:
    if args.preset or args.app or args.preset_file:
        return cli_presets.add_from_preset(paths, args)
    if not args.id:
        raise DestsError("name a destination id, or use --preset, --app or --preset-file")
    cat = load_dests(dests_path(paths))
    draft = _with_profiles(_draft(cat, args.id, args, paths.home), args)
    changed = put(cat, draft)
    changed = bind(changed, args.id, args.key) if args.key is not None else changed
    dest = find(changed, args.id)
    assert dest is not None
    allowed = opt_in(dest, args.yes)
    if allowed is None:
        return 1
    changed = put(changed, allowed) if allowed is not dest else changed
    record(paths, changed, "add", allowed)
    dest = allowed
    print(f"{dest.id}: {STATE_TEXT[state_of(dest)]}, slot {dest.slot}, key {(dest.key or '-').upper()}")
    return 0


def dest_rm(paths: Paths, args: argparse.Namespace) -> int:
    cat = load_dests(dests_path(paths))
    changed = remove(cat, args.id)
    before, after = find(cat, args.id), find(changed, args.id)
    assert before is not None
    record(paths, changed, "rm", after or replace(before, enabled=False))
    print(f"{args.id}: {'turned off' if after is not None else 'removed'}")
    return 0


def dest_bind(paths: Paths, args: argparse.Namespace) -> int:
    letter = None if args.key == "-" else args.key
    if letter is not None and not (len(letter) == 1 and letter.isascii() and letter.isalpha()):
        raise DestsError("key must be one letter, or - to unbind")
    changed = bind(load_dests(dests_path(paths)), args.id, letter)
    dest = find(changed, args.id)
    assert dest is not None
    record(paths, changed, "bind", dest)
    print(f"{dest.id}: key {(dest.key or '-').upper()}")
    return 0


def dest_test(paths: Paths, args: argparse.Namespace) -> int:
    """Inspects only (0 = usable, 1 = not); with --send, hands one marked test text to a url / app destination."""
    cat = load_catalog(dests_path(paths))
    dest = find(cat, args.id)
    if dest is None:
        print(f"{args.id}: unknown destination")
        return 1
    if state_of(dest) != "on":
        print(f"{dest.id}: {STATE_TEXT[state_of(dest)]}")
        return 1
    if args.send:
        return cli_test.send_test(paths, dest, args)
    found = probe(dest, make_adapters(Path.home(), denied=(paths.home,), mac=cli_presets.MAC()))
    print(f"{dest.id}: {found.state}{' (' + found.reason + ')' if found.reason else ''}")
    return 0 if found.state == "ok" else 1


def audit(paths: Paths, args: argparse.Namespace) -> int:
    """Reads events.jsonl directly: works without a running daemon."""
    since = parse_since(args.since or "7d", time.time())
    rows = audit_rows(EventLog(paths.events).load(), since=since, leaves_mac=args.leaves_mac, dest=args.dest)
    if args.json:
        print(json.dumps([asdict(row) for row in rows], ensure_ascii=False))
        return 0
    for line in format_rows(rows) or [f"no destination events since {since}"]:
        print(line)
    return 0


def _wrap(fn: Any) -> Any:
    return lambda paths, args, deps: fn(paths, args)


def add_parsers(sub: Any) -> None:
    """Registers `cpa dest {ls,add,rm,bind,test}` and `cpa audit`."""
    dest = sub.add_parser("dest", help="capture destinations: which key sends a capture where (never text)")
    actions = dest.add_subparsers(dest="action", required=True)
    ls = actions.add_parser("ls", help="list destinations, keys and states")
    ls.add_argument("--json", action="store_true")
    ls.set_defaults(fn=_wrap(dest_ls))
    add = actions.add_parser("add", help="turn on or change a destination; file.<name> needs --root and --path")
    add.add_argument("id", nargs="?")
    cli_presets.add_flags(add)
    for flag in ("--key", "--label", "--root", "--path"):
        add.add_argument(flag)
    add.add_argument("--slot", type=int)
    add.add_argument("--local", action="store_true", help="the file's folder is never synced off this Mac")
    add.add_argument("--yes", action="store_true", help="allow a destination that leaves the Mac without asking")
    add.add_argument("--profile", action="append", help="docs/18 profile it serves (repeat; default: every profile)")
    add.set_defaults(fn=_wrap(dest_add))
    rm = actions.add_parser("rm", help="remove a file destination or turn off clip / paste")
    rm.add_argument("id")
    rm.set_defaults(fn=_wrap(dest_rm))
    bind_ = actions.add_parser("bind", help="bind a letter (b c f j k o u x y), or - to unbind")
    bind_.add_argument("id")
    bind_.add_argument("key")
    bind_.set_defaults(fn=_wrap(dest_bind))
    test = actions.add_parser("test", help="check that a destination can be used (writes nothing)")
    test.add_argument("id")
    test.add_argument("--send", action="store_true", help="url / app: hand it one marked test text, then ask")
    test.add_argument("--json", action="store_true", help="with --send: protocol/dest-cli/v1 tested; asks nothing")
    test.set_defaults(fn=_wrap(dest_test))
    cli_presets.add_parsers(actions, _wrap)
    cli_send.add_parsers(actions, _wrap)
    cli_test.add_parsers(actions, _wrap)
    audit_ = sub.add_parser("audit", help="what went where: sizes and hashes, never text")
    audit_.add_argument("--since", help="7d (default), 24h, 30m or an ISO time")
    audit_.add_argument("--leaves-mac", action="store_true", help="only what left this Mac or was allowed to")
    audit_.add_argument("--dest", help="only this destination id")
    audit_.add_argument("--json", action="store_true")
    audit_.set_defaults(fn=_wrap(audit))
