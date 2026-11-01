"""05 §9.3 item 11: soak. Every second a ping and a snapshot (fake-claude flips a session and the
daemon polls at 1 s for the duration), every minute a note typed on the device. Pass: 0 notes
lost or doubled, no host request left without a reply for more than 5 s (TX stall), the link
never reopened, the outbox empty at the end.
"""

from __future__ import annotations

import asyncio
import secrets
import time
from typing import Any

from harness import keys as k
from harness.fakeclaude import tty_session
from harness.report import check, result
from harness.rig import until
from harness.stats import summary
from harness.wire import exchanges, latencies, nudges, stuck
from scenarios.common import Ctx, note_appears, note_count, token

STALL_S = 5.0
FIRST_NOTE_S = 10.0
NOTE_EVERY_S = 60.0


async def _ticker(ctx: Ctx, end: float) -> int:
    rig = ctx.rig
    sid, root, n = secrets.token_hex(4), str(rig.project_root), 0
    while time.monotonic() < end:
        tick = time.monotonic()
        rig.fake.set_sessions([tty_session(sid, "浸泡测试", root, "waiting" if n % 2 else "busy")])
        link = rig.link
        if link is not None:
            link.request_raw({"t": "ping"})
        n += 1
        await asyncio.sleep(max(0.0, tick + 1.0 - time.monotonic()))
    return n


async def _notes(ctx: Ctx, start: float, end: float) -> list[str]:
    texts: list[str] = []
    due = start + FIRST_NOTE_S
    while due < end:
        await asyncio.sleep(max(0.0, due - time.monotonic()))
        text = f"soak {len(texts) + 1:03d} {token(4)}"
        texts.append(text)
        await ctx.dev.home()
        await ctx.dev.presses(k.note_presses(text))
        due += NOTE_EVERY_S
    return texts


async def soak(ctx: Ctx) -> dict[str, Any]:
    rig, tap = ctx.rig, ctx.rig.tap
    rig.set_config(poll_s=1.0)
    await until(lambda: rig.daemon.config.poll_s == 1.0, 10, what="poll_s 1 reloaded by the daemon")
    mark, start = tap.size(), time.monotonic()
    end = start + ctx.opts.minutes * 60
    ticks, texts = await asyncio.gather(_ticker(ctx, end), _notes(ctx, start, end))
    for text in texts:
        await note_appears(rig, text, 30)
    final = await ctx.dev.wait_state(lambda s: s["outbox"]["pending"] == 0, 30, what="outbox empty")
    await asyncio.sleep(STALL_S + 1)  # the last requests get their replies (or count as stalled)
    rig.set_config(poll_s=3.0)
    recs = tap.since(mark)
    ex = exchanges(recs)
    late = stuck(ex, time.monotonic(), STALL_S)
    counts = {t: note_count(rig, t) for t in texts}
    lost = [t for t, c in counts.items() if c == 0]
    doubled = [t for t, c in counts.items() if c > 1]
    reopened = sum(1 for r in recs if r.dir == "open")
    lat_ms = [x * 1000 for x in latencies(ex)]
    checks = [
        check("notes_typed", len(texts) >= int(ctx.opts.minutes) - 1, notes=len(texts)),
        check("lost_0", not lost, lost=lost),
        check("doubled_0", not doubled, doubled=doubled),
        check("tx_stalls_0", not late, stalled=[(e.t, e.id) for e in late][:10], limit_s=STALL_S),
        check("link_never_reopened", reopened == 0, reopened=reopened),
        check("outbox_empty_at_end", final.msg["outbox"]["pending"] == 0),
    ]
    return result(
        checks,
        minutes=ctx.opts.minutes,
        ticks=ticks,
        notes=len(texts),
        lost=len(lost),
        tx_stuck=len(late),
        requests=len(ex),
        reply_ms=summary(lat_ms, 1),
        replies_over_1s=sum(1 for x in lat_ms if x > 1000),
        nudges=nudges(recs),
        snapshots=sum(1 for e in ex if e.t.endswith(".snapshot")),
    )
