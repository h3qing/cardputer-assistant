"""gh #51 (docs/20 U26) memo scenarios: G0's release grace and the hands-free double tap (test firmware
with the card; the injected keys of debug.key skip the device's G0 debounce, as they always skipped
M5Unified's).

rec_memo_grace      hold G0 2.5 s, let go for 0.3 s (inside the 0.6 s grace), hold 2 s -> one memo, one
                    seg-000.wav whose samples run without a gap from the first sample to the stop (the
                    second release + the grace), no second session.
rec_memo_handsfree  tap G0 twice -> a memo on the second press; G0 up for 5 s and it still records; one
                    G0 press ends it (the stop on its down edge) -> one memo, one file, samples to the
                    end press; then a single tap still opens the note editor (after the 0.35 s window).
"""

from __future__ import annotations

import asyncio
from typing import Any

from harness import keys as k
from harness.rec_checks import timeline
from harness.report import check, result
from scenarios.common import Ctx
from scenarios.rec_common import (
    MEMO_GRACE_MS,
    card_cleaned,
    known,
    landed,
    landed_checks,
    live,
    new_archives,
    rec_of,
    samples,
    seg_ids,
    settle,
    up0,
)

HOLD_1_MS, SLIP_MS, HOLD_2_MS = 2500, 300, 2000
# a lone modifier is no key event: G0 is up for its hold and the two 30 ms gaps around it
SLIP = k.Press(("shift",), SLIP_MS - 2 * k.GAP_MS)
HANDS_FREE_S = 5.0
SAMPLES_TOL = 4096  # rec_memo's: a block or two of the 16 kHz stream
EXTRA_WAIT_S = 3.0  # a second session, had the memo split, would be archived by then
MIC_START_MS = 500  # the cue: the first sample comes this long after the press at most (rec_memo's limit is 1 s)


def _memo(state) -> bool:
    return live(state) and rec_of(state).get("mode") == "memo"


async def _one_memo(ctx: Ctx, before: set[str]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """The memo recorded since `before`: its landing checks, and it alone."""
    rig = ctx.rig
    await ctx.dev.wait_state(lambda s: not live(s), 15, what="memo saved")
    (session,) = await new_archives(rig, before, ("memo",), 60)
    await asyncio.sleep(EXTRA_WAIT_S)
    fresh = known(rig) - before
    got = landed(rig, session)
    checks = [
        *landed_checks(rig, session),
        check("one_session", fresh == {session["rec"]}, sessions=sorted(fresh)),
        check("one_file", [s["file"] for s in got.manifest["segments"]] == ["seg-000.wav"]),
        *(await card_cleaned(ctx, seg_ids(got.manifest))),
    ]
    return checks, dict(session)


async def rec_memo_grace(ctx: Ctx) -> dict[str, Any]:
    rig, dev = ctx.rig, ctx.dev
    await settle(dev)
    before = known(rig)
    await dev.preload((k.Press(("g0",), HOLD_1_MS), SLIP, k.Press(("g0",), HOLD_2_MS)))
    key_ms = (await dev.wait_state(_memo, 3, interval=0.05, what="memo live")).msg["key_ms"]
    checks, session = await _one_memo(ctx, before)
    sid, manifest = session["rec"], landed(rig, session).manifest
    stop_ms = key_ms + HOLD_1_MS + SLIP_MS + HOLD_2_MS + MEMO_GRACE_MS
    tl = timeline(start_key_ms=key_ms, up0_ms=up0(rig, sid) or 0, stop_key_ms=stop_ms, samples=samples(manifest))
    checks.append(check("samples_continuous_through_the_slip", tl.within(SAMPLES_TOL), diff_samples=tl.diff_samples))
    return result(checks, sid=sid, duration_ms=manifest["duration_ms"], diff_samples=tl.diff_samples)


async def _single_tap_opens_the_note(ctx: Ctx) -> list[dict[str, Any]]:
    dev = ctx.dev
    await dev.press(k.G0)
    opened = await dev.wait_state(lambda s: s.get("overlay") == "compose", 3, what="note open")
    await dev.home()
    return [check("single_tap_opens_note", opened.msg["overlay"] == "compose" and not live(opened.msg))]


async def rec_memo_handsfree(ctx: Ctx) -> dict[str, Any]:
    rig, dev = ctx.rig, ctx.dev
    await settle(dev)
    before = known(rig)
    await dev.preload((k.G0, k.G0))  # the second press 30 ms after the first's release
    start_ms = (await dev.wait_state(_memo, 3, interval=0.05, what="hands-free memo live")).msg["key_ms"]
    await asyncio.sleep(HANDS_FREE_S)
    still = (await dev.state()).msg
    await dev.press(k.G0)  # the end, on its down edge
    end_ms = (await dev.state()).msg["key_ms"]
    checks, session = await _one_memo(ctx, before)
    sid, manifest = session["rec"], landed(rig, session).manifest
    tl = timeline(start_key_ms=start_ms, up0_ms=up0(rig, sid) or 0, stop_key_ms=end_ms, samples=samples(manifest))
    ms = manifest["duration_ms"]
    checks += [
        check("records_with_g0_up", _memo(still), rec=rec_of(still)),
        check("g0_up_for_5_s_kept", ms >= HANDS_FREE_S * 1000 - MIC_START_MS, duration_ms=ms),
        check("samples_to_the_end_press", tl.within(SAMPLES_TOL), diff_samples=tl.diff_samples),
        *(await _single_tap_opens_the_note(ctx)),
    ]
    return result(checks, sid=sid, duration_ms=ms, diff_samples=tl.diff_samples)
