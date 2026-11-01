"""05 §9.3 item 10: real Chinese session names on the device, and which glyphs it lacks.

The names are the real local session names quoted in docs/06 (the G-list's Chinese glyph coverage item) and the
redacted daemon fixture (daemon/tests/fixtures/agents/); ~/.claude is not read. The device must
accept and ack the snapshot, show the project page and the inbox; which characters would be
drawn as □ is computed from the efontCN_12 table the firmware links (harness.glyphs). Whether
the screen looks right is a human check (docs/07-hil-checklist.md).
"""

from __future__ import annotations

import secrets
from typing import Any

from harness import keys as k
from harness.fakeclaude import bg_session, tty_session
from harness.glyphs import GlyphError, find_font_source, font_codepoints, missing
from harness.report import check, result
from harness.rig import until
from harness.wire import find, reply_to
from scenarios.common import FIRMWARE_DIR, Ctx, revs_synced

NAMES = (
    ("设置页面和字体修复", "tty", "waiting"),
    ("Acme Web 登录页面改版", "tty", "idle"),
    ("cpa/等待权限确认", "bg", "waiting"),
    ("docs/写一份周报", "bg", "busy"),
    ("scratch/跑测试", "tty", "busy"),
    ("登录页面改版的设置页面和字体修复问题", "bg", "idle"),  # 54 B: the daemon cuts it to <= 32 B
)
# A background session's state beside its status, as the CLI prints them (daemon/tests/fixtures/agents/
# agents-2.1.306.json). Only "blocked" makes cpad count a non-waiting bg session as needing you (#5).
BG_STATE = {"waiting": "blocked", "busy": "working", "idle": "done"}


def _sessions(root: str) -> list[dict[str, Any]]:
    out = []
    for name, kind, status in NAMES:
        sid = secrets.token_hex(4)
        if kind == "tty":
            out.append(tty_session(sid, name, root, status))
        else:
            out.append(bg_session(sid, name, root, status, BG_STATE[status]))
    return out


def _coverage(texts: list[str]) -> dict[str, Any]:
    source = find_font_source(FIRMWARE_DIR)
    if source is None:
        return {"computed": False, "why": "M5GFX efont source not found (build cardputer-adv-test first)"}
    try:
        cps = font_codepoints(source)
    except (GlyphError, OSError) as exc:
        return {"computed": False, "why": f"{type(exc).__name__}: {exc}"}
    return {"computed": True, "font": "efontCN_12", "glyphs": len(cps), "missing": list(missing(texts, cps))}


async def _visit(ctx: Ctx, press: k.Press, mod: str) -> bool:
    await ctx.dev.press(press)
    s = await ctx.dev.wait_state(lambda s: s["mod"] == mod and s["overlay"] == "none", 3, what=f"{mod} page")
    return s.msg["mod"] == mod


async def cjk(ctx: Ctx) -> dict[str, Any]:
    rig, tap = ctx.rig, ctx.rig.tap
    await ctx.dev.home()
    mark = tap.size()
    rig.fake.set_sessions(_sessions(str(rig.project_root)))
    full = lambda m: len(m["sessions"]) == len(NAMES)  # noqa: E731
    i = await until(lambda: find(tap.since(mark), "tx", "sessions.snapshot", where=full), 15,
                    what="sessions.snapshot with every name")  # fmt: skip
    snap = tap.since(mark)[i].msg
    ack = await until(lambda: reply_to(tap.since(mark), snap["id"]), 5, what="device reply to the snapshot")
    synced = await ctx.dev.wait_state(revs_synced(rig), 10, what="device revs equal host revs")
    page = await _visit(ctx, k.Press(("opt", "1")), "board")
    inbox = await _visit(ctx, k.Press(("opt", "i")), "inbox")
    await ctx.dev.home()
    errors = [r.msg for r in tap.since(mark) if r.dir == "rx" and r.msg and r.msg.get("t") == "error"]
    sent = [s["name"] for s in snap["sessions"]]
    shown = sent + [rig.daemon.state.projects[0].name]
    cover = _coverage(shown)
    checks = [
        check("snapshot_acked", ack.msg.get("t") == "ack", reply=ack.msg.get("t")),
        check("no_device_errors", not errors, errors=errors[:3]),
        check("names_at_most_32_bytes", all(len(n.encode()) <= 32 for n in sent)),
        check("device_revs_match_host", synced.msg["revs"]["sessions"] == snap["rev"], revs=synced.msg["revs"]),
        check("inbox_counts_waiting", synced.msg["need"] == 2, need=synced.msg["need"]),
        check("project_page_and_inbox_shown", page and inbox),
        check("glyph_coverage_recorded", cover["computed"], why=cover.get("why")),
    ]
    truncated = sorted(set(sent) - {name for name, _, _ in NAMES})
    return result(checks, names_sent=sent, truncated=truncated, glyphs=cover)
