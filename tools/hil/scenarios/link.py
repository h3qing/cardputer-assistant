"""05 §9.3 items 1 and 5: handshake without a reset, and debug.reboot -> resync."""

from __future__ import annotations

import time
from typing import Any

from harness.report import check, result
from harness.rig import until
from harness.wire import find, handshakes
from scenarios.common import Ctx, host_revs, revs_synced

HANDSHAKE_MAX_S = 3.0
UP_MS_DRIFT_MAX = 1000  # device up_ms vs host clock between two hellos


def _hello_time(recs, hello: dict[str, Any]) -> float:
    return next(r.t for r in recs if r.dir == "rx" and r.msg == hello)


async def handshake(ctx: Ctx) -> dict[str, Any]:
    rig, tap = ctx.rig, ctx.rig.tap
    first = handshakes(tap.since())[-1]
    reboots0 = rig.daemon.status()["reboots"]  # an earlier scenario (sd_store, reboot) may have rebooted it
    mark = tap.size()
    await rig.pause(2)
    await rig.released()
    await rig.ready(timeout=20)
    recs = tap.since()
    second = handshakes(tap.since(mark))[-1]
    h1, h2 = first.hello, second.hello
    host_gap_ms = (_hello_time(recs, h2) - _hello_time(recs, h1)) * 1000
    drift = (h2["up_ms"] - h1["up_ms"]) - host_gap_ms
    status = rig.daemon.status()
    checks = [
        check(
            "first_handshake_s",
            first.seconds <= HANDSHAKE_MAX_S,
            value=round(first.seconds, 3),
            limit=HANDSHAKE_MAX_S,
            attempts=first.attempts,
        ),  # fmt: skip
        check(
            "reopen_handshake_s",
            second.seconds <= HANDSHAKE_MAX_S,
            value=round(second.seconds, 3),
            limit=HANDSHAKE_MAX_S,
            attempts=second.attempts,
        ),  # fmt: skip
        check("same_boot_after_reopen", h1["boot"] == h2["boot"], before=h1["boot"], after=h2["boot"]),
        check(
            "up_ms_continuous",
            h2["up_ms"] > h1["up_ms"] and abs(drift) <= UP_MS_DRIFT_MAX,
            up_ms_before=h1["up_ms"],
            up_ms_after=h2["up_ms"],
            drift_ms=round(drift),
        ),  # fmt: skip
        check("no_reboot_detected", status["reboots"] == reboots0, reboots=status["reboots"], before=reboots0),
        check("released_for_pause", status["last_release"] == "paused", last_release=status["last_release"]),
    ]
    return result(checks, fw=h2.get("fw"), rst=h2.get("rst"), boot=h2["boot"], store=h2.get("store"),
                  launcher=h2.get("launcher"))  # fmt: skip


async def reboot(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    old = rig.link.hello
    reboots0 = rig.daemon.status()["reboots"]
    await dev.reboot()
    asked = time.monotonic()
    link = await until(lambda: rig.link if rig.link and rig.link.hello.boot != old.boot else None, 30,
                       what="handshake with a new boot id")  # fmt: skip
    ready = time.monotonic()
    new = link.hello
    synced = await dev.wait_state(revs_synced(rig), 15, what="device revs equal host revs")
    recs = tap.since()
    hello_i = find(recs, "rx", "hello", where=lambda m: m.get("boot") == new.boot)
    resent = sorted({r.msg["t"] for r in recs[hello_i:] if r.dir == "tx" and r.msg and r.msg.get("t") in
                     ("time.set", "projects.snapshot", "sessions.snapshot", "queue.snapshot")})  # fmt: skip
    reboots = rig.daemon.status()["reboots"]
    checks = [
        check("new_boot_id", new.boot != old.boot, before=old.boot, after=new.boot),
        check("fresh_up_ms", new.up_ms < 30000, up_ms=new.up_ms, rst=new.rst),
        check("daemon_detected_reboot", reboots == reboots0 + 1, reboots=reboots),
        check("time_and_snapshots_resent", len(resent) == 4, resent=resent),
        check("device_revs_match_host", synced.msg["revs"] == host_revs(rig), revs=synced.msg["revs"]),
        check("board_restored", any(p.get("slot") == 1 for p in synced.msg["projects"])),
    ]
    return result(checks, reconnect_s=round(ready - asked, 2), resync_s=round(synced.recv_at - ready, 2))
