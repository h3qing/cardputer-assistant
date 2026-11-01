"""The routing decision (docs/25 §5.2, docs/14 §5). Pure: no files, no clock; same inputs, same answer.

decide() first runs the preconditions in order; the first one that fails downgrades the capture to the daily
note (whose line is already written), never to a paste, a copy, a link or an execution. Otherwise verdict()
applies the 12-column table, reading the destination's fl (model.flags / model.klass), never its name.
Invariant 2 (docs/25 §5.2): a live kind delivers only when confirmed on the device while online; for a typed
live key (clip, drafts); for a voice live capture, only to a verified draft; or confirmed on this Mac
(origin "mac", drafts and clip only). Sync, restart and retry never open, copy or paste. Kinds that execute (k)
take only the device's confirmation. decide() never spawns a session; the text never picks the place.
"""

from __future__ import annotations

from dataclasses import dataclass

from cpad.dest.model import (
    CONFIRM_DUR_MAX_MS,
    CONFIRM_TEXT_MAX,
    DAILY,
    LIVE_WINDOW_MS,
    Capture,
    Decision,
    Dest,
    Origin,
    Reason,
    Source,
    flags,
    klass,
    spec,
)

EXPLICIT = ("key", "pick")
MEETING_KINDS = ("daily", "todo", "file")


@dataclass(frozen=True, slots=True)
class DeviceClock:
    boot: str  # hello.boot
    up_ms: int  # device up_ms now (last hello / pong + elapsed)


def _boot_of(item_id: str) -> str | None:
    parts = item_id.split("-") if isinstance(item_id, str) else []
    return "-".join(parts[:2]) if len(parts) == 3 else None


def origin_of(
    live: bool | None, item_id: str, at_up_ms: int, clock: DeviceClock | None, window_ms: int = LIVE_WINDOW_MS
) -> Origin:
    """`live` only for a flagged item of this boot, captured at most window_ms ago (device time); else `queued`."""
    if live is not True or clock is None or _boot_of(item_id) != clock.boot:
        return "queued"
    return "live" if 0 <= clock.up_ms - at_up_ms <= window_ms else "queued"


def _meeting_ok(dest: Dest | None) -> bool:
    return dest is not None and dest.kind in MEETING_KINDS and not dest.leaves_mac


def _other_profile(capture: Capture, dest: Dest) -> bool:
    return bool(dest.profiles) and capture.prof not in dest.profiles


def _too_long(capture: Capture) -> bool:
    long_voice = capture.source == "voice" and (capture.dur_ms or 0) > CONFIRM_DUR_MAX_MS
    return long_voice or len((capture.text or "").encode("utf-8")) > CONFIRM_TEXT_MAX


def precondition(capture: Capture, dest: Dest | None) -> Reason | None:
    """The first docs/25 §5.2 check that fails, or None."""
    if capture.meeting and not _meeting_ok(dest):
        return "meeting"
    if dest is None:
        return "unknown"
    fl = flags(dest)
    checks: tuple[tuple[bool, Reason], ...] = (
        (not spec(dest).available, "unavailable"),
        (not dest.enabled, "disabled"),
        (_other_profile(capture, dest), "other_profile"),
        (dest.leaves_mac and dest.optin is None, "no_optin"),
        (capture.how not in EXPLICIT and any(f in fl for f in "lkc"), "not_explicit"),
        (not capture.text, "no_text"),
        ("p" in fl and not capture.project, "no_project"),
        ("k" in fl and _too_long(capture), "too_long"),
    )
    return next((reason for failed, reason in checks if failed), None)


def _live(dest: Dest, source: Source, origin: Origin, online: bool) -> Decision:
    """Clip and drafts (fl l without k)."""
    if origin == "mac":
        return Decision("deliver", dest.id, "mac")
    if not online:
        return Decision("hold", dest.id, "await_device")
    if origin == "confirmed":
        return Decision("deliver", dest.id, "confirmed")
    if origin == "live" and (source == "typed" or klass(dest) == "draft"):
        return Decision("deliver", dest.id, "live_key")
    return Decision("hold", dest.id, "await_confirm")


def _exec(dest: Dest, origin: Origin, online: bool) -> Decision:
    """Kinds that may act (k): only a confirmation on the device, online, delivers."""
    if origin == "mac":
        return Decision("hold", dest.id, "needs_device")
    if not online:
        return Decision("hold", dest.id, "await_device")
    if origin == "confirmed":
        return Decision("deliver", dest.id, "confirmed")
    return Decision("hold", dest.id, "await_confirm")


def verdict(dest: Dest, source: Source, origin: Origin, online: bool) -> Decision:
    """The docs/25 §5.2 table, for a capture that passed every precondition."""
    row = klass(dest)
    if row == "archive":
        return Decision("deliver", dest.id, "archive")
    if row == "record":
        intent = spec(dest).intent
        reason: Reason = "queue" if intent == "todo" else "never_auto_exec"
        return Decision("queue", dest.id, reason, intent=intent, voice=source == "voice")
    if row == "exec":
        return _exec(dest, origin, online)
    return _live(dest, source, origin, online)


def decide(capture: Capture, dest: Dest | None, online: bool, origin: Origin) -> Decision:
    reason = precondition(capture, dest)
    if reason is not None or dest is None:
        return Decision("downgrade", DAILY, reason or "unknown")
    return verdict(dest, capture.source, origin, online)
