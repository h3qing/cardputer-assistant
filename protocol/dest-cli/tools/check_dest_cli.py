#!/usr/bin/env python3
"""Validate the dest-cli contract v1: the JSON of `cpa dest pending|send|drop|usual|test --send` and `cpa filing send`
with --json (docs/25 §5.4, §5.5).

Usage: uv run --with 'jsonschema>=4.23,<5' python protocol/dest-cli/tools/check_dest_cli.py [--root .../v1]

validate(contract, doc, data) is a reader's verdict on one document (doc: pending | sent | dropped | usual | tested |
error), as protocol/filing/tools/check_filing.py gives it: not an object -> E_INVALID (root); v not the integer 1
-> E_VERSION v; schema errors -> E_INVALID <paths>. Unknown fields and unknown values of an extensible enum
(x-knownValues) never cause a rejection.
check_all(root) checks the contract itself as check_filing does: valid Draft 2020-12 schemas using only the keywords
the WhisperWoof validator knows (one validator reads both contracts); every valid vector accepted and every invalid
one rejected with exactly its reason; the required invalid cases and a valid case per document, one of them with an
unknown field and unknown enum values; VECTORS.sha256 lists exactly the vector files and their digests.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass
from itertools import chain
from pathlib import Path
from types import MappingProxyType
from typing import Any

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

HERE = Path(__file__).resolve()
for tools in (HERE.parents[2] / "recording" / "tools", HERE.parents[2] / "filing" / "tools", HERE.parents[2] / "tools"):
    sys.path.insert(0, str(tools))
from check_filing import unknown_keywords  # noqa: E402
from check_recording import DIGESTS, SUFFIX, Report, Verdict, digest_problems, load_vectors  # noqa: E402
from schemas import ContractValidator, error_fields, load_json  # noqa: E402

DOCS = ("pending", "sent", "dropped", "usual", "tested", "error")
CODES = ("E_INVALID", "E_VERSION")
Pred = Callable[[Mapping[str, Any]], bool]


@dataclass(frozen=True)
class DestCliContract:
    root: Path
    schemas: Mapping[str, Mapping[str, Any]]
    validators: Mapping[str, Any]


def load(root: Path) -> DestCliContract:
    loaded = {f.name[: -len(SUFFIX)]: load_json(f) for f in sorted((root / "schema").glob("*" + SUFFIX))}
    resources = ((s["$id"], Resource.from_contents(s, default_specification=DRAFT202012)) for s in loaded.values())
    registry = Registry().with_resources(resources)
    validators = {n: ContractValidator(s, registry=registry) for n, s in loaded.items() if n in DOCS}
    return DestCliContract(root, MappingProxyType(loaded), MappingProxyType(validators))


def validate(contract: DestCliContract, doc: str, data: Any) -> Verdict:
    if not isinstance(data, dict):
        return Verdict("E_INVALID", ("(root)",))
    v = data.get("v")
    if not (isinstance(v, int) and not isinstance(v, bool) and v == 1):
        return Verdict("E_VERSION", ("v",))
    fields = {f for e in contract.validators[doc].iter_errors(data) for f in error_fields(e)}
    return Verdict("E_INVALID", tuple(sorted(fields))) if fields else Verdict("ok")


def _row0(d: Mapping[str, Any]) -> Mapping[str, Any]:
    rows = d.get("rows")
    return rows[0] if isinstance(rows, list) and rows and isinstance(rows[0], dict) else {}


def _unsafe(text: Any) -> bool:
    return isinstance(text, str) and any(ord(c) < 0x20 or 0x7F <= ord(c) <= 0x9F or 0x202A <= ord(c) <= 0x202E
                                         or 0x2066 <= ord(c) <= 0x2069 for c in text)  # fmt: skip


def _upper_key(d: Mapping[str, Any]) -> bool:
    return any(u.get("project", "").lower() != u.get("project") for u in d.get("usual", []))


# (doc, code, field, predicate): invalid cases the contract must carry.
REQUIRED_INVALID: tuple[tuple[str, str, str, Pred], ...] = (
    ("pending", "E_VERSION", "v", lambda d: d.get("v") == 2),
    ("pending", "E_INVALID", "rows[0].text", lambda d: _unsafe(_row0(d).get("text"))),
    ("pending", "E_INVALID", "rows[0].fl", lambda d: _row0(d).get("fl") == "kl"),
    ("pending", "E_INVALID", "rows[0].st", lambda d: _row0(d).get("why") == "held" and _row0(d).get("st") is None),
    ("pending", "E_INVALID", "rows[0].code", lambda d: _row0(d).get("st") == "failed" and _row0(d).get("code") is None),
    ("pending", "E_INVALID", "rows[0].dest", lambda d: len(str(_row0(d).get("dest")).partition(".")[2]) > 12),
    ("sent", "E_INVALID", "code", lambda d: d.get("outcome") == "failed" and d.get("code") is None),
    ("sent", "E_INVALID", "via", lambda d: d.get("outcome") == "handed" and d.get("via") is None),
    ("sent", "E_INVALID", "label", lambda d: _unsafe(d.get("label"))),
    ("tested", "E_INVALID", "code", lambda d: d.get("handed") is False and d.get("code") is None),
    ("tested", "E_INVALID", "marker", lambda d: len(str(d.get("marker"))) != 4),
    ("error", "E_INVALID", "error", lambda d: not str(d.get("error")).startswith("E_")),
    ("usual", "E_INVALID", "usual[0].project", lambda d: _upper_key(d)),
    ("dropped", "E_INVALID", "ref", lambda d: " " in str(d.get("ref"))),
)


def _known(contract: DestCliContract) -> tuple[frozenset[str], frozenset[str], frozenset[str]]:
    """(a pending row's fields, the x-knownValues of why, of src)."""
    props = contract.schemas["pending"]["properties"]["rows"]["items"]["properties"]
    return frozenset(props), frozenset(props["why"]["x-knownValues"]), frozenset(props["src"]["x-knownValues"])


def has_unknown_values(contract: DestCliContract, case: Mapping[str, Any]) -> bool:
    """A pending row with an unknown field and an unknown why or src value: what a reader must keep."""
    fields, whys, srcs = _known(contract)
    row = _row0(case.get("data") or {}) if case.get("doc") == "pending" else {}
    return bool(set(row) - fields) and (row.get("why") not in whys or row.get("src") not in srcs)


def schema_problems(contract: DestCliContract) -> tuple[str, ...]:
    meta = Draft202012Validator(Draft202012Validator.META_SCHEMA)
    invalid = (f"schema {n}: {e.message}" for n, s in contract.schemas.items() for e in meta.iter_errors(dict(s)))
    missing = (f"schema/{d}{SUFFIX}: missing" for d in DOCS if d not in contract.validators)
    keywords = (f"schema {n}: uses {k}" for n, s in contract.schemas.items() for k in unknown_keywords(dict(s)))
    return tuple(chain(invalid, missing, keywords))


def _wrapper_problem(kind: str, case: Any) -> str:
    if not isinstance(case, dict) or case.get("doc") not in DOCS:
        return f"doc must be one of {list(DOCS)}"
    if not isinstance(case.get("name"), str) or not case["name"] or "data" not in case:
        return "needs a non-empty name and data"
    reason = case.get("reason")
    if kind == "invalid" and not (isinstance(reason, dict) and reason.get("code") in CODES
                                  and isinstance(reason.get("field"), str)):  # fmt: skip
        return f"reason must be {{code: one of {list(CODES)}, field: path}}"
    return ""


def _case_problem(contract: DestCliContract, kind: str, case: Mapping[str, Any]) -> str:
    wrapper = _wrapper_problem(kind, case)
    if wrapper:
        return wrapper
    got = validate(contract, case["doc"], case["data"])
    want = Verdict("ok") if kind == "valid" else Verdict(case["reason"]["code"], (case["reason"]["field"],))
    return "" if got == want else f"expected {want.code} {list(want.fields)}, got {got.code} {list(got.fields)}"


def _cases(vectors: Mapping[str, tuple[str, Any]], kind: str) -> tuple[Mapping[str, Any], ...]:
    return tuple(c for k, c in vectors.values() if k == kind and not _wrapper_problem(k, c))


def coverage_problems(contract: DestCliContract, vectors: Mapping[str, tuple[str, Any]]) -> tuple[str, ...]:
    invalid, valid = _cases(vectors, "invalid"), _cases(vectors, "valid")
    missing = (f"needs an invalid {doc} case ({code} {field})" for doc, code, field, wanted in REQUIRED_INVALID
               if not any(c["doc"] == doc and c["reason"] == {"code": code, "field": field} and wanted(c["data"])
                          for c in invalid))  # fmt: skip
    thin = (f"needs a valid {doc} case" for doc in DOCS if not any(c["doc"] == doc for c in valid))
    unknown = () if any(has_unknown_values(contract, c) for c in valid) else (
        "needs a valid pending case whose row has an unknown field and an unknown why or src",)  # fmt: skip
    return tuple(chain(missing, thin, unknown))


def check_all(root: Path) -> Report:
    contract = load(root)
    vectors, load_errors = load_vectors(root)
    checked = ((label, _case_problem(contract, kind, case)) for label, (kind, case) in vectors.items())
    problems = tuple(f"vectors/{label}: {problem}" for label, problem in checked if problem)
    errors = tuple(chain(load_errors, schema_problems(contract), problems, coverage_problems(contract, vectors),
                         digest_problems(root)))  # fmt: skip
    counts = tuple((d, sum(1 for c in _cases(vectors, "valid") if c["doc"] == d),
                    sum(1 for c in _cases(vectors, "invalid") if c["doc"] == d)) for d in DOCS)  # fmt: skip
    return Report(errors, counts)


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate the dest-cli contract v1.")
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
