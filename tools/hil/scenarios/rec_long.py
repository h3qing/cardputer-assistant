"""docs/12 M3-H recording scenarios that reboot the device or run for minutes (test firmware, card in):

rec_reboot    debug.reboot (a) mid-segment: the open segment comes back as rcv:true and the session
              completes; (b) within the first second of a new segment (its 44 B header is not fsynced
              yet): the session ends last:false and is finalised with gaps [k+1] (or, if the card had
              the header, as (a)). Lost audio at (a) is reported against the 1 s budget (docs/11 §10).
rec_rotation  G-A4: seg_ms 10 s, 30 rotations; every full segment sample-exact, t0 contiguous, and the
              summed samples equal the device-clock wall time within one 2048-sample block.
rec_soak      G-A2: a 10-minute meeting (5-minute default segments, the 9.6 MB files of the SD fix)
              with a ping and a snapshot push every second: reply p95 <= 100 ms, none over 600 ms,
              0 E_NOT_READY, the link never reopened, no samples dropped (device-clock accounting).
Both read debug.state.rec's health counters once the meeting stopped (gh #21): all reported and
mic_missing == 0; ring_high, rot_ms_max and the heap minima of the whole app go into the report.
"""

from __future__ import annotations

import asyncio
import secrets
import time
from typing import Any

from harness.fakeclaude import tty_session
from harness.fakewoof import check_doc
from harness.rec_checks import contiguous_t0, health_checks, rec_health, timeline
from harness.report import check, result
from harness.rig import until
from harness.stats import summary
from harness.wire import exchanges, latencies, not_ready
from scenarios.common import Ctx
from scenarios.rec_common import (
    card_cleaned,
    end_meeting,
    known,
    landed,
    landed_checks,
    new_archives,
    rec_of,
    samples,
    seg_ids,
    set_seg_ms,
    settle,
    sleep_until,
    start_meeting,
    transfer,
    up0,
)

BLOCK = 2048  # samples per 4 KiB ring block (128 ms)
REBOOT_SEG_MS = 10000
ROT_SEG_MS = 10000
ROT_SEGMENTS = 30
SOAK_MIN = 10.0
SOAK_SEG_MS = 300000
P95_MS = 100.0
SLOW_MS = 600.0
LOST_MAX_MS = 1500  # 1 s fsync cadence (docs/11 §4.5) plus the block in flight


async def _reboot(ctx: Ctx) -> Any:
    rig, old = ctx.rig, ctx.rig.link.hello.boot
    await ctx.dev.reboot()
    link = await until(lambda: rig.link if rig.link and rig.link.hello.boot != old else None, 40, what="new boot")
    return link.hello


async def _reboot_and_reconnect(ctx: Ctx) -> dict[str, Any]:
    """debug.reboot; what the next boot came up with. A boot without the card (store != sd, no files) is
    a finding: it gets one more debug.reboot so that the session can still be recovered and checked."""
    hello = await _reboot(ctx)
    after = {"boot": hello.boot, "store": hello.store, "files": hello.files is not None}
    if hello.store == "sd" and hello.files is not None:
        return {**after, "second_boot_store": None}
    again = await _reboot(ctx)
    return {**after, "second_boot_store": again.store}


