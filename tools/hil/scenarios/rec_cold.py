"""rec_cold_start (issue #31): the first recording after a codec power-on, without a power cycle.

A USB-powered device never gets its codec cold by itself. debug.codec (test firmware) puts the ES8311 into
the state a power-on leaves it in: its soft reset (REG00 = 0x1F) with the microphone off, which the issue's
bench showed to mute the next start for 933-995 ms exactly like a real power-on. Three memos, each held
HOLD_MS with the real G0 while the Mac's built-in speakers play a continuous 1 kHz tone (harness.mic_ref,
-20 dBFS, the system volume untouched):
  cold      debug.codec {warm: false}: the codec mutes about 1 s; only the defensive start saves the memo
  warm      right after, nothing sent: the codec is warm
  power_on  debug.codec {warm: true}: the soft reset and the boot's warm-up, as after a real power-on; the
            Start arrives about 0.5 s later, while the warm-up may still run, and waits for it
The in-process cpad pulls each memo; harness.cold_start checks it (no leading mute over 20 ms, the tone from
the start, Start -> Started under 2.5 s, the cold take really cold, the power-on take warmed).

It runs alone with mic_cal's preflight (unmuted built-in speakers, nothing on the card, the real cpad
paused), and like mic_cal any failure reboots the device and lets the HIL daemon empty the card before the
error leaves: a recording of the room must never reach the real cpad. The audio stays in the rig's temp dir
(removed unless --keep); the report has only numbers.
"""

from __future__ import annotations

import asyncio
import contextlib
import itertools
from array import array
from collections.abc import Awaitable
from pathlib import Path
from typing import Any

from harness import mic_ref
from harness.cold_start import Take, take_checks, take_of
from harness.device import Device, DeviceError
from harness.mic_cal import TONE_HZ, pcm
from harness.report import check, result
from scenarios.common import Ctx
from scenarios.mic_cal import _guarded
from scenarios.rec_common import card_cleaned, hold_memo, known, landed, landed_checks, live, new_archives, settle, up0

HOLD_MS = 4500  # G0Long at 500 ms, up to about 1.1 s muted on the cold take: about 3 s written
PRE_ROLL_S = 1.0  # afplay is playing before debug.codec and G0
TONE_MS, TONE_DBFS = 9000, -20.0
STOP_S = HOLD_MS / 1000 + 10
ARCHIVE_S = 90.0
TAKES: tuple[tuple[str, bool | None], ...] = (("cold", False), ("warm", None), ("power_on", True))


async def set_codec(dev: Device, warm: bool) -> None:
    _, reply = await dev.request({"t": "debug.codec", "warm": warm})
    if reply.msg.get("t") != "ack":
        code = reply.msg.get("code")
        hint = " (flash cardputer-adv-test with debug.codec)" if code == "E_UNKNOWN_TYPE" else ""
        raise DeviceError(f"debug.codec answered {reply.msg.get('t')} {code}{hint}")


def tone_file(folder: Path) -> Path:
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    path = folder / "tone.wav"
    mic_ref.write_wav(path, mic_ref.tone(TONE_HZ, TONE_DBFS, TONE_MS))
    return path


async def _with_tone(path: Path, body: Awaitable[int]) -> int:
    """body while afplay plays the tone; afplay is stopped when body is done (a failed afplay shows up as a
    missing tone in the take's checks)."""
    play = asyncio.create_task(mic_ref.play(path, TONE_MS / 1000 + 20))
    try:
        return await body
    finally:
        play.cancel()
        with contextlib.suppress(asyncio.CancelledError, RuntimeError):
            await play


async def _memo(dev: Device, warm: bool | None) -> int:
    """debug.codec (unless warm is None), then a memo held HOLD_MS; the device time G0 went down."""
    await asyncio.sleep(PRE_ROLL_S)
    if warm is not None:
        await set_codec(dev, warm)
    key_ms = await hold_memo(dev, HOLD_MS)
    await dev.wait_state(lambda s: not live(s), STOP_S, what="memo stopped")
    return key_ms


async def _take(ctx: Ctx, label: str, warm: bool | None, tone: Path) -> tuple[Take, tuple[str, ...], tuple[str, ...]]:
    """One take: what harness.cold_start measured, its segment ids and failed landed checks."""
    await settle(ctx.dev)
    before = known(ctx.rig)
    key_ms = await _with_tone(tone, _memo(ctx.dev, warm))
    (session,) = await new_archives(ctx.rig, before, ("memo",), ARCHIVE_S)
    bad = tuple(c["name"] for c in landed_checks(ctx.rig, session) if not c["ok"])
    got = landed(ctx.rig, session)
    segs = got.manifest["segments"]
    x = array("h", itertools.chain.from_iterable(pcm((got.folder / g["file"]).read_bytes()) for g in segs))
    up = up0(ctx.rig, session["rec"])
    if up is None:
        raise DeviceError(f"{label}: no at.up_ms for segment 0 of {session['rec']}")
    return take_of(label, x, key_ms, up), tuple(g["id"] for g in segs), bad


async def rec_cold_start(ctx: Ctx) -> dict[str, Any]:
    tone = await asyncio.to_thread(tone_file, ctx.rig.tmp / "rec_cold")
    return await _guarded(ctx, _run(ctx, tone))


async def _run(ctx: Ctx, tone: Path) -> dict[str, Any]:
    takes: tuple[Take, ...] = ()
    ids: tuple[str, ...] = ()
    bad: dict[str, list[str]] = {}
    for label, warm in TAKES:
        t, seg_ids, failed = await _take(ctx, label, warm, tone)
        takes, ids = (*takes, t), (*ids, *seg_ids)
        bad = {**bad, label: list(failed)} if failed else bad
    await settle(ctx.dev)
    checks = [
        check("captures_intact", not bad, bad=bad),
        *(c for t in takes for c in take_checks(t)),
        *(await card_cleaned(ctx, ids)),
    ]
    return result(checks, takes=[t.doc() for t in takes])
