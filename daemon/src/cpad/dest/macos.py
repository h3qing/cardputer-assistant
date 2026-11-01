"""LaunchServices and code signing through ctypes (docs/25 §5.3, D7): no PyObjC, no `open`, no permissions.

A link is opened in this process with LSOpenFromURLSpec, naming the app (appURL) that was pinned when the
destination was added, so another app registering the same scheme later cannot take it, and the text is
never in any process's argv (`open URL` would show it to every process of this user, and fail on long links).
Before opening, the pinned bundle id is looked up (LSCopyApplicationURLsForBundleIdentifier) and the app's
signature is checked (SecStaticCode against the requirement `anchor apple generic`, without validating resources):
only a certificate Apple issued counts, so its Team ID can be compared with the one recorded when it was added.
Info.plist is read with plistlib for the URL schemes an app registers. Nothing here knows any app.
"""

from __future__ import annotations

import ctypes
import plistlib
from collections.abc import Iterable
from ctypes import POINTER, byref, c_bool, c_char_p, c_int32, c_long, c_uint32, c_void_p
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

UTF8 = 0x08000100  # kCFStringEncodingUTF8
LAUNCH_DEFAULTS = 0x1  # kLSLaunchDefaults: open, async, bring the app forward
SIGNING_INFORMATION = 0x2  # kSecCSSigningInformation
NO_RESOURCES = 0x4  # kSecCSDoNotValidateResources
APP_NOT_FOUND = -10814  # kLSApplicationNotFoundErr
ISSUED_BY_APPLE = "anchor apple generic"  # App Store, Developer ID, Apple Development and Apple's own
APPLE_ITSELF = "anchor apple"  # Apple's own software only
PATH_MAX = 1024
APP_ROOTS = (Path("/Applications"), Path.home() / "Applications")


class LaunchSpec(ctypes.Structure):
    """LSLaunchURLSpec; LSOpen.h declares it under #pragma pack(push, 2)."""

    _pack_ = 2
    _fields_ = [("app_url", c_void_p), ("item_urls", c_void_p), ("pass_thru", c_void_p), ("flags", c_uint32),
                ("ref_con", c_void_p)]  # fmt: skip


@dataclass(frozen=True, slots=True)
class Signature:
    valid: bool  # signed through a certificate Apple issued (`anchor apple generic`): never ad hoc or self-signed
    team: str | None  # read only from such a signature; None: Apple's own software has no Team ID
    apple: bool = False  # Apple's own software (`anchor apple`): the only app pinned without a Team ID


@dataclass(frozen=True, slots=True)
class AppInfo:
    path: Path
    bundle: str
    name: str
    version: str
    schemes: frozenset[str]


class Mac(Protocol):
    """What the adapters and the CLI need from macOS; tests use a fake."""

    def app_paths(self, bundle: str) -> tuple[Path, ...]: ...
    def signature(self, app: Path) -> Signature: ...
    def open_url(self, url: str, app: Path) -> int: ...
    def open_app(self, app: Path) -> int: ...
    def info(self, app: Path) -> AppInfo | None: ...


def _fn(lib: ctypes.CDLL, name: str, restype: object, *argtypes: object) -> ctypes._CFuncPtr:
    fn = getattr(lib, name)
    fn.restype, fn.argtypes = restype, list(argtypes)
    return fn


class _CF:
    """The CoreFoundation, LaunchServices and Security calls, loaded once."""

    def __init__(self) -> None:
        cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
        ls = ctypes.CDLL("/System/Library/Frameworks/CoreServices.framework/CoreServices")
        sec = ctypes.CDLL("/System/Library/Frameworks/Security.framework/Security")
        self.release = _fn(cf, "CFRelease", None, c_void_p)
        self.string = _fn(cf, "CFStringCreateWithBytes", c_void_p, c_void_p, c_char_p, c_long, c_uint32, c_bool)
        self.cstring = _fn(cf, "CFStringGetCString", c_bool, c_void_p, c_char_p, c_long, c_uint32)
        self.url = _fn(cf, "CFURLCreateWithBytes", c_void_p, c_void_p, c_char_p, c_long, c_uint32, c_void_p)
        self.file_url = _fn(cf, "CFURLCreateFromFileSystemRepresentation", c_void_p, c_void_p, c_char_p, c_long,
                            c_bool)  # fmt: skip
        self.url_path = _fn(cf, "CFURLGetFileSystemRepresentation", c_bool, c_void_p, c_bool, c_char_p, c_long)
        self.array = _fn(cf, "CFArrayCreate", c_void_p, c_void_p, POINTER(c_void_p), c_long, c_void_p)
        self.count = _fn(cf, "CFArrayGetCount", c_long, c_void_p)
        self.item = _fn(cf, "CFArrayGetValueAtIndex", c_void_p, c_void_p, c_long)
        self.get = _fn(cf, "CFDictionaryGetValue", c_void_p, c_void_p, c_void_p)
        self.array_callbacks = ctypes.addressof(c_void_p.in_dll(cf, "kCFTypeArrayCallBacks"))
        self.apps = _fn(ls, "LSCopyApplicationURLsForBundleIdentifier", c_void_p, c_void_p, c_void_p)
        self.open = _fn(ls, "LSOpenFromURLSpec", c_int32, POINTER(LaunchSpec), c_void_p)
        self.static_code = _fn(sec, "SecStaticCodeCreateWithPath", c_int32, c_void_p, c_uint32, POINTER(c_void_p))
        self.check = _fn(sec, "SecStaticCodeCheckValidity", c_int32, c_void_p, c_uint32, c_void_p)
        self.requirement = _fn(sec, "SecRequirementCreateWithString", c_int32, c_void_p, c_uint32, POINTER(c_void_p))
        self.info = _fn(sec, "SecCodeCopySigningInformation", c_int32, c_void_p, c_uint32, POINTER(c_void_p))
        self.team_key = c_void_p.in_dll(sec, "kSecCodeInfoTeamIdentifier").value

    def text(self, value: str) -> int:
        data = value.encode("utf-8")
        return self.string(None, data, len(data), UTF8, False)

    def read_text(self, ref: int | None) -> str | None:
        buf = ctypes.create_string_buffer(512)
        return buf.value.decode("utf-8") if ref and self.cstring(ref, buf, len(buf), UTF8) else None

    def file(self, path: Path) -> int:
        data = str(path).encode("utf-8")
        return self.file_url(None, data, len(data), True)

    def path_of(self, url: int) -> Path | None:
        buf = ctypes.create_string_buffer(PATH_MAX)
        return Path(buf.value.decode("utf-8")) if self.url_path(url, True, buf, PATH_MAX) else None


