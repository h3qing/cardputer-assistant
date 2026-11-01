"""Outbox items end to end (05 §7.3, §7.8; US-03, US-12): durability, dedupe, orphans, notes; v0.4.1 todo."""

from __future__ import annotations

import asyncio
import json
import re

from tests.support.rig import note_item, queue_item, until

ID1, ID2, ID3 = "3QZK1M7-0000C-0003", "3QZK1M7-0000C-0004", "3QZK1M7-0000C-0005"


async def test_offline_intents_never_execute(rigs):
    rig = rigs()
    await rig.start()
    await rig.synced()
    spawn = await rig.request(queue_item(ID1, "spawn", text="Write native tests for the chord detector"))
    feedback = await rig.request(queue_item(ID2, "feedback", text="looks good", sid="5c2e8a14"))
    todo = await rig.request(queue_item(ID3, "todo"))
    assert [r["status"] for r in (spawn, feedback, todo)] == ["stored"] * 3
    added = [e for e in rig.events() if e["ev"] == "queue.added"]
    assert [(e["qid"], e["intent"], e["origin"]) for e in added] == [
        ("q1", "spawn", "device"), ("q2", "feedback", "device"), ("q3", "todo", "device"),
    ]  # fmt: skip
    await until(lambda: len(rig.fake.snapshots["queue"]["items"]) == 3)
    await asyncio.sleep(0.5)
    assert rig.claude.spawns() == [], "a queued intent is never executed"
    assert not any(e["ev"].startswith("agent.") for e in rig.events())


async def test_events_fsync_before_ack(rigs, monkeypatch):
    rig = rigs()
    order = []
    import os as os_module

    real_fsync = os_module.fsync

    def fsync(fd):
        real_fsync(fd)
        order.append(("fsync", rig.paths.events.read_text() if rig.paths.events.exists() else ""))

    def before_write(data: bytes):
        if b'"t":"outbox.ack"' in data:
            order.append(("ack", rig.paths.events.read_text()))

    monkeypatch.setattr(os_module, "fsync", fsync)
    rig.recorder.before_write = before_write
    await rig.start()
    await rig.synced()
    ack = await rig.request(note_item(ID1))
    assert ack["status"] == "stored"
    ack_index = next(i for i, (kind, _) in enumerate(order) if kind == "ack")
    assert ID1 in order[ack_index][1], "the event is on disk when the ack is written"
    assert any(kind == "fsync" and ID1 in text for kind, text in order[:ack_index]), "fsync happened before the ack"


async def test_replay_dedupe(rigs):
    rig = rigs()
    await rig.start()
    await rig.synced()
    first = await rig.request(note_item(ID1))
    rig.fake.acks.clear()
    again = await rig.request(note_item(ID1))
    assert (first["status"], again["status"]) == ("stored", "duplicate")
    await rig.restart()
    await rig.synced()
    rig.fake.acks.clear()
    after_restart = await rig.request(note_item(ID1))
    assert after_restart["status"] == "duplicate", "the (dev, id) index is rebuilt from events.jsonl"
    received = [e for e in rig.events() if e["ev"] == "item.received"]
    assert len(received) == 1
    notes = list(rig.notes_dir.glob("*.md"))
    assert sum(p.read_text().count("^cpa-3qzk1m7-0000c-0003") for p in notes) == 1


async def test_orphan_item_kept(rigs):
    rig = rigs()
    await rig.start()
    await rig.synced()
    note = await rig.request(note_item(ID1, text="for a deleted project", project="gone"))
    queued = await rig.request(queue_item(ID2, "todo", text="kept anyway", project="gone"))
    assert note["status"] == queued["status"] == "stored"
    received = [e for e in rig.events() if e["ev"] == "item.received"]
    assert all(e.get("orphan") is True and e["body"]["project"] == "gone" for e in received)
    (day,) = rig.notes_dir.glob("*.md")
    line = day.read_text().strip()
    assert "for a deleted project" in line and "#gone" not in line
    assert any(e["ev"] == "queue.added" and e["project"] == "gone" for e in rig.events())
    await asyncio.sleep(0.5)
    assert rig.fake.snapshots["queue"]["items"] == [], "an orphan queue item has no slot on the device"