async def _mid_segment(ctx: Ctx) -> dict[str, Any]:
    """(a): reboot 5 s into segment 1."""
    rig, dev = ctx.rig, ctx.dev
    await settle(dev)
    await set_seg_ms(dev, REBOOT_SEG_MS)
    before = known(rig)
    await start_meeting(dev)
    await dev.wait_state(lambda s: rec_of(s).get("seg") == 1, 20, what="segment 1")
    await asyncio.sleep(5.0)
    before_reboot = (await dev.state()).msg["up_ms"]
    after = await _reboot_and_reconnect(ctx)
    (session,) = await new_archives(rig, before, ("meeting",), 90)
    got = landed(rig, session)
    segs = got.manifest["segments"]
    lost = before_reboot - ((up0(rig, session["rec"]) or 0) + samples(got.manifest) // 16)
    return {
        "session": session,
        "landed": got,
        "after": after,
        "lost_ms": lost,
        "recovered": [s["i"] for s in segs if s["recovered"]],
        "repaired": [s["i"] for s in segs if s["repaired"]],
    }


async def _after_rotation(ctx: Ctx) -> dict[str, Any]:
    """(b): reboot as soon as debug.state shows segment 1 (its first fsync is 1 s away)."""
    rig, dev = ctx.rig, ctx.dev
    await settle(dev)
    await set_seg_ms(dev, REBOOT_SEG_MS)
    before = known(rig)
    await start_meeting(dev)
    await dev.wait_state(lambda s: rec_of(s).get("seg") == 1, 20, interval=0.02, what="segment 1")
    after = await _reboot_and_reconnect(ctx)
    relisted = False
    try:
        (session,) = await new_archives(rig, before, ("meeting",), 45)
    except TimeoutError:  # finalising with gaps needs a full listing that shows none of the session
        relisted = True
        await rig.pause(2)
        await rig.released()
        await rig.ready(timeout=30)
        (session,) = await new_archives(rig, before, ("meeting",), 45)
    got = landed(rig, session)
    gap = got.manifest["gaps"] == [1] and [s["i"] for s in got.manifest["segments"]] == [0]
    return {"session": session, "landed": got, "after": after, "trailing_gap": gap, "needed_relist": relisted}


def _mounted(prefix: str, after: dict[str, Any]) -> dict[str, Any]:
    return check(f"{prefix}card_mounted_after_reboot", after["store"] == "sd" and after["files"], **after)


async def rec_reboot(ctx: Ctx) -> dict[str, Any]:
    a = await _mid_segment(ctx)
    b = await _after_rotation(ctx)
    ma, mb = a["landed"].manifest, b["landed"].manifest
    checks = [
        _mounted("mid_", a["after"]),
        *landed_checks(ctx.rig, a["session"], "mid_"),
        check("mid_rcv_segment_landed", a["recovered"] == [1], recovered=a["recovered"], repaired=a["repaired"]),
        check("mid_session_complete", ma["gaps"] == [] and len(ma["segments"]) == 2, gaps=ma["gaps"]),
        check("mid_consent_kept", ma.get("consent") == "ack"),
        check("mid_lost_le_1_5s", -BLOCK // 16 <= a["lost_ms"] <= LOST_MAX_MS, lost_ms=a["lost_ms"]),
        _mounted("rot_", b["after"]),
        *landed_checks(ctx.rig, b["session"], "rot_"),
        check(
            "rot_session_finalised",
            check_doc("manifest", mb) == ("ok", ()),
            gaps=mb["gaps"],
            segments=[s["i"] for s in mb["segments"]],
            recovered=[s["i"] for s in mb["segments"] if s["recovered"]],
        ),
        *(await card_cleaned(ctx, seg_ids(ma) + seg_ids(mb))),
    ]
    return result(
        checks,
        mid_sid=a["session"]["rec"],
        mid_lost_ms=a["lost_ms"],
        rot_sid=b["session"]["rec"],
        rot_trailing_gap=b["trailing_gap"],
        rot_needed_relist=b["needed_relist"],
        rot_gaps=mb["gaps"],
    )


async def rec_rotation(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    await settle(dev)
    await set_seg_ms(dev, ROT_SEG_MS)
    before, mark = known(rig), tap.size()
    start_key = await start_meeting(dev)
    t0 = time.monotonic()
    await sleep_until(t0 + ROT_SEGMENTS * ROT_SEG_MS / 1000 + 5)
    stop_key = await end_meeting(dev, timeout=30)
    health = rec_health(rec_of((await dev.state()).msg))  # the meeting's, until the next recording
    (session,) = await new_archives(rig, before, ("meeting",), 900)
    got = landed(rig, session)
    m = got.manifest
    full = [s["samples"] for s in m["segments"][:-1]]
    tl = timeline(
        start_key_ms=start_key, up0_ms=up0(rig, session["rec"]) or 0, stop_key_ms=stop_key, samples=samples(m)
    )
    checks = [
        *landed_checks(rig, session),
        check("thirty_rotations", len(m["segments"]) == ROT_SEGMENTS + 1, segments=len(m["segments"])),
        check("full_segments_sample_exact", set(full) == {ROT_SEG_MS * 16}, distinct=sorted(set(full))),
        check("t0_contiguous_no_gaps", contiguous_t0(m), gaps=m["gaps"]),
        check("samples_eq_wall_within_1_block", tl.within(BLOCK), diff_samples=tl.diff_samples, wall_ms=tl.wall_ms),
        *health_checks(health),
        *(await card_cleaned(ctx, seg_ids(m))),
    ]
    return result(
        checks,
        sid=session["rec"],
        segments=len(m["segments"]),
        wall_ms=tl.wall_ms,
        samples=samples(m),
        diff_samples=tl.diff_samples,
        diff_ms=round(tl.diff_samples / 16, 1),
        start_latency_ms=tl.start_latency_ms,
        health=health,
        transfer=transfer(tap.since(mark), seg_ids(m)),
    )


async def _ticker(ctx: Ctx, end: float) -> int:
    """A ping and a sessions.snapshot push (fake-claude flips, poll_s 1) every second."""
    rig, sid, n = ctx.rig, secrets.token_hex(4), 0
    while time.monotonic() < end:
        tick = time.monotonic()
        rig.fake.set_sessions([tty_session(sid, "录音浸泡", str(rig.project_root), "waiting" if n % 2 else "busy")])
        if rig.link is not None:
            rig.link.request_raw({"t": "ping"})
        n += 1
        await asyncio.sleep(max(0.0, tick + 1.0 - time.monotonic()))
    return n


def _statuses_live(recs) -> list[bool]:
    return [r.msg["files"]["live"] for r in recs if r.dir == "rx" and r.msg and isinstance(r.msg.get("files"), dict)]


def _link_checks(recs, t_end: float) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """G-A2 on the requests sent while recording (replies may come after t_end)."""
    ex = [e for e in exchanges(recs) if e.sent_at <= t_end]
    window = [r for r in recs if r.t <= t_end]
    lat = [x * 1000 for x in latencies(ex)]
    stats, slow = summary(lat, 1), sum(1 for x in lat if x > SLOW_MS)
    unanswered = sum(1 for e in ex if e.reply_at is None)
    lives = _statuses_live(window)
    checks = [
        check("reply_p95_le_100ms", stats.get("p95", 1e9) <= P95_MS, reply_ms=stats),
        check("no_reply_over_600ms", slow == 0, slow=slow),
        check("every_request_answered", unanswered == 0, unanswered=unanswered),
        check("no_e_not_ready", not_ready(window) == 0, e_not_ready=not_ready(window)),
        check("link_never_reopened", not any(r.dir == "open" for r in window)),
        check("stayed_live", all(lives) and len(lives) >= SOAK_MIN * 60 / 5 - 2, pongs=len(lives)),
    ]
    return checks, {"requests": len(ex), "reply_ms": stats, "snapshots": sum(e.t.endswith(".snapshot") for e in ex)}


async def rec_soak(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    await settle(dev)
    await set_seg_ms(dev, SOAK_SEG_MS)
    rig.set_config(poll_s=1.0)
    await until(lambda: rig.daemon.config.poll_s == 1.0, 10, what="poll_s 1 reloaded by the daemon")
    before = known(rig)
    start_key = await start_meeting(dev)
    mark, t0 = tap.size(), time.monotonic()
    ticks = await _ticker(ctx, t0 + SOAK_MIN * 60)
    t_end = time.monotonic()
    stop_key = await end_meeting(dev, timeout=30)
    health = rec_health(rec_of((await dev.state()).msg))  # the meeting's, until the next recording
    rig.set_config(poll_s=3.0)
    (session,) = await new_archives(rig, before, ("meeting",), 900)
    m = landed(rig, session).manifest
    link_checks, link = _link_checks(tap.since(mark), t_end)
    tl = timeline(
        start_key_ms=start_key, up0_ms=up0(rig, session["rec"]) or 0, stop_key_ms=stop_key, samples=samples(m)
    )
    checks = [
        *link_checks,
        check("no_dropped_samples", tl.within(BLOCK), diff_samples=tl.diff_samples, wall_ms=tl.wall_ms),
        check("t0_contiguous_no_gaps", contiguous_t0(m), gaps=m["gaps"]),
        *health_checks(health),
        *landed_checks(rig, session),
        *(await card_cleaned(ctx, seg_ids(m))),
    ]
    return result(
        checks,
        minutes=SOAK_MIN,
        ticks=ticks,
        **link,
        segments=len(m["segments"]),
        diff_samples=tl.diff_samples,
        health=health,
        transfer=transfer(tap.since(mark), seg_ids(m)),
    )
