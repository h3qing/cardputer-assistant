"""The destination catalog (docs/14 §2.2, §3.1, §4.3, §4.4): builtin defaults, whole-catalog rules, immutable edits.

Pure. validate() is the one gate every catalog passes through (parsing, put, remove, bind), so an
invalid catalog never exists. The four fixed destinations (daily, todo, queue, session) are always
on, keep their letters (N T Q S) and cannot be removed; only b c f j k o u x y can be bound, one
destination each. Nothing here ever takes a key away from another destination. The builtins' default labels are in
the language of the moment (cpad.i18n): what a catalog without destinations.json shows, and what is written into
destinations.json the first time it is saved; a label once written stays as written.
"""

from __future__ import annotations

import re
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, replace
from datetime import datetime
from typing import Literal

from cpad.dest.model import (
    ADAPTER_KINDS,
    BINDABLE_KEYS,
    BUILTIN_IDS,
    DESTS_MAX,
    FIXED_KEYS,
    LABEL_MAX,
    LEAVING_KINDS,
    Dest,
    UrlOpts,
    flags,
    kind_of,
    spec,
)
from cpad.dest.paths import PathRefused, check_spec
from cpad.dest.template import TemplateError, check_template
from cpad.i18n import T

SLOT_MIN, SLOT_MAX, CUSTOM_SLOT_MIN = 1, 12, 7
LABEL_RE = re.compile(r'[^\u0000-\u001f\u007f"\\]+\Z')
PROFILE_RE = re.compile(r"[a-z][a-z0-9_-]{0,15}\Z")
BUNDLE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.-]{0,154}\Z")
TEAM_RE = re.compile(r"[A-Z0-9]{10}\Z")
MAP_KEY_RE = re.compile(r"(\*|[a-z0-9][a-z0-9-]{0,11})\Z")  # "*" or a project key
MAP_VALUE_RE = re.compile(r"[A-Za-z0-9_-]{1,128}\Z")
PROJECT_RE = re.compile(r"[a-z0-9][a-z0-9-]{0,11}\Z")  # protocol/limits.json project_key_pattern
URL_TEXT_MAX, URL_MAX = 16_384, 65_536
State = Literal["on", "off", "needs_optin", "unavailable"]


class DestsError(ValueError):
    """destinations.json or a requested change breaks a catalog rule."""


@dataclass(frozen=True, slots=True)
class Catalog:
    dests: tuple[Dest, ...]  # every builtin present, sorted by slot
    problem: str | None = None  # destinations.json error; fail_closed() in use
    usual: tuple[tuple[str, str], ...] = ()  # project key -> its usual destination (docs/25 §3.4), sorted


LABELS = {"daily": ("今日笔记", "Today's note"), "todo": ("待办", "To-do"), "queue": ("项目队列", "Project queue"),
          "session": ("开 session", "New session"), "clip": ("剪贴板", "Clipboard"),
          "paste": ("光标处", "Insertion point")}  # fmt: skip


def _defaults() -> tuple[Dest, ...]:
    label = {key: T(*pair) for key, pair in LABELS.items()}
    return (
        Dest("daily", "daily", label["daily"], 1, key="n"),
        Dest("todo", "todo", label["todo"], 2, key="t"),
        Dest("queue", "queue", label["queue"], 3, key="q"),
        Dest("session", "session", label["session"], 4, key="s"),
        Dest("clip", "clip", label["clip"], 5, key="c"),
        Dest("paste", "paste", label["paste"], 6, enabled=False),
    )


def defaults() -> Catalog:
    return Catalog(_defaults())


def fail_closed() -> Catalog:
    """What a broken destinations.json falls back to: the fixed four only, every live kind off and unbound.

    Not defaults(): a newer or mistyped file must never turn the clipboard back on, or give it back its key,
    after the user turned it off (a config error fails closed, like a refused file path).
    """
    return Catalog(tuple(replace(d, enabled=False, key=None) if "l" in flags(d) else d for d in _defaults()))


def builtin(dest_id: str) -> Dest | None:
    return next((d for d in _defaults() if d.id == dest_id), None)


def _check_label(dest: Dest) -> None:
    size = len(dest.label.encode("utf-8")) if isinstance(dest.label, str) else 0
    if not 1 <= size <= LABEL_MAX or not LABEL_RE.match(dest.label):
        raise DestsError(f"{dest.id}: label must be 1-{LABEL_MAX} UTF-8 bytes without control characters, '\"' or '\\'")


def _check_key(dest: Dest) -> None:
    fixed = FIXED_KEYS.get(dest.id)
    if fixed is not None and dest.key != fixed:
        raise DestsError(f"{dest.id} is fixed to the letter {fixed}")
    if fixed is None and dest.key is not None and dest.key not in BINDABLE_KEYS:
        raise DestsError(f"{dest.id}: key must be one of {' '.join(sorted(BINDABLE_KEYS))}")


