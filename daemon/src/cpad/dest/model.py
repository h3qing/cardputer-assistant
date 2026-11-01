"""Capture destinations (docs/14 §2, §3, §5): kinds, ids, keys and the frozen records every dest module shares.

A destination id carries its kind: builtins are the kind itself (`daily`, `clip`, ...), custom ones are
`<kind>.<name>` (`file.sb`, `url.<name>`, `app.<name>`). Kinds v2 (docs/25 §2.2): `url` and `app` are new,
`plugin` is gone, `paste` may be named. What a destination may do is its `fl` (flags()): l live (only while
you are there, never replayed), c leaves the Mac, p needs a project, k each capture is confirmed on the device.
Policy and the device read only fl; no app is named anywhere in code (presets are data, docs/25 §2.1).
Delivery lives here, not in router.py, because records, adapters and the router all use it.
"""

from __future__ import annotations

import re
from collections.abc import Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Literal

Kind = Literal["daily", "todo", "queue", "session", "clip", "paste", "file", "url", "app"]
Source = Literal["typed", "voice"]
How = Literal["key", "pick", "app"]
Origin = Literal["live", "queued", "confirmed", "mac"]  # mac: confirmed by a person on this Mac (docs/25 §5.2)
Action = Literal["deliver", "hold", "queue", "downgrade"]
Reason = Literal[
    "archive", "live_key", "confirmed", "mac", "await_confirm", "await_device", "needs_device", "queue",
    "never_auto_exec", "meeting", "unknown", "unavailable", "disabled", "other_profile", "no_optin", "not_explicit",
    "no_text", "no_project", "too_long", "bad_text",
]  # fmt: skip
By = Literal["auto", "device", "cli", "app"]
Effect = Literal["draft", "act"]
Klass = Literal["archive", "record", "clip", "draft", "exec"]

DAILY = "daily"
BUILTIN_IDS: tuple[str, ...] = ("daily", "todo", "queue", "session", "clip", "paste")
FIXED_KEYS: Mapping[str, str] = MappingProxyType({"daily": "n", "todo": "t", "queue": "q", "session": "s"})
BINDABLE_KEYS: frozenset[str] = frozenset("bcfjkouxy")
RESERVED_KEYS: frozenset[str] = frozenset("adeghilmprvwz")
DEST_ID = r"^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,11})?$"  # names <= 12, inside main's dest_pattern (docs/25 §2.2)
CUSTOM_NAME = r"^[a-z0-9][a-z0-9-]{0,11}$"
DESTS_MAX = 12
LABEL_MAX = 24  # UTF-8 bytes
LIVE_WINDOW_MS = 30_000
HOLD_TTL_S = 86_400
MAX_ATTEMPTS = 3
VOICE_TEXT_MAX = 2000  # characters
CONFIRM_DUR_MAX_MS = 40_000  # k kinds: longer voice is never offered for confirmation (docs/25 §5.2 check 9)
CONFIRM_TEXT_MAX = 500  # UTF-8 bytes, same check
FLAG_ORDER = "lcpk"
HOWS: tuple[str, ...] = ("key", "pick", "app")


@dataclass(frozen=True, slots=True)
class KindSpec:
    kind: Kind
    flags: str  # the kind's own fl; leaves_mac adds c, an unverified url adds k (flags())
    available: bool  # this cpad has an executor (False: paste until R5)
    builtin: bool  # the bare kind is a destination ("clip")
    custom: bool  # instances are "<kind>.<name>"
    intent: Literal["todo", "spawn"] | None = None  # record kinds: what the queue item is for


KINDS: Mapping[Kind, KindSpec] = MappingProxyType(
    {
        "daily": KindSpec("daily", "", available=True, builtin=True, custom=False),
        "todo": KindSpec("todo", "", available=True, builtin=True, custom=False),
        "queue": KindSpec("queue", "p", available=True, builtin=True, custom=False, intent="todo"),
        "session": KindSpec("session", "p", available=True, builtin=True, custom=False, intent="spawn"),
        "clip": KindSpec("clip", "l", available=True, builtin=True, custom=False),
        "paste": KindSpec("paste", "l", available=False, builtin=True, custom=True),
        "file": KindSpec("file", "", available=True, builtin=False, custom=True),
        "url": KindSpec("url", "l", available=True, builtin=False, custom=True),
        "app": KindSpec("app", "l", available=True, builtin=False, custom=True),
    }
)
# Kinds delivered by an adapter after the append; daily / todo are the daily-note line, queue / session a record.
ADAPTER_KINDS: frozenset[str] = frozenset({"clip", "paste", "file", "url", "app"})
# Kinds that may be added with leaves_mac (the others never leave the Mac).
LEAVING_KINDS: frozenset[str] = frozenset({"file", "url", "app"})


@dataclass(frozen=True, slots=True)
class FileOpts:
    root: str  # absolute or "~/..."
    path: str  # relative to root


