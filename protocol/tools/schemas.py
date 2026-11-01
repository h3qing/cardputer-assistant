"""Load the protocol v0 contract: limits.json, message schemas and custom keywords.

Custom keywords (ignored by standard validators, enforced here):
  x-maxBytes   string length limit in UTF-8 bytes
  x-uniqueBy   array of objects: the named property must not repeat
  x-maxPerKey  array of objects: at most `max` items share one value of `key`
  x-b64Len     object: the base64 string property `data` decodes to exactly `len` (an integer property) bytes
`pattern` follows ECMA-262 semantics: `$` does not match before a trailing newline.
"""

from __future__ import annotations

import base64
import binascii
import functools
import json
import re
from collections.abc import Iterable, Iterator, Mapping
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any

from jsonschema import Draft202012Validator, ValidationError, validators
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

SCHEMA_DIR = Path("schema/v0")
META_DIR = Path("schema/meta")
COMMON = "common"
SUFFIX = ".schema.json"

# limits.json key -> (schema name, JSON pointer) holding the same value.
LIMIT_POINTERS = (
    ("protocol_version", COMMON, "/$defs/version/const"),
    ("id_pattern", COMMON, "/$defs/id/pattern"),
    ("type_pattern", COMMON, "/$defs/type/pattern"),
    ("monoid_pattern", COMMON, "/$defs/monoId/pattern"),
    ("boot_pattern", COMMON, "/$defs/bootId/pattern"),
    ("project_key_pattern", COMMON, "/$defs/projectKey/pattern"),
    ("slot_min", COMMON, "/$defs/slot/minimum"),
    ("slot_max", COMMON, "/$defs/slot/maximum"),
    ("project_name_max", COMMON, "/$defs/projectName/x-maxBytes"),
    ("session_name_max", COMMON, "/$defs/sessionName/x-maxBytes"),
    ("queue_text_max", COMMON, "/$defs/queueText/x-maxBytes"),
    ("text_min", COMMON, "/$defs/userText/minLength"),
    ("text_max", COMMON, "/$defs/userText/x-maxBytes"),
    ("sid_pattern", COMMON, "/$defs/sid/pattern"),
    ("short_max", COMMON, "/$defs/short/maxLength"),
    ("qid_pattern", COMMON, "/$defs/qid/pattern"),
    ("epoch_ms_min", COMMON, "/$defs/epochMs/minimum"),
    ("tz_min_min", COMMON, "/$defs/tzMin/minimum"),
    ("tz_min_max", COMMON, "/$defs/tzMin/maximum"),
    ("snapshot_projects_max", "projects.snapshot", "/properties/projects/maxItems"),
    ("snapshot_sessions_max", "sessions.snapshot", "/properties/sessions/maxItems"),
    ("snapshot_queue_max", "queue.snapshot", "/properties/items/maxItems"),
    ("snapshot_queue_per_project_max", "queue.snapshot", "/properties/items/x-maxPerKey/max"),
    ("file_list_max", "file.list", "/properties/max/anyOf/0/maximum"),
    ("file_list_max", "file.items", "/properties/items/maxItems"),
    ("file_get_b64_max", "file.get", "/properties/len/maximum"),
    ("file_get_b64_max", "file.data", "/properties/n/maximum"),
    ("file_pending_max", COMMON, "/$defs/filesN/maximum"),
    ("file_marks_max", COMMON, "/$defs/fileItem/properties/stars/maxItems"),
    ("file_seg_max", COMMON, "/$defs/segIndex/maximum"),
    ("sha256_pattern", COMMON, "/$defs/sha256/pattern"),
    ("lead_ms_max", COMMON, "/$defs/fileItem/properties/lead_ms/anyOf/0/maximum"),
    ("seg_ms_max", COMMON, "/$defs/fileItem/properties/dur_ms/maximum"),
    ("seg_ms_max", COMMON, "/$defs/fileItem/properties/stars/items/exclusiveMaximum"),
    ("seg_ms_min", "debug.rec", "/properties/seg_ms/minimum"),
    ("seg_ms_max", "debug.rec", "/properties/seg_ms/maximum"),
    ("rec_list_max", "rec.list", "/properties/max/maximum"),
    ("rec_list_max", "rec.items", "/properties/items/maxItems"),
    ("rec_title_max", COMMON, "/$defs/recTitle/x-maxBytes"),
    ("rec_cursor_max", COMMON, "/$defs/recCursor/maxLength"),
    ("rec_cursor_pattern", COMMON, "/$defs/recCursor/pattern"),
    ("host_label_max", COMMON, "/$defs/hostLabel/x-maxBytes"),
    ("host_id_pattern", COMMON, "/$defs/hostId/pattern"),
    ("dest_pattern", COMMON, "/$defs/destId/pattern"),
    ("dest_max", COMMON, "/$defs/destId/x-maxBytes"),
)


@dataclass(frozen=True)
class Contract:
    root: Path
    limits: Mapping[str, Any]
    common: Mapping[str, Any]
    schemas: Mapping[str, Mapping[str, Any]]
    validators: Mapping[str, Any]


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


@functools.cache
def _compile(pattern: str) -> re.Pattern[str]:
    ecma = pattern[:-1] + r"\Z" if pattern.endswith("$") and not pattern.endswith(r"\$") else pattern
    return re.compile(ecma)


