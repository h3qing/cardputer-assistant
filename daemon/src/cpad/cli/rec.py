"""cpa rec ls [--json] [--hidden] | rec show <sid> [--json] | rec unhide <sid> (docs/11 §6.9, docs/13 §8):
ids, times, paths, states, ★ and project keys, never a title or any text.

Rows come from the running daemon ({"cmd":"rec"}: it also knows what is still on the device), or,
when cpad is not running, from events.jsonl and the status.json files on disk. Either way the star /
project / hidden edits in events.jsonl are applied on top, so an unhide shows at once, and rows come
newest first by start time. Hidden sessions (rec.hidden) are listed only with --hidden; `rec unhide`
appends rec.unhidden. Hiding never touches a file.
docs/23 §2.1: the project shown is the effective one, folded from events.jsonl here (filing.view), so a cpad
older than this cpa (its rows have no project key) or a hand from `cpa filing` read the same.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from collections.abc import Mapping
from datetime import datetime
from pathlib import Path
from typing import Any

from cpad.config.settings import Paths, load_config, secure_home
from cpad.control import client
from cpad.events.log import EventLog
from cpad.events.records import iso
from cpad.filing import view
from cpad.recordings import index, records, sessions
from cpad.recordings.results import offline_statuses

Row = Mapping[str, Any]


def offline_rows(paths: Paths, idx: index.RecIndex) -> tuple[dict[str, Any], ...]:
    return sessions.rows(idx, statuses=offline_statuses(load_config(paths.config).notes_path(), idx))


def offline_summary(paths: Paths) -> dict[str, Any]:
    idx = index.fold(EventLog(paths.events).load())
    found = sessions.rows(idx, statuses=offline_statuses(load_config(paths.config).notes_path(), idx))
    return sessions.summary(found, idx=idx, on_device=None, pulling=False)


def _daemon_rows(paths: Paths, idx: index.RecIndex) -> tuple[Row, ...]:
    try:
        doc = client.request(paths.sock, {"cmd": "rec"})
    except client.DaemonNotRunning:
        return offline_rows(paths, idx)
    return tuple(doc["sessions"]) if doc.get("ok") else offline_rows(paths, idx)


def rec_rows(paths: Paths, *, hidden: bool = True) -> tuple[Row, ...]:
    """Every session newest first with the edits of events.jsonl; hidden ones only when `hidden`."""
    book = view.load(paths)
    found = sessions.with_edits(_daemon_rows(paths, book.idx), book.idx, book.projects())
    return tuple(row for row in found if hidden or not row["hidden"])


def when(row: Row) -> str:
    if row.get("started_ms") is None:
        return "-"
    mark = "~" if row.get("uncertain") else ""
    return f"{mark}{datetime.fromtimestamp(row['started_ms'] / 1000):%Y-%m-%d %H:%M}"


def duration(row: Row) -> str:
    """Seconds under 10 minutes (memos are short), whole minutes above."""
    dur = row.get("dur_ms")
    if dur is None:
        return "-"
    s = (dur + 500) // 1000
    if s < 60:
        return f"{s}s"
    return f"{s // 60}m{s % 60:02d}s" if s < 600 else f"{(dur + 30000) // 60000}m"


def state(row: Row) -> str:
    return f"{row['state']}({row['reason']})" if row.get("reason") else str(row["state"])


def flags(row: Row) -> str:
    return " ".join(f for f, on in (("★", row.get("starred")), ("hidden", row.get("hidden"))) if on) or "-"


def _ls_line(row: Row) -> str:
    star = "★" if row.get("starred") else " "
    hidden = " (hidden)" if row.get("hidden") else ""
    return (f"{row['sid']:<19} {star} {row.get('mode') or '-':<8} {when(row):<17} {duration(row):>5}  "
            f"{row.get('project') or '-':<12} {state(row)}{hidden}")  # fmt: skip


def rec_ls(paths: Paths, args: argparse.Namespace) -> int:
    rows = rec_rows(paths, hidden=args.hidden)
    if args.json:
        print(json.dumps(list(rows), ensure_ascii=False))
        return 0
    print(f"{'SID':<19} ★ {'MODE':<8} {'STARTED':<17} {'DUR':>5}  {'PROJECT':<12} STATE")
    for row in rows:
        print(_ls_line(row))
    return 0


def _files(folder: Path) -> str:
    try:
        names = sorted(n for n in os.listdir(folder) if not n.startswith("."))
    except OSError:
        return "-"
    return " ".join(names) or "-"


def _show_lines(row: Row, notes_dir: Path) -> list[str]:
    folder = notes_dir / row["dir"] if row.get("dir") else None
    result = row.get("result") or {}
    gaps = ",".join(str(g) for g in row.get("gaps") or ()) or "-"
    return [
        f"session   {row['sid']}",
        f"mode      {row.get('mode') or '-'}",
        f"state     {state(row)}",
        f"project   {row.get('project') or '-'}",
        f"flags     {flags(row)}",
        f"started   {when(row)}",
        f"duration  {duration(row)}",
        f"segments  {row.get('segments')}  gaps {gaps}",
        f"dir       {folder or '-'}",
        f"files     {_files(folder) if folder else '-'}",
        f"result    {result.get('state') or '-'} {result.get('level') or '-'} error={result.get('error_code') or '-'}",
        f"audio     {'removed after 30 days' if row.get('audio_swept') else 'kept'}",
    ]


def rec_show(paths: Paths, args: argparse.Namespace) -> int:
    row = sessions.find(rec_rows(paths), args.sid)
    if row is None:
        print(f"error: no recording session {args.sid}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(row, ensure_ascii=False))
        return 0
    print("\n".join(_show_lines(row, load_config(paths.config).notes_path())))
    return 0


def rec_unhide(paths: Paths, args: argparse.Namespace) -> int:
    row = sessions.find(rec_rows(paths), args.sid)
    if row is None:
        print(f"error: no recording session {args.sid}", file=sys.stderr)
        return 1
    if not row.get("hidden"):
        print(f"{row['sid']} is not hidden")
        return 0
    secure_home(paths)
    EventLog(paths.events).append((records.unhidden(rec=row["sid"], at=iso(time.time())),))
    print(f"unhidden {row['sid']}")
    return 0


def add_parsers(sub: Any) -> None:
    rec = sub.add_parser("rec", help="recording sessions (ids, paths and states; never text)")
    actions = rec.add_subparsers(dest="action", required=True)
    ls = actions.add_parser("ls", help="newest first; ★ and project; hidden sessions only with --hidden")
    ls.add_argument("--json", action="store_true")
    ls.add_argument("--hidden", action="store_true", help="also list sessions hidden on the device")
    ls.set_defaults(fn=lambda paths, args, deps: rec_ls(paths, args))
    show = actions.add_parser("show")
    show.add_argument("sid")
    show.add_argument("--json", action="store_true")
    show.set_defaults(fn=lambda paths, args, deps: rec_show(paths, args))
    unhide = actions.add_parser("unhide", help="list a hidden session again (hiding never touched its files)")
    unhide.add_argument("sid")
    unhide.set_defaults(fn=lambda paths, args, deps: rec_unhide(paths, args))
