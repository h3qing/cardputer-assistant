"""Presets: one JSON file per app (docs/25 §2.1, §2.3), the format published in protocol/dest-preset/v1.

A preset is data only: kind, template, limits, a suggested letter, whether text leaves the Mac, how to
recognise the installed app, and `effect`. Shipped presets live in this package (presets/v1, read with
importlib.resources, never __file__); yours live in $CPA_HOME/presets/*.json (0600, the same strict format,
never a shipped id). `effect` absent counts as act until this Mac's test (cpa dest verify) answers
"appeared, not sent"; only a shipped preset (a reviewed template) can be verified to draft, and a preset
that says act, or any preset of yours, never can (D12). deny.json says where a link may never point.
"""

from __future__ import annotations

import json
import re
import stat
from collections.abc import Mapping
from dataclasses import dataclass
from importlib import resources
from pathlib import Path
from typing import Any, Literal

from cpad.dest.catalog import BUNDLE_RE, LABEL_RE
from cpad.dest.model import BINDABLE_KEYS, CUSTOM_NAME, LABEL_MAX
from cpad.dest.template import TemplateError, check_template, scheme_of

PRESET_ID = re.compile(r"[a-z][a-z0-9-]{1,23}\Z")
SPECIAL = frozenset({"deny.json"})
COMMON = frozenset({"v", "preset", "name", "label", "kind", "detect", "leaves_mac", "key_hint", "note"})
URL_KEYS = frozenset({"template", "focus", "effect", "max_text_utf16", "max_url", "overflow", "map"})
FIELDS = {"url": COMMON | URL_KEYS, "app": COMMON, "file": COMMON | {"file"}}
REQUIRED = {"url": COMMON - {"key_hint"} | {"template"}, "app": COMMON - {"key_hint"},
            "file": COMMON - {"key_hint"} | {"file"}}  # fmt: skip


class PresetError(ValueError):
    """A preset breaks the format or a rule."""


@dataclass(frozen=True, slots=True)
class Deny:
    schemes: frozenset[str]
    scheme_prefixes: tuple[str, ...]
    routes: tuple[str, ...]
    handlers: frozenset[str]
    secret_prefixes: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Preset:
    preset: str
    name: str
    label: str
    kind: Literal["url", "app", "file"]
    bundle: str
    leaves_mac: bool
    note: str
    shipped: bool
    key_hint: str | None = None
    effect: Literal["draft", "act"] | None = None  # None: unknown, act until this Mac's test
    template: str | None = None
    focus: str | None = None
    max_text_utf16: int = 1000
    max_url: int = 2048
    overflow: Literal["clip"] | None = None
    map_pattern: str | None = None
    from_link: str | None = None
    file_path: str | None = None

    @property
    def may_be_draft(self) -> bool:
        """Whether this Mac's test can make a destination of it a draft (D12)."""
        return self.kind == "url" and self.shipped and self.effect != "act"


def _data_dir() -> Any:
    return resources.files("cpad.dest").joinpath("presets", "v1")


def load_deny() -> Deny:
    doc = json.loads(_data_dir().joinpath("deny.json").read_text(encoding="utf-8"))
    return Deny(frozenset(doc["schemes"]), tuple(doc["scheme_prefixes"]), tuple(doc["routes"]),
                frozenset(doc["handlers"]), tuple(doc["secret_prefixes"]))  # fmt: skip


def _str(doc: Mapping[str, Any], key: str, pattern: re.Pattern[str] | None = None) -> str:
    value = doc.get(key)
    if not isinstance(value, str) or (pattern is not None and not pattern.match(value)):
        raise PresetError(f"{key} is missing or malformed")
    return value


def _int(doc: Mapping[str, Any], key: str, low: int, high: int, default: int) -> int:
    value = doc.get(key, default)
    if not isinstance(value, int) or isinstance(value, bool) or not low <= value <= high:
        raise PresetError(f"{key} must be an integer {low}-{high}")
    return value


def _keys(doc: Any) -> str:
    if not isinstance(doc, Mapping) or doc.get("v") != 1:
        raise PresetError("a preset is an object with v 1")
    kind = doc.get("kind")
    if kind not in FIELDS:
        raise PresetError("kind must be url, app or file")
    extra, missing = sorted(set(doc) - FIELDS[kind]), sorted(REQUIRED[kind] - set(doc))
    if extra or missing:
        raise PresetError(f"{'unknown' if extra else 'missing'} field {(extra or missing)[0]!r}")
    return kind


def _object(doc: Mapping[str, Any], key: str, fields: frozenset[str]) -> Mapping[str, str]:
    value = doc.get(key)
    if not isinstance(value, Mapping) or set(value) - fields or not all(isinstance(v, str) for v in value.values()):
        raise PresetError(f"{key} must be an object of {', '.join(sorted(fields))}")
    return value


def _common(doc: Mapping[str, Any], kind: str, shipped: bool) -> dict[str, Any]:
    label, note = _str(doc, "label", LABEL_RE), _str(doc, "note")
    hint = doc.get("key_hint")
    if len(label.encode("utf-8")) > LABEL_MAX or not 1 <= len(note) <= 300:
        raise PresetError(f"label is at most {LABEL_MAX} UTF-8 bytes, note 1-300 characters")
    if hint is not None and hint not in BINDABLE_KEYS:
        raise PresetError("key_hint must be one of b c f j k o u x y")
    if not isinstance(doc.get("leaves_mac"), bool):
        raise PresetError("leaves_mac must be true or false")
    detect = _object(doc, "detect", frozenset({"bundle"}))
    return {"preset": _str(doc, "preset", PRESET_ID), "name": _str(doc, "name", re.compile(CUSTOM_NAME[:-1] + r"\Z")),
            "label": label, "kind": kind, "bundle": _str(detect, "bundle", BUNDLE_RE),
            "leaves_mac": doc["leaves_mac"], "note": note, "shipped": shipped, "key_hint": hint}  # fmt: skip


