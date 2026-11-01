"""ClipWriter: the clipboard writes of clip, app.<name> and url overflow (docs/25 §5.3, D9).

Native first, through the Objective-C runtime with ctypes (no PyObjC): NSPasteboard
prepareForNewContentsWithOptions:CurrentHostOnly (never synced to an iPhone by Universal Clipboard),
the text, plus the org.nspasteboard ConcealedType and TransientType markers so clipboard-history apps
skip it. CLEAR_AFTER_S later the clipboard is cleared, but only if its changeCount is still ours: you
copied nothing since. When the native path is unavailable the text goes to pbcopy on stdin (never argv),
and only when a person asked just now (allow_plain): history apps may keep that copy.
"""

from __future__ import annotations

import ctypes
import subprocess
import threading
from collections.abc import Callable, Mapping
from ctypes import c_bool, c_char_p, c_long, c_uint32, c_ulong, c_void_p
from dataclasses import dataclass
from types import MappingProxyType
from typing import Protocol

PBCOPY = "/usr/bin/pbcopy"
CLIP_ENV: Mapping[str, str] = MappingProxyType(
    {"LANG": "en_US.UTF-8", "LC_ALL": "en_US.UTF-8", "PATH": "/usr/bin:/bin"}
)
CLEAR_AFTER_S = 120.0
CURRENT_HOST_ONLY = 1  # NSPasteboardContentsCurrentHostOnly
TEXT_TYPE = "public.utf8-plain-text"
MARKERS = ("org.nspasteboard.ConcealedType", "org.nspasteboard.TransientType")
UTF8 = 0x08000100


@dataclass(frozen=True, slots=True)
class ClipWrite:
    ok: bool
    concealed: bool = False  # local-only and marked for history apps (native)
    code: str | None = None  # E_DEST_IO E_DEST_TIMEOUT E_DEST_MISSING E_CLIP_PLAIN


class Pasteboard(Protocol):
    def write(self, text: str) -> int: ...  # the changeCount after writing; raises OSError when it cannot
    def change_count(self) -> int: ...
    def clear(self) -> None: ...


class NativePasteboard:
    """The general pasteboard through objc_msgSend. Loads AppKit on first use, never on import."""

    def __init__(self) -> None:
        self._rt: tuple[ctypes.CDLL, ctypes.CDLL, int] | None = None

    def _runtime(self) -> tuple[ctypes.CDLL, ctypes.CDLL, int]:
        if self._rt is None:
            objc = ctypes.CDLL("/usr/lib/libobjc.A.dylib")
            cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
            ctypes.CDLL("/System/Library/Frameworks/AppKit.framework/AppKit")
            objc.objc_getClass.restype, objc.objc_getClass.argtypes = c_void_p, [c_char_p]
            objc.sel_registerName.restype, objc.sel_registerName.argtypes = c_void_p, [c_char_p]
            cf.CFStringCreateWithBytes.restype = c_void_p
            cf.CFStringCreateWithBytes.argtypes = [c_void_p, c_char_p, c_long, c_uint32, c_bool]
            cf.CFRelease.restype, cf.CFRelease.argtypes = None, [c_void_p]
            board = _send(objc, objc.objc_getClass(b"NSPasteboard"), "generalPasteboard", c_void_p)
            if not board:
                raise OSError("no general pasteboard")
            self._rt = (objc, cf, board)
        return self._rt

    def _set(self, value: str, kind: str) -> bool:
        objc, cf, board = self._runtime()
        refs = [cf.CFStringCreateWithBytes(None, t.encode(), len(t.encode()), UTF8, False) for t in (value, kind)]
        try:
            return bool(_send(objc, board, "setString:forType:", c_bool, (c_void_p, refs[0]), (c_void_p, refs[1])))
        finally:
            for ref in refs:
                if ref:
                    cf.CFRelease(ref)

    def write(self, text: str) -> int:
        objc, _, board = self._runtime()
        _send(objc, board, "prepareForNewContentsWithOptions:", c_long, (c_ulong, CURRENT_HOST_ONLY))
        for value, kind in ((text, TEXT_TYPE), *(("", m) for m in MARKERS)):
            if not self._set(value, kind):
                raise OSError(f"the pasteboard refused {kind}")
        return self.change_count()

    def change_count(self) -> int:
        objc, _, board = self._runtime()
        return int(_send(objc, board, "changeCount", c_long))  # type: ignore[call-overload]

    def clear(self) -> None:
        objc, _, board = self._runtime()
        _send(objc, board, "clearContents", c_long)


def _send(objc: ctypes.CDLL, target: int, selector: str, restype: object, *args: tuple[object, object]) -> object:
    """objc_msgSend cast to this call's exact signature (arm64 needs the real prototype)."""
    fn = ctypes.cast(objc.objc_msgSend, ctypes.CFUNCTYPE(restype, c_void_p, c_void_p, *(t for t, _ in args)))
    return fn(target, objc.sel_registerName(selector.encode()), *(v for _, v in args))


def _later(delay_s: float, fn: Callable[[], None]) -> None:
    timer = threading.Timer(delay_s, fn)
    timer.daemon = True
    timer.start()


class ClipWriter:
    def __init__(
        self,
        native: Pasteboard | None = None,
        run: Callable[..., subprocess.CompletedProcess] = subprocess.run,
        exe: str = PBCOPY,
        schedule: Callable[[float, Callable[[], None]], None] = _later,
        timeout_s: float = 2.0,
        clear_later: Callable[[int], None] | None = None,
    ) -> None:
        """clear_later: a short-lived process hands the changeCount to clip_clear.spawn instead of a timer."""
        self._native, self._run, self._exe = native, run, exe
        self._schedule, self._timeout_s, self._clear_later = schedule, timeout_s, clear_later

    def write(self, text: str, *, allow_plain: bool) -> ClipWrite:
        if self._native is not None:
            try:
                count = self._native.write(text)
            except (OSError, AttributeError, ValueError):
                pass  # fall back below
            else:
                if self._clear_later is not None:
                    self._clear_later(count)
                else:
                    self._schedule(CLEAR_AFTER_S, lambda: self._clear_if(count))
                return ClipWrite(True, concealed=True)
        return self._plain(text) if allow_plain else ClipWrite(False, code="E_CLIP_PLAIN")

    def _clear_if(self, count: int) -> None:
        native = self._native
        try:
            if native is not None and native.change_count() == count:
                native.clear()
        except (OSError, AttributeError, ValueError):
            pass  # best effort: the copy stays until you copy something else

    def _plain(self, text: str) -> ClipWrite:
        try:
            done = self._run([self._exe], input=text.encode("utf-8", "replace"), env=dict(CLIP_ENV),
                             capture_output=True, timeout=self._timeout_s, check=False)  # fmt: skip
        except FileNotFoundError:
            return ClipWrite(False, code="E_DEST_MISSING")
        except subprocess.TimeoutExpired:
            return ClipWrite(False, code="E_DEST_TIMEOUT")
        except OSError:
            return ClipWrite(False, code="E_DEST_IO")
        return ClipWrite(done.returncode == 0, code=None if done.returncode == 0 else "E_DEST_IO")