def ecma_search(pattern: str, text: str) -> bool:
    return _compile(pattern).search(text) is not None


def _pattern(validator: Any, pattern: str, instance: Any, schema: Any) -> Iterator[ValidationError]:
    if validator.is_type(instance, "string") and not ecma_search(pattern, instance):
        yield ValidationError(f"{instance!r} does not match {pattern!r}")


def _max_bytes(validator: Any, limit: int, instance: Any, schema: Any) -> Iterator[ValidationError]:
    if validator.is_type(instance, "string"):
        size = len(instance.encode("utf-8", "surrogatepass"))
        if size > limit:
            yield ValidationError(f"{size} UTF-8 bytes > {limit}")


def _key_values(items: list, key: str) -> tuple:
    return tuple(item.get(key) if isinstance(item, dict) else None for item in items)


def _unique_by(validator: Any, key: str, instance: Any, schema: Any) -> Iterator[ValidationError]:
    if not validator.is_type(instance, "array"):
        return
    values = _key_values(instance, key)
    dupes = [i for i, v in enumerate(values) if v is not None and v in values[:i]]
    if dupes:
        yield ValidationError(f"duplicate {key} {values[dupes[0]]!r}", path=(dupes[0], key))


def _max_per_key(validator: Any, spec: Mapping, instance: Any, schema: Any) -> Iterator[ValidationError]:
    if not validator.is_type(instance, "array"):
        return
    key, cap = spec["key"], spec["max"]
    values = _key_values(instance, key)
    over = [i for i, v in enumerate(values) if v is not None and values[: i + 1].count(v) > cap]
    if over:
        yield ValidationError(f"more than {cap} items share {key} {values[over[0]]!r}", path=(over[0], key))


def _decoded_size(text: str) -> int | None:
    try:
        return len(base64.b64decode(text, validate=True))
    except binascii.Error:
        return None


def _b64_len(validator: Any, spec: Mapping, instance: Any, schema: Any) -> Iterator[ValidationError]:
    if not validator.is_type(instance, "object"):
        return
    data, length = instance.get(spec["data"]), instance.get(spec["len"])
    if not validator.is_type(data, "string") or not validator.is_type(length, "integer"):
        return
    size = _decoded_size(data)
    if size is not None and size != length:
        yield ValidationError(
            f"{spec['data']} decodes to {size} bytes, {spec['len']} is {length}", path=(spec["data"],)
        )


ContractValidator = validators.extend(
    Draft202012Validator,
    {
        "pattern": _pattern,
        "x-maxBytes": _max_bytes,
        "x-uniqueBy": _unique_by,
        "x-maxPerKey": _max_per_key,
        "x-b64Len": _b64_len,
    },
)


def _registry(schemas: Iterable[Mapping[str, Any]]) -> Registry:
    resources = (
        (s["$id"], Resource.from_contents(s, default_specification=DRAFT202012)) for s in schemas if "$id" in s
    )
    return Registry().with_resources(resources)


def load_contract(root: Path) -> Contract:
    files = sorted((root / SCHEMA_DIR).glob("*" + SUFFIX))
    loaded = {f.name[: -len(SUFFIX)]: load_json(f) for f in files}
    registry = _registry(loaded.values())
    messages = {name: s for name, s in loaded.items() if name != COMMON}
    return Contract(
        root=root,
        limits=MappingProxyType(load_json(root / "limits.json")),
        common=MappingProxyType(loaded.get(COMMON, {})),
        schemas=MappingProxyType(messages),
        validators=MappingProxyType({n: ContractValidator(s, registry=registry) for n, s in messages.items()}),
    )


def meta_validator(root: Path, name: str) -> Any:
    return ContractValidator(load_json(root / META_DIR / f"{name}{SUFFIX}"))


def format_path(parts: Iterable[Any]) -> str:
    return "".join(f"[{p}]" if isinstance(p, int) else f".{p}" for p in parts).lstrip(".")


def error_fields(error: ValidationError) -> tuple[str, ...]:
    path = tuple(error.absolute_path)
    if error.validator == "required" and isinstance(error.instance, dict):
        missing = [p for p in error.validator_value if p not in error.instance]
        return tuple(format_path((*path, p)) for p in missing)
    return (format_path(path),)


def field_errors(contract: Contract, msg: Mapping[str, Any]) -> tuple[str, ...]:
    validator = contract.validators[msg["t"]]
    return tuple(sorted({f for e in validator.iter_errors(msg) for f in error_fields(e)}))


def resolve_pointer(document: Any, pointer: str) -> Any:
    def step(node: Any, token: str) -> Any:
        key = token.replace("~1", "/").replace("~0", "~")
        if isinstance(node, list) and key.isdigit():
            return node[int(key)] if int(key) < len(node) else None
        return node.get(key) if isinstance(node, Mapping) else None

    return functools.reduce(step, pointer.lstrip("/").split("/"), document)


def limit_mismatches(contract: Contract) -> tuple[str, ...]:
    documents = {COMMON: contract.common, **contract.schemas}
    found = ((key, name, ptr, resolve_pointer(documents.get(name, {}), ptr)) for key, name, ptr in LIMIT_POINTERS)
    return tuple(
        f"limits.json {key}={contract.limits.get(key)!r} but {name}{SUFFIX}{ptr}={value!r}"
        for key, name, ptr, value in found
        if value != contract.limits.get(key)
    )