@dataclass(frozen=True, slots=True)
class UrlOpts:
    """A url.<name>, copied from its preset when added (docs/25 §2.3); later preset edits never change it."""

    template: str  # "{text}" exactly once, as one whole query value
    bundle: str  # the app that must handle the link, pinned when added (docs/25 §5.3)
    team: str | None  # its signing Team ID when added; None for a binary without one
    effect: Effect = "act"  # the preset's claim; "draft" counts only once verified on this Mac
    verified: Effect | None = None  # this Mac's test answer (cpa dest verify)
    focus: str | None = None  # link that only brings the target forward (overflow, a draft still there)
    max_text_utf16: int = 1000
    max_url: int = 2048
    overflow: Literal["clip"] | None = None
    map: tuple[tuple[str, str], ...] = ()  # project key or "*" -> the {p} value
    preset: str | None = None  # where it came from


@dataclass(frozen=True, slots=True)
class AppOpts:
    """An app.<name>: the text goes to the clipboard, the app is brought forward, you press Cmd-V."""

    bundle: str
    team: str | None = None


@dataclass(frozen=True, slots=True)
class Dest:
    id: str
    kind: Kind
    label: str
    slot: int
    key: str | None = None
    enabled: bool = True
    leaves_mac: bool = False
    optin: str | None = None
    file: FileOpts | None = None
    url: UrlOpts | None = None
    app: AppOpts | None = None
    profiles: tuple[str, ...] = ()  # docs/18 profiles it serves; () = every profile while this Mac serves one


@dataclass(frozen=True, slots=True)
class Capture:
    ref: str  # outbox item id or recording session id (MonoId)
    source: Source
    dest: str  # dest id chosen on the device
    how: How = "key"
    project: str | None = None  # None for orphans
    text: str | None = None  # None / "" = nothing to deliver
    stamp: str = ""  # "YYYY-MM-DD HH:MM", "~" prefix when the time is uncertain
    meeting: bool = False  # a meeting has other people's words: local archives only (docs/25 §5.2 check 0)
    prof: str | None = None  # docs/18 profile it was captured in
    dur_ms: int | None = None  # voice length


@dataclass(frozen=True, slots=True)
class Decision:
    action: Action
    dest: str  # effective dest id ("daily" on downgrade)
    reason: Reason
    intent: Literal["todo", "spawn"] | None = None
    voice: bool = False  # a queue item that came from speech (docs/22 §5.4 item 8; not U25's `uncertain`)


@dataclass(frozen=True, slots=True)
class Delivery:
    key: str  # f"{capture.ref}:{dest.id}"; adapters are idempotent by it
    capture: Capture
    dest: Dest
    attempt: int = 1
    by: By = "auto"
    re: str | None = None  # dest.send id to answer when it finishes


@dataclass(frozen=True, slots=True)
class Probe:
    """What `cpa dest test` reports; probing never touches content."""

    state: Literal["ok", "needs_setup", "missing", "unavailable"]
    reason: str = ""


@dataclass(frozen=True, slots=True)
class DeliveryResult:
    ok: bool
    code: str | None = None  # E_DEST_IO E_DEST_PATH E_DEST_MISSING E_DEST_TIMEOUT E_INTERRUPTED E_HANDLER_CHANGED ...
    retryable: bool = False
    ref: str | None = None  # opaque, goes to dest.delivered (block id for files)
    via: Literal["url", "clip"] | None = None  # handed to another app (dest.handed): it may or may not take it


_DEST_ID = re.compile(DEST_ID[:-1] + r"\Z")


def kind_of(dest_id: str) -> Kind | None:
    """The kind an id names, or None when it is not a well-formed id of a known kind."""
    if not isinstance(dest_id, str) or not _DEST_ID.match(dest_id):
        return None
    head, dot, _ = dest_id.partition(".")
    found = KINDS.get(head)  # type: ignore[call-overload]
    if found is None:
        return None
    return found.kind if (found.custom if dot else found.builtin) else None


def spec(dest: Dest) -> KindSpec:
    return KINDS[dest.kind]


def is_draft(dest: Dest) -> bool:
    """Only fills a draft and never acts: app.<name>, paste.<name>, and a url whose preset says draft and whose
    test on this Mac answered "appeared, not sent" (docs/25 §2.3 effect, D12)."""
    if dest.kind == "url":
        return dest.url is not None and dest.url.effect == "draft" and dest.url.verified == "draft"
    return dest.kind == "app" or (dest.kind == "paste" and "." in dest.id)


def flags(dest: Dest) -> str:
    """The destination's fl, letters in FLAG_ORDER. Bare paste and an unverified url need the device (k)."""
    have = set(spec(dest).flags)
    have |= {"c"} if dest.leaves_mac else set()
    executes = (dest.kind == "url" and not is_draft(dest)) or dest.id == "paste"
    have |= {"k"} if executes else set()
    return "".join(f for f in FLAG_ORDER if f in have)


def klass(dest: Dest) -> Klass:
    """The truth-table row (docs/25 §5.2), read from fl; clip differs from a draft only for live voice."""
    fl = flags(dest)
    if spec(dest).intent is not None:
        return "record"
    if "l" not in fl:
        return "archive"
    if "k" in fl:
        return "exec"
    return "clip" if dest.kind == "clip" else "draft"


def delivery_key(ref: str, dest_id: str) -> str:
    return f"{ref}:{dest_id}"