def denied(bundle: str, deny: Deny) -> bool:
    """deny.json names this app (bundle ids compare without case, as LaunchServices does)."""
    return bundle.casefold() in {h.casefold() for h in deny.handlers}


def _check_target(template: str, focus: str | None, deny: Deny) -> None:
    try:
        check_template(template, focus=False)
        if focus is not None:
            check_template(focus, focus=True)
    except TemplateError as exc:
        raise PresetError(str(exc)) from exc
    scheme = scheme_of(template)
    if focus is not None and scheme_of(focus) != scheme:
        raise PresetError("focus must use the template's scheme")
    if scheme in deny.schemes or scheme.startswith(deny.scheme_prefixes):
        raise PresetError(f"links of scheme {scheme} are not allowed")
    if any(route in t.lower() for route in deny.routes for t in (template, focus or "")):
        raise PresetError("this link route is not allowed")


def _map(doc: Mapping[str, Any]) -> tuple[str | None, str | None]:
    if "map" not in doc:
        return None, None
    found = _object(doc, "map", frozenset({"pattern", "from_link"}))
    try:
        compiled = [re.compile(p) for p in found.values()]
    except re.error as exc:
        raise PresetError("map patterns must be regular expressions") from exc
    if "from_link" in found and compiled[list(found).index("from_link")].groups != 1:
        raise PresetError("map.from_link must capture exactly one group")
    return found.get("pattern"), found.get("from_link")


def _url(doc: Mapping[str, Any], base: dict[str, Any], deny: Deny) -> Preset:
    template, focus = _str(doc, "template"), doc.get("focus")
    if focus is not None and not isinstance(focus, str):
        raise PresetError("focus must be a string")
    _check_target(template, focus, deny)
    effect, overflow = doc.get("effect"), doc.get("overflow")
    if effect not in (None, "draft", "act") or overflow not in (None, "clip"):
        raise PresetError("effect is draft or act; overflow is clip")
    pattern, from_link = _map(doc)
    return Preset(**base, effect=effect if base["shipped"] else "act", template=template, focus=focus,
                  max_text_utf16=_int(doc, "max_text_utf16", 1, 16_384, 1000),
                  max_url=_int(doc, "max_url", 16, 65_536, 2048), overflow=overflow,
                  map_pattern=pattern, from_link=from_link)  # fmt: skip


def parse_preset(doc: Any, *, shipped: bool, deny: Deny) -> Preset:
    """Strict; raises PresetError. A preset that is not shipped always acts (effect act)."""
    kind = _keys(doc)
    base = _common(doc, kind, shipped)
    if denied(base["bundle"], deny):
        raise PresetError("this app may not handle destinations")
    if kind == "url":
        return _url(doc, base, deny)
    if kind == "file":
        path = _object(doc, "file", frozenset({"path"})).get("path")
        if not path:
            raise PresetError("file.path is required")
        return Preset(**base, file_path=path)
    return Preset(**base)


@dataclass(frozen=True, slots=True)
class PresetSet:
    presets: tuple[Preset, ...]
    problems: tuple[str, ...] = ()

    def find(self, preset_id: str) -> Preset | None:
        return next((p for p in self.presets if p.preset == preset_id), None)


def shipped_presets(deny: Deny | None = None) -> tuple[Preset, ...]:
    deny = deny or load_deny()
    files = sorted((f for f in _data_dir().iterdir() if f.name.endswith(".json")), key=lambda f: f.name)
    docs = (json.loads(f.read_text(encoding="utf-8")) for f in files if f.name not in SPECIAL
            and not f.name.endswith(".vectors.json"))  # fmt: skip
    return tuple(parse_preset(doc, shipped=True, deny=deny) for doc in docs)


def read_user_preset(path: Path, deny: Deny, taken: frozenset[str]) -> Preset:
    """One of your preset files: private (0600), valid, not a shipped id; raises PresetError."""
    try:
        mode = path.stat().st_mode
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise PresetError(f"{path.name}: cannot be read as JSON") from exc
    if stat.S_IMODE(mode) & 0o077:
        raise PresetError(f"{path.name}: must be readable only by you (chmod 600)")
    preset = parse_preset(doc, shipped=False, deny=deny)
    if preset.preset in taken:
        raise PresetError(f"{path.name}: {preset.preset} is a preset that ships with cpad")
    return preset


def load_presets(cpa_home: Path | None) -> PresetSet:
    """Shipped presets, then yours; a broken file of yours is reported and skipped, never fatal."""
    deny = load_deny()
    shipped = shipped_presets(deny)
    taken = frozenset(p.preset for p in shipped)
    folder = cpa_home / "presets" if cpa_home is not None else None
    mine, problems = [], []
    for path in sorted(folder.glob("*.json")) if folder is not None and folder.is_dir() else []:
        try:
            mine.append(read_user_preset(path, deny, taken | {p.preset for p in mine}))
        except PresetError as exc:
            problems.append(str(exc))
    return PresetSet((*shipped, *mine), tuple(problems))


def map_entry(preset: Preset, raw: str, deny: Deny) -> str:
    """A --map value: the preset's pattern, or a link its from_link takes the value from; never a credential."""
    value = raw.strip()
    if preset.from_link is not None:
        found = re.match(preset.from_link, value)
        value = found.group(1) if found else value
    if value.startswith(deny.secret_prefixes):
        raise PresetError("that looks like a secret key; map takes an id, never a key")
    if preset.map_pattern is None or not re.match(preset.map_pattern, value):
        raise PresetError(f"{preset.label} does not take that value (paste the id or a link to it)")
    return value
