"""09 §9.3: the outbox on the SD card (sd_empty, sd_store, sd_bulk) and the internal -> SD migration
across three flashes (migrate_seed on nosd, migrate_check on test, migrate_drained on nosd again).

The device takes debug.key only while Ready and queues at most 16 presses, so tens of notes cannot
be typed while it is really offline. "Offline" here is the tap's hold_rx("outbox.item"): the device
stays Ready, but the daemon never hears an item, so every note stays pending on the device's store
until the next handshake (a reboot or a reopened port), which also puts in-flight items back to
pending. Notes are short numbered texts (harness.keys.numbered_texts).
"""

from __future__ import annotations

import time
from dataclasses import replace
from typing import Any

from harness import keys as k
from harness.expect import Delivery, delivery, store_of
from harness.report import check, result
from harness.rig import until
from harness.state import Seed, load_state, save_state
from harness.wire import handshakes
from scenarios.common import Ctx, events, first_hello, token

SD_STORE_N = 3
BULK_N = 100
BULK_DRAIN_S = 5.0
MIGRATE_N = 20
EMPTY_WAIT_S = 60.0


def received_ids(ctx: Ctx, since: int) -> list[str]:
    """item.received ids in events.jsonl, skipping the first `since` item.received events."""
    return [e["id"] for e in events(ctx.rig, "item.received")[since:]]


def missing(ctx: Ctx, texts: tuple[str, ...]) -> list[str]:
    noted = {n.text for n in ctx.rig.notes()}
    return [t for t in texts if t not in noted]


async def type_unheard(ctx: Ctx, texts: tuple[str, ...]) -> int:
    """Type the notes while the daemon hears none of them; returns debug.state pending afterwards."""
    ctx.rig.tap.hold_rx("outbox.item")
    await ctx.dev.home()
    await ctx.dev.presses(tuple(p for t in texts for p in k.note_presses(t)))

    def stored(s: dict[str, Any]) -> bool:
        return s["outbox"]["pending"] >= len(texts)

    return (await ctx.dev.wait_state(stored, 20, what=f"{len(texts)} notes pending")).msg["outbox"]["pending"]


async def outbox_empty(ctx: Ctx, timeout: float = 15.0) -> bool:
    sample = await ctx.dev.wait_state(lambda s: s["outbox"]["pending"] == 0, timeout, what="outbox empty")
    return sample.msg["outbox"]["pending"] == 0


def delivery_checks(d: Delivery) -> list[dict[str, Any]]:
    return [
        check(
            "each_note_exactly_once", not d.missing and not d.doubled, missing=list(d.missing), doubled=list(d.doubled)
        ),
        check(
            "no_other_or_repeated_ids",
            not d.extra_ids and not d.received_twice,
            extra=list(d.extra_ids),
            twice=list(d.received_twice),
        ),
    ]


async def sd_empty(ctx: Ctx) -> dict[str, Any]:
    hello = ctx.rig.link.hello
    empty = await outbox_empty(ctx, EMPTY_WAIT_S)
    checks = [
        check("hello_store_sd", store_of(hello) == "sd", store=store_of(hello)),
        check("device_outbox_empty", empty, pending_at_hello=hello.pending),
    ]
    return result(checks, store=store_of(hello), launcher=hello.launcher, pending_at_hello=hello.pending)


async def sd_store(ctx: Ctx) -> dict[str, Any]:
    rig, dev = ctx.rig, ctx.dev
    texts = k.numbered_texts("s" + token(2), SD_STORE_N)
    before, ev0 = rig.link.hello, len(events(rig, "item.received"))
    pending = await type_unheard(ctx, texts)
    unheard = missing(ctx, texts) == list(texts)
    await dev.reboot()
    link = await until(
        lambda: rig.link if rig.link and rig.link.hello.boot != before.boot else None,
        30,
        what="handshake with a new boot id",
    )
    await until(lambda: not missing(ctx, texts), 20, what="the 3 notes in notes_dir")
    empty = await outbox_empty(ctx)
    d = delivery(texts, rig.notes(), received_ids(ctx, ev0))
    checks = [
        check("hello_store_sd", store_of(before) == "sd", store=store_of(before)),
        check("pending_3_before_reboot", pending == SD_STORE_N, pending=pending),
        check("nothing_heard_before_reboot", unheard),
        check(
            "rebooted_on_sd",
            link.hello.boot != before.boot and store_of(link.hello) == "sd",
            boot=link.hello.boot,
            store=store_of(link.hello),
        ),
        check("hello_pending_3_after_reboot", link.hello.pending == SD_STORE_N, pending=link.hello.pending),
        *delivery_checks(d),
        check("device_outbox_empty", empty),
    ]
    return result(checks, texts=list(texts), ids=list(d.ids))


