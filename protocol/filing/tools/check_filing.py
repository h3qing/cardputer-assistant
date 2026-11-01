#!/usr/bin/env python3
"""Validate the filing contract v1: the JSON of `cpa filing ... --json` and `cpa project ls --json` (docs/23 §3.2).

Usage: uv run --with 'jsonschema>=4.23,<5' python protocol/filing/tools/check_filing.py [--root .../v1]

validate(contract, doc, data) is a reader's verdict on one document (doc: ls | show | result | error | projects |
host | row, where row is one line of the export's data/filing.jsonl):
  1. not a JSON object                                        -> E_INVALID (root)
  2. v is not the integer 1 (missing, 2, "1", true; not row)  -> E_VERSION v; nothing else is read
  3. schema errors                                            -> E_INVALID <paths>
Unknown fields and unknown values of an extensible enum (x-knownValues) never cause a rejection.

check_all(root) checks the contract itself: valid Draft 2020-12 schemas that use only the keywords the WhisperWoof
validator knows (KEYWORDS, or an x- annotation), walked where it walks (properties and $defs values, anyOf / allOf
items, items, if, then), since it throws at start-up on any other; well-formed vector files; every valid vector
accepted; every invalid vector rejected with exactly its declared reason; the required invalid cases, a valid case
per document and one with unknown values present; VECTORS.sha256 lists exactly the vector files and their digests.
Prints per-document counts; exits 0 when clean, 1 otherwise.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Callable, Iterable, Iterator, Mapping
from dataclasses import dataclass
from itertools import chain
from pathlib import Path
from types import MappingProxyType
from typing import Any

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

# The recording contract's checker holds the vector loading and digest rules; schemas the pattern semantics.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "recording" / "tools"))
from check_recording import DIGESTS, SUFFIX, Report, Verdict, digest_problems, load_vectors  # noqa: E402
from schemas import ContractValidator, error_fields, load_json  # noqa: E402

DOCS = ("ls", "show", "result", "error", "projects", "host", "row")
UNVERSIONED = ("row",)
CODES = ("E_INVALID", "E_VERSION")
# The only keywords the WhisperWoof contract validator knows (contract-schema.js ANNOTATIONS + ASSERTIONS, docs/23
# §3.2); it throws at start-up on any other that is not an x- annotation.
KEYWORDS = frozenset({"$schema", "$id", "title", "description", "$defs", "x-knownValues", "$ref", "type", "const",
                      "enum", "pattern", "minimum", "maximum", "minLength", "maxLength", "required", "properties",
                      "items", "minItems", "maxItems", "anyOf", "allOf", "if", "then"})  # fmt: skip
FORBIDDEN = ("oneOf", "else", "not", "format", "patternProperties", "exclusiveMinimum", "additionalProperties")
NAMED = ("properties", "$defs")  # their values are schemas by name
LISTS = ("anyOf", "allOf")  # their items are schemas
ONE = ("items", "if", "then")  # their value is a schema


@dataclass(frozen=True)
class FilingContract:
    root: Path
    schemas: Mapping[str, Mapping[str, Any]]
    validators: Mapping[str, Any]


def load(root: Path) -> FilingContract:
    loaded = {f.name[: -len(SUFFIX)]: load_json(f) for f in sorted((root / "schema").glob("*" + SUFFIX))}
    resources = (
        (s["$id"], Resource.from_contents(s, default_specification=DRAFT202012)) for s in loaded.values() if "$id" in s
    )
    registry = Registry().with_resources(resources)
    return FilingContract(
        root=root,
        schemas=MappingProxyType(loaded),
        validators=MappingProxyType(
            {n: ContractValidator(s, registry=registry) for n, s in loaded.items() if n in DOCS}
        ),
    )


# --- the reader's verdict -------------------------------------------------------------


def _is_v1(data: Mapping[str, Any]) -> bool:
    v = data.get("v")
    return isinstance(v, int) and not isinstance(v, bool) and v == 1


def validate(contract: FilingContract, doc: str, data: Any) -> Verdict:
    if not isinstance(data, dict):
        return Verdict("E_INVALID", ("(root)",))
    if doc not in UNVERSIONED and not _is_v1(data):
        return Verdict("E_VERSION", ("v",))
    fields = {f for e in contract.validators[doc].iter_errors(data) for f in error_fields(e)}
    return Verdict("E_INVALID", tuple(sorted(fields))) if fields else Verdict("ok")


# --- extensible enums -----------------------------------------------------------------


def _known(contract: FilingContract) -> tuple[frozenset[str], Mapping[str, frozenset[str]]]:
    """(the row's fields, the x-knownValues of kind, why and tx), from row.schema.json."""
    props = contract.schemas.get("row", {}).get("properties", {})
    values = {"kind": props.get("kind", {}), "why": props.get("why", {}).get("items", {}), "tx": props.get("tx", {})}
    return frozenset(props), {k: frozenset(v.get("x-knownValues", ())) for k, v in values.items()}


def _rows(case: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    data = case.get("data") if isinstance(case.get("data"), dict) else {}
    found = {"ls": data.get("rows"), "show": [data.get("row")], "row": [data]}.get(str(case.get("doc")))
    return [r for r in found or () if isinstance(r, dict)]


def has_unknown_values(contract: FilingContract, case: Mapping[str, Any]) -> bool:
    """A row with an unknown field and an unknown kind, why or tx value: what a reader must keep."""
    fields, known = _known(contract)

    def unknown(row: Mapping[str, Any]) -> bool:
        whys = row.get("why") if isinstance(row.get("why"), list) else []
        odd = row.get("kind") not in known["kind"] or row.get("tx") not in known["tx"]
        return bool(set(row) - fields) and (odd or any(w not in known["why"] for w in whys))

    return any(unknown(row) for row in _rows(case))


# --- the contract's own checks ---------------------------------------------------------


def _text(data: Mapping[str, Any], *path: Any) -> str:
    node: Any = data
    for step in path:
        node = node[step] if isinstance(node, list | dict) and _has(node, step) else None
    return node if isinstance(node, str) else ""


def _has(node: list | dict, step: Any) -> bool:
    return step in node if isinstance(node, dict) else isinstance(step, int) and 0 <= step < len(node)


def _row0(data: Mapping[str, Any]) -> Mapping[str, Any]:
    rows = data.get("rows")
    return rows[0] if isinstance(rows, list) and rows and isinstance(rows[0], dict) else {}


def _unsafe(text: str) -> bool:
    return any(ord(c) < 0x20 or 0x7F <= ord(c) <= 0x9F or c in "\u2028\u2029" or 0x202A <= ord(c) <= 0x202E
               or 0x2066 <= ord(c) <= 0x2069 or 0xD800 <= ord(c) <= 0xDFFF for c in text)  # fmt: skip


def _env(data: Mapping[str, Any]) -> Mapping[str, Any]:
    env = _row0(data).get("env")
    return env if isinstance(env, dict) else {}


# (doc, code, field, label, predicate on the document): invalid cases the contract must carry (docs/23 §7.2).
REQUIRED_INVALID: tuple[tuple[str, str, str, str, Callable[[Mapping[str, Any]], bool]], ...] = (
    ("ls", "E_VERSION", "v", "v=2 ls", lambda d: d.get("v") == 2),
    ("ls", "E_INVALID", "rows[0].id", "row id not a MonoId", lambda d: "id" in _row0(d)),
    ("ls", "E_INVALID", "rows[0].text", "text over 280 characters", lambda d: len(_text(d, "rows", 0, "text")) > 280),
    (
        "ls",
        "E_INVALID",
        "rows[0].title",
        "title with a bidirectional control",
        lambda d: _unsafe(_text(d, "rows", 0, "title")),
    ),
    ("ls", "E_INVALID", "rows[0].text", "text with a terminal escape", lambda d: "\x1b" in _text(d, "rows", 0, "text")),
    (
        "ls",
        "E_INVALID",
        "rows[0].text",
        "text with a lone surrogate",
        lambda d: any(0xD800 <= ord(c) <= 0xDFFF for c in _text(d, "rows", 0, "text")),
    ),
    (
        "ls",
        "E_INVALID",
        "rows[0].env.link",
        "env.link not an enum word",
        lambda d: isinstance(_env(d).get("link"), str) and not _env(d)["link"].islower(),
    ),
    ("ls", "E_INVALID", "rows[0].env.host", "env.host over 24", lambda d: len(_text(d, "rows", 0, "env", "host")) > 24),
    (
        "ls",
        "E_INVALID",
        "rows[0].env.host",
        "env.host with a bidirectional control",
        lambda d: _unsafe(_text(d, "rows", 0, "env", "host")),
    ),
    (
        "ls",
        "E_INVALID",
        "rows[0].asked",
        "asked that is not a key",
        lambda d: _text(d, "rows", 0, "asked") != _text(d, "rows", 0, "asked").lower(),
    ),
    (
        "ls",
        "E_INVALID",
        "rows[0].device",
        "device id that is a MAC address",
        lambda d: ":" in _text(d, "rows", 0, "device"),
    ),
    (
        "ls",
        "E_INVALID",
        "rows[0].filing",
        "unknown filing state",
        lambda d: _row0(d).get("filing") not in ("new", "filed", "handed", "done", "hidden"),
    ),
    (
        "ls",
        "E_INVALID",
        "rows[0].queue",
        "handed row without its queue item",
        lambda d: _row0(d).get("filing") == "handed" and _row0(d).get("queue") is None,
    ),
    (
        "ls",
        "E_INVALID",
        "rows[0].filing",
        "asked on a row that is not new",
        lambda d: isinstance(_row0(d).get("asked"), str) and _row0(d).get("filing") != "new",
    ),
    ("row", "E_INVALID", "project", "project that is not a key", lambda d: isinstance(d.get("project"), str)),
    (
        "show",
        "E_INVALID",
        "sentences[0].text",
        "sentence with a control character",
        lambda d: _unsafe(_text(d, "sentences", 0, "text")),
    ),
    ("show", "E_INVALID", "audio[0]", "audio given as a path", lambda d: "/" in _text(d, "audio", 0)),
    ("result", "E_INVALID", "undo", "changed false with an undo", lambda d: d.get("changed") is False),
    ("result", "E_VERSION", "v", "v=2 result", lambda d: d.get("v") == 2),
    ("error", "E_INVALID", "error", "unknown error code", lambda d: "error" in d),
    ("host", "E_INVALID", "host", "Mac name with a control character", lambda d: _unsafe(_text(d, "host"))),
    (
        "projects",
        "E_INVALID",
        "projects[0].key",
        "project key with upper case",
        lambda d: _text(d, "projects", 0, "key") != _text(d, "projects", 0, "key").lower(),
    ),
    (
        "projects",
        "E_INVALID",
        "projects[0].folder",
        "folder given as a path",
        lambda d: "/" in _text(d, "projects", 0, "folder"),
    ),
)


def _subschemas(key: str, value: Any) -> tuple[Any, ...]:
    """The schemas under a keyword, where contract-schema.js assertSupported looks for them."""
    if key in NAMED and isinstance(value, dict):
        return tuple(value.values())
    if key in LISTS and isinstance(value, list):
        return tuple(value)
    return (value,) if key in ONE else ()


def unknown_keywords(node: Any) -> tuple[str, ...]:
    """Keywords outside KEYWORDS (x- annotations aside) anywhere WhisperWoof's validator walks a schema."""
    if not isinstance(node, dict):
        return ()
    own = tuple(k for k in node if k not in KEYWORDS and not k.startswith("x-"))
    nested = (unknown_keywords(sub) for k, v in node.items() if k in KEYWORDS for sub in _subschemas(k, v))
    return own + tuple(chain.from_iterable(nested))


def keyword_problems(contract: FilingContract) -> tuple[str, ...]:
    return tuple(f"schema {n}: uses {k}" for n, s in contract.schemas.items() for k in unknown_keywords(dict(s)))


def schema_problems(contract: FilingContract) -> tuple[str, ...]:
    meta = Draft202012Validator(Draft202012Validator.META_SCHEMA)
    invalid = (f"schema {n}: {e.message}" for n, s in contract.schemas.items() for e in meta.iter_errors(dict(s)))
    missing = (f"schema/{d}{SUFFIX}: missing" for d in DOCS if d not in contract.validators)
    return tuple(chain(invalid, missing, keyword_problems(contract)))


def _wrapper_problem(kind: str, case: Any) -> str:
    if not isinstance(case, dict) or case.get("doc") not in DOCS:
        return f"doc must be one of {list(DOCS)}"
    if not isinstance(case.get("name"), str) or not case["name"] or "data" not in case:
        return "needs a non-empty name and data"
    reason = case.get("reason")
    well_formed = isinstance(reason, dict) and reason.get("code") in CODES and isinstance(reason.get("field"), str)
    if kind == "invalid" and not well_formed:
        return f"reason must be {{code: one of {list(CODES)}, field: path}}"
    return ""


def _case_problem(contract: FilingContract, kind: str, case: Mapping[str, Any]) -> str:
    wrapper = _wrapper_problem(kind, case)
    if wrapper:
        return wrapper
    if case["doc"] not in contract.validators:
        return "its schema is missing"
    got = validate(contract, case["doc"], case["data"])
    want = Verdict("ok") if kind == "valid" else Verdict(case["reason"]["code"], (case["reason"]["field"],))
    return "" if got == want else f"expected {want.code} {list(want.fields)}, got {got.code} {list(got.fields)}"


def vector_problems(contract: FilingContract, vectors: Mapping[str, tuple[str, Any]]) -> tuple[str, ...]:
    checked = ((label, _case_problem(contract, kind, case)) for label, (kind, case) in vectors.items())
    return tuple(f"vectors/{label}: {problem}" for label, problem in checked if problem)


def _cases(vectors: Mapping[str, tuple[str, Any]], kind: str) -> tuple[Mapping[str, Any], ...]:
    return tuple(c for k, c in vectors.values() if k == kind and not _wrapper_problem(k, c))


def _missing_invalid(invalid: tuple[Mapping[str, Any], ...]) -> Iterator[str]:
    for doc, code, field, label, wanted in REQUIRED_INVALID:
        reason = {"code": code, "field": field}
        if not any(c["doc"] == doc and c["reason"] == reason and wanted(c["data"]) for c in invalid):
            yield f"needs an invalid {doc} case: {label} ({code} {field})"


def coverage_problems(contract: FilingContract, vectors: Mapping[str, tuple[str, Any]]) -> tuple[str, ...]:
    invalid, valid = _cases(vectors, "invalid"), _cases(vectors, "valid")
    thin = (f"needs a valid {doc} case" for doc in DOCS if not any(c["doc"] == doc for c in valid))
    unknown = () if any(has_unknown_values(contract, c) for c in valid) else (
        "needs a valid case whose row has an unknown field and an unknown kind, why or tx value",)  # fmt: skip
    return tuple(chain(_missing_invalid(invalid), thin, unknown))


def _counts(vectors: Mapping[str, tuple[str, Any]]) -> tuple[tuple[str, int, int], ...]:
    def count(doc: str, kind: str) -> int:
        return sum(1 for c in _cases(vectors, kind) if c["doc"] == doc)

    return tuple((doc, count(doc, "valid"), count(doc, "invalid")) for doc in DOCS)


def check_all(root: Path) -> Report:
    contract = load(root)
    vectors, load_errors = load_vectors(root)
    errors = tuple(
        chain(
            load_errors,
            schema_problems(contract),
            vector_problems(contract, vectors),
            coverage_problems(contract, vectors),
            digest_problems(root),
        )
    )
    return Report(errors, _counts(vectors))


def _print(report: Report) -> None:
    for doc, valid, invalid in report.counts:
        print(f"{doc:<10} valid={valid:<3} invalid={invalid}")
    for error in report.errors:
        print(f"ERROR {error}")


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate the filing contract v1.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1] / "v1")
    report = check_all(parser.parse_args(list(argv)).root)
    _print(report)
    if report.errors:
        print(f"FAILED: {len(report.errors)} problems")
        return 1
    print(f"OK: {sum(v + i for _, v, i in report.counts)} vectors, {DIGESTS} matches")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
