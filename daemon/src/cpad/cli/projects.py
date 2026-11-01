"""cpa project add|rename|move|folder|rm|ls (05 §7.9, US-05; docs/25 §8, 2026-10-26).

A project is a named group the device shows on a number key 1-9; a code folder is optional (only a linked project
gets hooks, cwd matching and spawns). With --json every command prints the protocol/filing/v1 `projects` document
(after the change, for a write) or its `error`: the code and the argument at fault, never the value, and nothing
written. Names come after `--` (they may start like an option and may be CJK); keys are ASCII.
"""

from __future__ import annotations

import argparse
import json
import os
import time
from collections.abc import Callable
from typing import Any

from cpad.config.settings import Paths, secure_home
from cpad.dest.catalog import with_usual
from cpad.dest.cli_common import save
from cpad.dest.config import dests_path, load_catalog
from cpad.dest.records import dest_usual
from cpad.events.log import EventLog
from cpad.events.records import iso
from cpad.filing.args import E_BAD_ARG, E_UNAVAILABLE, VERSION, FilingError
from cpad.filing.text import excerpt
from cpad.projects import edit
from cpad.projects.registry import MODES, Project, ProjectsError, find, load_registry, save_registry
from cpad.protocol.limits import LIMITS

FOLDER_MAX = 64
EDIT_HELP = {"rename": "rename a project: cpa project rename KEY -- NEW_NAME",
             "move": "put a project on another number key 1-9; a project already there takes its old one"}  # fmt: skip
Change = Callable[[tuple[Project, ...]], tuple[Project, ...]]


def _row(p: Project) -> dict[str, Any]:
    folder = excerpt(os.path.basename(p.root), FOLDER_MAX) if p.root else None
    return {"key": p.key, "name": excerpt(p.name, LIMITS["project_name_max"]) or p.key, "slot": p.slot, "ok": p.ok,
            "linked": p.linked, "folder": folder}  # fmt: skip


def projects_doc(projects: tuple[Project, ...]) -> dict[str, Any]:
    """protocol/filing/v1 projects (docs/23 §3.1): the 1-9 the device shows, display_safe names, no local path."""
    return {"v": VERSION, "projects": [_row(p) for p in sorted(projects, key=lambda p: p.slot)]}


def _print_json(doc: dict[str, Any]) -> None:
    print(json.dumps(doc, ensure_ascii=False))


def _refused(err: FilingError) -> int:
    _print_json(err.doc())
    return err.exit_code


def _load(paths: Paths, as_json: bool) -> tuple[Project, ...] | None:
    try:
        return load_registry(paths.projects)
    except (ProjectsError, OSError):
        if not as_json:
            raise
    return None


def _change(paths: Paths, args: argparse.Namespace, change: Change, say: Callable[[tuple[Project, ...]], str]) -> int:
    before = _load(paths, args.json)
    if before is None:
        return _refused(FilingError(E_UNAVAILABLE, "projects"))
    try:
        after = change(before)
    except ProjectsError as exc:
        if not args.json:
            raise
        return _refused(FilingError(exc.code, exc.field))
    secure_home(paths)
    save_registry(paths.projects, after)
    if args.json:
        _print_json(projects_doc(after))
    else:
        print(say(after))
    return 0


def _describe(p: Project | None) -> str:
    if p is None:
        return ""
    where = p.root if p.root else "no folder"
    return f"{p.slot}  {p.key}  {p.name}  ({where})"


def _event_keys(e: dict[str, Any]) -> tuple[str, ...]:
    body = e.get("body")
    found = (e.get("project"), body.get("project") if isinstance(body, dict) else None)
    return tuple(k for k in found if isinstance(k, str))


def history_keys(paths: Paths) -> frozenset[str]:
    """Every project key history still carries: events (recordings, filings, queue items) and usual destinations.
    A new project never gets one of these by accident, or it would inherit a removed project's recordings."""
    events = EventLog(paths.events).load() if paths.events.exists() else ()
    usual = {p for p, _d in load_catalog(dests_path(paths)).usual}
    return frozenset({k for e in events for k in _event_keys(e)} | usual)


def _forget_usual(paths: Paths, key: str) -> None:
    """A removed project's usual destination goes with it (a later project must never inherit it)."""
    cat = load_catalog(dests_path(paths))
    if cat.problem is None and any(p == key for p, _d in cat.usual):
        save(paths, with_usual(cat, key, None), dest_usual(key, None, by="cli", at=iso(time.time())))


