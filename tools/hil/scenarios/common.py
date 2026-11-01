"""What every scenario gets (Ctx) and small helpers shared between scenarios (I/O shell)."""

from __future__ import annotations

import asyncio
import secrets
import string
import time
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from harness import keys as k
from harness import ui
from harness.device import Device
from harness.notes import NoteLine, count_text
from harness.rig import Rig, until
from harness.wire import handshakes

ALNUM = string.ascii_lowercase + string.digits
FIRMWARE_DIR = Path(__file__).resolve().parents[3] / "firmware" / "app"
OFFLINE_IDLE_MS = 20000  # > 15 s link timeout: the device is Attached before the preloaded keys act
SLOT = 1
SETTLE_S = 3.0  # a key's page, overlay or menu shows in debug.state well within this


@dataclass
class Options:
    minutes: float = 60.0
    flips: int = 100
    taps: int = 30
    project_dir: Path | None = None
    state: Path | None = None  # migrate_*: the seed file shared by the three phases
    expect_launcher: bool = False
    report: Path | None = None  # --report: mic_cal writes its numbers next to it


@dataclass
class Ctx:
    rig: Rig
    dev: Device
    opts: Options = field(default_factory=Options)
    meta: dict[str, Any] = field(default_factory=dict)


def token(n: int = 4, first: str = string.ascii_lowercase) -> str:
    return secrets.choice(first) + "".join(secrets.choice(ALNUM) for _ in range(n - 1))


def first_hello(ctx: Ctx) -> dict[str, Any]:
    """The device hello of this run's first handshake: what the device booted with."""
    return dict(handshakes(ctx.rig.tap.since())[0].hello or {})


def host_revs(rig: Rig) -> dict[str, int]:
    revs = rig.daemon.state.published.revs
    return {"projects": revs.projects, "sessions": revs.sessions, "queue": revs.queue}


def revs_synced(rig: Rig):
    return lambda s: s.get("revs") == host_revs(rig)


async def reached(ctx: Ctx, want: ui.View, timeout: float = SETTLE_S) -> tuple[bool, Mapping[str, Any]]:
    """Whether the device shows `want` (page, overlay, menu, lock) in time, and the last state either way."""
    try:
        return True, (await ctx.dev.wait_state(want.met, timeout)).msg
    except TimeoutError:
        return False, (await ctx.dev.state()).msg


async def note_appears(rig: Rig, text: str, timeout: float) -> tuple[NoteLine, float]:
    """The note line with exactly this text, and the host time it was first seen."""

    def seen() -> tuple[NoteLine, float] | None:
        hit = next((n for n in rig.notes() if n.text == text), None)
        return (hit, time.monotonic()) if hit else None

    return await until(seen, timeout, 0.02, what=f"note {text!r} in notes_dir")


def note_count(rig: Rig, text: str) -> int:
    return count_text(rig.notes(), text)


def events(rig: Rig, ev: str, **match: Any) -> list[Mapping[str, Any]]:
    return [e for e in rig.events() if e.get("ev") == ev and all(e.get(key) == v for key, v in match.items())]


def items_received(rig: Rig, item_id: str) -> int:
    return len(events(rig, "item.received", id=item_id))


async def offline_run(ctx: Ctx, body: tuple[k.Press, ...]) -> dict[str, Any]:
    """Preload body behind 20 s of idle holds, `cpa pause 120`, let the device act offline, resume.

    Returns what the scenario checks: the device hello after the resume (its `pending`), the
    tap index of the resume, and whether anything reached the daemon while it was paused.
    """
    rig, dev = ctx.rig, ctx.dev
    await dev.home()
    plan = k.offline_plan(body, idle_ms=OFFLINE_IDLE_MS)
    acked = await dev.preload(plan)
    await rig.pause(120)
    await rig.released()
    events_paused = len(rig.events())
    act_s = sum(k.duration_ms(p) for p in plan) / 1000
    await asyncio.sleep(max(0.0, acked + act_s + 2.0 - time.monotonic()))
    events_before_resume = len(rig.events())
    mark = rig.tap.size()
    await rig.resume()
    await rig.ready(timeout=20)
    hello = handshakes(rig.tap.since(mark))[-1].hello
    return {"plan": len(plan), "hello": hello, "mark": mark, "quiet": events_before_resume == events_paused}
