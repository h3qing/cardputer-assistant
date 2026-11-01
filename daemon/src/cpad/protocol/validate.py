"""Hand-written validator combinators (no jsonschema at runtime).

A Check takes (value, path) and returns the paths of the faults it found. Paths are tuples of
keys and indexes; format_path() renders them like the contract: ``sessions[3].name``.
`pattern` strings follow ECMA-262: a trailing ``$`` matches only at the very end of the string.
"""

from __future__ import annotations

import functools
import re
from collections.abc import Callable, Iterable, Mapping
from typing import Any

Path = tuple[str | int, ...]
Errors = tuple[Path, ...]
Check = Callable[[Any, Path], Errors]

NO_CONTROL = r"^[^\u0000-\u001f\u007f]*$"


@functools.cache
def compile_pattern(pattern: str) -> re.Pattern[str]:
    ecma = pattern[:-1] + r"\Z" if pattern.endswith("$") and not pattern.endswith(r"\$") else pattern
    return re.compile(ecma)


def matches(pattern: str, value: Any) -> bool:
    return isinstance(value, str) and compile_pattern(pattern).search(value) is not None


def format_path(path: Iterable[str | int]) -> str:
    return "".join(f"[{p}]" if isinstance(p, int) else f".{p}" for p in path).lstrip(".")


def is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def utf8_len(text: str) -> int:
    return len(text.encode("utf-8", "surrogatepass"))


def _fault(path: Path) -> Errors:
    return (path,)


def string(
    pattern: str | None = None, *, min_len: int = 0, max_len: int | None = None, max_bytes: int | None = None
) -> Check:
    def check(value: Any, path: Path) -> Errors:
        if not isinstance(value, str) or len(value) < min_len:
            return _fault(path)
        too_long = (max_len is not None and len(value) > max_len) or (
            max_bytes is not None and utf8_len(value) > max_bytes
        )
        if too_long or (pattern is not None and not matches(pattern, value)):
            return _fault(path)
        return ()

    return check


def integer(lo: int, hi: int) -> Check:
    def check(value: Any, path: Path) -> Errors:
        return () if is_int(value) and lo <= value <= hi else _fault(path)

    return check


def boolean() -> Check:
    def check(value: Any, path: Path) -> Errors:
        return () if isinstance(value, bool) else _fault(path)

    return check


def one_of(*allowed: str) -> Check:
    def check(value: Any, path: Path) -> Errors:
        return () if isinstance(value, str) and value in allowed else _fault(path)

    return check


def nullable(inner: Check) -> Check:
    def check(value: Any, path: Path) -> Errors:
        return () if value is None else inner(value, path)

    return check


def absent() -> Check:
    """The field must not be present at all (used for `re` on requests)."""

    def check(_value: Any, path: Path) -> Errors:
        return _fault(path)

    return check


def all_of(*checks: Check) -> Check:
    def check(value: Any, path: Path) -> Errors:
        return tuple(dict.fromkeys(e for c in checks for e in c(value, path)))

    return check


def any_object() -> Check:
    def check(value: Any, path: Path) -> Errors:
        return () if isinstance(value, dict) else _fault(path)

    return check


def obj(
    required: Mapping[str, Check],
    optional: Mapping[str, Check] | None = None,
    rules: Iterable[Check] = (),
) -> Check:
    """An object: required fields, optional fields (checked only when present) and extra rules."""
    optional = optional or {}

    def check(value: Any, path: Path) -> Errors:
        if not isinstance(value, dict):
            return _fault(path)
        missing = tuple((*path, k) for k in required if k not in value)
        fields = {**required, **optional}
        nested = tuple(e for k, c in fields.items() if k in value for e in c(value[k], (*path, k)))
        ruled = tuple(e for rule in rules for e in rule(value, path))
        return tuple(dict.fromkeys(missing + nested + ruled))

    return check


def _key(item: Any, key: str) -> Any:
    value = item.get(key) if isinstance(item, dict) else None
    return None if isinstance(value, dict | list) else value


def _duplicates(items: list, key: str, cap: int) -> int | None:
    """Index of the first item whose `key` value has occurred more than `cap` times so far."""
    values = [_key(item, key) for item in items]
    return next((i for i, v in enumerate(values) if v is not None and values[: i + 1].count(v) > cap), None)


def array(
    item: Check,
    *,
    min_items: int = 0,
    max_items: int | None = None,
    unique: bool = False,
    unique_by: str | None = None,
    max_per_key: tuple[str, int] | None = None,
) -> Check:
    def check(value: Any, path: Path) -> Errors:
        if not isinstance(value, list) or len(value) < min_items:
            return _fault(path)
        if (max_items is not None and len(value) > max_items) or (unique and _has_repeats(value)):
            return _fault(path)
        nested = tuple(e for i, v in enumerate(value) for e in item(v, (*path, i)))
        rules = ((unique_by, 1), max_per_key or (None, 0))
        dupes = tuple((key, _duplicates(value, key, cap)) for key, cap in rules if key)
        return tuple(dict.fromkeys(nested + tuple((*path, i, key) for key, i in dupes if i is not None)))

    return check


def _has_repeats(values: list) -> bool:
    return any(v in values[:i] for i, v in enumerate(values))
