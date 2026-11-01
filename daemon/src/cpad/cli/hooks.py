"""cpa hook (Claude Code runs it for each hook event) and cpa hooks print|install|uninstall (#33).

`cpa hook [EVENT] [--home DIR]` reads one hook input from stdin, keeps the allowlisted fields and sends them
to $CPA_HOME/cpad.sock within SEND_TIMEOUT_S. It always exits 0 and never prints: exit 2 would block the
prompt or the tool, and stdout can be read by Claude as context.
`cpa hooks install` only shows the change; it writes settings.json only with --yes.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any, BinaryIO

from cpad.agents.claude_code import hook_settings as hset
from cpad.agents.claude_code import hooks
from cpad.config.files import ensure_private_dir
from cpad.config.settings import Paths, cpa_home
from cpad.control import client

SEND_TIMEOUT_S = 0.5
CHUNK = 1 << 16


def _hook_args(argv: Sequence[str]) -> tuple[str | None, Path]:
    """[EVENT] [--home DIR]; anything else raises ValueError and the event is dropped."""
    args, home = list(argv), cpa_home()
    if "--home" in args:
        at = args.index("--home")
        if at + 1 >= len(args):
            raise ValueError("--home needs a directory")
        home, args = Path(args[at + 1]).expanduser(), args[:at] + args[at + 2 :]
    if len(args) > 1 or any(a.startswith("-") for a in args):
        raise ValueError("unexpected arguments")
    return (args[0] if args else None), home


def _read(stream: BinaryIO) -> bytes | None:
    """At most STDIN_MAX bytes; a bigger input is drained (so Claude's write never fails) and dropped."""
    data = stream.read(hooks.STDIN_MAX + 1)
    if len(data) <= hooks.STDIN_MAX:
        return data
    while stream.read(CHUNK):
        continue
    return None


def _forward(argv: Sequence[str], stream: BinaryIO) -> None:
    data = _read(stream)
    event, home = _hook_args(argv)
    payload = hooks.compact(json.loads(data), event) if data else None
    if payload is not None:
        client.request(Paths.at(home).sock, payload, timeout=SEND_TIMEOUT_S)


def hook_main(argv: Sequence[str], stdin: BinaryIO | None = None) -> int:
    with contextlib.suppress(Exception):
        _forward(argv, stdin if stdin is not None else sys.stdin.buffer)
    return 0


# --- cpa hooks ------------------------------------------------------------------------------


def _cpa(args: argparse.Namespace, deps: Any) -> str:
    return hset.check_cpa(args.cpa) if args.cpa else hset.find_cpa(sys.argv[0], lambda name: deps.which(name))


def _home() -> str | None:
    """--home for the hook only when CPA_HOME is set here: Claude sessions may not have it."""
    return os.path.abspath(cpa_home()) if os.environ.get("CPA_HOME") else None


def _target(args: argparse.Namespace) -> Path:
    return Path(args.settings).expanduser() if args.settings else hset.default_path()


def _backups(paths: Paths) -> Path:
    """$CPA_HOME/hook-backups, created private: never inside a project that git could commit."""
    ensure_private_dir(paths.home)
    return paths.home / hset.BACKUP_DIR


def _snippet_text(cpa: str) -> str:
    return json.dumps(hset.snippet(cpa, _home()), ensure_ascii=False, indent=2)


def hooks_print(paths: Paths, args: argparse.Namespace, deps: Any) -> int:
    print(_snippet_text(_cpa(args, deps)))
    return 0


def hooks_install(paths: Paths, args: argparse.Namespace, deps: Any) -> int:
    cpa, target = _cpa(args, deps), _target(args)
    loaded = hset.load(target)
    new = hset.install(loaded.doc, cpa, _home())
    if new == loaded.doc:
        print(f"cpa hooks are already installed in {target}")
        return 0
    if not args.yes:
        print(f"would add these hooks to {target}:\n{_snippet_text(cpa)}")
        backup = hset.backup_path(paths.home / hset.BACKUP_DIR, target)
        print(f"nothing written; run again with --yes (the old file is kept as {backup})")
        return 0
    backup = hset.save(loaded, new, _backups(paths))
    print(f"cpa hooks installed in {target}" + (f" (backup: {backup})" if backup else ""))
    print("Claude Code picks them up in open sessions; a terminal session runs hooks only in a trusted folder")
    return 0


def hooks_uninstall(paths: Paths, args: argparse.Namespace, deps: Any) -> int:
    target = _target(args)
    loaded = hset.load(target)
    new = hset.uninstall(loaded.doc)
    if new == loaded.doc:
        print(f"no cpa hooks in {target}")
        return 0
    if not args.yes:
        print(f"would remove the cpa hooks from {target}; nothing written, run again with --yes")
        return 0
    print(f"cpa hooks removed from {target} (backup: {hset.save(loaded, new, _backups(paths))})")
    return 0


ACTIONS = (("print", hooks_print, "show the settings.json snippet"),
           ("install", hooks_install, "add the hooks (writes only with --yes)"),
           ("uninstall", hooks_uninstall, "remove the cpa hooks (writes only with --yes)"))  # fmt: skip


def add_parsers(sub: Any) -> None:
    hook = sub.add_parser("hook", help="forward one Claude Code hook event to cpad (Claude Code runs this)")
    hook.set_defaults(fn=lambda paths, args, deps: hook_main([]))
    actions = sub.add_parser("hooks", help="Claude Code hooks in settings.json")
    actions = actions.add_subparsers(dest="action", required=True)
    for name, fn, text in ACTIONS:
        action = actions.add_parser(name, help=text)
        if name != "uninstall":
            action.add_argument("--cpa", help="absolute path of the cpa executable the hooks run")
        if name != "print":
            action.add_argument("--settings", help="settings file (default ~/.claude/settings.json)")
            action.add_argument("--yes", action="store_true", help="write the file")
        action.set_defaults(fn=fn)
