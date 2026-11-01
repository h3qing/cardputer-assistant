"""Link templates of url.<name> destinations (docs/25 §2.3). Pure; parsed with urllib, never pasted together.

Shape: `<scheme>://<host>/<path>?<name>=<value>&...`. Scheme, host and path are literals; each query value is
either one whole placeholder ({text}, {p}, {project}) or a literal of at most 16 safe characters, and no literal
parameter name may look like a credential. A template has {text} exactly once; a focus link (it only brings the
target forward) never has it. render() drops a parameter whose value is empty and percent-encodes every value
with nothing left safe, so `+` becomes %2B (some apps read `+` as a space).
"""

from __future__ import annotations

import re
from collections.abc import Mapping
from urllib.parse import quote, urlsplit

PLACEHOLDERS = ("{text}", "{p}", "{project}")
SCHEME_RE = re.compile(r"[a-z][a-z0-9+.-]{1,31}\Z")
LITERAL_RE = re.compile(r"[A-Za-z0-9/._~-]*\Z")  # host and path
NAME_RE = re.compile(r"[A-Za-z0-9_.-]{1,32}\Z")
VALUE_RE = re.compile(r"[A-Za-z0-9_.~-]{1,16}\Z")
SECRET_NAME_RE = re.compile(r"key|token|auth|secret|sig", re.IGNORECASE)


class TemplateError(ValueError):
    """A template breaks a shape rule."""


def _pairs(query: str) -> tuple[tuple[str, str], ...]:
    pairs = tuple(tuple(part.split("=", 1)) for part in query.split("&")) if query else ()
    if any(len(p) != 2 for p in pairs):
        raise TemplateError("every query part must be name=value")
    return pairs  # type: ignore[return-value]


def _check_pair(name: str, value: str) -> None:
    if not NAME_RE.match(name) or SECRET_NAME_RE.search(name):
        raise TemplateError(f"query parameter name {name!r} is not allowed")
    if value not in PLACEHOLDERS and not VALUE_RE.match(value):
        raise TemplateError(f"query value of {name!r} must be a placeholder or a literal of at most 16 characters")


def check_template(template: str, *, focus: bool) -> tuple[str, tuple[tuple[str, str], ...]]:
    """(scheme, query pairs); raises TemplateError."""
    if not isinstance(template, str) or len(template) > 512 or "#" in template or "\\" in template:
        raise TemplateError("a template is at most 512 characters, without '#' or '\\'")
    parts = urlsplit(template)
    if not SCHEME_RE.match(parts.scheme) or not template.startswith(parts.scheme + "://"):
        raise TemplateError("a template starts with a lower-case <scheme>://")
    if not LITERAL_RE.match(parts.netloc) or not LITERAL_RE.match(parts.path):
        raise TemplateError("host and path are literals")
    pairs = _pairs(parts.query)
    for name, value in pairs:
        _check_pair(name, value)
    texts = sum(value == "{text}" for _, value in pairs)
    if texts != (0 if focus else 1):
        raise TemplateError("a focus link never carries {text}" if focus else "{text} must appear exactly once")
    return parts.scheme, pairs


def scheme_of(template: str) -> str:
    return urlsplit(template).scheme


def render(template: str, values: Mapping[str, str]) -> str:
    """The link with placeholders filled from values ({text}, {p}, {project}); empty values drop their parameter."""
    parts = urlsplit(template)
    query = []
    for name, value in _pairs(parts.query):
        filled = values.get(value, "") if value in PLACEHOLDERS else value
        if filled:
            query.append(f"{name}={quote(filled, safe='')}")
    base = f"{parts.scheme}://{parts.netloc}{parts.path}"
    return f"{base}?{'&'.join(query)}" if query else base
