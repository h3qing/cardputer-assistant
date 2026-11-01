"""v0.4.0 Tab menu (docs/13 §6): Tab sets debug.state.ui.menu over the launcher, each app, Compose and the
meeting page, and Tab closes it again (overlay keeps reporting the layer under the menu, §12.1). Enter on
a row runs it: home's Here row 1 (P), Go row 1 (Opt+H), a letter on Go (Opt+letter), Control row 1
(Opt+L). Over a live meeting the page's own keys (★, tags, Opt+R) do nothing while the menu is open;
Enter on Here row 1 and G0 add one ★ each, so the archived manifest has exactly 2 marks and no project.
The meeting part needs the card (debug.state.rec); without it the result says card false.
"""

from __future__ import annotations

import asyncio
import dataclasses
from typing import Any

from harness import keys as k
from harness import ui
from harness.report import check, result
from scenarios.common import Ctx, reached
from scenarios.rec_common import end_meeting, known, landed, live, new_archives, rec_of, settle, start_meeting

LEAK_SETTLE_S = 0.6  # an Opt+R that got through would have opened the end confirm by now
MEETING_STARS = 2


async def _place(ctx: Ctx, place: ui.MenuPlace) -> list[dict[str, Any]]:
    await ctx.dev.home()
    await ctx.dev.presses(place.opener)
    shown, s0 = await reached(ctx, place.under)
    await ctx.dev.press(ui.TAB)
    opened, s1 = await reached(ctx, dataclasses.replace(place.under, menu=True))
    await ctx.dev.press(ui.TAB)
    closed, s2 = await reached(ctx, place.under)
    return [
        check(f"{place.name}_shown", shown, got=ui.view_text(s0)),
        check(f"{place.name}_tab_opens_menu", opened, got=ui.view_text(s1)),
        check(f"{place.name}_tab_closes_menu", closed, got=ui.view_text(s2)),
    ]


async def _run(ctx: Ctx, run: ui.MenuRun) -> dict[str, Any]:
    await ctx.dev.home()
    await ctx.dev.presses((*run.before, *run.keys))
    ok, s = await reached(ctx, run.want)
    return check(f"{run.name}_runs", ok, want=run.want.text(), got=ui.view_text(s))


async def _over_meeting(ctx: Ctx) -> list[dict[str, Any]]:
    """The menu over a live meeting; returns the checks made while it records."""
    dev, menu = ctx.dev, ui.View(ui.ROOT, menu=True)
    await dev.press(ui.TAB)
    opened, s1 = await reached(ctx, menu)
    await dev.presses(ui.MEETING_LEAKS)
    await asyncio.sleep(LEAK_SETTLE_S)
    s2 = (await dev.state()).msg
    await dev.presses((ui.TAB, ui.TAB, k.ENTER))  # closed, opened again on row 1 (★), run
    row, s3 = await reached(ctx, ui.View(ui.ROOT))
    await dev.presses((ui.TAB, k.G0))  # G0 in the menu: the menu closes and G0 stars (§6.1)
    g0, s4 = await reached(ctx, ui.View(ui.ROOT))
    return [
        check("meeting_tab_opens_menu", opened and live(s1), got=ui.view_text(s1)),
        check("meeting_keys_do_not_leak", menu.met(s2) and live(s2), got=ui.view_text(s2), rec=rec_of(s2)),
        check("meeting_enter_on_row_1_closes_menu", row, got=ui.view_text(s3)),
        check("meeting_g0_closes_menu", g0 and live(s4), got=ui.view_text(s4)),
    ]


async def _meeting(ctx: Ctx) -> list[dict[str, Any]]:
    rig, dev = ctx.rig, ctx.dev
    await settle(dev)
    before = known(rig)
    await start_meeting(dev)
    checks = await _over_meeting(ctx)
    await end_meeting(dev)
    (session,) = await new_archives(rig, before, ("meeting",), 60)
    manifest = landed(rig, session).manifest
    problems = ui.meeting_problems(manifest, MEETING_STARS)
    await dev.wait_state(lambda s: rec_of(s).get("files_n") == 0, 60, interval=1.0, what="card empty")
    return [*checks, check("meeting_two_stars_no_tag", not problems, problems=problems, rec=session["rec"])]


async def menu(ctx: Ctx) -> dict[str, Any]:
    await ctx.dev.home()
    card = "rec" in (await ctx.dev.state()).msg
    checks: list[dict[str, Any]] = []
    for place in ui.MENU_PLACES:
        checks += await _place(ctx, place)
    for run in ui.MENU_RUNS:
        checks.append(await _run(ctx, run))
    if card:
        checks += await _meeting(ctx)
    await ctx.dev.home()
    return result(checks, card=card)
