"""v0.4.0 nav (docs/13 §5.1-§5.6): from the home launcher, each tile letter, Enter on a tile, a digit and
Opt+H/P/I/M reach the expected page or overlay (debug.state mod / overlay / ui), and back from an app's
first level (` alone) returns to home. R opens the meeting confirm only with the card (rec in debug.state;
the nosd build refuses it), and is cancelled after its 400 ms guard. The plan is harness.ui.nav_plan.
"""

from __future__ import annotations

from typing import Any

from harness import ui
from harness.report import check, result
from scenarios.common import Ctx, reached


async def _step(ctx: Ctx, step: ui.NavStep) -> list[dict[str, Any]]:
    await ctx.dev.home()
    await ctx.dev.presses((*step.before, step.press))
    ok, s1 = await reached(ctx, step.want)
    checks = [check(f"{step.name}_reached", ok, want=step.want.text(), got=ui.view_text(s1))]
    if step.back:
        await ctx.dev.presses(step.back)
        home, s2 = await reached(ctx, ui.View(ui.ROOT))
        checks.append(check(f"{step.name}_back_home", home, got=ui.view_text(s2)))
    return checks


async def nav(ctx: Ctx) -> dict[str, Any]:
    await ctx.dev.home()
    rec = "rec" in (await ctx.dev.state()).msg
    checks: list[dict[str, Any]] = []
    for step in ui.nav_plan(rec):
        checks += await _step(ctx, step)
    await ctx.dev.home()
    return result(checks, card=rec, steps=len(ui.nav_plan(rec)))