async def test_notes_idempotent(rigs):
    rig = rigs()
    await rig.start()
    await rig.synced()
    await rig.request(note_item(ID1, text="first idea", wall_ms=1792440000000))
    await rig.request(note_item(ID2, text="no clock yet"))
    notes_dir = rig.notes_dir
    days = sorted(notes_dir.glob("*.md"))
    text = "".join(p.read_text() for p in days)
    assert text.count("^cpa-3qzk1m7-0000c-0003") == 1 and text.count("^cpa-3qzk1m7-0000c-0004") == 1
    assert "#cpa first idea" in text and "- ~" in text
    for day in days:
        day.unlink()
    await rig.restart()
    await until(lambda: len(list(notes_dir.glob("*.md"))) == len(days))
    await rig.restart()
    text = "".join(p.read_text() for p in sorted(notes_dir.glob("*.md")))
    assert text.count("^cpa-3qzk1m7-0000c-0003") == 1 and text.count("^cpa-3qzk1m7-0000c-0004") == 1


async def test_out_of_range_wall_clock_note_is_kept_and_survives_restart(rigs):
    """E14: wall_ms up to 2^53-1 is protocol-valid but past datetime's year 9999."""
    rig = rigs()
    await rig.start()
    await rig.synced()
    ack = await rig.request(note_item(ID1, text="far future", wall_ms=9007199254740991))
    assert ack["status"] == "stored"
    (day,) = rig.notes_dir.glob("*.md")
    assert re.match(r"- ~\d\d:\d\d #cpa far future \^cpa-3qzk1m7-0000c-0003$", day.read_text().strip())
    later = await rig.request(note_item(ID2, text="next one"), timeout=3)
    assert later["status"] == "stored" and rig.daemon.status()["link"] == "ready"
    await rig.restart()
    await rig.synced()
    assert rig.daemon.status()["items_stored"] == 2


async def test_rejected_item_gets_a_rejected_ack(rigs):
    rig = rigs()
    await rig.start()
    await rig.synced()
    bad = note_item(ID1, text="x")
    bad["kind"] = "spawn"
    ack = await rig.request(bad)
    assert ack["status"] == "rejected" and ack["code"] == "E_BAD_FIELD"
    assert not any(e["ev"] == "item.received" for e in rig.events())


async def test_queue_done_from_the_device(rigs):
    rig = rigs()
    await rig.start()
    await rig.synced()
    await rig.request(queue_item(ID1, "todo"))
    done = await rig.request({"t": "queue.done", "id": "d50", "qid": "q1"})
    missing = await rig.request({"t": "queue.done", "id": "d51", "qid": "q9"})
    assert done["t"] == "ack" and missing["code"] == "E_UNKNOWN_ITEM"
    await until(lambda: rig.fake.snapshots["queue"]["items"] == [])
    assert json.dumps(rig.events()).count('"ev": "queue.done"') == 1


def _with_dest(item: dict, dest: str) -> dict:
    return {**item, "body": {**item["body"], "dest": dest}}


async def test_a_todo_note_is_a_checkbox_and_an_unknown_dest_a_plain_line(rigs):
    """docs/13 §9.3, §11.1: dest todo -> '- [ ] ' line; no dest -> the v0.3 line; unknown -> plain + downgraded."""
    rig = rigs()
    await rig.start()
    await rig.synced()
    wall = 1792440045000
    todo = await rig.request(_with_dest(note_item(ID1, text="call the plumber", wall_ms=wall), "todo"))
    plain = await rig.request(note_item(ID2, text="plain idea", wall_ms=wall))
    later = await rig.request(_with_dest(note_item(ID3, text="for a later dest", wall_ms=wall), "ww.plugin.todoist"))
    assert [a["status"] for a in (todo, plain, later)] == ["stored"] * 3
    (day,) = rig.notes_dir.glob("*.md")
    text = day.read_text()
    assert re.search(r"^- \[ \] \d\d:\d\d #cpa call the plumber \^cpa-3qzk1m7-0000c-0003$", text, re.MULTILINE)
    assert re.search(r"^- \d\d:\d\d #cpa plain idea \^cpa-3qzk1m7-0000c-0004$", text, re.MULTILINE)
    assert re.search(r"^- \d\d:\d\d #cpa for a later dest \^cpa-3qzk1m7-0000c-0005$", text, re.MULTILINE)
    downgraded = [e for e in rig.events() if e["ev"] == "dest.downgraded"]
    assert [(e["item"], e["dest"]) for e in downgraded] == [(ID3, "ww.plugin.todoist")]
    assert "for a later dest" not in json.dumps(downgraded)
    day.unlink()
    await rig.restart()
    await until(lambda: day.exists() and day.read_text() == text)
