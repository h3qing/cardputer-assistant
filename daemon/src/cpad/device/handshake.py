"""Handshake verdicts and reboot detection (05 §6.4, US-04).

classify() looks at one decoded line while the host waits for a hello reply:
  ok         a valid device hello answering one of this handshake's attempts
  version    E_VERSION from the device, or a reply in another major version
  foreign    a reply to our hello that is not a valid cpa-fw hello (e.g. another firmware)
  unrelated  noise, other traffic, or a reply to something else
"""

from __future__ import annotations

from dataclasses import dataclass

from cpad.protocol.codec import Decoded


@dataclass(frozen=True, slots=True)
class Seen:
    boot: str | None
    up_ms: int


def _answers(decoded: Decoded, attempts: tuple[str, ...]) -> bool:
    return decoded.msg is not None and decoded.msg.get("re") in attempts


def classify(decoded: Decoded, attempts: tuple[str, ...]) -> str:
    if not _answers(decoded, attempts):
        return "unrelated"
    msg = decoded.msg or {}
    if decoded.outcome == "E_VERSION" or (decoded.ok and msg["t"] == "error" and msg.get("code") == "E_VERSION"):
        return "version"
    if decoded.ok and msg["t"] == "hello" and msg.get("role") == "device":
        return "ok"
    return "foreign"


def is_reboot(prev: Seen | None, boot: str | None, up_ms: int) -> bool:
    """boot changed, or the same boot with a smaller up_ms (05 §6.4). A pong has no boot."""
    if prev is None:
        return False
    boot_changed = boot is not None and prev.boot is not None and boot != prev.boot
    return boot_changed or up_ms < prev.up_ms
