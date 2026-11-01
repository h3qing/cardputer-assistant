"""`cpa setup check [--json | --quiet]` (docs/27 D3, §2.6).

--json: one protocol/setup/v1 row per line (JSON Lines), exit 0. --quiet: prints nothing, exit 0 when every required
row is ok, else 1. --todo: only the rows left, in the language of cpad.i18n (the installer's last screen), exit 0.
Neither: the rows as a table for a terminal, exit 0. Read-only.
"""

from __future__ import annotations

import argparse
import json
from typing import Any

from cpad.config.settings import Paths
from cpad.setup import facts, rows


def check(paths: Paths, args: argparse.Namespace, deps: facts.SetupDeps) -> int:
    out = rows.rows(facts.gather(paths, deps))
    if args.quiet:
        return 0 if rows.all_required_ok(out) else 1
    if args.json:
        print("\n".join(json.dumps(r, ensure_ascii=False) for r in out))
    elif args.todo:
        print(rows.todo(out))
    else:
        print(rows.table(out))
    return 0


def add_parsers(sub: Any, run: Any) -> None:
    parser = sub.add_parser("setup", help="what this Mac still needs (docs/27)")
    setup = parser.add_subparsers(dest="action", required=True)
    chk = setup.add_parser("check", help="the setup rows: service, notes, name, device, disk, stt, hooks, claude")
    how = chk.add_mutually_exclusive_group()
    how.add_argument("--json", action="store_true", help="protocol/setup/v1 rows, one per line")
    how.add_argument("--quiet", action="store_true", help="no output; exit 0 when every required row is ok")
    how.add_argument("--todo", action="store_true", help="only the rows left to do")
    chk.set_defaults(fn=run)