def _check_url(dest_id: str, url: UrlOpts) -> None:
    sizes_ok = all(isinstance(n, int) and not isinstance(n, bool) for n in (url.max_text_utf16, url.max_url))
    if not sizes_ok or not (1 <= url.max_text_utf16 <= URL_TEXT_MAX and 16 <= url.max_url <= URL_MAX):
        raise DestsError(f"{dest_id}: max_text_utf16 must be 1-{URL_TEXT_MAX}, max_url 16-{URL_MAX}")
    if url.effect not in ("draft", "act") or url.verified not in (None, "draft", "act"):
        raise DestsError(f"{dest_id}: effect and verified are draft or act")
    if url.overflow not in (None, "clip"):
        raise DestsError(f"{dest_id}: overflow is clip or null")
    bad = next((k for k, v in url.map if not MAP_KEY_RE.match(k) or not MAP_VALUE_RE.match(v)), None)
    if bad is not None or len({k for k, _ in url.map}) != len(url.map):
        raise DestsError(f"{dest_id}: map takes project keys (or *) to [A-Za-z0-9_-]{{1,128}}, once each")
    _check_template(dest_id, url)


def _check_template(dest_id: str, url: UrlOpts) -> None:
    try:
        check_template(url.template, focus=False)
        if url.focus is not None:
            check_template(url.focus, focus=True)
    except TemplateError as exc:
        raise DestsError(f"{dest_id}: {exc}") from exc


def _check_handler(dest: Dest) -> None:
    pinned = dest.url or dest.app
    if pinned is None:
        return
    if not isinstance(pinned.bundle, str) or not BUNDLE_RE.match(pinned.bundle):
        raise DestsError(f"{dest.id}: bundle must be an app's bundle identifier")
    if pinned.team is not None and not (isinstance(pinned.team, str) and TEAM_RE.match(pinned.team)):
        raise DestsError(f"{dest.id}: team must be a 10-character signing Team ID or null")


def _check_kind_fields(dest: Dest) -> None:
    for field in ("file", "url", "app"):
        if (getattr(dest, field) is not None) != (dest.kind == field):
            raise DestsError(f"{dest.id}: {field} is required for {field}.* and nothing else has it")
    if dest.leaves_mac and dest.kind not in LEAVING_KINDS:
        raise DestsError(f"{dest.id}: only file, url and app destinations leave the Mac")
    if dest.kind == "url" and not dest.leaves_mac:
        raise DestsError(f"{dest.id}: a url destination always leaves the Mac (the app may sync its drafts)")
    _check_handler(dest)
    if dest.url is not None:
        _check_url(dest.id, dest.url)
    if dest.file is not None:
        try:
            check_spec(dest.file)
        except PathRefused as exc:
            raise DestsError(f"{dest.id}: {exc}") from exc


def _check_profiles(dest: Dest) -> None:
    ok = isinstance(dest.profiles, tuple) and all(isinstance(p, str) and PROFILE_RE.match(p) for p in dest.profiles)
    if not ok or len(set(dest.profiles)) != len(dest.profiles):
        raise DestsError(f"{dest.id}: profiles must be distinct profile keys")
    if dest.profiles and dest.id in BUILTIN_IDS:
        raise DestsError(f"{dest.id}: built-in destinations serve every profile")


def _check_optin(dest: Dest) -> None:
    if dest.optin is None:
        return
    if not dest.leaves_mac:
        raise DestsError(f"{dest.id}: optin is only for destinations that leave the Mac")
    try:
        datetime.fromisoformat(dest.optin)
    except (TypeError, ValueError) as exc:
        raise DestsError(f"{dest.id}: optin must be an ISO time") from exc


def _check_one(dest: Dest) -> None:
    if kind_of(dest.id) != dest.kind:
        raise DestsError(f"unknown destination id {dest.id!r}")
    if not isinstance(dest.slot, int) or isinstance(dest.slot, bool) or not SLOT_MIN <= dest.slot <= SLOT_MAX:
        raise DestsError(f"{dest.id}: slot must be {SLOT_MIN}-{SLOT_MAX}")
    if dest.id in FIXED_KEYS and not dest.enabled:
        raise DestsError(f"{dest.id} is a fixed destination and is always on")
    _check_label(dest)
    _check_key(dest)
    _check_kind_fields(dest)
    _check_profiles(dest)
    _check_optin(dest)


def _repeated(values: Iterable[object]) -> object | None:
    counts = Counter(v for v in values if v is not None)
    return next((v for v, n in counts.items() if n > 1), None)


