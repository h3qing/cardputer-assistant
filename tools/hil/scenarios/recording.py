"""docs/12 M3-H recording scenarios (docs/11 §3, §5.4, §6, §7; test firmware with the card):

rec_memo       hold G0 4 s (it stops 0.6 s after the release, gh #51) -> seg-000.wav lands (SHA, header,
               0600) -> manifest -> fakewoof -> one '- [ ]' daily line -> card empty -> no queue.added; plus a
               dry check of the 30-day audio sweep.
rec_meeting    seg_ms 30 s; Opt+R, Enter; tag 1; 2 stars; 70 s; Opt+R, Enter -> 3 sample-exact segments,
               consent ack, marks in session time, summary line; app-level pull rate (target >= 100 KB/s).
rec_resume     a 3-segment meeting; `cpa pause` while the second segment is being pulled -> the next
               connection resumes from the .part size - 64 KiB, no duplicate events.
rec_live_hold  a 3-segment meeting; mid-pull, hold G0 for a memo -> E_BUSY 'live' window (docs/11 §5.4),
               then the pull resumes by offset and both sessions land intact.
rec_nosd       nosd firmware: no `files`, no debug.state.rec, G0 hold and Opt+R refused, no file.*.
The long and destructive ones (rec_reboot, rec_rotation, rec_soak) are in scenarios/rec_long.py.
"""

from __future__ import annotations

import asyncio
import time
from typing import Any

from cpad.recordings import retention

from harness import keys as k
from harness.rec_checks import busy_window, contiguous_t0, gets_by_connection, mark_errors, meeting_line, timeline
from harness.report import check, result
from harness.rig import until
from scenarios.common import Ctx, events, token
from scenarios.rec_common import (
    MEMO_GRACE_MS,
    OPEN_1,
    OPT_R,
    TAG_1,
    card_cleaned,
    counts,
    end_meeting,
    hold_memo,
    known,
    landed,
    landed_checks,
    live,
    new_archives,
    note_lines,
    result_seen,
    samples,
    seg_ids,
    set_seg_ms,
    settle,
    sleep_until,
    star,
    start_meeting,
    transfer,
    up0,
    watching,
)

MEMO_HOLD_MS = 4000
MEETING_S = 70.0
STAR_AT_S = (20.0, 50.0)
SEG_MS = 30000
DAY_MS = 86_400_000
MARK_TOL_MS = 400  # a star is stamped from the samples written so far: up to a block or two behind
TARGET_KB_S = 100.0
FLOOR_HOUR_MIN = 20.0
RESUME_PAUSE_S = 4
CROSSING_MS = 20.0  # a file.* written this soon after the first E_BUSY reached the tap crossed it on the wire


def events_ids(rig) -> list[str]:
    return [e["rec"] for e in events(rig, "rec.archived")]


def _retention_dry(rig, sid: str, folder) -> list[dict[str, Any]]:
    """The 30-day sweep (cpad recordings/retention.py) without deleting anything."""
    idx, now = rig.daemon.recordings.index, int(time.time() * 1000)
    text_ok = all((folder / n).is_file() for n in retention.TEXT_NEEDED)
    return [
        check("retention_not_due_now", sid not in retention.due(idx, now)),
        check("retention_due_after_30_days", sid in retention.due(idx, now + 30 * DAY_MS + 60_000)),
        check("retention_text_kept_files_present", text_ok),
        check("retention_nothing_swept", counts(rig, "rec.audio.swept") == 0),
    ]


