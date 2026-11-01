#!/usr/bin/env python3
"""Validate the recording file contract v1 (docs/11-m3-design.md §7.2-§7.7).

Usage: uv run --with 'jsonschema>=4.23,<5' python protocol/recording/tools/check_recording.py [--root .../v1]

validate(contract, doc, data) is a reader's verdict on one file (doc: manifest | status | transcript | result):
  1. not a JSON object                              -> E_INVALID (root)
  2. v is not the integer 1 (missing, 2, "1", ...)  -> E_VERSION v; nothing else is read
  3. result over 64 KiB (compact UTF-8 JSON)        -> E_INVALID (size)
  4. schema errors, then the manifest index rules   -> E_INVALID <paths>
Unknown fields never cause a rejection. Field paths look like segments[1].file.

check_all(root) checks the contract itself: valid Draft 2020-12 schemas; well-formed vector files; every valid
vector accepted; every invalid vector rejected with exactly its declared reason; the required invalid cases
and the unknown-field result case present; VECTORS.sha256 lists exactly the vector files and their digests.
Prints per-document counts; exits 0 when clean, 1 otherwise.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from itertools import chain
from pathlib import Path
from types import MappingProxyType
from typing import Any

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

# The protocol v0 tools hold the ECMA-262 pattern semantics and the error-path format; reuse them.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from schemas import ContractValidator, error_fields, load_json  # noqa: E402

DOCS = ("manifest", "status", "transcript", "result")
CODES = ("E_INVALID", "E_VERSION")
MAX_BYTES: Mapping[str, int] = MappingProxyType({"result": 65536})
DIGESTS = "VECTORS.sha256"
SUFFIX = ".schema.json"


@dataclass(frozen=True)
class Verdict:
    code: str
    fields: tuple[str, ...] = ()


@dataclass(frozen=True)
class RecordingContract:
    root: Path
    schemas: Mapping[str, Mapping[str, Any]]
    validators: Mapping[str, Any]


@dataclass(frozen=True)
class Report:
    errors: tuple[str, ...]
    counts: tuple[tuple[str, int, int], ...]


def load(root: Path) -> RecordingContract:
    loaded = {f.name[: -len(SUFFIX)]: load_json(f) for f in sorted((root / "schema").glob("*" + SUFFIX))}
    resources = (
        (s["$id"], Resource.from_contents(s, default_specification=DRAFT202012)) for s in loaded.values() if "$id" in s
    )
    registry = Registry().with_resources(resources)
    docs = {name: s for name, s in loaded.items() if name in DOCS}
    return RecordingContract(
        root=root,
        schemas=MappingProxyType(loaded),
        validators=MappingProxyType({n: ContractValidator(s, registry=registry) for n, s in docs.items()}),
    )


def known_fields(contract: RecordingContract, doc: str) -> frozenset[str]:
    return frozenset(contract.schemas[doc].get("properties", {}))


# --- the reader's verdict -------------------------------------------------------------


def _ordered(values: Sequence[int], label: Callable[[int], str]) -> tuple[str, ...]:
    return tuple(label(k) for k in range(1, len(values)) if values[k] <= values[k - 1])


def index_problems(manifest: Mapping[str, Any]) -> tuple[str, ...]:
    """Manifest rules beyond the schema (docs/11 §6.4, §7.3); assumes a schema-valid manifest."""
    segments, gaps = manifest["segments"], manifest["gaps"]
    indices = tuple(s["i"] for s in segments)
    covered = set(indices) | set(gaps)
    return (
        _ordered(indices, lambda k: f"segments[{k}].i")
        + _ordered(gaps, lambda k: f"gaps[{k}]")
        + tuple(f"gaps[{k}]" for k, g in enumerate(gaps) if g in indices)
        + tuple(f"segments[{k}].file" for k, s in enumerate(segments) if s["file"] != f"seg-{s['i']:03d}.wav")
        + tuple(f"segments[{k}].bytes" for k, s in enumerate(segments) if s["bytes"] != 44 + 2 * s["samples"])
        + (("gaps",) if covered != set(range(max(covered) + 1)) else ())
    )


def _is_v1(data: Mapping[str, Any]) -> bool:
    v = data.get("v")
    return isinstance(v, int) and not isinstance(v, bool) and v == 1


def _size(data: Any) -> int:
    return len(json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8", "surrogatepass"))


def validate(contract: RecordingContract, doc: str, data: Any) -> Verdict:
    if not isinstance(data, dict):
        return Verdict("E_INVALID", ("(root)",))
    if not _is_v1(data):
        return Verdict("E_VERSION", ("v",))
    if _size(data) > MAX_BYTES.get(doc, sys.maxsize):
        return Verdict("E_INVALID", ("(size)",))
    fields = {f for e in contract.validators[doc].iter_errors(data) for f in error_fields(e)}
    if not fields and doc == "manifest":
        fields = set(index_problems(data))
    return Verdict("E_INVALID", tuple(sorted(fields))) if fields else Verdict("ok")


# --- the contract's own checks ---------------------------------------------------------


def _first_segment(data: Mapping[str, Any]) -> Mapping[str, Any]:
    segments = data.get("segments")
    return segments[0] if isinstance(segments, list) and segments and isinstance(segments[0], dict) else {}


def _seg_file(data: Mapping[str, Any]) -> str:
    return str(_first_segment(data).get("file", ""))


def _summary_path(data: Mapping[str, Any]) -> str:
    outputs = data.get("outputs") if isinstance(data.get("outputs"), dict) else {}
    summary = outputs.get("summary") if isinstance(outputs.get("summary"), dict) else {}
    return str(summary.get("path", ""))


def _memo_text(data: Mapping[str, Any]) -> str:
    memo = data.get("memo") if isinstance(data.get("memo"), dict) else {}
    return str(memo.get("text") or "")


# (doc, code, field, label, predicate on the document): invalid cases the contract must carry (docs/12 M3-P (4)).
REQUIRED_INVALID: tuple[tuple[str, str, str, str, Callable[[Mapping[str, Any]], bool]], ...] = (
    ("manifest", "E_INVALID", "segments[0].file", "'../' in a segment file name", lambda d: "../" in _seg_file(d)),
    ("manifest", "E_INVALID", "segments[0].file", "absolute '/' segment file name", lambda d: _seg_file(d)[:1] == "/"),
    ("result", "E_INVALID", "outputs.summary.path", "outputs path containing '/'", lambda d: "/" in _summary_path(d)),
    ("manifest", "E_INVALID", "session", "session not a MonoId", lambda d: "session" in d),
    ("manifest", "E_INVALID", "attempt", "attempt 0", lambda d: d.get("attempt") == 0),
    ("manifest", "E_INVALID", "gaps", "indices not contiguous without matching gaps", lambda d: d.get("gaps") == []),
    ("manifest", "E_INVALID", "segments[0].sha256", "sha256 not hex", lambda d: "sha256" in _first_segment(d)),
    ("result", "E_INVALID", "actions", "more than 20 actions", lambda d: len(d.get("actions") or ()) > 20),
    ("result", "E_INVALID", "title", "title over 80 characters", lambda d: len(str(d.get("title") or "")) > 80),
    ("result", "E_INVALID", "memo.text", "memo.text over 2000 characters", lambda d: len(_memo_text(d)) > 2000),
    ("manifest", "E_INVALID", "mode", "unknown mode", lambda d: d.get("mode") not in ("memo", "meeting")),
    ("result", "E_INVALID", "error", "state done with a non-null error", lambda d: d.get("state") == "done"),
    ("result", "E_VERSION", "v", "v=2 result", lambda d: d.get("v") == 2),
    ("manifest", "E_VERSION", "v", "v=2 manifest", lambda d: d.get("v") == 2),
)


def schema_problems(contract: RecordingContract) -> tuple[str, ...]:
    meta = Draft202012Validator(Draft202012Validator.META_SCHEMA)
    invalid = (f"schema {n}: {e.message}" for n, s in contract.schemas.items() for e in meta.iter_errors(dict(s)))
    missing = (f"schema/{d}{SUFFIX}: missing" for d in DOCS if d not in contract.validators)
    return tuple(chain(invalid, missing))


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


def _case_problem(contract: RecordingContract, kind: str, case: Mapping[str, Any]) -> str:
    wrapper = _wrapper_problem(kind, case)
    if wrapper:
        return wrapper
    got = validate(contract, case["doc"], case["data"])
    want = Verdict("ok") if kind == "valid" else Verdict(case["reason"]["code"], (case["reason"]["field"],))
    return "" if got == want else f"expected {want.code} {list(want.fields)}, got {got.code} {list(got.fields)}"


def load_vectors(root: Path) -> tuple[dict[str, tuple[str, Any]], tuple[str, ...]]:
    """'valid/x.json' -> (kind, parsed case); plus load errors."""
    found: dict[str, tuple[str, Any]] = {}
    errors: list[str] = []
    for kind in ("valid", "invalid"):
        for path in sorted((root / "vectors" / kind).glob("*.json")):
            try:
                found[f"{kind}/{path.name}"] = (kind, load_json(path))
            except (OSError, json.JSONDecodeError) as exc:
                errors.append(f"vectors/{kind}/{path.name}: unreadable: {exc}")
    return found, tuple(errors)


def vector_problems(contract: RecordingContract, vectors: Mapping[str, tuple[str, Any]]) -> tuple[str, ...]:
    checked = ((label, _case_problem(contract, kind, case)) for label, (kind, case) in vectors.items())
    return tuple(f"vectors/{label}: {problem}" for label, problem in checked if problem)


def _cases(vectors: Mapping[str, tuple[str, Any]], kind: str) -> tuple[Mapping[str, Any], ...]:
    return tuple(c for k, c in vectors.values() if k == kind and not _wrapper_problem(k, c))


def coverage_problems(contract: RecordingContract, vectors: Mapping[str, tuple[str, Any]]) -> tuple[str, ...]:
    invalid, valid = _cases(vectors, "invalid"), _cases(vectors, "valid")
    missing = (
        f"needs an invalid {doc} case: {label} ({code} {field})"
        for doc, code, field, label, wanted in REQUIRED_INVALID
        if not any(
            c["doc"] == doc and c["reason"] == {"code": code, "field": field} and wanted(c["data"]) for c in invalid
        )
    )
    thin = (f"needs a valid {doc} case" for doc in DOCS if not any(c["doc"] == doc for c in valid))
    extra = known_fields(contract, "result") if "result" in contract.schemas else frozenset()
    unknown = any(c["doc"] == "result" and isinstance(c["data"], dict) and set(c["data"]) - extra for c in valid)
    no_unknown = () if unknown else ("needs a valid result case with an unknown top-level field",)
    return tuple(chain(missing, thin, no_unknown))


def _digest_lines(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    pairs = (line.split(None, 1) for line in path.read_text(encoding="utf-8").splitlines() if line.strip())
    return {name.strip().lstrip("*"): digest for digest, name in pairs}


def digest_problems(root: Path) -> tuple[str, ...]:
    listed = _digest_lines(root / DIGESTS)
    actual = {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted((root / "vectors").glob("*/*.json"))
    }
    unlisted = (f"{name}: not in {DIGESTS}" for name in actual if name not in listed)
    gone = (f"{name}: listed in {DIGESTS} but missing" for name in listed if name not in actual)
    changed = (
        f"{name}: does not match {DIGESTS}" for name in actual if name in listed and listed[name] != actual[name]
    )
    return tuple(chain(unlisted, gone, changed))


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
        print(f"{doc:<12} valid={valid:<3} invalid={invalid}")
    for error in report.errors:
        print(f"ERROR {error}")


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate the recording file contract v1.")
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
