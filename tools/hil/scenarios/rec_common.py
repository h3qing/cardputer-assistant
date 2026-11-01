"""Shared steps of the recording scenarios (I/O shell; the checks are harness.rec_checks).

Recordings start and stop through debug.key with the real keys (docs/11 §4.1, §5.3): hold G0 for a
memo; Opt+R, Enter for a meeting (Opt+R, Enter again ends it). debug.rec only sets seg_ms, and
debug.state.rec / key_ms give the device's view: key_ms is the device time of the last key going
down, which with the segments' at.up_ms (millis() after Mic.begin) times a recording on one clock.
"""

from __future__ import annotations

import asyncio
import contextlib
import hashlib
import json
import os
import stat
import time
from collections.abc import AsyncIterator, Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from harness import keys as k
from harness.device import Device, DeviceError
from harness.fakewoof import FakeWoof, check_doc
from harness.rec_checks import RecLine, file_rates, parse_rec_line, segment_problems
from harness.report import check
from harness.rig import Rig, until
from harness.wire import Rec
from scenarios.common import Ctx, events

OPT_R = k.Press(("opt", "r"))
SPACE = k.Press(("space",))
TAG_1 = k.Press(("1",))
OPEN_1 = k.Press(("opt", "1"))
HOUR_BYTES = 115_200_000  # one hour of 16 kHz 16-bit mono (docs/11 §4.4)
MEMO_GRACE_MS = 600  # firmware rec_core.h kMemoGraceMs (gh #51): a memo stops this long after G0 goes up


def rec_of(state: Mapping[str, Any]) -> dict[str, Any]:
    return dict(state.get("rec") or {})


def live(state: Mapping[str, Any]) -> bool:
    return bool(rec_of(state).get("live"))


async def sleep_until(t: float) -> None:
    await asyncio.sleep(max(0.0, t - time.monotonic()))


async def settle(dev: Device, timeout: float = 300.0) -> None:
    """Not recording and nothing left on the card (earlier recordings pulled), overlays closed."""
    await dev.wait_state(
        lambda s: not live(s) and rec_of(s).get("files_n") == 0,
        timeout,
        interval=1.0,
        what="card empty and not recording",
    )
    await dev.home()


async def set_seg_ms(dev: Device, seg_ms: int) -> None:
    _, reply = await dev.request({"t": "debug.rec", "seg_ms": seg_ms})
    if reply.msg.get("t") != "ack":
        raise DeviceError(f"debug.rec answered {reply.msg.get('t')} {reply.msg.get('code')}")


async def _confirm(dev: Device) -> int:
    """The rec confirm is open: outlast its 400 ms guard, Enter; returns the device time of Enter."""
    await dev.wait_state(lambda s: s.get("overlay") == "confirm", 3, what="rec confirm open")
    await dev.press(k.CONFIRM_GUARD)
    await dev.press(k.ENTER)
    return (await dev.state()).msg["key_ms"]


async def start_meeting(dev: Device) -> int:
    """Opt+R, Enter (consent); returns the device time of Enter once the device is live."""
    await dev.press(OPT_R)
    key_ms = await _confirm(dev)
    await dev.wait_state(lambda s: live(s) and rec_of(s).get("mode") == "meeting", 5, what="meeting live")
    return key_ms


async def end_meeting(dev: Device, timeout: float = 15.0) -> int:
    """Opt+R, Enter on the end confirm; returns the device time of Enter once recording stopped."""
    await dev.press(OPT_R)
    key_ms = await _confirm(dev)
    await dev.wait_state(lambda s: not live(s), timeout, what="recording stopped")
    return key_ms


async def star(dev: Device) -> int:
    await dev.press(SPACE)
    return (await dev.state()).msg["key_ms"]


async def hold_memo(dev: Device, hold_ms: int) -> int:
    """Hold G0 for hold_ms (G0Long at 500 ms starts the memo, the release stops it MEMO_GRACE_MS later);
    returns the device time G0 went down. Waits until the memo is live, not for its end."""
    await dev.press(k.Press(("g0",), hold_ms), pace=False)
    started = await dev.wait_state(
        lambda s: live(s) and rec_of(s).get("mode") == "memo", 3, interval=0.05, what="memo live"
    )
    return started.msg["key_ms"]


# --- what landed on the Mac ---------------------------------------------------------------------


def archived(rig: Rig) -> dict[str, Mapping[str, Any]]:
    return {e["rec"]: e for e in events(rig, "rec.archived")}


def known(rig: Rig) -> set[str]:
    """Sessions the daemon has seen so far (archived, or with a stored segment): a scenario's own
    recording is the one that is not in here. A leftover session from an earlier run may be archived
    (with gaps) in the middle of a later scenario, so `archived` alone is not enough."""
    return set(archived(rig)) | {e["rec"] for e in events(rig, "rec.seg.stored")}


async def new_archives(rig: Rig, before: set[str], modes: Sequence[str], timeout: float) -> list[Mapping[str, Any]]:
    """The sessions archived since `before`, one per mode in modes (in that order)."""

    def found() -> list[Mapping[str, Any]] | None:
        fresh = [e for r, e in sorted(archived(rig).items()) if r not in before]
        picked = [next((e for e in fresh if e["mode"] == m), None) for m in modes]
        return picked if all(picked) and len(fresh) >= len(modes) else None

    return await until(found, timeout, 0.2, what=f"{'+'.join(modes)} archived")


@dataclass(frozen=True, slots=True)
class Landed:
    folder: Path
    manifest: Mapping[str, Any]
    contract: tuple[str, tuple[str, ...]]
    problems: Mapping[str, tuple[str, ...]]  # segment file -> what is wrong (size/header/sha256/samples)
    device_sha_bad: tuple[str, ...]  # segment files whose data SHA differs from the device's .M
    modes_ok: bool  # session dir 0700, files 0600


