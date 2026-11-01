"""$CPA_HOME/destinations.json (docs/14 §4): strict parse, dump, atomic 0600 save.

Strict on purpose: the top level holds only `version` and `dests`, each entry only the fields of
§4.2, file / url / app only theirs (docs/25 §2.3). One unknown field rejects the whole file, so a token, API key
or password has nowhere to go: cpad never stores third-party secrets. Builtins left out of the file get
their defaults (§4.4); a custom destination without a slot takes the first free one from 7.
A broken file never stops the daemon: load_catalog() falls back to fail_closed() (daily, todo, queue,
session; clip and paste off and unbound) and names the problem.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path
from typing import Any

from cpad.config.files import read_json, write_json
from cpad.config.settings import Paths
from cpad.dest.catalog import Catalog, DestsError, builtin, defaults, fail_closed, free_slot, validate, with_usual
from cpad.dest.model import BUILTIN_IDS, DESTS_MAX, FIXED_KEYS, LEAVING_KINDS, AppOpts, Dest, FileOpts, UrlOpts, kind_of

FILE_NAME = "destinations.json"
VERSION = 1
TOP_FIELDS = frozenset({"version", "dests"})
OPTIONAL_TOP = frozenset({"usual"})
ENTRY_FIELDS = frozenset(
    {"id", "slot", "key", "label", "enabled", "leaves_mac", "optin", "profiles", "file", "url", "app"}
)
FILE_FIELDS = frozenset({"root", "path"})
URL_FIELDS = frozenset({"template", "bundle", "team", "effect", "verified", "focus", "max_text_utf16", "max_url",
                        "overflow", "map", "preset"})  # fmt: skip
APP_FIELDS = frozenset({"bundle", "team"})
Options = tuple[FileOpts | None, UrlOpts | None, AppOpts | None]


def dests_path(paths: Paths) -> Path:
    return paths.home / FILE_NAME


def _exact(value: Any, fields: frozenset[str], what: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise DestsError(f"{what} must be an object")
    extra = sorted(set(value) - fields - (OPTIONAL_TOP if what == FILE_NAME else frozenset()))
    missing = sorted(fields - set(value)) if what != "destination" else ([] if "id" in value else ["id"])
    if extra or missing:
        raise DestsError(f"{what}: {'unknown' if extra else 'missing'} field {(extra or missing)[0]!r}")
    return value


def _opt(entry: Mapping[str, Any], key: str, kind: type, default: Any, nullable: bool = False) -> Any:
    if key not in entry:
        return default
    value = entry[key]
    ok = isinstance(value, bool) if kind is bool else isinstance(value, kind) and not isinstance(value, bool)
    if not ok and not (nullable and value is None):
        raise DestsError(f"{entry.get('id')}: {key} must be a {kind.__name__}{' or null' if nullable else ''}")
    return value


def _key(entry: Mapping[str, Any], default: str | None) -> str | None:
    key = _opt(entry, "key", str, default, nullable=True)
    if key is not None and not (len(key) == 1 and "a" <= key <= "z"):
        raise DestsError(f"{entry.get('id')}: key must be one lower-case letter or null")
    return key


def _strings(entry: Mapping[str, Any], opts: Mapping[str, Any], nullable: frozenset[str]) -> None:
    for key, value in opts.items():
        if not (isinstance(value, str) or (key in nullable and value is None)):
            raise DestsError(f"{entry['id']}: {key} must be a string{' or null' if key in nullable else ''}")


def _url(entry: Mapping[str, Any], raw: Any) -> UrlOpts:
    opts = _exact(raw, URL_FIELDS, "url")
    texts = {k: v for k, v in opts.items() if k not in ("max_text_utf16", "max_url", "map")}
    _strings(entry, texts, frozenset({"team", "verified", "focus", "overflow", "preset"}))
    mapping = opts["map"]
    if not isinstance(mapping, Mapping) or not all(isinstance(v, str) for v in mapping.values()):
        raise DestsError(f"{entry['id']}: url.map must be an object of strings")
    return UrlOpts(template=opts["template"], bundle=opts["bundle"], team=opts["team"], effect=opts["effect"],
                   verified=opts["verified"], focus=opts["focus"], max_text_utf16=opts["max_text_utf16"],
                   max_url=opts["max_url"], overflow=opts["overflow"], map=tuple(sorted(mapping.items())),
                   preset=opts["preset"])  # fmt: skip


def _options(entry: Mapping[str, Any]) -> Options:
    file = _exact(entry["file"], FILE_FIELDS, "file") if "file" in entry else None
    app = _exact(entry["app"], APP_FIELDS, "app") if "app" in entry else None
    for opts, nullable in ((file or {}, frozenset()), (app or {}, frozenset({"team"}))):
        _strings(entry, opts, nullable)
    url = _url(entry, entry["url"]) if "url" in entry else None
    return (FileOpts(file["root"], file["path"]) if file else None, url,
            AppOpts(app["bundle"], app["team"]) if app else None)  # fmt: skip


def _profiles(entry: Mapping[str, Any]) -> tuple[str, ...]:
    value = entry.get("profiles", [])
    if not isinstance(value, list) or not all(isinstance(p, str) for p in value):
        raise DestsError(f"{entry['id']}: profiles must be a list of profile keys")
    return tuple(value)


def _base(dest_id: str, kind: Any) -> Dest:
    return builtin(dest_id) or Dest(dest_id, kind, dest_id.partition(".")[2], 0, leaves_mac=kind in LEAVING_KINDS)


def _entry(raw: Any) -> tuple[Dest, bool]:
    """(dest, slot given); a custom dest without a slot gets slot 0 until _fill_slots."""
    entry = _exact(raw, ENTRY_FIELDS, "destination")
    kind = kind_of(entry["id"]) if isinstance(entry["id"], str) else None
    if kind is None:
        raise DestsError(f"unknown destination id {entry['id']!r}")
    base = _base(entry["id"], kind)
    file, url, app = _options(entry)
    dest = replace(
        base,
        slot=_opt(entry, "slot", int, base.slot),
        key=_key(entry, base.key),
        label=_opt(entry, "label", str, base.label),
        enabled=_opt(entry, "enabled", bool, base.enabled),
        leaves_mac=_opt(entry, "leaves_mac", bool, base.leaves_mac),
        optin=_opt(entry, "optin", str, None, nullable=True),
        profiles=_profiles(entry),
        file=file,
        url=url,
        app=app,
    )
    return dest, "slot" in entry or base.slot != 0


def _fill_slots(pairs: list[tuple[Dest, bool]]) -> tuple[Dest, ...]:
    placed = Catalog(tuple(d for d, given in pairs if given))
    filled: list[Dest] = []
    for dest, given in pairs:
        if not given:
            dest = replace(dest, slot=free_slot(Catalog((*placed.dests, *filled))))
        filled.append(dest)
    return tuple(filled)


def parse_dests(doc: Any) -> Catalog:
    """Strict (§4.3); raises DestsError."""
    top = _exact(doc, TOP_FIELDS, FILE_NAME)
    version = top["version"]
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        raise DestsError("version must be 1")
    if version > VERSION:
        raise DestsError(f"destinations.json version {version} is newer than this cpad understands")
    if not isinstance(top["dests"], list) or len(top["dests"]) > DESTS_MAX:
        raise DestsError(f"dests must be a list of at most {DESTS_MAX}")
    dests = _fill_slots([_entry(raw) for raw in top["dests"]])
    present = {d.id for d in dests}
    return _usual(validate((*dests, *(builtin(b) for b in BUILTIN_IDS if b not in present))), top.get("usual", {}))


def _usual(cat: Catalog, raw: Any) -> Catalog:
    if not isinstance(raw, Mapping):
        raise DestsError("usual must be an object of project key -> destination id")
    for project, dest_id in raw.items():
        cat = with_usual(cat, project, dest_id if isinstance(dest_id, str) else "")
    return cat


def _dump_url(u: UrlOpts) -> dict[str, Any]:
    return {"template": u.template, "bundle": u.bundle, "team": u.team, "effect": u.effect, "verified": u.verified,
            "focus": u.focus, "max_text_utf16": u.max_text_utf16, "max_url": u.max_url, "overflow": u.overflow,
            "map": dict(u.map), "preset": u.preset}  # fmt: skip


def _dump_one(d: Dest) -> dict[str, Any]:
    out: dict[str, Any] = {"id": d.id, "slot": d.slot, "key": d.key, "label": d.label}
    if d.id not in FIXED_KEYS:
        out["enabled"] = d.enabled
    if d.kind in LEAVING_KINDS:
        out["leaves_mac"] = d.leaves_mac
        out["optin"] = d.optin
    if d.profiles:
        out["profiles"] = list(d.profiles)
    if d.file is not None:
        out["file"] = {"root": d.file.root, "path": d.file.path}
    if d.url is not None:
        out["url"] = _dump_url(d.url)
    if d.app is not None:
        out["app"] = {"bundle": d.app.bundle, "team": d.app.team}
    return out


def dump_dests(cat: Catalog) -> dict[str, Any]:
    """parse_dests(dump_dests(c)) == c for every valid catalog."""
    doc: dict[str, Any] = {"version": VERSION, "dests": [_dump_one(d) for d in cat.dests]}
    return {**doc, "usual": dict(cat.usual)} if cat.usual else doc


def load_dests(path: Path) -> Catalog:
    """Missing file -> defaults(); raises DestsError."""
    if not path.exists():
        return defaults()
    try:
        doc = read_json(path)
    except ValueError as exc:
        raise DestsError(f"{FILE_NAME} is not valid JSON") from exc
    return parse_dests(doc)


def load_catalog(path: Path) -> Catalog:
    """Never raises: fail_closed() with `problem` set when the file cannot be used (never the defaults)."""
    try:
        return load_dests(path)
    except (DestsError, OSError) as exc:
        return replace(fail_closed(), problem=str(exc))


def save_dests(path: Path, cat: Catalog) -> None:
    """Atomic, 0600 (config.files.write_json)."""
    write_json(path, dump_dests(cat))
