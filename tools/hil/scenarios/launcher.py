"""09 §9.3 launcher_boot (with --expect-launcher): the test image installed from the Launcher's SD
menu says so in its hello (launcher true) and was started by the Launcher's software restart.
Run first, before any scenario that reboots the device: the run's first hello is that boot.
"""

from __future__ import annotations

from typing import Any

from harness.expect import launcher_of, store_of
from harness.report import check, result
from scenarios.common import Ctx, first_hello

STORES = ("sd", "int", "none")


async def launcher_boot(ctx: Ctx) -> dict[str, Any]:
    hello = first_hello(ctx)
    checks = [
        check("hello_launcher_true", launcher_of(hello) is True, launcher=launcher_of(hello)),
        check("rst_software", hello.get("rst") == "software", rst=hello.get("rst")),
        check("store_reported", store_of(hello) in STORES, store=store_of(hello)),
    ]
    return result(checks, fw=hello.get("fw"), boot=hello.get("boot"), up_ms=hello.get("up_ms"))
