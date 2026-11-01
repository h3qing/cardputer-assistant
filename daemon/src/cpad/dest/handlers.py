"""Executors that hand a capture to another app (docs/25 §5.3, §3.6): UrlAdapter and AppAdapter.

Both deliver at most once and say only "handed" (DeliveryResult.via): LaunchServices taking a link does not
mean the app took the draft. Neither ever presses a key. Before anything opens, the pinned handler is checked:
the app with the pinned bundle id must exist (E_DEST_MISSING), still be validly signed by the pinned Team ID
and, for a link, still register the link's scheme (E_HANDLER_CHANGED). Nothing is retried.

UrlAdapter, in order: header + cleaned text (handoff); the draft occupancy (one draft per target: a second
pre-fill within DRAFT_HOLD_S would be dropped by the app silently, so it goes to the clipboard instead);
the length limits; then LSOpenFromURLSpec with the pinned app. Overflow and a busy draft go to the
clipboard only when the url's overflow is "clip" and the clip destination is on (clip_on), and bring the
target forward with the focus link (or the app alone). AppAdapter: clipboard, then the app forward.
"""

from __future__ import annotations

import time
from collections.abc import Callable, Mapping
from pathlib import Path
from types import MappingProxyType

from cpad.dest.handoff import draft_target, handed_text, plan
from cpad.dest.macos import APP_NOT_FOUND, Mac
from cpad.dest.model import AppOpts, Delivery, DeliveryResult, Dest, Probe, UrlOpts
from cpad.dest.pasteboard import ClipWriter
from cpad.dest.template import scheme_of

DRAFT_HOLD_S = 30 * 60.0


def _passes(mac: Mac, app: Path, pinned: UrlOpts | AppOpts, scheme: str | None) -> bool:
    signature = mac.signature(app)
    signed = signature.valid and signature.team == pinned.team and (pinned.team is not None or signature.apple)
    if not signed or scheme is None:
        return signed
    info = mac.info(app)
    return info is not None and scheme in info.schemes and info.bundle == pinned.bundle


def resolve(mac: Mac, pinned: UrlOpts | AppOpts, scheme: str | None = None) -> Path | str:
    """The first copy of the pinned app that passes every check, or the failure code (E_DEST_MISSING,
    E_HANDLER_CHANGED). Another copy with the same bundle id (an old one in Downloads, a mounted disk image) may
    come back first; it is skipped, never opened. No Team ID is trusted only for Apple's own software."""
    paths = mac.app_paths(pinned.bundle)
    if not paths:
        return "E_DEST_MISSING"
    return next((app for app in paths if _passes(mac, app, pinned, scheme)), "E_HANDLER_CHANGED")


def _opened(status: int, via: str, ref: str | None = None) -> DeliveryResult:
    if status == 0:
        return DeliveryResult(True, ref=ref, via=via)  # type: ignore[arg-type]
    return DeliveryResult(False, "E_DEST_MISSING" if status == APP_NOT_FOUND else "E_DEST_IO")


def _probe(mac: Mac, pinned: UrlOpts | AppOpts | None, scheme: str | None) -> Probe:
    if pinned is None:
        return Probe("needs_setup", "no app pinned")
    found = resolve(mac, pinned, scheme)
    if isinstance(found, Path):
        return Probe("ok")
    return Probe("missing" if found == "E_DEST_MISSING" else "needs_setup", found)


class AppAdapter:
    def __init__(self, mac: Mac, clip: ClipWriter) -> None:
        self._mac, self._clip = mac, clip

    def probe(self, dest: Dest) -> Probe:
        return _probe(self._mac, dest.app, None)

    def deliver(self, d: Delivery) -> DeliveryResult:
        text = handed_text(d.capture)
        if text is None or d.dest.app is None:
            return DeliveryResult(False, "E_BAD_TEXT" if text is None else "E_DEST_MISSING")
        app = resolve(self._mac, d.dest.app)
        if not isinstance(app, Path):
            return DeliveryResult(False, app)
        wrote = self._clip.write(text, allow_plain=d.by != "auto")
        if not wrote.ok:
            return DeliveryResult(False, wrote.code or "E_DEST_IO")
        return _opened(self._mac.open_app(app), "clip")


class UrlAdapter:
    def __init__(self, mac: Mac, clip: ClipWriter, clip_on: Callable[[], bool],
                 clock: Callable[[], float] = time.monotonic,
                 recent: Mapping[tuple[str, str], float] | None = None) -> None:  # fmt: skip
        """recent: (dest id, draft_target) -> when it was filled, on `clock`'s scale (a new cpa process seeds it
        from dest.handed{via:"url", to} with time.time)."""
        self._mac, self._clip, self._clip_on, self._clock = mac, clip, clip_on, clock
        self._drafts: Mapping[tuple[str, str], float] = MappingProxyType(dict(recent or {}))

    def probe(self, dest: Dest) -> Probe:
        return _probe(self._mac, dest.url, scheme_of(dest.url.template) if dest.url else None)

    def _busy(self, key: tuple[str, str]) -> bool:
        last = self._drafts.get(key)
        return last is not None and self._clock() - last < DRAFT_HOLD_S

    def _remember(self, key: tuple[str, str]) -> None:
        self._drafts = MappingProxyType({**self._drafts, key: self._clock()})

    def deliver(self, d: Delivery) -> DeliveryResult:
        opts, text = d.dest.url, handed_text(d.capture)
        if text is None or opts is None:
            return DeliveryResult(False, "E_BAD_TEXT" if text is None else "E_DEST_MISSING")
        app = resolve(self._mac, opts, scheme_of(opts.template))
        if not isinstance(app, Path):
            return DeliveryResult(False, app)
        key = (d.dest.id, draft_target(opts, d.capture.project))
        planned = plan(opts, text, d.capture.project)
        if self._busy(key) or planned.url is None:
            why = "draft_busy" if planned.url is not None else "too_long"
            return self._overflow(d, opts, app, text, planned.focus, why)
        result = _opened(self._mac.open_url(planned.url, app), "url")
        if result.ok:
            self._remember(key)
        return result

    def _overflow(self, d: Delivery, opts: UrlOpts, app: Path, text: str, focus: str | None,
                  why: str) -> DeliveryResult:  # fmt: skip
        if opts.overflow != "clip" or not self._clip_on():
            return DeliveryResult(False, "E_DRAFT_BUSY" if why == "draft_busy" else "E_TOO_LONG")
        wrote = self._clip.write(text, allow_plain=d.by != "auto")
        if not wrote.ok:
            return DeliveryResult(False, wrote.code or "E_DEST_IO")
        status = self._mac.open_url(focus, app) if focus else self._mac.open_app(app)
        return _opened(status, "clip", ref=why)
