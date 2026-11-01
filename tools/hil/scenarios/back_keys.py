"""Issue #15 single-key back, injected with debug.key: Backspace on an empty Compose closes it (one
with text deletes first), and the top-left key (`) alone goes back from a list page to the root page
(the home launcher since v0.4.0, docs/13 §5.5; the board before). Fn+` keeps working (Device.home uses
it); this scenario covers only the new single keys.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping
from typing import Any

from harness import keys as k
from harness.report import check, result
from harness.ui import ROOT
from scenarios.common import Ctx

BKSP = k.Press(("bksp",))
GRAVE = k.Press(("`",))
INBOX = k.Press(("opt", "i"))
SYSTEM = k.Press(("opt", "m"))
SETTLE_S = 2.0


async def _after(ctx: Ctx, press: k.Press, want: Callable[[Mapping[str, Any]], bool]) -> tuple[bool, dict[str, Any]]:
    """Press, then whether the device reached `want` (and the last state either way)."""
    await ctx.dev.press(press)
    try:
        sample = await ctx.dev.wait_state(want, SETTLE_S)
    except TimeoutError:
        return False, dict((await ctx.dev.state()).msg)
    return True, dict(sample.msg)


def _view(s: Mapping[str, Any]) -> str:
    return f"{s.get('mod')}/{s.get('overlay')}"


def _overlay(name: str) -> Callable[[Mapping[str, Any]], bool]:
    return lambda s: s.get("overlay") == name


def _page(mod: str) -> Callable[[Mapping[str, Any]], bool]:
    return lambda s: s.get("overlay") == "none" and s.get("mod") == mod


async def _compose_checks(ctx: Ctx) -> list[dict[str, Any]]:
    opened, _ = await _after(ctx, k.G0, _overlay("compose"))
    closed, s1 = await _after(ctx, BKSP, _overlay("none"))
    await _after(ctx, k.G0, _overlay("compose"))
    await ctx.dev.presses(k.text_presses("a"))
    kept, s2 = await _after(ctx, BKSP, _overlay("compose"))
    closed2, s3 = await _after(ctx, BKSP, _overlay("none"))
    return [
        check("g0_opens_compose", opened),
        check("bksp_on_empty_compose_closes_it", closed, view=_view(s1)),
        check("bksp_with_text_deletes_and_stays", kept, view=_view(s2)),
        check("second_bksp_then_closes", closed2, view=_view(s3)),
    ]


async def _page_checks(ctx: Ctx, opener: k.Press, mod: str) -> list[dict[str, Any]]:
    opened, s1 = await _after(ctx, opener, _page(mod))
    back, s2 = await _after(ctx, GRAVE, _page(ROOT))
    return [
        check(f"{mod}_page_opened", opened, view=_view(s1)),
        check(f"grave_goes_back_from_{mod}", back, view=_view(s2)),
    ]


async def back_keys(ctx: Ctx) -> dict[str, Any]:
    await ctx.dev.home()
    checks = await _compose_checks(ctx)
    checks += await _page_checks(ctx, INBOX, "inbox")
    checks += await _page_checks(ctx, SYSTEM, "system")
    return result(checks)
