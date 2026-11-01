"""06 WP5: --scenarios real_claude_smoke --project-dir <empty temp dir>. MANUAL ONLY.

Starts ONE real Claude Code session from the device (plan mode, in the given directory, harmless
prompt), checks it is listed there, then `claude stop` + `claude rm` it. Never part of `all`, never
in CI; run_e2e.py only builds a real-claude rig when this scenario is the only one selected.
"""

from __future__ import annotations

import asyncio
import json
import os
from typing import Any

from harness import keys as k
from harness.report import check, result
from harness.rig import until
from harness.wire import find
from scenarios.common import SLOT, Ctx

PROMPT = "Reply with the single word OK"


async def _claude(claude: str, *args: str) -> tuple[int, str]:
    proc = await asyncio.create_subprocess_exec(claude, *args, stdout=asyncio.subprocess.PIPE,
                                                stderr=asyncio.subprocess.DEVNULL)  # fmt: skip
    out, _ = await asyncio.wait_for(proc.communicate(), 30)
    return proc.returncode or 0, out.decode("utf-8", "replace")


def _listed(stdout: str, short: str) -> dict[str, Any] | None:
    try:
        doc = json.loads(stdout)
    except ValueError:
        return None
    if not isinstance(doc, list):
        return None
    return next((s for s in doc if isinstance(s, dict) and s.get("id") == short), None)


async def real_claude_smoke(ctx: Ctx) -> dict[str, Any]:
    rig, dev, tap = ctx.rig, ctx.dev, ctx.rig.tap
    claude = rig.daemon.config.claude_bin
    await dev.home()
    mark = tap.size()
    await dev.presses(k.spawn_presses(PROMPT, SLOT))
    await dev.wait_state(lambda s: s["overlay"] == "confirm", 3, what="Confirm open")
    await asyncio.sleep(0.6)
    await dev.presses((k.ENTER,))
    i = await until(lambda: find(tap.since(mark), "tx", "agent.result"), 40, what="agent.result")
    reply = tap.since(mark)[i].msg
    short = reply.get("short") or ""
    listing = _listed((await _claude(claude, "agents", "--json", "--all"))[1], short) if short else None
    stop_rc = (await _claude(claude, "stop", short))[0] if short else None
    rm_rc = (await _claude(claude, "rm", short))[0] if short else None
    cwd = os.path.realpath(listing.get("cwd", "")) if listing else None
    checks = [
        check("spawned", reply.get("ok") is True and bool(short), code=reply.get("code"), short=short),
        check("listed_in_project_dir", cwd == str(rig.project_root), cwd=cwd),
        check("stopped", stop_rc == 0, rc=stop_rc),
        check("removed", rm_rc == 0, rc=rm_rc),
    ]
    return result(checks, name=reply.get("name"), short=short)
