"""mic_cal and mic_verify: the mic gain calibration and its verify pass (mic gain brief §6 steps 1-7; test
firmware with debug.mic, card in). The whole procedure is docs/gates/m3-device-gates.md §5.2.

The Mac's built-in speakers play a reference (harness.mic_ref: 1 kHz tones at -20 / -32 dBFS, English
and Chinese `say`) to the Cardputer lying next to them, once per setting of brief §5. Each setting is a
debug.mic {pga, vol, mag} (acked; applied at the next Mic.begin) and a meeting started and ended with
the real keys; the in-process cpad pulls it into the rig's temp dir, harness.mic_cal measures it and
harness.mic_decide turns the sweep into the gain to ship (brief §7). B1 is probed first and the
reference rescaled once if tone A misses [-48, -27] dBFS. Then five short silent meetings at the chosen
gain measure the Mic.begin transient (lead_ms), and a reboot drops the debug.mic override.
mic_verify (after kMicGain is committed and the test build re-flashed): the B1 probe, a reboot, and one
capture at the firmware's own kMicGain (no debug.mic) checked against mic_gain.h (harness.mic_decide.verify).

preflight() runs in the runner before the rig opens the port (so $CPA_HOME is still the user's): it
refuses unless the default output is the unmuted built-in speakers and the real cpad's `cpa status
--json` shows nothing on the card and nothing pulling (the temporary daemon would pull and delete what
it finds), then pauses the real cpad for 1800 s and waits for the port. Only numbers reach the report
(<report>.mic_cal.data.json and .md); the audio stays in the rig's temp dir, removed unless --keep.
No file.* is sent from here: the daemon's puller owns the card.

Any failure (afplay, a timeout, the runner's own scenario timeout, a DeviceError) reboots the device and
lets the HIL daemon empty the card before the error leaves, within DRAIN_S: a calibration recording must
never reach the real cpad (brief §3), and no swept gain may outlive the run. The error says whether that
worked (`cleanup:` / `cleanup FAILED`). Either way the device still runs the test build afterwards and
the real cpad stays paused: flash cardputer-adv, `cpa pause 1`, and check `cpa status` shows link ready.
"""

from __future__ import annotations

import asyncio
import itertools
import json
import shutil
import subprocess
import sys
import time
from array import array
from collections.abc import Awaitable, Callable, Mapping
from dataclasses import dataclass
from typing import Any

from harness import mic_ref
from harness.device import Device, DeviceError
from harness.mic_cal import BY_LABEL, SWEEP, Capture, Setting, lead_of, measure, pcm, shipped_gain
from harness.mic_decide import decide, lead_verdict, markdown, probe_step, rows, status_problem, validity, verify
from harness.report import check, result
from harness.rig import Rig, port_holders, until
from scenarios.common import FIRMWARE_DIR, Ctx
from scenarios.rec_common import (
    card_cleaned,
    end_meeting,
    known,
    landed,
    landed_checks,
    new_archives,
    set_seg_ms,
    settle,
    start_meeting,
)

PAUSE_S = 1800
PORT_FREE_S = 10.0
SEG_MS = 60000  # every capture is one segment
PRE_ROLL_S = 4.0  # harness.mic_cal NOISE_MS ends at 4 s; the first sample is before start_meeting returns
TAIL_S = 0.5
LEAD_RUNS, LEAD_REC_S = 5, 2.5
ARCHIVE_S = 90.0
DRAIN_S = 120.0  # failure path: reboot, then the card emptied (at most two meetings of about 1 MB to pull)
MIC_GAIN_H = FIRMWARE_DIR / "lib" / "core" / "mic_gain.h"
LEFT_BEHIND = ("the card may still hold a calibration recording and a swept gain may still be active, see "
               "docs/gates/m3-device-gates.md §5.2")  # fmt: skip


class CalibrationError(RuntimeError):
    """The reference cannot be placed in the probe window: nothing to decide from."""


# --- before the rig ----------------------------------------------------------------------------