def validate(dests: Iterable[Dest]) -> Catalog:
    """Whole-catalog rules; raises DestsError. Returns the catalog sorted by slot."""
    items = tuple(dests)
    if len(items) > DESTS_MAX:
        raise DestsError(f"at most {DESTS_MAX} destinations")
    for dest in items:
        _check_one(dest)
    missing = [b for b in BUILTIN_IDS if b not in {d.id for d in items}]
    if missing:
        raise DestsError(f"builtin destinations missing: {', '.join(missing)}")
    for field in ("id", "slot", "key"):
        twice = _repeated(getattr(d, field) for d in items)
        if twice is not None:
            raise DestsError(f"two destinations share the {field} {twice}")
    return Catalog(tuple(sorted(items, key=lambda d: d.slot)))


def find(cat: Catalog, dest_id: str) -> Dest | None:
    return next((d for d in cat.dests if d.id == dest_id), None)


def by_key(cat: Catalog, key: str) -> Dest | None:
    return next((d for d in cat.dests if d.key is not None and d.key == key.lower()), None)


def state_of(dest: Dest) -> State:
    if not spec(dest).available:
        return "unavailable"
    if not dest.enabled:
        return "off"
    return "needs_optin" if dest.leaves_mac and dest.optin is None else "on"


def usable(cat: Catalog) -> tuple[Dest, ...]:
    """Enabled, available and opted in: what the device sees."""
    return tuple(d for d in cat.dests if state_of(d) == "on")


def _kept(cat: Catalog, changed: Catalog) -> Catalog:
    """changed, with cat's usual destinations that still exist in it."""
    return replace(changed, usual=tuple((p, d) for p, d in cat.usual if find(changed, d) is not None))


def put(cat: Catalog, dest: Dest) -> Catalog:
    """Add, or replace by id, then validate."""
    others = tuple(d for d in cat.dests if d.id != dest.id)
    return _kept(cat, validate((*others, dest)))


def remove(cat: Catalog, dest_id: str) -> Catalog:
    """Custom: delete; clip / paste: disable; fixed: DestsError."""
    dest = find(cat, dest_id)
    if dest is None:
        raise DestsError(f"no destination {dest_id}")
    if dest_id in FIXED_KEYS:
        raise DestsError("daily / todo / queue / session are fixed destinations")
    if dest_id in BUILTIN_IDS:
        return put(cat, replace(dest, enabled=False))
    return _kept(cat, validate(d for d in cat.dests if d.id != dest_id))


def bind(cat: Catalog, dest_id: str, key: str | None) -> Catalog:
    """Bind (or unbind with None) a letter; never takes a key away from another destination."""
    dest = find(cat, dest_id)
    if dest is None:
        raise DestsError(f"no destination {dest_id}")
    letter = key.lower() if key is not None else None
    holder = by_key(cat, letter) if letter is not None else None
    if holder is not None and holder.id != dest_id:
        raise DestsError(f"{letter} is already bound to {holder.id}; first: cpa dest bind {holder.id} -")
    return put(cat, replace(dest, key=letter))


def free_slot(cat: Catalog) -> int:
    """First free slot from 7 (custom destinations), then any free slot."""
    taken = {d.slot for d in cat.dests}
    order = (*range(CUSTOM_SLOT_MIN, SLOT_MAX + 1), *range(SLOT_MIN, CUSTOM_SLOT_MIN))
    slot = next((s for s in order if s not in taken), None)
    if slot is None:
        raise DestsError(f"all {SLOT_MAX} slots are taken")
    return slot


def check_usual(cat: Catalog, project: str, dest_id: str) -> None:
    """A usual destination: a project key and a destination of this catalog that the Mac can deliver to."""
    if not isinstance(project, str) or not PROJECT_RE.match(project):
        raise DestsError("usual: each name must be a project key")
    dest = find(cat, dest_id) if isinstance(dest_id, str) else None
    if dest is None:
        raise DestsError(f"usual: no destination {dest_id!r}")
    if dest.kind not in ADAPTER_KINDS or not spec(dest).available:
        raise DestsError(f"usual: {dest_id} has no executor on this Mac (use clip, file, url or app)")


def with_usual(cat: Catalog, project: str, dest_id: str | None) -> Catalog:
    """Set (or forget, with None) the project's usual destination; raises DestsError."""
    others = tuple((p, d) for p, d in cat.usual if p != project)
    if dest_id is None:
        return replace(cat, usual=others)
    check_usual(cat, project, dest_id)
    return replace(cat, usual=tuple(sorted((*others, (project, dest_id)))))


def usual_of(cat: Catalog, project: str | None) -> str | None:
    return next((d for p, d in cat.usual if p == project), None) if project else None
