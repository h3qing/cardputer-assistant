"""05 §9.3 items 6-8: board counts from fake-claude, spawn argv, offline spawn becomes a queue item."""

from __future__ import annotations

import asyncio
import secrets
import time
from typing import Any

from cpad.agents.claude_code.argv import session_name

from harness import keys as k
from harness.device import slot_row
from harness.fakeclaude import bg_session, tty_session
from harness.report import check, result
from harness.rig import until
from harness.wire import find
from scenarios.common import SLOT, Ctx, events, offline_run, revs_synced, token

BOARD_MAX_S = 5.0


async def board(ctx: Ctx) -> dict[str, Any]:
    rig, dev = ctx.rig, ctx.dev
    root = str(rig.project_root)
    rig.fake.set_sessions([
        bg_session(secrets.token_hex(4), "cpa/看板一", root, "busy", "running"),
        tty_session(secrets.token_hex(4), "看板二", root, "waiting"),
    ])  # fmt: skip
    written = time.monotonic()
    want = {"slot": SLOT, "run": 1, "need": 1}

    def shown(s) -> bool:
        row = slot_row(s, SLOT) or {}
        return {key: row.get(key) for key in want} == want and s["need"] == 1

    seen = await dev.wait_state(shown, 15, what="slot 1 run=1 need=1 on the device")
    synced = await dev.wait_state(revs_synced(rig), 10, what="device revs equal host revs")
    elapsed = seen.recv_at - written
    checks = [
        check("device_counts_two_sessions", shown(seen.msg), row=slot_row(seen.msg, SLOT), need=seen.msg["need"]),
        check("board_update_within_5s", elapsed <= BOARD_MAX_S, value=round(elapsed, 2), limit=BOARD_MAX_S),
        check("device_revs_match_host", True, revs=synced.msg["revs"]),
    ]
    return result(checks, board_s=round(elapsed, 2))


async def spawn(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    text = f"hil spawn {token(4)}"
    rig.fake.set_sessions([])
    await dev.home()
    before, mark = len(rig.fake.spawns()), tap.size()
    await dev.presses(k.spawn_presses(text, SLOT))
    confirm = await dev.wait_state(lambda s: s["overlay"] == "confirm", 3, what="Confirm open")
    await asyncio.sleep(0.6)  # Confirm ignores keys for its first 400 ms
    await dev.presses((k.ENTER,))
    await until(lambda: len(rig.fake.spawns()) > before, 15, what="fake-claude spawn call")
    i = await until(lambda: find(tap.since(mark), "tx", "agent.result"), 15, what="agent.result to the device")
    reply = tap.since(mark)[i].msg
    call = rig.fake.spawns()[-1]
    expected = ["--bg", "--name", session_name("cpa", text, 0), "--permission-mode", "plan", "--", text]
    request = next(r.msg for r in tap.since(mark) if r.dir == "rx" and r.msg and r.msg.get("t") == "agent.spawn")
    after = await dev.wait_state(lambda s: s["overlay"] == "none", 5, what="overlay closed")
    checks = [
        check("confirm_opened", confirm.msg["overlay"] == "confirm"),
        check("one_spawn_call", len(rig.fake.spawns()) == before + 1, calls=len(rig.fake.spawns()) - before),
        check("argv_exact", call["argv"][1:] == expected, argv=call["argv"][1:]),
        check("permission_mode_plan_then_dashdash", call["argv"][-3:-1] == ["plan", "--"]),
        check("cwd_is_project_root", call["cwd"] == str(rig.project_root), cwd=call["cwd"]),
        check("device_sent_agent_spawn", request.get("project") == "cpa" and request.get("text") == text),
        check("agent_result_ok", reply.get("ok") is True and bool(reply.get("short")), short=reply.get("short")),
        check("agent_spawned_event", len(events(rig, "agent.spawned", project="cpa")) >= 1),
        check("overlay_closed", after.msg["overlay"] == "none"),
    ]
    return result(checks, name=reply.get("name"), short=reply.get("short"))


async def offline_spawn(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    text = token(7)
    before, mark = len(rig.fake.spawns()), tap.size()
    run = await offline_run(ctx, (*k.spawn_presses(text, SLOT), k.CONFIRM_GUARD, k.ENTER))
    added = await until(lambda: events(rig, "queue.added", text=text), 20, what="queue.added for the offline spawn")
    queued = await dev.wait_state(lambda s: (slot_row(s, SLOT) or {}).get("queued", 0) >= 1, 15,
                                  what="queued count on the device")  # fmt: skip
    spawn_lines = [r for r in tap.since(mark) if r.dir == "rx" and r.msg and r.msg.get("t") == "agent.spawn"]
    item = added[0]
    checks = [
        check("preloaded_inject_queue", run["plan"] == k.INJECT_QUEUE_MAX, presses=run["plan"]),
        check("nothing_stored_while_paused", run["quiet"]),
        check("device_pending_1_at_reconnect", run["hello"]["pending"] == 1, pending=run["hello"]["pending"]),
        check("queued_as_spawn_intent", item["intent"] == "spawn" and item["project"] == "cpa", intent=item["intent"]),
        check("queued_offline_not_uncertain", item["uncertain"] is False and item["origin"] == "device"),
        check("fake_claude_spawn_calls_0", len(rig.fake.spawns()) == before, calls=len(rig.fake.spawns()) - before),
        check("device_never_sent_agent_spawn", not spawn_lines, lines=len(spawn_lines)),
        check("device_shows_queued", (slot_row(queued.msg, SLOT) or {}).get("queued", 0) >= 1),
    ]
    return result(checks, qid=item.get("qid"), text=text)
