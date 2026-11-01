"""Executors for destinations (docs/14 §12.2, docs/25 §5.3); with handlers.py the only code of cpad/dest that
touches the Mac.

ClipAdapter  the text as it is, through ClipWriter (pasteboard.py): native and marked when it can, else pbcopy
             with the text on stdin only (never in argv). Unlike url overflow and app.*, it falls back to pbcopy
             for an automatic delivery too, on purpose: the clip destination is the clipboard you chose with its
             key (v0.4.1 behaviour). Every failure is final for automation (retryable=False): a live action waits
             for Enter.
FileAdapter  one line appended to the file a `file.*` destination names (paths.py rules), idempotent by
             the block id; refused paths are E_DEST_PATH, I/O errors E_DEST_IO (retryable).
url / app    handlers.UrlAdapter and AppAdapter.
daily / todo are the daily-note line NotesWriter already writes; queue / session are queue.added records
the router produces: none of them needs an adapter. paste waits for its executor (docs/25 R5).
probe() only inspects (cpa dest test); it never writes, copies or opens anything.
"""

from __future__ import annotations

import os
import subprocess
import time
from collections.abc import Callable, Iterable, Mapping
from pathlib import Path
from types import MappingProxyType
from typing import Protocol

from cpad.dest.handlers import AppAdapter, UrlAdapter
from cpad.dest.macos import LaunchServices, Mac
from cpad.dest.model import KINDS, Capture, Delivery, DeliveryResult, Dest, Kind, Probe
from cpad.dest.pasteboard import CLIP_ENV, PBCOPY, ClipWriter, NativePasteboard
from cpad.dest.paths import PathRefused, append_line, target_path
from cpad.notes.render import block_id, clean

__all__ = ["CLIP_ENV", "PBCOPY", "Adapter", "ClipAdapter", "FileAdapter", "Probe", "execute", "make_adapters", "probe"]


class Adapter(Protocol):
    def probe(self, dest: Dest) -> Probe: ...  # never touches content

    def deliver(self, d: Delivery) -> DeliveryResult: ...


class ClipAdapter:
    """The clip destination: the text as it is (no header), because you chose the clipboard yourself."""

    def __init__(
        self,
        run: Callable[..., subprocess.CompletedProcess] = subprocess.run,
        exe: str = PBCOPY,
        timeout_s: float = 2.0,
        writer: ClipWriter | None = None,
    ) -> None:
        self._exe = exe
        self._writer = writer or ClipWriter(None, run, exe, timeout_s=timeout_s)

    def probe(self, dest: Dest) -> Probe:
        if os.path.isfile(self._exe) and os.access(self._exe, os.X_OK):
            return Probe("ok")
        return Probe("missing", f"{self._exe} not found")

    def deliver(self, d: Delivery) -> DeliveryResult:
        wrote = self._writer.write(d.capture.text or "", allow_plain=True)
        return DeliveryResult(True) if wrote.ok else DeliveryResult(False, wrote.code or "E_DEST_IO")


def file_line(cap: Capture) -> tuple[str, str]:
    """(line, marker): "- {stamp} #{project} {text} ^cpa-<ref>", control characters as spaces."""
    marker = block_id(cap.ref)
    parts = (cap.stamp, f"#{cap.project}" if cap.project else "", clean(cap.text or "").strip(), marker)
    return "- " + " ".join(p for p in parts if p), marker


class FileAdapter:
    def __init__(self, home: Path, denied: Iterable[Path] = ()) -> None:
        self._home = home
        self._denied = tuple(denied)  # more folders to refuse, e.g. $CPA_HOME

    def _target(self, dest: Dest) -> Path:
        if dest.file is None:
            raise PathRefused("no_parent", "no file configured")
        return target_path(dest.file, self._home, self._denied)

    def probe(self, dest: Dest) -> Probe:
        try:
            target = self._target(dest)
        except PathRefused as exc:
            return Probe("needs_setup", str(exc))
        writable = os.access(target, os.W_OK) if target.exists() else os.access(target.parent, os.W_OK)
        return Probe("ok") if writable else Probe("needs_setup", "not writable")

    def deliver(self, d: Delivery) -> DeliveryResult:
        try:
            line, marker = file_line(d.capture)
            append_line(self._target(d.dest), line, marker)
        except PathRefused:
            return DeliveryResult(False, "E_DEST_PATH")
        except OSError:
            return DeliveryResult(False, "E_DEST_IO", retryable=True)
        return DeliveryResult(True, ref=marker)


def make_adapters(home: Path, denied: Iterable[Path] = (), *, mac: Mac | None = None,
                  clip_on: Callable[[], bool] = lambda: False, writer: ClipWriter | None = None,
                  clock: Callable[[], float] = time.monotonic,
                  recent: Mapping[tuple[str, str], float] | None = None) -> Mapping[Kind, Adapter]:  # fmt: skip
    """The executors; nothing is loaded from macOS until a delivery or probe needs it. clock and recent: the
    draft occupancy of UrlAdapter (a cpa process passes time.time and what dest.handed says)."""
    mac = mac or LaunchServices()
    writer = writer or ClipWriter(NativePasteboard())
    url = UrlAdapter(mac, writer, clip_on, clock, recent)
    return MappingProxyType({"clip": ClipAdapter(writer=writer), "file": FileAdapter(home, denied), "url": url,
                             "app": AppAdapter(mac, writer)})  # fmt: skip


def probe(dest: Dest, adapters: Mapping[Kind, Adapter]) -> Probe:
    """Whether this cpad can deliver to dest now (cpa dest test); inspects only."""
    if not KINDS[dest.kind].available:
        return Probe("unavailable", "no executor in this version")
    if KINDS[dest.kind].intent is not None:
        return Probe("ok", "captures need a project")
    if dest.kind in ("daily", "todo"):
        return Probe("ok")
    adapter = adapters.get(dest.kind)
    return adapter.probe(dest) if adapter is not None else Probe("unavailable", "no executor")


def execute(d: Delivery, adapters: Mapping[Kind, Adapter]) -> DeliveryResult:
    """Run one delivery (the hub calls this off the event loop, one at a time)."""
    adapter = adapters.get(d.dest.kind)
    return adapter.deliver(d) if adapter is not None else DeliveryResult(False, "E_DEST_MISSING")
