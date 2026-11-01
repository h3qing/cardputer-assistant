"""Confirm an install another way when the OK line was lost in the reboot (pure).

Partition diff: on the next Launcher session exactly one new OTA app partition must exist, at least as
big as the image, and marked BOOT (the install selects it). Daemon handshake: our firmware booted under
the Launcher (`launcher=yes`) and reports the version the image name promises (cpa-vX.Y.Z -> cpa-fw/X.Y.Z;
a reinstall of the same version cannot be told apart this way, the partition diff can).
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any

from cpalauncher.console import Table, label_base

_CPA_NAME_RE = re.compile(r"^cpa-v(\d+\.\d+\.\d+)")


@dataclass(frozen=True, slots=True)
class Verdict:
    ok: bool
    detail: str


def verify_partitions(before: Table, after: Table, name: str, size: int) -> Verdict:
    if not after.complete:
        return Verdict(False, "the partition table printout was cut short")
    new = [p for p in after.ota_apps() if before.find(p.label) is None]
    if len(new) != 1:
        return Verdict(False, f"expected one new app partition, found {len(new)}")
    app = new[0]
    if app.size < size:
        return Verdict(False, f"{app.label} (0x{app.size:X} B) is smaller than the image ({size} B)")
    if not app.boot:
        return Verdict(False, f"{app.label} is there but not selected to boot")
    base = label_base(name)
    note = "" if app.label[:5] == base[:5] else f" (label {app.label}; expected {base} or {base[:5]}N)"
    return Verdict(True, f"{app.label} at 0x{app.offset:06X}, 0x{app.size:X} B, selected to boot{note}")


def expected_fw(name: str) -> str | None:
    match = _CPA_NAME_RE.match(name)
    return f"cpa-fw/{match.group(1)}" if match else None


def _fw_matches(fw: Any, expect: str) -> bool:
    return isinstance(fw, str) and (fw == expect or fw.startswith((expect + "-", expect + "+")))


def _shown(value: Any) -> str:
    if value is None:
        return "-"
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def daemon_verdict(doc: dict[str, Any] | None, expect_fw: str | None) -> tuple[bool, str]:
    """(done, summary) from `cpa status --json`; the summary carries no user content."""
    if not doc or not doc.get("running"):
        return False, "cpad is not running"
    if doc.get("link") != "ready":
        return False, f"link {_shown(doc.get('link'))}"
    device = doc.get("device") or {}
    fw, launcher = device.get("fw"), device.get("launcher")
    if launcher is not True:
        return False, f"link ready but launcher={_shown(launcher)}"
    if expect_fw and not _fw_matches(fw, expect_fw):
        return False, f"link ready but fw={_shown(fw)} (expected {expect_fw})"
    return True, f"link ready fw={_shown(fw)} launcher=yes store={_shown(device.get('store'))}"
