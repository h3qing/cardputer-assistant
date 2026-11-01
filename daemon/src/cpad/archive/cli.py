"""cpa export DEST [--zip] [--since D] [--until D] [--no-audio] [--allow-cloud] (docs/15 §2.4).

Prints counts and where to verify, never note or transcript text. Needs no daemon and no device.
"""

from __future__ import annotations

import argparse
import sys
from datetime import date
from pathlib import Path
from typing import Any

from cpad.archive import export, ids
from cpad.archive.fs import Root
from cpad.config.settings import Paths, load_config


def _date(text: str | None) -> date | None:
    return ids.parse_date(text) if text is not None else None


def run_export(paths: Paths, args: argparse.Namespace) -> int:
    options = export.ExportOptions(since=_date(args.since), until=_date(args.until), audio=not args.no_audio,
                                   allow_cloud=args.allow_cloud)  # fmt: skip
    root = Root.at(load_config(paths.config).notes_path())
    write = export.export_zip if args.zip else export.export_dir
    report = write(paths, root, Path(args.dest), options)
    print(f"exported {report.files} files ({report.bytes} bytes) to {report.target}: {report.notes} daily notes, "
          f"{report.recordings} recordings, {report.captures} captures, {report.queue} open queue items")  # fmt: skip
    if report.skipped:
        print(f"warning: skipped {report.skipped} files (unreadable or over the size cap)", file=sys.stderr)
    print("verify: shasum -a 256 -c SHA256SUMS (inside the export)")
    print("the export is plain text and audio: keep it as private as ~/CardputerNotes")
    return 0


def add_parsers(sub: Any) -> None:
    parser = sub.add_parser("export", help="copy notes and recordings to a folder or zip, with SHA256SUMS")
    parser.add_argument("dest", help="a new folder (or an empty one), or a new .zip file with --zip")
    parser.add_argument("--zip", action="store_true", help="write one zip file instead of a folder")
    parser.add_argument("--since", help="first day to include, YYYY-MM-DD")
    parser.add_argument("--until", help="last day to include, YYYY-MM-DD")
    parser.add_argument("--no-audio", action="store_true", help="leave out the WAV files")
    cloud = "allow a target that syncs online (iCloud Drive, Desktop/Documents kept by iCloud, cloud storage)"
    parser.add_argument("--allow-cloud", action="store_true", help=cloud)
    parser.set_defaults(fn=lambda paths, args, deps: run_export(paths, args))