def _cpa(*args: str) -> subprocess.CompletedProcess:
    """The user's cpa (as pre_upload.py finds it), else this checkout's, in the user's environment."""
    found = shutil.which("cpa")
    cmd = [found] if found else [sys.executable, "-m", "cpad"]
    return subprocess.run([*cmd, *args], capture_output=True, text=True, check=False, timeout=30)


def preflight() -> str | None:
    """Brief §6.1 and §6.2's output check; None when mic_cal may take the port (the real cpad is paused)."""
    problem, _ = mic_ref.output_state()
    if problem:
        return problem
    if "en" not in mic_ref.choose_voices(mic_ref.installed_voices()):
        return "no English `say` voice installed"
    done = _cpa("status", "--json")
    try:
        problem = status_problem(json.loads(done.stdout))
    except ValueError:
        return f"`cpa status --json` gave no JSON: {done.stderr.strip()[:200]}"
    if problem:
        return problem
    paused = _cpa("pause", str(PAUSE_S))
    if paused.returncode != 0:
        return f"cpa pause {PAUSE_S} failed: {paused.stderr.strip()[:200]}"
    deadline = time.monotonic() + PORT_FREE_S
    while port_holders():
        if time.monotonic() > deadline:
            return f"the port is still held after cpa pause: {port_holders()}"
        time.sleep(0.25)
    return None


# --- one capture -------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Taken:
    cap: Capture | None  # None for a lead run
    lead: int | None
    ids: tuple[str, ...]
    bad: tuple[str, ...]  # failed landed checks (manifest, header, SHA, modes)


async def set_mic(dev: Device, s: Setting) -> None:
    _, reply = await dev.request(s.wire())
    if reply.msg.get("t") != "ack":
        code = reply.msg.get("code")
        hint = " (flash cardputer-adv-test with debug.mic)" if code == "E_UNKNOWN_TYPE" else ""
        raise DeviceError(f"debug.mic {s.label} answered {reply.msg.get('t')} {code}{hint}")


async def _meeting(ctx: Ctx, s: Setting | None, body: Callable[[], Awaitable[Any]]) -> tuple[array, tuple, tuple]:
    """One meeting at s around body() (s None sends no debug.mic: right after a reboot that is the build's
    own kMicGain); its samples once pulled, segment ids and failed landed checks."""
    await settle(ctx.dev)
    if s is not None:
        await set_mic(ctx.dev, s)
    before = known(ctx.rig)
    await start_meeting(ctx.dev)
    try:
        await body()
    finally:  # afplay failing must not leave the device recording
        await end_meeting(ctx.dev)
    (session,) = await new_archives(ctx.rig, before, ("meeting",), ARCHIVE_S)
    bad = tuple(c["name"] for c in landed_checks(ctx.rig, session) if not c["ok"])
    got = landed(ctx.rig, session)
    segs = got.manifest["segments"]
    x = array("h", itertools.chain.from_iterable(pcm((got.folder / g["file"]).read_bytes()) for g in segs))
    return x, tuple(g["id"] for g in segs), bad


async def _take(ctx: Ctx, s: Setting | None, ref: mic_ref.Reference) -> Taken:
    async def hear() -> None:
        await asyncio.sleep(PRE_ROLL_S)
        await mic_ref.play(ref.path, ref.ms / 1000 + 20)
        await asyncio.sleep(TAIL_S)

    x, ids, bad = await _meeting(ctx, s, hear)
    return Taken(await asyncio.to_thread(measure, x, ref.blocks), None, ids, bad)


async def _lead_run(ctx: Ctx, s: Setting) -> Taken:
    x, ids, bad = await _meeting(ctx, s, lambda: asyncio.sleep(LEAD_REC_S))
    return Taken(None, lead_of(x), ids, bad)


# --- the run -----------------------------------------------------------------------------------


def _probe_doc(t: Taken, ref: mic_ref.Reference) -> dict[str, Any]:
    tone = t.cap.tone_a_db if t.cap else None
    return {"ref_gain_db": round(ref.gain_db, 2), "tone_a_dbfs": None if tone is None else round(tone, 2)}


