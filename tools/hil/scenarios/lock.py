"""v0.4.0 lock (docs/13 §7.1, §19), from the home launcher with Opt+L:
- 200 random keys (no G0, no Enter, no Opt+L; seeded, the seed is in the result) change nothing: page,
  overlay, menu, lock, outbox, recording (harness.ui.changed), and nothing reaches the Mac;
- G0 held on the dark locked screen only lights the card: no memo goes live;
- once the card is dark again, G0 tapped, then held, records one memo and the device stays locked;
- Opt+L then Enter within 3 s unlocks; Enter 3.5 s after Opt+L does not.
The memo parts need the card (debug.state.rec); without it the result says card false. Device times
(debug.state.key_ms) give the real Opt+L -> Enter gaps.
"""

from __future__ import annotations

import asyncio
import random
import secrets
from typing import Any

from harness import keys as k
from harness import ui
from harness.report import check, result
from scenarios.common import Ctx, events, reached
from scenarios.rec_common import archived, known, live, new_archives, settle

NOISE_N = 200
HOLD_MS = 3000  # G0Long at 500 ms: a 2.5 s memo, over the 1 s minimum
LATE_ENTER_S = ui.UNLOCK_WINDOW_S + 0.5


async def _ever_live(ctx: Ctx, within_s: float) -> bool:
    try:
        await ctx.dev.wait_state(live, within_s, interval=0.05, what="memo live")
    except TimeoutError:
        return False
    return True


async def _noise(ctx: Ctx, seed: int) -> list[dict[str, Any]]:
    rig, dev = ctx.rig, ctx.dev
    notes, received = len(rig.notes()), len(events(rig, "item.received"))
    before = (await dev.state()).msg
    rng = random.Random(seed)  # noqa: S311 - key noise, reproducible from the seed in the result
    await dev.presses(ui.noise_presses(rng, NOISE_N))
    after = (await dev.state()).msg
    moved = ui.changed(before, after)
    return [
        check("noise_reached_the_device", after.get("key_ms", 0) > before.get("key_ms", 0)),
        check("noise_changed_nothing", not moved, changed=list(moved), got=ui.view_text(after)),
        check("noise_sent_nothing", len(rig.notes()) == notes and len(events(rig, "item.received")) == received),
    ]


async def _memos(ctx: Ctx) -> list[dict[str, Any]]:
    rig, dev = ctx.rig, ctx.dev
    before = known(rig)
    await dev.press(k.Press(("g0",), HOLD_MS), pace=False)  # dark: lights the card only
    dark_live = await _ever_live(ctx, HOLD_MS / 1000 + 1)
    await asyncio.sleep(ui.CARD_S + 0.5)  # the card lit by that hold goes dark again
    dark_ok, s1 = await reached(ctx, ui.View(ui.ROOT, locked=True))
    await dev.press(k.G0)  # lights the card
    await dev.press(k.Press(("g0",), HOLD_MS), pace=False)  # held on the lit card: a memo
    memo_live = await _ever_live(ctx, 3)
    await dev.wait_state(lambda s: not live(s), HOLD_MS / 1000 + 10, what="memo saved")
    kept, s2 = await reached(ctx, ui.View(ui.ROOT, locked=True))
    await new_archives(rig, before, ("memo",), 60)
    fresh = [r for r in archived(rig) if r not in before]
    return [
        check("dark_hold_records_nothing", not dark_live and dark_ok, got=ui.view_text(s1)),
        check("tap_then_hold_records_a_memo", memo_live),
        check("stays_locked_after_the_memo", kept, got=ui.view_text(s2)),
        check("exactly_one_memo", len(fresh) == 1, sessions=fresh),
    ]


async def _gap(ctx: Ctx, wait_s: float) -> tuple[bool, int]:
    """Opt+L (locked: lights the card, opens the window), wait, Enter: (unlocked, device gap in ms)."""
    dev = ctx.dev
    await dev.press(ui.OPT_L)
    t0 = (await dev.state()).msg["key_ms"]
    await asyncio.sleep(wait_s)
    await dev.press(k.ENTER)
    await asyncio.sleep(0.3)  # the unlock is immediate; this is margin, not a wait for it
    s = (await dev.state()).msg
    return not ui.locked(s), s["key_ms"] - t0


async def _unlock(ctx: Ctx) -> list[dict[str, Any]]:
    quick, quick_ms = await _gap(ctx, 0)
    back, s1 = await reached(ctx, ui.View(ui.ROOT))
    await ctx.dev.press(ui.OPT_L)
    relocked, _ = await reached(ctx, ui.View(ui.ROOT, locked=True))
    late, late_ms = await _gap(ctx, LATE_ENTER_S)
    again, again_ms = await _gap(ctx, 0)
    return [
        check("opt_l_enter_within_3s_unlocks", quick and back, gap_ms=quick_ms, got=ui.view_text(s1)),
        check("opt_l_locks_again", relocked),
        check("enter_after_3s_stays_locked", not late and late_ms >= ui.UNLOCK_WINDOW_S * 1000, gap_ms=late_ms),
        check("then_opt_l_enter_unlocks", again, gap_ms=again_ms),
    ]


async def lock(ctx: Ctx) -> dict[str, Any]:
    dev = ctx.dev
    await dev.home()
    card = "rec" in (await dev.state()).msg
    if card:
        await settle(dev)
    await dev.wait_state(lambda s: s["outbox"]["pending"] == 0, 15, what="outbox empty")
    await dev.press(ui.OPT_L)
    locked, s0 = await reached(ctx, ui.View(ui.ROOT, locked=True))
    seed = secrets.randbits(32)
    checks = [check("opt_l_locks", locked, got=ui.view_text(s0)), *await _noise(ctx, seed)]
    if card:
        checks += await _memos(ctx)
    checks += await _unlock(ctx)
    await dev.home()
    return result(checks, card=card, seed=seed, noise=NOISE_N)
