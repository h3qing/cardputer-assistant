"""docs/21 §4.3, §9.4: the UI language through the device's own keys. From home, Opt+M . Enter shows the
other language at once (debug.state ui.lang: Chinese on a device in English); after debug.reboot the new boot
still shows it (NVS); Opt+M . Enter switches back, which a reboot keeps too (to English when the start is
unknown). The runner puts the starting language back at the end of every run anyway.
"""

from __future__ import annotations

from typing import Any

from harness.lang import FALLBACK_LANG, SWITCH_KEYS, shown_lang
from harness.report import check, result
from harness.rig import until
from scenarios.common import SETTLE_S, Ctx


async def _switch(ctx: Ctx, want: str) -> tuple[bool, str | None]:
    await ctx.dev.home()
    await ctx.dev.presses(SWITCH_KEYS)
    try:
        s = await ctx.dev.wait_state(lambda m: shown_lang(m) == want, SETTLE_S, what=f"ui.lang {want}")
    except TimeoutError:
        return False, shown_lang((await ctx.dev.state()).msg)
    return True, shown_lang(s.msg)


async def _rebooted_lang(ctx: Ctx) -> str | None:
    old = ctx.rig.link.hello.boot
    await ctx.dev.reboot()
    await until(lambda: ctx.rig.link if ctx.rig.link and ctx.rig.link.hello.boot != old else None, 30,
                what="handshake with a new boot id")  # fmt: skip
    return shown_lang((await ctx.dev.state()).msg)


async def lang(ctx: Ctx) -> dict[str, Any]:
    start = shown_lang((await ctx.dev.state()).msg)
    checks = [check("reports_its_language", start in ("zh", "en"), got=start)]
    other = "en" if start == "zh" else "zh"
    ok, got = await _switch(ctx, other)
    checks.append(check("opt_m_dot_enter_switches", ok, want=other, got=got))
    after = await _rebooted_lang(ctx)
    checks.append(check("switch_survives_reboot", after == other, want=other, got=after))
    ok, got = await _switch(ctx, start or FALLBACK_LANG)
    checks.append(check("opt_m_dot_enter_switches_back", ok, want=start, got=got))
    after = await _rebooted_lang(ctx)
    checks.append(check("back_survives_reboot", after == start, want=start, got=after))
    await ctx.dev.home()
    return result(checks, start=start)