def _drain_s(ctx: Ctx, mark: int, ids: tuple[str, ...]) -> float | None:
    """Host time from the reopened link's device hello to the last `stored` ack of ids (tap)."""
    recs = ctx.rig.tap.since(mark)
    ready = handshakes(recs)[-1].ready_at
    acks = [
        r.t
        for r in recs
        if r.dir == "tx"
        and r.msg
        and r.msg.get("t") == "outbox.ack"
        and r.msg.get("re") in ids
        and r.msg.get("status") == "stored"
    ]
    return None if ready is None or len(acks) < len(ids) else max(acks) - ready


async def sd_bulk(ctx: Ctx) -> dict[str, Any]:
    rig = ctx.rig
    texts = k.numbered_texts(token(1), BULK_N)
    store, ev0 = store_of(rig.link.hello), len(events(rig, "item.received"))
    typed = time.monotonic()
    pending = await type_unheard(ctx, texts)
    typing_s = time.monotonic() - typed
    unheard = missing(ctx, texts) == list(texts)
    mark = rig.tap.size()
    await rig.pause(2)
    await rig.released()
    await rig.resume()
    link = await rig.ready(timeout=20)
    await until(lambda: not missing(ctx, texts), 60, 0.05, what="all 100 notes in notes_dir")
    empty = await outbox_empty(ctx)
    d = delivery(texts, rig.notes(), received_ids(ctx, ev0))
    drain = _drain_s(ctx, mark, d.ids)
    checks = [
        check("hello_store_sd", store == "sd", store=store),
        check("pending_100_offline", pending == BULK_N, pending=pending),
        check("nothing_heard_offline", unheard),
        check("hello_pending_100", link.hello.pending == BULK_N, pending=link.hello.pending),
        check(
            "all_delivered_within_5s",
            drain is not None and drain <= BULK_DRAIN_S,
            value=None if drain is None else round(drain, 3),
            limit=BULK_DRAIN_S,
        ),
        *delivery_checks(d),
        check("device_outbox_empty", empty),
    ]
    return result(checks, drain_s=None if drain is None else round(drain, 3), typing_s=round(typing_s, 1))


async def migrate_seed(ctx: Ctx) -> dict[str, Any]:
    """nosd firmware: plant MIGRATE_N notes in cpafs and leave them there (the hold outlives the run)."""
    rig, path = ctx.rig, ctx.opts.state
    hello = rig.link.hello
    pending0 = (await ctx.dev.state()).msg["outbox"]["pending"]
    texts = k.numbered_texts("m" + token(1), MIGRATE_N)
    pending = await type_unheard(ctx, texts)
    seed = Seed(dev=hello.dev, boot=hello.boot, texts=texts)
    save_state(path, seed)
    checks = [
        check("hello_store_int", store_of(hello) == "int", store=store_of(hello)),
        check("source_empty_before", pending0 == 0, pending=pending0),
        check("seeded_pending", pending == MIGRATE_N, pending=pending),
        check("nothing_delivered", missing(ctx, texts) == list(texts) and not events(rig, "item.received")),
        check("state_written", load_state(path) == seed, state=str(path)),
    ]
    return result(checks, n=MIGRATE_N, boot=hello.boot, texts=list(texts))


async def migrate_check(ctx: Ctx) -> dict[str, Any]:
    """test firmware (SD): the boot moved the seed to the card; each seeded note arrives exactly once."""
    rig, path = ctx.rig, ctx.opts.state
    seed, hello = load_state(path), first_hello(ctx)
    await until(lambda: not missing(ctx, seed.texts), 30, what="every seeded note in notes_dir")
    empty = await outbox_empty(ctx)
    d = delivery(seed.texts, rig.notes(), received_ids(ctx, 0))
    from_seed = bool(d.ids) and all(i.startswith(seed.boot.upper() + "-") for i in d.ids)
    checks = [
        check("hello_store_sd", store_of(hello) == "sd", store=store_of(hello)),
        check("same_device", hello.get("dev") == seed.dev, dev=hello.get("dev")),
        check("hello_pending_n", hello.get("pending") == len(seed.texts), pending=hello.get("pending")),
        *delivery_checks(d),
        check("ids_from_the_seed_boot", from_seed, boot=seed.boot),
        check("device_outbox_empty", empty),
    ]
    res = result(checks, n=len(seed.texts), ids=list(d.ids))
    save_state(path, replace(seed, ids=d.ids, checked=res["ok"]))
    return res


async def migrate_drained(ctx: Ctx) -> dict[str, Any]:
    """nosd firmware again: the migration emptied cpafs, so nothing is left to send."""
    rig = ctx.rig
    seed, hello = load_state(ctx.opts.state), first_hello(ctx)
    sample = await ctx.dev.state()
    checks = [
        check("hello_store_int", store_of(hello) == "int", store=store_of(hello)),
        check("same_device", hello.get("dev") == seed.dev, dev=hello.get("dev")),
        check("hello_pending_0", hello.get("pending") == 0, pending=hello.get("pending")),
        check("device_outbox_empty", sample.msg["outbox"]["pending"] == 0, pending=sample.msg["outbox"]["pending"]),
        check("nothing_delivered", not events(rig, "item.received")),
    ]
    return result(checks, migrate_check_passed=seed.checked)
