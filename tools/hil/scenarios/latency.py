"""05 §9.3 item 9: fake-claude flips a session 100 times (device update p95 <= 5 s) and G0 opens
Compose (p95 <= 650 ms: 300 ms, plus the 350 ms a tap waits for a second one since gh #51).

Board latency is host-measured: from the state.json write to the first debug.state reply that
shows the new counts (an upper bound: it includes the 0.1 s poll of debug.state). Each flip is
written right after the previous one was seen, i.e. just after a daemon poll: close to the worst
phase of the 3 s poll. Compose latency is device-measured, as the protocol intends: debug.state
overlay_ms - key_ms, both device up_ms stamps of the same loop clock (key_ms = G0 pressed,
overlay_ms = Compose opened; the injected tap holds G0 for 50 ms and G0Tap fires on release; the
note opens kDoubleTapMs = 350 ms later, when no second tap came: docs/20 U26).
The host-observed time (send -> reply showing Compose) is reported alongside.
"""

from __future__ import annotations

import secrets
import time
from typing import Any

from harness import keys as k
from harness.device import slot_row
from harness.fakeclaude import tty_session
from harness.report import check, result
from harness.stats import percentile, summary
from scenarios.common import SLOT, Ctx

BOARD_P95_MAX_S = 5.0
DOUBLE_TAP_MS = 350  # firmware/app/lib/input/g0_gesture.h kDoubleTapMs
COMPOSE_P95_MAX_MS = 300 + DOUBLE_TAP_MS
# docs/20 D5: ` alone closes the empty editor a G0 tap opened; Fn+` would type a backtick into it.
COMPOSE_CLOSE_EMPTY = k.Press(("`",))


def _counts(state) -> tuple[int | None, int | None]:
    row = slot_row(state, SLOT) or {}
    return row.get("run"), row.get("need")


async def _board_flips(ctx: Ctx, flips: int) -> list[float]:
    rig, dev = ctx.rig, ctx.dev
    sid, root = secrets.token_hex(4), str(rig.project_root)

    def write(status: str) -> None:
        rig.fake.set_sessions([tty_session(sid, "延迟测试", root, status)])

    write("busy")
    await dev.wait_state(lambda s: _counts(s) == (1, 0), 15, what="baseline run=1 need=0")
    out = []
    for i in range(flips):
        status, want = ("waiting", (0, 1)) if i % 2 == 0 else ("busy", (1, 0))
        written = time.monotonic()
        write(status)
        seen = await dev.wait_state(lambda s, w=want: _counts(s) == w, 15, what=f"flip {i} to {status}")
        out.append(seen.recv_at - written)
    return out


async def _compose_taps(ctx: Ctx, taps: int) -> tuple[list[int], list[float]]:
    dev = ctx.dev
    await dev.home()
    device_ms, host_ms = [], []
    for _ in range(taps):
        sent = time.monotonic()
        await dev.press(k.G0, pace=False)
        s = await dev.wait_state(lambda s: s["overlay"] == "compose", 3, 0.02, what="Compose open")
        device_ms.append(s.msg["overlay_ms"] - s.msg["key_ms"])
        host_ms.append((s.recv_at - sent) * 1000)
        await dev.press(COMPOSE_CLOSE_EMPTY)
        await dev.wait_state(lambda s: s["overlay"] == "none", 3, 0.02, what="Compose closed")
    return device_ms, host_ms


async def latency(ctx: Ctx) -> dict[str, Any]:
    board = await _board_flips(ctx, ctx.opts.flips)
    device_ms, host_ms = await _compose_taps(ctx, ctx.opts.taps)
    p95_board = round(percentile(board, 95), 3)
    p95_compose = percentile(device_ms, 95)
    checks = [
        check("all_flips_seen", len(board) == ctx.opts.flips, flips=len(board)),
        check("p95_board_s", p95_board <= BOARD_P95_MAX_S, value=p95_board, limit=BOARD_P95_MAX_S),
        check("p95_compose_ms", p95_compose <= COMPOSE_P95_MAX_MS, value=p95_compose, limit=COMPOSE_P95_MAX_MS),
    ]
    return result(
        checks,
        p95_board_s=p95_board,
        p95_compose_ms=p95_compose,
        p95_compose_host_ms=round(percentile(host_ms, 95), 1),
        board_s=summary(board, 3),
        compose_ms=summary(device_ms, 1),
        compose_host_ms=summary(host_ms, 1),
        poll_s=ctx.rig.daemon.config.poll_s,
    )