def _mode(path: Path) -> int:
    return stat.S_IMODE(os.lstat(path).st_mode)


def landed(rig: Rig, session: Mapping[str, Any]) -> Landed:
    folder = rig.notes_dir / session["dir"]
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    stored = {e["id"]: e for e in events(rig, "rec.seg.stored", rec=session["rec"])}
    problems, bad = {}, []
    for seg in manifest["segments"]:
        blob = (folder / seg["file"]).read_bytes()
        found = segment_problems(blob, seg)
        problems = {**problems, seg["file"]: found} if found else problems
        device_sha = (stored.get(seg["id"]) or {}).get("sha256")
        if device_sha and hashlib.sha256(blob[44:]).hexdigest() != device_sha:
            bad.append(seg["file"])
    files = [folder / s["file"] for s in manifest["segments"]] + [folder / "manifest.json"]
    modes = _mode(folder) == 0o700 and all(_mode(f) == 0o600 for f in files)
    return Landed(folder, manifest, check_doc("manifest", manifest), problems, tuple(bad), modes)


def landed_checks(rig: Rig, session: Mapping[str, Any], prefix: str = "") -> list[dict[str, Any]]:
    got = landed(rig, session)
    return [
        check(f"{prefix}manifest_contract_v1", got.contract == ("ok", ()), verdict=list(got.contract)),
        check(f"{prefix}segments_intact", not got.problems, problems=got.problems),
        check(f"{prefix}device_sha_matches", not got.device_sha_bad, bad=list(got.device_sha_bad)),
        check(f"{prefix}modes_0700_0600", got.modes_ok),
    ]


async def card_cleaned(ctx: Ctx, ids: Sequence[str]) -> list[dict[str, Any]]:
    final = await ctx.dev.wait_state(lambda s: rec_of(s).get("files_n") == 0, 60, what="card empty")
    with contextlib.suppress(TimeoutError):  # the last ack's rec.seg.deleted lands just after files_n hits 0
        await until(lambda: all(once_each(ctx.rig, "rec.seg.deleted", ids).values()), 10, 0.1)
    deleted = once_each(ctx.rig, "rec.seg.deleted", ids)
    stored = once_each(ctx.rig, "rec.seg.stored", ids)
    return [
        check("card_empty", rec_of(final.msg).get("files_n") == 0, files_n=rec_of(final.msg).get("files_n")),
        check("stored_once_each", set(stored.values()) == {1}, stored=stored),
        check("deleted_once_each", set(deleted.values()) == {1}, deleted=deleted),
    ]


def once_each(rig: Rig, ev: str, ids: Sequence[str]) -> dict[str, int]:
    """How many `ev` events each id has (1 each = no duplicates)."""
    seen = [e["id"] for e in events(rig, ev)]
    return {i: seen.count(i) for i in ids}


def rec_lines(rig: Rig, sid: str) -> tuple[RecLine, ...]:
    days = sorted(rig.notes_dir.glob("*.md")) if rig.notes_dir.is_dir() else []
    lines = (line for p in days for line in p.read_text(encoding="utf-8").splitlines())
    return tuple(r for r in map(parse_rec_line, lines) if r is not None and r.sid == sid)


async def note_lines(rig: Rig, sid: str, timeout: float = 20.0) -> tuple[RecLine, ...]:
    return await until(lambda: rec_lines(rig, sid), timeout, 0.2, what=f"daily-note line for {sid}")


async def result_seen(rig: Rig, sid: str, timeout: float = 30.0) -> Mapping[str, Any]:
    return await until(
        lambda: next(iter(events(rig, "rec.result", rec=sid)), None), timeout, 0.2, what=f"rec.result for {sid}"
    )


@contextlib.asynccontextmanager
async def watching(rig: Rig, text_for: Callable[[Mapping[str, Any]], str]) -> AsyncIterator[FakeWoof]:
    """fakewoof on <notes_dir>/recordings for the duration of the block."""
    woof, stop = FakeWoof(rig.notes_dir / "recordings", text_for=text_for), asyncio.Event()
    task = asyncio.create_task(woof.watch(stop, interval=0.5))
    try:
        yield woof
    finally:
        stop.set()
        await task


def transfer(recs: Sequence[Rec], ids: Sequence[str]) -> dict[str, Any]:
    """App-level pull rate from the tap: distinct payload bytes over first file.get -> last file.data."""
    rates = file_rates(recs)
    per = [rates[i] for i in ids if i in rates]
    total_b, total_s = sum(r.bytes for r in per), sum(r.seconds for r in per)
    kb_s = total_b / 1024 / total_s if total_s > 0 else 0.0
    return {
        "files": len(per),
        "bytes": total_b,
        "seconds": round(total_s, 3),
        "kb_s": round(kb_s, 1),
        "hour_min": round(HOUR_BYTES / 1024 / kb_s / 60, 1) if kb_s else None,
        "repeats": sum(r.repeats for r in per),
        "per_file_kb_s": [round(r.kb_s, 1) for r in per],
    }


def seg_ids(manifest: Mapping[str, Any]) -> list[str]:
    return [s["id"] for s in manifest["segments"]]


def samples(manifest: Mapping[str, Any]) -> int:
    return sum(s["samples"] for s in manifest["segments"])


def up0(rig: Rig, sid: str) -> int | None:
    """at.up_ms of the session's segment 0 (device millis() right after Mic.begin)."""
    first = next((e for e in events(rig, "rec.seg.stored", rec=sid) if e.get("seg") == 0), None)
    return ((first or {}).get("at_dev") or {}).get("up_ms")


def counts(rig: Rig, ev: str) -> int:
    return len(events(rig, ev))