def _verdict(t: Taken, ref: mic_ref.Reference) -> tuple[str, float | None]:
    if t.cap is None or t.cap.tone_a_db is None:
        return "no_tone", None
    return probe_step(t.cap.tone_a_db, ref.gain_db, ref.max_gain_db)


async def _probe(ctx: Ctx, ref: mic_ref.Reference) -> tuple[mic_ref.Reference, Taken, list[dict[str, Any]]]:
    """Brief §6.3: B1 until tone A lands in the probe window, rescaling the reference at most once."""
    taken = await _take(ctx, BY_LABEL["B1"], ref)
    steps = [_probe_doc(taken, ref)]
    verdict, gain = _verdict(taken, ref)
    if verdict == "rescale":
        ref = await asyncio.to_thread(mic_ref.rescale, ref, gain)
        taken = await _take(ctx, BY_LABEL["B1"], ref)
        steps = [*steps, _probe_doc(taken, ref)]
        verdict, _ = _verdict(taken, ref)
    if verdict != "ok":
        hint = {"too_quiet": "move the Cardputer closer to the speakers", "no_tone": "is the reference audible?"}
        raise CalibrationError(f"B1 probe {verdict}: {steps} ({hint.get(verdict, 'tone A outside [-48, -27]')})")
    return ref, taken, steps


def _lead_setting(decision: Mapping[str, Any]) -> Setting:
    """The chosen gain, or the predicted pick S10 when there is no decision."""
    return Setting("lead", decision["pga"], decision["vol"], decision["mag"]) if decision["ok"] else BY_LABEL["S10"]


def _report(ctx: Ctx, doc: Mapping[str, Any]) -> list[str]:
    """<report>.mic_cal.data.json and <report>.mic_cal.md next to --report; written after every capture."""
    rep = ctx.opts.report
    if rep is None:
        return []
    data, table = rep.with_name(f"{rep.stem}.mic_cal.data.json"), rep.with_name(f"{rep.stem}.mic_cal.md")
    data.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    table.write_text(markdown(doc["settings"], doc["decision"], doc["lead"]), encoding="utf-8")
    return [str(data), str(table)]


def _doc(taken: Mapping[str, Taken], leads: tuple[Taken, ...], extra: Mapping[str, Any]) -> dict[str, Any]:
    caps = {k: t.cap for k, t in taken.items() if t.cap is not None}
    return {"settings": rows(caps), "decision": decide(caps), "lead": lead_verdict([t.lead for t in leads]),
            **extra}  # fmt: skip


async def _reboot(ctx: Ctx) -> None:
    """A new boot: the RAM-only debug.mic and debug.rec overrides are gone (kMicGain, the default seg_ms)."""
    rig: Rig = ctx.rig
    old = (await rig.ready()).hello.boot
    await ctx.dev.reboot()
    await until(lambda: rig.link if rig.link and rig.link.hello.boot != old else None, 40, what="new boot")


async def _cleanup(ctx: Ctx, ids: list[str]) -> list[dict[str, Any]]:
    """Brief §6.6: the card emptied, a reboot drops the RAM-only debug.mic, every segment pulled once."""
    await settle(ctx.dev)
    await _reboot(ctx)
    return await card_cleaned(ctx, ids)


async def _drain(ctx: Ctx) -> None:
    """After a failure: the reboot first (it also ends a meeting the failure left live, its segment kept as
    in rec_reboot, and closes any overlay), then the HIL daemon pulls and deletes all that is on the card."""
    await _reboot(ctx)
    await settle(ctx.dev)


async def _guarded(ctx: Ctx, run: Awaitable[dict[str, Any]]) -> dict[str, Any]:
    """run, and on any failure (the runner's timeout included) the bounded drain before the error leaves; the
    error then says whether the drain worked."""
    try:
        return await run
    except BaseException as exc:
        try:
            await asyncio.wait_for(_drain(ctx), DRAIN_S)
            exc.add_note("cleanup: rebooted (no debug.mic left) and the card emptied into the rig's temp dir")
        except Exception as why:  # noqa: BLE001 - the original error is the one to raise
            exc.add_note(f"cleanup FAILED ({type(why).__name__}: {why}): {LEFT_BEHIND}")
        raise


