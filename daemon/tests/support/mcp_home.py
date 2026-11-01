"""A temporary $CPA_HOME and archive for the cpa mcp contract and integration tests.

The archive is tests.support.archive_tree; events.jsonl holds one typed note and one queue item, both
carrying CANARY so tests can prove the text never reaches the audit log or stderr.
"""

from __future__ import annotations

import json
from pathlib import Path

from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.events.log import EventLog
from tests.support import archive_tree

CANARY = "ZZ-CANARY-7f3a91-ZZ"
GOLDEN = Path(__file__).resolve().parents[1] / "fixtures" / "mcp"


def build(base: Path) -> Paths:
    """base/home ($CPA_HOME, 0700) and base/notes (the archive, with CANARY in a daily note and a summary)."""
    notes = archive_tree.build(base / "notes")
    note = notes / "2026-10-21.md"
    task = f"- [ ] 16:00 #cpa 别忘了 {CANARY} ^cpa-3qzk1m7-0000d-0002\n"
    note.write_text(note.read_text(encoding="utf-8") + task, encoding="utf-8")
    summary = notes / archive_tree.rec_dir("2026-10-21", "1402", archive_tree.MEETING) / "summary.md"
    summary.write_text(summary.read_text(encoding="utf-8") + f"\n备注：{CANARY}\n", encoding="utf-8")
    home = base / "home"
    home.mkdir(mode=0o700)
    project_root = base / "project"
    project_root.mkdir()
    write_json(home / "config.json", {"notes_dir": str(notes)})
    write_json(home / "projects.json", {"version": 1, "projects": [
        {"key": "cpa", "name": "Cardputer 助手", "slot": 1, "root": str(project_root)}]})  # fmt: skip
    EventLog(home / "events.jsonl").append((
        {"ev": "item.received", "at": "2026-10-21T07:00:00.000+00:00", "dev": "020000000001",
         "id": "3QZK1M7-0000D-0001", "kind": "note", "body": {"text": f"Pricing idea {CANARY}", "project": "cpa"},
         "at_dev": {"up_ms": 1, "wall_ms": None}, "received_at": "2026-10-21T07:00:00.000+00:00"},
        {"ev": "queue.added", "at": "2026-10-21T07:01:00.000+00:00", "qid": "q1", "project": "cpa",
         "text": f"修 bug {CANARY}", "intent": "todo", "sid": None, "origin": "device", "from_item": None,
         "uncertain": False},
    ))  # fmt: skip
    return Paths.at(home)


def golden(name: str) -> list[dict]:
    return [json.loads(line) for line in (GOLDEN / name).read_text(encoding="utf-8").splitlines()]


def schema(version: str) -> dict:
    return json.loads((GOLDEN / f"schema-{version}.json").read_text(encoding="utf-8"))