def project_add(paths: Paths, args: argparse.Namespace) -> int:
    """add NAME [--key K] [--slot N] [--folder PATH]; the older `add KEY PATH --slot N [--name NAME]` still works."""
    if args.path is not None and (args.key is not None or args.folder is not None):
        if not args.json:
            raise ProjectsError('a name with spaces must be quoted: cpa project add -- "My Project"', "argv")
        return _refused(FilingError(E_BAD_ARG, "argv"))
    if args.path is not None:
        key, name, folder = args.name, args.label or args.name, args.path
    else:
        key, name, folder = args.key, args.name, args.folder
    used = history_keys(paths)

    def change(projects: tuple[Project, ...]) -> tuple[Project, ...]:
        return edit.new_project(projects, name=name, key=key, slot=args.slot, folder=folder, mode=args.mode,
                                used=used, reuse=args.reuse)  # fmt: skip

    return _change(paths, args, change, lambda after: "added " + _describe(after[-1]))


def project_rename(paths: Paths, args: argparse.Namespace) -> int:
    return _change(paths, args, lambda ps: edit.rename(ps, args.key, args.name),
                   lambda after: "renamed " + _describe(find(after, args.key)))  # fmt: skip


def project_move(paths: Paths, args: argparse.Namespace) -> int:
    return _change(paths, args, lambda ps: edit.move(ps, args.key, args.slot),
                   lambda after: "moved " + _describe(find(after, args.key)))  # fmt: skip


def project_folder(paths: Paths, args: argparse.Namespace) -> int:
    return _change(paths, args, lambda ps: edit.link(ps, args.key, None if args.none else args.path),
                   lambda after: "folder " + _describe(find(after, args.key)))  # fmt: skip


def project_rm(paths: Paths, args: argparse.Namespace) -> int:
    code = _change(paths, args, lambda ps: edit.remove(ps, args.key),
                   lambda _after: f"removed {args.key} (its recordings keep the key)")  # fmt: skip
    if code == 0:
        _forget_usual(paths, args.key)
    return code


def project_ls(paths: Paths, args: argparse.Namespace) -> int:
    if getattr(args, "json", False):
        projects = _load(paths, True)
        if projects is None:
            return _refused(FilingError(E_UNAVAILABLE, "projects"))
        _print_json(projects_doc(projects))
        return 0
    for p in sorted(load_registry(paths.projects), key=lambda p: p.slot):
        state = "ok" if p.ok else "MISSING ROOT"
        print(f"{p.slot}  {p.key:<12}  {p.name}  [{p.mode}]  {p.root or '(no folder)'}  {state}")
    return 0


def _json_flag(parser: Any) -> None:
    parser.add_argument("--json", action="store_true", help="print protocol/filing/v1 projects, or its error")


def _add_parser(project: Any, wrap: Callable) -> None:
    add = project.add_parser("add", help="add a project: cpa project add [--key K] [--slot N] [--folder PATH] -- NAME")
    add.add_argument(
        "name",
        help="the name shown on the device (up to 24 letters or 8 Chinese characters; after --; "
        "quote a name with spaces)",
    )
    add.add_argument("path", nargs="?", help=argparse.SUPPRESS)  # older form: add KEY PATH --slot N [--name NAME]
    add.add_argument("--key", help="ASCII key, at most 12 (default: from the name, or p<slot>)")
    add.add_argument("--slot", type=int, help="1-9 (default: the first free one)")
    add.add_argument("--folder", help="link a code folder: hooks and spawns then work for it")
    add.add_argument("--name", dest="label", help=argparse.SUPPRESS)
    add.add_argument("--reuse", action="store_true", help="give a key that recordings already carry to this project")
    add.add_argument("--mode", help=f"one of {', '.join(MODES)} (default plan; bypassPermissions is refused)")
    _json_flag(add)
    add.set_defaults(fn=wrap(project_add))


def add_parsers(sub: Any, wrap: Callable) -> None:
    project = sub.add_parser("project", help="projects: named groups 1-9").add_subparsers(dest="action", required=True)
    _add_parser(project, wrap)
    for verb, value, kind, fn in (("rename", "name", str, project_rename), ("move", "slot", int, project_move)):
        cmd = project.add_parser(verb, help=EDIT_HELP[verb])
        cmd.add_argument("key")
        cmd.add_argument(value, type=kind)
        _json_flag(cmd)
        cmd.set_defaults(fn=wrap(fn))
    folder = project.add_parser("folder", help="link a code folder, or --none to unlink")
    folder.add_argument("key")
    which = folder.add_mutually_exclusive_group(required=True)
    which.add_argument("path", nargs="?")
    which.add_argument("--none", action="store_true")
    _json_flag(folder)
    folder.set_defaults(fn=wrap(project_folder))
    for verb in ("ls", "list"):
        ls = project.add_parser(verb)
        ls.add_argument("--json", action="store_true", help="protocol/filing/v1 projects: no local path")
        ls.set_defaults(fn=wrap(project_ls))
    rm = project.add_parser("rm", help="forget a project; its recordings keep the key")
    rm.add_argument("key")
    _json_flag(rm)
    rm.set_defaults(fn=wrap(project_rm))