async def rec_memo(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    await settle(dev)
    text = f"待办 HIL 便条 {token(4)}"
    before, queued0, mark = known(rig), counts(rig, "queue.added"), tap.size()
    async with watching(rig, lambda _m: text):
        await dev.press(OPEN_1)  # the context slot is the project of the memo
        key_ms = await hold_memo(dev, MEMO_HOLD_MS)
        await dev.wait_state(lambda s: not live(s), 10, what="memo saved")
        (session,) = await new_archives(rig, before, ("memo",), 60)
        sid = session["rec"]
        res = await result_seen(rig, sid)
        lines = await note_lines(rig, sid)
    got = landed(rig, session)
    stop_ms = key_ms + MEMO_HOLD_MS + MEMO_GRACE_MS  # gh #51: it records on through the grace
    tl = timeline(start_key_ms=key_ms, up0_ms=up0(rig, sid) or 0, stop_key_ms=stop_ms, samples=samples(got.manifest))
    line = lines[0]
    checks = [
        *landed_checks(rig, session),
        check("one_segment", [s["file"] for s in got.manifest["segments"]] == ["seg-000.wav"]),
        check("project_cpa", (got.manifest["project"] or {}).get("key") == "cpa", project=got.manifest["project"]),
        check("memo_start_le_1s", tl.start_latency_ms <= 1000, start_ms=tl.start_latency_ms),
        check("samples_match_hold", tl.within(4096), diff_samples=tl.diff_samples, wall_ms=tl.wall_ms),
        check("result_done", res.get("state") == "done" and res.get("memo_intent") == "todo", result=dict(res)),
        check(
            "one_daily_line_todo",
            len(lines) == 1 and line.todo and line.tag == "cpa" and text in line.body,
            lines=len(lines),
        ),
        *(await card_cleaned(ctx, seg_ids(got.manifest))),
        check("no_queue_added", counts(rig, "queue.added") == queued0),
        *_retention_dry(rig, sid, got.folder),
    ]
    return result(
        checks,
        sid=sid,
        duration_ms=got.manifest["duration_ms"],
        lead_ms=got.manifest["lead_ms"],
        start_latency_ms=tl.start_latency_ms,
        diff_samples=tl.diff_samples,
        transfer=transfer(tap.since(mark), seg_ids(got.manifest)),
    )


async def _meeting(ctx: Ctx, seconds: float, stars: tuple[float, ...] = ()) -> dict[str, Any]:
    """Record one meeting of `seconds`; returns device key times."""
    dev = ctx.dev
    start_key = await start_meeting(dev)
    t0 = time.monotonic()
    await dev.press(TAG_1)
    star_keys = []
    for at in stars:
        await sleep_until(t0 + at)
        star_keys.append(await star(dev))
    await sleep_until(t0 + seconds)
    stop_key = await end_meeting(dev)
    return {"start_key": start_key, "stop_key": stop_key, "star_keys": tuple(star_keys)}


def _meeting_checks(rig, session, keys: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    got = landed(rig, session)
    m, first = got.manifest, up0(rig, session["rec"]) or 0
    tl = timeline(start_key_ms=keys["start_key"], up0_ms=first, stop_key_ms=keys["stop_key"], samples=samples(m))
    errs = mark_errors(m["marks"], star_key_ms=keys["star_keys"], up0_ms=first)
    full = [s["samples"] for s in m["segments"][:-1]]
    checks = [
        *landed_checks(rig, session),
        check("three_segments", len(m["segments"]) == 3, segments=len(m["segments"])),
        check("full_segments_sample_exact", full == [SEG_MS * 16] * 2, samples=full),
        check("t0_contiguous_no_gaps", contiguous_t0(m), gaps=m["gaps"]),
        check("consent_ack", m.get("consent") == "ack"),
        check("project_cpa", (m["project"] or {}).get("key") == "cpa", project=m["project"]),
        check(
            "marks_in_session_time",
            errs is not None and all(abs(e) <= MARK_TOL_MS for e in errs),
            errors_ms=errs,
            marks=m["marks"],
        ),
        check("meeting_start_le_0_8s", tl.start_latency_ms <= 800, start_ms=tl.start_latency_ms),
        check("samples_match_wall", tl.within(4096), diff_samples=tl.diff_samples, wall_ms=tl.wall_ms),
    ]
    return checks, {
        "start_latency_ms": tl.start_latency_ms,
        "diff_samples": tl.diff_samples,
        "mark_errors_ms": errs,
        "duration_ms": m["duration_ms"],
        "ids": seg_ids(m),
    }


def _rate_checks(rate: dict[str, Any]) -> list[dict[str, Any]]:
    hour = rate["hour_min"]
    return [
        check("app_kb_s_ge_100", rate["kb_s"] >= TARGET_KB_S, kb_s=rate["kb_s"]),
        check("hour_le_20_min", hour is not None and hour <= FLOOR_HOUR_MIN, hour_min=hour),
    ]


async def rec_meeting(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    await settle(dev)
    await set_seg_ms(dev, SEG_MS)
    before, queued0, mark = known(rig), counts(rig, "queue.added"), tap.size()
    async with watching(rig, lambda _m: "HIL 会议：定价和交付"):
        keys = await _meeting(ctx, MEETING_S, STAR_AT_S)
        (session,) = await new_archives(rig, before, ("meeting",), 120)
        sid = session["rec"]
        res = await result_seen(rig, sid)
        lines = await note_lines(rig, sid)
    checks, info = _meeting_checks(rig, session, keys)
    rate = transfer(tap.since(mark), info["ids"])
    body = lines[0].body if lines else ""
    checks += [
        check("result_summary", res.get("state") == "done" and res.get("level") == "summary"),
        check(
            "one_meeting_line",
            len(lines) == 1 and meeting_line(body, todos=1) and lines[0].tag == "cpa",
            lines=[ln.body for ln in lines],
        ),
        *(await card_cleaned(ctx, info["ids"])),
        check("no_queue_added", counts(rig, "queue.added") == queued0),
        *_rate_checks(rate),
    ]
    return result(checks, sid=sid, transfer=rate, **{k: v for k, v in info.items() if k != "ids"})


async def _record_3_segments(ctx: Ctx) -> tuple[set[str], int]:
    """A 65 s meeting in 30 s segments; the sessions known before it and the tap index at its end
    (the card was empty before it, so every file.data after that index is this meeting's)."""
    await settle(ctx.dev)
    await set_seg_ms(ctx.dev, SEG_MS)
    before = known(ctx.rig)
    await _meeting(ctx, 65.0)
    return before, ctx.rig.tap.size()


def _data_seen(tap, mark: int, nth_file: int, min_off: int):
    """(fid, off) once the nth distinct file of the pull has a file.data at or past min_off."""
    fids: list[str] = []
    for r in tap.since(mark):
        if r.dir != "rx" or not r.msg or r.msg.get("t") != "file.data":
            continue
        fid = r.msg["fid"]
        fids = fids if fid in fids else [*fids, fid]
        if fids.index(fid) == nth_file and r.msg["off"] >= min_off:
            return fid, r.msg["off"]
    return None


async def rec_resume(ctx: Ctx) -> dict[str, Any]:
    rig, tap = ctx.rig, ctx.rig.tap
    before, mark = await _record_3_segments(ctx)
    dev_id = rig.link.hello.dev
    fid, _ = await until(lambda: _data_seen(tap, mark, 1, 256 * 1024), 60, 0.01, what="second segment mid-pull")
    await rig.pause(RESUME_PAUSE_S)
    await rig.released()
    await asyncio.sleep(1.0)  # the puller is cancelled with the link: its .part is final before the reopen
    part = rig.paths.home / "rec-staging" / dev_id / f"{fid}.part"
    part_size = part.stat().st_size if part.exists() else 0
    await rig.ready(timeout=30)
    (session,) = await new_archives(rig, before, ("meeting",), 120)
    m = landed(rig, session).manifest
    conns = gets_by_connection(tap.since(mark), fid)
    expect = max(0, part_size - 64 * 1024)
    ids = seg_ids(m)
    checks = [
        *landed_checks(rig, session),
        check("pulled_over_two_connections", len(conns) >= 2, connections=len(conns)),
        check(
            "resumed_from_part_minus_64k",
            len(conns) >= 2 and conns[-1][0] == expect > 0,
            first_off=conns[-1][0] if conns else None,
            part_size=part_size,
            expected=expect,
        ),
        check("archived_once", events_ids(rig).count(session["rec"]) == 1),
        *(await card_cleaned(ctx, ids)),
    ]
    return result(
        checks,
        sid=session["rec"],
        fid=fid,
        part_size=part_size,
        resume_off=expect,
        transfer=transfer(tap.since(mark), ids),
    )


def _inside_detail(recs, w) -> list[dict[str, Any]]:
    """Each file.* sent inside the E_BUSY window: ms after the first E_BUSY reached the tap, and what."""
    t0 = recs[w.start].t if w.start is not None else 0.0
    sent = [r for r in recs if r.dir == "tx" and r.msg and r.msg.get("id") in w.sent_inside]
    return [{"ms": round((r.t - t0) * 1000, 1), "t": r.msg["t"], "off": r.msg.get("off")} for r in sent]


def _after_window(recs, end: int | None, fid: str) -> tuple[list[str], int]:
    """Files asked for after the window (pull order), and file.get of the interrupted file at offset 0."""
    gets = [(i, r.msg) for i, r in enumerate(recs) if r.dir == "tx" and r.msg and r.msg.get("t") == "file.get"]
    order = list(dict.fromkeys(g["fid"] for i, g in gets if i >= (end if end is not None else len(recs))))
    return order, sum(1 for _, g in gets if g["fid"] == fid and g["off"] == 0)


async def rec_live_hold(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    before, mark = await _record_3_segments(ctx)
    fid, off = await until(lambda: _data_seen(tap, mark, 0, 100 * 1024), 60, 0.01, what="meeting mid-pull")
    hold_mark = tap.size()
    await hold_memo(dev, 4500)
    await dev.wait_state(lambda s: not live(s), 15, what="memo saved")
    meeting, memo = await new_archives(rig, before, ("meeting", "memo"), 180)
    recs = tap.since(hold_mark)
    w = busy_window(recs)
    order, restarts = _after_window(recs, w.end, fid)
    inside = _inside_detail(recs, w)
    m_ids, memo_ids = seg_ids(landed(rig, meeting).manifest), seg_ids(landed(rig, memo).manifest)
    rest = [i for i in m_ids if i in order and i != fid]
    checks = [
        check("e_busy_window_opened_and_cleared", w.start is not None and w.end is not None),
        check(
            "device_refused_with_live_only",
            w.ok(max_late_data=1),
            sent_inside=list(w.sent_inside),
            non_busy=list(w.non_busy_inside),
            data_inside=w.data_inside,
            busy=list(w.busy_msgs),
        ),
        check("host_sent_no_file_requests_while_busy", not w.host_late(CROSSING_MS), sent=inside),
        check("interrupted_file_not_restarted", restarts == 0, restarts=restarts),
        check(
            "memo_pulled_before_remaining_meeting_segments",
            not rest or order.index(memo_ids[0]) < order.index(rest[0]),
            order=order,
        ),
        *landed_checks(rig, meeting, "meeting_"),
        *landed_checks(rig, memo, "memo_"),
        *(await card_cleaned(ctx, m_ids + memo_ids)),
    ]
    return result(
        checks,
        interrupted=fid,
        interrupted_at=off,
        live_status_seen=w.live_seen,
        busy_replies=len(w.busy_msgs),
        late_data=w.data_inside,
        meeting=meeting["rec"],
        memo=memo["rec"],
    )


async def rec_nosd(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    hello = rig.link.hello
    mark = tap.size()
    await dev.home()
    s0 = (await dev.state()).msg
    await dev.press(k.Press(("g0",), 1500))
    s1 = await dev.wait_state(lambda s: s.get("overlay") == "none", 3, what="no overlay after G0 hold")
    await dev.press(OPT_R)
    await asyncio.sleep(0.6)
    s2 = (await dev.state()).msg
    file_reqs = [r for r in tap.since(mark) if r.dir == "tx" and r.msg and str(r.msg.get("t", "")).startswith("file.")]
    raw_hello = next(
        (r.msg for r in reversed(tap.since()) if r.dir == "rx" and r.msg and r.msg.get("t") == "hello"), {}
    )
    checks = [
        check("store_not_sd", getattr(hello, "store", None) != "sd", store=getattr(hello, "store", None)),
        check("hello_without_files", hello.files is None and "files" not in raw_hello),
        check("debug_state_without_rec", "rec" not in s0 and "rec" not in s1.msg),
        check("g0_hold_refused", s1.msg["overlay"] == "none" and s1.msg["outbox"] == s0["outbox"]),
        check("opt_r_refused", s2["overlay"] == "none" and "rec" not in s2),
        check("no_file_requests", not file_reqs, sent=len(file_reqs)),
    ]
    return result(checks, store=getattr(hello, "store", None), fw=hello.fw)
