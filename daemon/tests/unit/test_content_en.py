"""What cpad and cpa-stt write or send, in English (the default, cpad.i18n): the board's Voice· prefix, a meeting's
default title, the header another app receives, the builtin destination labels, the MCP truncation markers, the
nothing-heard transcript and the export README. The Chinese ones are pinned by each module's own suite."""

from __future__ import annotations

import json
import re
from dataclasses import replace

import pytest

from cpa_stt import contract_out
from cpad.archive import export
from cpad.dest import catalog, handoff
from cpad.dest import snapshot as s
from cpad.dest.config import load_dests, save_dests
from cpad.dest.model import Capture
from cpad.events import records
from cpad.hub import snapshots
from cpad.mcp import resources
from cpad.protocol import codec
from cpad.protocol.messages import to_wire
from tests.support.fake_reader import FakeReader, fake_status
from tests.unit.test_dest_snapshot import SCHEMA
from tests.unit.test_rec_history import _titled
from tests.unit.test_snapshots import AT, _q, _state, _valid


def has_han(text: str) -> bool:
    return any("一" <= ch <= "鿿" for ch in text)


def test_a_handed_item_reads_voice_on_the_board_within_80_bytes():
    handed = records.queue_handed(qid="q2", project="cpa", text="long " * 30, intent="todo", rec="3QZK1M7-0000A-0001",
                                  at=AT)  # fmt: skip
    _, snaps, _ = snapshots.publish(_state(queue_events=[_q("q1"), handed]), now=1.0)
    voice, typed = snaps.queue.items
    assert voice.text.startswith("Voice·long long") and len(voice.text.encode()) <= 80
    assert typed.text == "todo item" and _valid(snaps.queue)


def test_a_meeting_without_a_title_is_called_by_its_length():
    assert _titled(title=None).title == "Meeting 42m"
    assert _titled(title=" \n\t ", dur_ms=70000).title == "Meeting 1m"


@pytest.mark.parametrize(
    ("source", "project", "head"),
    [("voice", "acmeshop", "[Cardputer voice·may contain misheard words or other people's speech·#acmeshop] "),
     ("typed", None, "[Cardputer typed] ")],
)  # fmt: skip
def test_the_header_another_app_receives(source, project, head):
    assert handoff.header(source, project) == head
    assert handoff.handed_text(Capture("r", source, "url.grok", "pick", project, "x\ny")) == head + "x y"


def test_builtin_labels_without_destinations_json():
    labels = {d.id: d.label for d in catalog.defaults().dests}
    assert labels == {"daily": "Today's note", "todo": "To-do", "queue": "Project queue", "session": "New session",
                      "clip": "Clipboard", "paste": "Insertion point"}  # fmt: skip
    assert all(len(label.encode()) <= 24 for label in labels.values())


def test_the_english_defaults_are_a_valid_snapshot_on_one_line():
    label = json.loads(SCHEMA.read_text(encoding="utf-8"))["$defs"]["label"]
    msg = to_wire(replace(s.dests_snapshot(catalog.defaults()), id="h12"))
    assert [d["label"] for d in msg["dests"]][:2] == ["Today's note", "To-do"]
    assert all(re.fullmatch(label["pattern"], d["label"]) for d in msg["dests"])
    assert codec.decode_line(codec.encode(msg)).ok


def test_a_label_once_written_stays_in_its_language(tmp_path, monkeypatch):
    monkeypatch.setenv("CPA_LANG", "zh")
    path = tmp_path / "destinations.json"
    save_dests(path, catalog.defaults())
    monkeypatch.setenv("CPA_LANG", "en")
    assert {d.id: d.label for d in load_dests(path).dests}["daily"] == "今日笔记"
    assert {d.id: d.label for d in catalog.defaults().dests}["daily"] == "Today's note"


def test_mcp_truncation_markers():
    reader = FakeReader()
    reader.notes["2026-10-19"] = "".join("x" * 300 + "\n" for _ in range(600))
    ctx = resources.Ctx(reader=reader, status=fake_status)
    text = resources.read(ctx, "cpa://notes/2026-10-19")["contents"][0]["text"]
    assert text.rstrip().endswith("]") and "[truncated: go on with read_daily_note at offset " in text
    assert resources.SUMMARY_CUT[1] == "\n\n[truncated: the summary is over the limit of one read]\n"


def test_nothing_heard():
    assert contract_out.transcript_md(()) == "(nothing heard)\n"


def test_the_export_readme_says_the_same_in_english():
    zh, en = export.README
    files = ("notes/YYYY-MM-DD.md", "data/captures.jsonl", "data/queue.jsonl", "data/projects.json",
             "data/filing.jsonl", "CPA-EXPORT.json", "SHA256SUMS", "shasum -a 256 -c SHA256SUMS")  # fmt: skip
    assert all(name in zh and name in en for name in files)
    assert zh.count("\n- ") == en.count("\n- ") and not has_han(en) and en.startswith("# Cardputer Assistant export")