async def _prepared(ctx: Ctx) -> tuple[dict[str, Any], mic_ref.Reference]:
    """The Mac's output checked again (read-only) and the reference built in the rig's temp dir."""
    problem, output = await asyncio.to_thread(mic_ref.output_state)
    if problem:
        raise CalibrationError(problem)
    return output, await asyncio.to_thread(mic_ref.build, ctx.rig.tmp / "mic_ref")


async def mic_cal(ctx: Ctx) -> dict[str, Any]:
    output, ref = await _prepared(ctx)
    return await _guarded(ctx, _calibrate(ctx, output, ref))


async def _calibrate(ctx: Ctx, output: Mapping[str, Any], ref: mic_ref.Reference) -> dict[str, Any]:
    await set_seg_ms(ctx.dev, SEG_MS)
    ref, b1, probe = await _probe(ctx, ref)
    taken: dict[str, Taken] = {"B1": b1}
    for s in SWEEP[1:]:
        taken = {**taken, s.label: await _take(ctx, s, ref)}
        _report(ctx, _doc(taken, (), {"output": output, "reference": ref.doc(), "probe": probe}))
    caps = {k: t.cap for k, t in taken.items()}
    decision = decide(caps)
    leads: tuple[Taken, ...] = ()
    for _ in range(LEAD_RUNS):
        leads = (*leads, await _lead_run(ctx, _lead_setting(decision)))
    extra = {"output": output, "reference": ref.doc(), "probe": probe, "lead_setting": _lead_setting(decision).wire()}
    doc = _doc(taken, leads, extra)
    files = _report(ctx, doc)
    named = {**taken, **{f"lead{i}": t for i, t in enumerate(leads)}}
    bad = {k: list(t.bad) for k, t in named.items() if t.bad}
    checks = [
        check("captures_intact", not bad, bad=bad),
        check("sweep_valid", not validity(caps), problems=list(validity(caps))),
        check("decision", decision["ok"], problems=decision["problems"]),
        check("headroom_n33_n36_clip_free", decision["headroom"]["ok"], headroom=decision["headroom"]),
        check("lead_measured", doc["lead"]["recommended"] is not None, leads=doc["lead"]["leads"]),
        *(await _cleanup(ctx, [i for t in named.values() for i in t.ids])),
    ]
    return result(checks, decision=decision, lead=doc["lead"], settings=doc["settings"], files=files, **extra)


async def mic_verify(ctx: Ctx) -> dict[str, Any]:
    """Brief §6.7, after kMicGain is committed and the test build re-flashed."""
    want = shipped_gain(MIC_GAIN_H.read_text(encoding="utf-8"))
    output, ref = await _prepared(ctx)
    return await _guarded(ctx, _verify(ctx, output, ref, want))


async def _verify(ctx: Ctx, output: Mapping[str, Any], ref: mic_ref.Reference, want: Setting) -> dict[str, Any]:
    await set_seg_ms(ctx.dev, SEG_MS)
    ref, b1, probe = await _probe(ctx, ref)
    await settle(ctx.dev)
    await _reboot(ctx)  # debug.mic and seg_ms gone: the next Mic.begin records at the build's own kMicGain
    k = await _take(ctx, None, ref)
    verdict = verify(b1.cap, k.cap, want)
    bad = {n: list(t.bad) for n, t in (("B1", b1), (want.label, k)) if t.bad}
    checks = [
        check("captures_intact", not bad, bad=bad),
        check("kmicgain_verified", verdict["ok"], problems=verdict["problems"]),
        *(await _cleanup(ctx, [*b1.ids, *k.ids])),
    ]
    return result(checks, verify=verdict, output=output, reference=ref.doc(), probe=probe)