class LaunchServices:
    """The real Mac. Loads the frameworks on first use, so importing this module never touches macOS."""

    def __init__(self) -> None:
        self._cf: _CF | None = None

    @property
    def cf(self) -> _CF:
        self._cf = self._cf or _CF()
        return self._cf

    def app_paths(self, bundle: str) -> tuple[Path, ...]:
        cf = self.cf
        name = cf.text(bundle)
        found = cf.apps(name, None)
        cf.release(name)
        if not found:
            return ()
        try:
            paths = (cf.path_of(cf.item(found, i)) for i in range(cf.count(found)))
            return tuple(p for p in paths if p is not None)
        finally:
            cf.release(found)

    def _team(self, code: int) -> str | None:
        cf, info = self.cf, c_void_p()
        if cf.info(code, SIGNING_INFORMATION, byref(info)) != 0 or not info.value:
            return None
        try:
            return cf.read_text(cf.get(info.value, cf.team_key))
        finally:
            cf.release(info.value)

    def signature(self, app: Path) -> Signature:
        cf, code = self.cf, c_void_p()
        url = cf.file(app)
        status = cf.static_code(url, 0, byref(code))
        cf.release(url)
        if status != 0 or not code.value:
            return Signature(False, None)
        try:
            if not self._meets(code.value, ISSUED_BY_APPLE):
                return Signature(False, None)
            return Signature(True, self._team(code.value), self._meets(code.value, APPLE_ITSELF))
        finally:
            cf.release(code.value)

    def _meets(self, code: int, requirement: str) -> bool:
        """SecStaticCodeCheckValidity against a code requirement: an ad hoc or self-signed app never meets one
        that needs Apple's anchor, whatever Team ID its certificate claims."""
        cf, req = self.cf, c_void_p()
        text = cf.text(requirement)
        status = cf.requirement(text, 0, byref(req))
        cf.release(text)
        if status != 0 or not req.value:
            return False
        try:
            return cf.check(code, NO_RESOURCES, req.value) == 0
        finally:
            cf.release(req.value)

    def _launch(self, app: Path, items: tuple[int, ...]) -> int:
        cf = self.cf
        app_url = cf.file(app)
        values = (c_void_p * max(1, len(items)))(*items)
        array = cf.array(None, values, len(items), cf.array_callbacks) if items else None
        try:
            return cf.open(byref(LaunchSpec(app_url, array, None, LAUNCH_DEFAULTS, None)), None)
        finally:
            for ref in (app_url, array, *items):
                if ref:
                    cf.release(ref)

    def open_url(self, url: str, app: Path) -> int:
        """Hands url to app (the pinned handler) only; the text travels inside this process."""
        data = url.encode("utf-8")
        ref = self.cf.url(None, data, len(data), UTF8, None)
        return self._launch(app, (ref,)) if ref else -1

    def open_app(self, app: Path) -> int:
        """Brings app forward (launching it if needed), with nothing to open."""
        return self._launch(app, ())

    def info(self, app: Path) -> AppInfo | None:
        return read_info(app)


def read_info(app: Path) -> AppInfo | None:
    """Bundle id, display name, version and registered URL schemes from Contents/Info.plist."""
    try:
        with (app / "Contents" / "Info.plist").open("rb") as f:
            info = plistlib.load(f)
    except (OSError, plistlib.InvalidFileException, ValueError):
        return None
    bundle = info.get("CFBundleIdentifier")
    if not isinstance(bundle, str):
        return None
    types = info.get("CFBundleURLTypes") if isinstance(info.get("CFBundleURLTypes"), list) else []
    schemes = {s.lower() for t in types if isinstance(t, dict) for s in t.get("CFBundleURLSchemes") or []
               if isinstance(s, str)}  # fmt: skip
    name = info.get("CFBundleDisplayName") or info.get("CFBundleName") or app.stem
    return AppInfo(app, bundle, str(name), str(info.get("CFBundleShortVersionString") or ""), frozenset(schemes))


def installed_apps(roots: Iterable[Path] = APP_ROOTS) -> tuple[AppInfo, ...]:
    """Every *.app directly in the roots (and one folder down), by name; read-only."""
    found: dict[str, AppInfo] = {}
    for root in roots:
        candidates = [*root.glob("*.app"), *root.glob("*/*.app")] if root.is_dir() else []
        for app in sorted(candidates):
            info = read_info(app)
            if info is not None and info.bundle not in found:
                found[info.bundle] = info
    return tuple(sorted(found.values(), key=lambda a: a.name.casefold()))
