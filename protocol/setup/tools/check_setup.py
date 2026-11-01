#!/usr/bin/env python3
"""Validate the setup contract v1: each line of `cpa setup check --json` (docs/27 §2.6), one row per line.

Usage: uv run --with 'jsonschema>=4.23,<5' python protocol/setup/tools/check_setup.py [--root .../v1]

validate(contract, data) is a reader's verdict on one row, as check_dest_cli gives it: not an object -> E_INVALID
(root); v not the integer 1 -> E_VERSION v; schema errors -> E_INVALID <paths>; a key that names this Mac or the
board (host_id, serial, dev) anywhere -> E_INVALID <that key>, a rule the schema keywords cannot say. Unknown
fields and unknown values of an extensible enum (x-knownValues) never cause a rejection.
check_all(root) checks the contract itself: valid Draft 2020-12 schemas using only the keywords the WhisperWoof
validator knows; every valid vector accepted and every invalid one rejected with exactly its reason; the required
invalid cases; a valid case for each row id cpa prints today and one with unknown values; VECTORS.sha256 lists
exactly the vector files and their digests.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Callable, Iterable, Iterator, Mapping
from dataclasses import dataclass
from itertools import chain
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

HERE = Path(__file__).resolve()
for tools in (HERE.parents[2] / "recording" / "tools", HERE.parents[2] / "filing" / "tools", HERE.parents[2] / "tools"):
    sys.path.insert(0, str(tools))
from check_filing import unknown_keywords  # noqa: E402
from check_recording import DIGESTS, Report, Verdict, digest_problems, load_vectors  # noqa: E402
from schemas import ContractValidator, error_fields, load_json  # noqa: E402

DOC = "row"
CODES = ("E_INVALID", "E_VERSION")
PRIVATE = ("host_id", "serial", "dev")
TODAY = ("service", "notes", "name", "device", "disk", "stt", "hooks", "claude")
Pred = Callable[[Mapping[str, Any]], bool]


@dataclass(frozen=True)
class SetupContract:
    root: Path
    schema: Mapping[str, Any]
    validator: Any


def load(root: Path) -> SetupContract:
    schema = load_json(root / "schema" / "row.schema.json")
    registry = Registry().with_resources([(schema["$id"], Resource.from_contents(schema, DRAFT202012))])
    return SetupContract(root, schema, ContractValidator(schema, registry=registry))


def private_keys(node: Any) -> Iterator[str]:
    if isinstance(node, dict):
        yield from (k for k in node if k in PRIVATE)
        yield from chain.from_iterable(private_keys(v) for v in node.values())
    elif isinstance(node, list):
        yield from chain.from_iterable(private_keys(v) for v in node)


def validate(contract: SetupContract, data: Any) -> Verdict:
    if not isinstance(data, dict):
        return Verdict("E_INVALID", ("(root)",))
    v = data.get("v")
    if not (isinstance(v, int) and not isinstance(v, bool) and v == 1):
        return Verdict("E_VERSION", ("v",))
    fields = {f for e in contract.validator.iter_errors(data) for f in error_fields(e)} | set(private_keys(data))
    return Verdict("E_INVALID", tuple(sorted(fields))) if fields else Verdict("ok")


def _fix0(d: Mapping[str, Any]) -> Mapping[str, Any]:
    fix = d.get("fix")
    return fix[0] if isinstance(fix, list) and fix and isinstance(fix[0], dict) else {}


def _unsafe(text: Any) -> bool:
    return isinstance(text, str) and any(ord(c) < 0x20 or 0x7F <= ord(c) <= 0x9F or 0x202A <= ord(c) <= 0x202E
                                         or 0x2066 <= ord(c) <= 0x2069 for c in text)  # fmt: skip


# (code, field, predicate): invalid cases the contract must carry.
REQUIRED_INVALID: tuple[tuple[str, str, Pred], ...] = (
    ("E_VERSION", "v", lambda d: d.get("v") == 2),
    ("E_INVALID", "title", lambda d: _unsafe(d.get("title"))),
    ("E_INVALID", "detail", lambda d: _unsafe(d.get("detail"))),
    ("E_INVALID", "fix", lambda d: "fix" not in d),
    ("E_INVALID", "state", lambda d: str(d.get("state")).upper() == d.get("state")),
    ("E_INVALID", "required", lambda d: d.get("required") == "yes"),
    ("E_INVALID", "fix[0].cand", lambda d: str(_fix0(d).get("cand", "")).count(":") > 0),
    ("E_INVALID", "fix[0].op", lambda d: " " in str(_fix0(d).get("op"))),
    ("E_INVALID", "host_id", lambda d: "host_id" in d),
    ("E_INVALID", "serial", lambda d: "serial" in _fix0(d)),
    ("E_INVALID", "dev", lambda d: "dev" in d),
)


def has_unknown_values(contract: SetupContract, case: Mapping[str, Any]) -> bool:
    """A row with an unknown field, an unknown id, state and fix op: what a reader must keep and show."""
    props = contract.schema["properties"]
    data = case.get("data") or {}
    ops = contract.schema["$defs"]["fix"]["properties"]["op"]["x-knownValues"]
    ids = contract.schema["$defs"]["rowId"]["x-knownValues"]
    unknown_field = bool(set(data) - set(props))
    unknown_state = data.get("state") not in props["state"]["x-knownValues"]
    return unknown_field and data.get("id") not in ids and unknown_state and _fix0(data).get("op") not in ops


def schema_problems(contract: SetupContract) -> tuple[str, ...]:
    meta = Draft202012Validator(Draft202012Validator.META_SCHEMA)
    invalid = (f"schema row: {e.message}" for e in meta.iter_errors(dict(contract.schema)))
    keywords = (f"schema row: uses {k}" for k in unknown_keywords(dict(contract.schema)))
    return tuple(chain(invalid, keywords))


def _wrapper_problem(kind: str, case: Any) -> str:
    if not isinstance(case, dict) or case.get("doc") != DOC:
        return f"doc must be {DOC!r}"
    if not isinstance(case.get("name"), str) or not case["name"] or "data" not in case:
        return "needs a non-empty name and data"
    reason = case.get("reason")
    if kind == "invalid" and not (isinstance(reason, dict) and reason.get("code") in CODES
                                  and isinstance(reason.get("field"), str)):  # fmt: skip
        return f"reason must be {{code: one of {list(CODES)}, field: path}}"
    return ""


def _case_problem(contract: SetupContract, kind: str, case: Mapping[str, Any]) -> str:
    wrapper = _wrapper_problem(kind, case)
    if wrapper:
        return wrapper
    got = validate(contract, case["data"])
    want = Verdict("ok") if kind == "valid" else Verdict(case["reason"]["code"], (case["reason"]["field"],))
    return "" if got == want else f"expected {want.code} {list(want.fields)}, got {got.code} {list(got.fields)}"


def _cases(vectors: Mapping[str, tuple[str, Any]], kind: str) -> tuple[Mapping[str, Any], ...]:
    return tuple(c for k, c in vectors.values() if k == kind and not _wrapper_problem(k, c))


def coverage_problems(contract: SetupContract, vectors: Mapping[str, tuple[str, Any]]) -> tuple[str, ...]:
    invalid, valid = _cases(vectors, "invalid"), _cases(vectors, "valid")

    def carried(code: str, field: str, wanted: Pred) -> bool:
        return any(c["reason"] == {"code": code, "field": field} and wanted(c["data"]) for c in invalid)

    missing = (f"needs an invalid case ({code} {field})" for code, field, wanted in REQUIRED_INVALID
               if not carried(code, field, wanted))  # fmt: skip
    thin = (f"needs a valid {row} row" for row in TODAY if not any(c["data"].get("id") == row for c in valid))
    unknown = () if any(has_unknown_values(contract, c) for c in valid) else (
        "needs a valid row with an unknown field, id, state and fix op",)  # fmt: skip
    return tuple(chain(missing, thin, unknown))


def check_all(root: Path) -> Report:
    contract = load(root)
    vectors, load_errors = load_vectors(root)
    checked = ((label, _case_problem(contract, kind, case)) for label, (kind, case) in vectors.items())
    problems = tuple(f"vectors/{label}: {problem}" for label, problem in checked if problem)
    errors = tuple(chain(load_errors, schema_problems(contract), problems, coverage_problems(contract, vectors),
                         digest_problems(root)))  # fmt: skip
    counts = ((DOC, len(_cases(vectors, "valid")), len(_cases(vectors, "invalid"))),)
    return Report(errors, counts)


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate the setup contract v1.")
    parser.add_argument("--root", type=Path, default=HERE.parents[1] / "v1")
    report = check_all(parser.parse_args(list(argv)).root)
    for doc, valid, invalid in report.counts:
        print(f"{doc:<10} valid={valid:<3} invalid={invalid}")
    for error in report.errors:
        print(f"ERROR {error}")
    if report.errors:
        print(f"FAILED: {len(report.errors)} problems")
        return 1
    print(f"OK: {sum(v + i for _, v, i in report.counts)} vectors, {DIGESTS} matches")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
