"""05 §9.3 items 2-4: quick note round trip, offline replay after `cpa pause`, a lost ack."""

from __future__ import annotations

from typing import Any

from harness import keys as k
from harness.notes import count_block
from harness.report import check, result
from harness.rig import until
from harness.wire import find
from scenarios.common import Ctx, items_received, note_appears, note_count, offline_run, token

NOTE_MAX_S = 2.0


async def capture(ctx: Ctx) -> dict[str, Any]:
    rig, dev = ctx.rig, ctx.dev
    text = f"hil capture {token(4)}"
    await dev.home()
    await dev.press(k.G0)
    opened = await dev.wait_state(lambda s: s["overlay"] == "compose", 2, what="Compose open")
    await dev.presses(k.text_presses(text))
    enter_at = await dev.presses((k.ENTER,))
    note, seen_at = await note_appears(rig, text, 10)
    synced = await dev.wait_state(lambda s: s["outbox"]["pending"] == 0, 10, what="outbox empty")
    elapsed = seen_at - enter_at
    checks = [
        check("compose_opened_by_g0", opened.msg["overlay"] == "compose"),
        check("note_within_2s", elapsed <= NOTE_MAX_S, value=round(elapsed, 3), limit=NOTE_MAX_S),
        check("note_written_once", note_count(rig, text) == 1 and count_block(rig.notes(), note.item_id) == 1),
        check("one_item_received_event", items_received(rig, note.item_id) == 1, id=note.item_id),
        check("device_outbox_empty", synced.msg["outbox"]["pending"] == 0),
    ]
    return result(checks, note_s=round(elapsed, 3), item_id=note.item_id)


async def offline_replay(ctx: Ctx) -> dict[str, Any]:
    rig, dev = ctx.rig, ctx.dev
    texts = [token(2) for _ in range(3)]
    while len(set(texts)) < 3:
        texts = [token(2) for _ in range(3)]
    before = len(rig.notes())
    run = await offline_run(ctx, tuple(p for t in texts for p in k.note_presses(t)))
    for text in texts:
        await note_appears(rig, text, 20)
    synced = await dev.wait_state(lambda s: s["outbox"]["pending"] == 0, 15, what="outbox empty")
    counts = {t: note_count(rig, t) for t in texts}
    checks = [
        check("preloaded_inject_queue", run["plan"] == k.INJECT_QUEUE_MAX, presses=run["plan"]),
        check("nothing_stored_while_paused", run["quiet"]),
        check("device_pending_3_at_reconnect", run["hello"]["pending"] == 3, pending=run["hello"]["pending"]),
        check("each_note_exactly_once", all(n == 1 for n in counts.values()), counts=counts),
        check("exactly_3_new_notes", len(rig.notes()) - before == 3, new=len(rig.notes()) - before),
        check("device_outbox_empty", synced.msg["outbox"]["pending"] == 0),
    ]
    return result(checks, texts=texts)


async def dup_ack(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    text = f"hil dup {token(4)}"
    await dev.home()
    mark = tap.size()
    tap.drop_next("outbox.ack", 1)
    await dev.presses(k.note_presses(text))
    i = await until(lambda: find(tap.since(mark), "rx", "outbox.item", where=lambda m: m["body"]["text"] == text),
                    10, what="outbox.item from the device")  # fmt: skip
    item_id = tap.since(mark)[i].msg["id"]

    def answered_duplicate() -> bool:
        recs = tap.since(mark)
        return find(recs, "tx", "outbox.ack", where=lambda m: m["re"] == item_id and m["status"] == "duplicate")

    await until(answered_duplicate, 20, what="duplicate ack for the resent item")
    synced = await dev.wait_state(lambda s: s["outbox"]["pending"] == 0, 10, what="outbox empty")
    recs = tap.since(mark)
    sends = sum(1 for r in recs if r.dir == "rx" and r.msg and r.msg.get("t") == "outbox.item"
                and r.msg.get("id") == item_id)  # fmt: skip
    dropped = [r.msg for r in tap.dropped if r.msg and r.msg.get("re") == item_id]
    checks = [
        check("first_ack_dropped", len(dropped) == 1 and dropped[0]["status"] == "stored", dropped=len(dropped)),
        check("device_resent_item", sends >= 2, sends=sends),
        check("resend_answered_duplicate", bool(answered_duplicate())),
        check("note_written_once", note_count(rig, text) == 1, count=note_count(rig, text)),
        check("one_item_received_event", items_received(rig, item_id) == 1, id=item_id),
        check("device_outbox_empty", synced.msg["outbox"]["pending"] == 0),
    ]
    return result(checks, item_id=item_id, device_sends=sends)
