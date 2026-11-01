#!/usr/bin/env python3
"""Validate the protocol v0 contract: schemas, limits, message vectors, flow vectors.

Usage: uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py [--root protocol]

Checks
  - every schema is valid Draft 2020-12 and limits.json matches the schemas;
  - every message type has a vector file with >=1 valid and >=1 invalid case (and vice versa);
  - valid cases decode to ok; invalid cases decode to their declared code / field;
  - outbox.item never accepts an execution kind; agent.spawn carries every hostile body (05 §9.2);
  - file.* carries the required invalid cases (docs/11 §5) and every valid file.data has a matching crc;
  - the v0.4.1 messages carry the required invalid cases (docs/13 §12.2, rec_rules.REQUIRED_REC_CASES);
  - the required flows exist (those where the host asks for files in flows/rec/, those where a device app sends
    rec.* in flows/app/, the others in flows/) and satisfy the invariants in flows.py, file_flows.py, rec_flows.py.
Prints per-type case counts; exits 0 when clean, 1 otherwise.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import sys
import zlib
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from itertools import chain
from pathlib import Path
from typing import Any

from flows import check_flow
from jsonschema import Draft202012Validator
from rec_rules import APP_REQUESTS, LARGE_HOST_REPLIES, REQUIRED_REC_CASES, sends_app_requests
from schemas import Contract, field_errors, format_path, limit_mismatches, load_contract, load_json, meta_validator
from wire import canonical_line, decode_line

MESSAGES = Path("vectors/v0/messages")
FLOWS = Path("vectors/v0/flows")
# Flows in which the host asks for files: only a device with the recording engine can answer them, so they live
# in flows/rec/ (firmware ui/test_rec_flows) and not in flows/ (core/test_link_flows replays the link cores only).
REC_FLOWS = FLOWS / "rec"
# Flows in which a device app (v0.4.1: the recordings app) sends requests the link cores never send themselves.
APP_FLOWS = FLOWS / "app"
REQUIRED_FLOWS = (
    "handshake",
    "tx_parking",
    "outbox_replay_dup_ack",
    "reboot_detect",
    "credit_window",
    "offline_spawn_becomes_queue",
    "agent_result_unknown",
    "old_daemon_ignores_files",
    "old_device_no_files",
    "caps_absent",
    "old_daemon_ignores_dest",
    "new_daemon_old_firmware",
)
REQUIRED_REC_FLOWS = (
    "file_pull_resume",
    "file_del_idempotent",
    "files_rev_relist",
    "file_list_paging",
    "file_busy_while_live",
)
REQUIRED_APP_FLOWS = (
    "rec_list_paging",
    "rec_act_hide",
    "rec_list_old_daemon",
)
FLOW_FOLDERS = (
    ("flows/", FLOWS, REQUIRED_FLOWS),
    ("flows/rec/", REC_FLOWS, REQUIRED_REC_FLOWS),
    ("flows/app/", APP_FLOWS, REQUIRED_APP_FLOWS),
)
FILE_REQUESTS = frozenset({"file.list", "file.get", "file.del"})
OUTBOX_KINDS = ("note", "queue")
EXEC_KINDS = ("spawn", "reply", "feedback", "agent.spawn", "agent.reply")
HOSTILE_SPAWN_TEXTS = (
    "--dangerously-skip-permissions",
    "-c",
    "--permission-mode=bypassPermissions",
    "$(rm -rf ~)",
    "; ls",
    "\u0000",
    "a" * 501,
)


def _b64_size(msg: Mapping[str, Any]) -> int | None:
    try:
        return len(base64.b64decode(msg.get("b64") or "", validate=True))
    except (binascii.Error, TypeError):
        return None


def _first_item(msg: Mapping[str, Any]) -> Mapping[str, Any]:
    items = msg.get("items")
    return items[0] if isinstance(items, list) and items and isinstance(items[0], dict) else {}


# (type, field, label, predicate on msg): invalid cases the v0.3 contract must carry (docs/12 M3-P (2)).
REQUIRED_FILE_CASES: tuple[tuple[str, str, str, Callable[[Mapping[str, Any]], bool]], ...] = (
    ("file.get", "len", "len 1381", lambda m: m.get("len") == 1381),
    ("file.get", "fid", "fid not a MonoId", lambda m: "fid" in m),
    ("file.items", "items", "5 items", lambda m: len(m.get("items") or ()) == 5),
    ("file.items", "items[0].stars", "17 stars", lambda m: len(_first_item(m).get("stars") or ()) == 17),
    ("file.items", "items[0].consent", "consent 'yes'", lambda m: _first_item(m).get("consent") == "yes"),
    ("file.data", "b64", "b64 decoding to a length other than n", lambda m: _b64_size(m) not in (None, m.get("n"))),
    ("file.del", "sha256", "upper-case sha256", lambda m: str(m.get("sha256", "")).isupper()),
    ("debug.rec", "seg_ms", "seg_ms 9999", lambda m: m.get("seg_ms") == 9999),
)
OUTBOX_TEMPLATE = {
    "v": 0,
    "t": "outbox.item",
    "id": "3QZK1M7-0000C-0001",
    "kind": "note",
    "at": {"up_ms": 1, "wall_ms": None},
    "body": {"text": "x", "project": "cpa"},
}


@dataclass(frozen=True)
class Report:
    errors: tuple[str, ...]
    type_counts: tuple[tuple[str, int, int], ...]
    flow_counts: tuple[tuple[str, int], ...]


def load_types(root: Path) -> tuple[str, ...]:
    return tuple(sorted(load_contract(root).schemas))


def _load_dir(directory: Path) -> tuple[dict[str, Any], tuple[str, ...]]:
    files = sorted(directory.glob("*.json"))
    parsed = {f.stem: _try_load(f) for f in files}
    good = {name: data for name, (data, _) in parsed.items() if data is not None}
    bad = tuple(f"{directory.name}/{name}.json: {err}" for name, (_, err) in parsed.items() if err)
    return good, bad


def _try_load(path: Path) -> tuple[Any, str]:
    try:
        return load_json(path), ""
    except (OSError, json.JSONDecodeError) as exc:
        return None, f"unreadable: {exc}"


def _meta_errors(validator: Any, label: str, data: Any) -> tuple[str, ...]:
    return tuple(
        f"{label}: {format_path(e.absolute_path) or '<root>'}: {e.message}" for e in validator.iter_errors(data)
    )


def schema_problems(contract: Contract) -> tuple[str, ...]:
    meta = Draft202012Validator(Draft202012Validator.META_SCHEMA)
    invalid = (
        f"schema {name}: {e.message}"
        for name, s in [("common", contract.common), *contract.schemas.items()]
        for e in meta.iter_errors(dict(s))
    )
    shape = (
        f"schema {name}: needs title {name!r} and x-dir within ['h2d', 'd2h']"
        for name, s in contract.schemas.items()
        if s.get("title") != name or not s.get("x-dir") or not set(s["x-dir"]) <= {"h2d", "d2h"}
    )
    return tuple(chain(invalid, shape))


def _case_bytes(case: Mapping[str, Any]) -> bytes:
    return base64.b64decode(case["line_b64"]) if "line_b64" in case else canonical_line(case["msg"])


def _framing_problem(case: Mapping[str, Any]) -> str:
    raw = _case_bytes(case)
    one_line = raw.endswith(b"\n") and raw.count(b"\n") == 1
    return "" if "line_b64" not in case or one_line else "line_b64 must be exactly one line ending in \\n"


def _valid_case(contract: Contract, label: str, case: Mapping[str, Any]) -> Iterable[str]:
    decoded = decode_line(contract, _case_bytes(case))
    msg = case["msg"]
    if decoded.outcome != "ok":
        yield f"{label}: expected valid, decodes as {decoded.outcome} {list(decoded.fields)}"
    elif "line_b64" in case and decoded.msg != msg:
        yield f"{label}: line_b64 does not decode to msg"
    reply_size = len(canonical_line(msg))
    large = msg.get("t") in LARGE_HOST_REPLIES
    if case["dir"] == "h2d" and "re" in msg and not large and reply_size > contract.limits["host_reply_max"]:
        yield f"{label}: host reply is {reply_size} bytes > host_reply_max"


def _invalid_case(contract: Contract, label: str, case: Mapping[str, Any]) -> Iterable[str]:
    decoded = decode_line(contract, _case_bytes(case))
    if decoded.outcome != case["code"]:
        yield f"{label}: expected {case['code']}, decodes as {decoded.outcome} {list(decoded.fields)}"
    elif "field" in case and decoded.fields != (case["field"],):
        yield f"{label}: expected field {case['field']!r}, got {list(decoded.fields)}"


def _case_problems(contract: Contract, type_: str, kind: str, index: int, case: Mapping[str, Any]) -> tuple[str, ...]:
    label = f"messages/{type_}.json {kind}[{index}] {case['name']!r}"
    framing = _framing_problem(case)
    if framing:
        return (f"{label}: {framing}",)
    dirs = contract.schemas.get(type_, {}).get("x-dir", ())
    wrong_dir = (f"{label}: dir {case['dir']} not allowed for {type_} {list(dirs)}",) if case["dir"] not in dirs else ()
    check = _valid_case if kind == "valid" else _invalid_case
    return wrong_dir + tuple(check(contract, label, case))


def message_file_problems(contract: Contract, meta: Any, type_: str, data: Any) -> tuple[str, ...]:
    label = f"messages/{type_}.json"
    structural = _meta_errors(meta, label, data)
    if structural:
        return structural
    mismatch = (f"{label}: type {data['type']!r} does not match the file name",) if data["type"] != type_ else ()
    cases = chain(
        (("valid", i, c) for i, c in enumerate(data["valid"])),
        (("invalid", i, c) for i, c in enumerate(data["invalid"])),
    )
    return mismatch + tuple(chain.from_iterable(_case_problems(contract, type_, k, i, c) for k, i, c in cases))


def coverage_problems(contract: Contract, vectors: Mapping[str, Any]) -> tuple[str, ...]:
    no_vectors = (
        f"messages/{t}.json: missing; schema {t} has no vectors" for t in contract.schemas if t not in vectors
    )
    no_schema = (f"messages/{t}.json: no schema/v0/{t}.schema.json" for t in vectors if t not in contract.schemas)
    thin = (
        f"messages/{t}.json: needs at least 1 {kind} case"
        for t, data in vectors.items()
        for kind in ("valid", "invalid")
        if isinstance(data, dict) and not data.get(kind)
    )
    return tuple(chain(no_vectors, no_schema, thin))


def _msgs(data: Mapping[str, Any], kind: str) -> tuple[Mapping[str, Any], ...]:
    return tuple(c["msg"] for c in data.get(kind, ()) if isinstance(c, dict) and "msg" in c)


def exec_kind_problems(contract: Contract, data: Mapping[str, Any] | None) -> tuple[str, ...]:
    if not data or "outbox.item" not in contract.schemas:
        return ()
    smuggled = (
        f"outbox.item: valid case carries kind {m.get('kind')!r}; only {list(OUTBOX_KINDS)} are allowed"
        for m in _msgs(data, "valid")
        if m.get("kind") not in OUTBOX_KINDS
    )
    covered = {m.get("kind") for m in _msgs(data, "invalid")}
    uncovered = (f"outbox.item: needs an invalid case for execution kind {k!r}" for k in EXEC_KINDS if k not in covered)
    accepted = (
        f"outbox.item: schema accepts execution kind {k!r}"
        for k in EXEC_KINDS
        if "kind" not in field_errors(contract, {**OUTBOX_TEMPLATE, "kind": k})
    )
    return tuple(chain(smuggled, uncovered, accepted))


def hostile_text_problems(data: Mapping[str, Any] | None) -> tuple[str, ...]:
    if not data:
        return ()
    texts = {m.get("text") for m in _msgs(data, "valid") + _msgs(data, "invalid")}
    return tuple(
        f"agent.spawn: hostile text {t!r} has no vector (05 §9.2)" for t in HOSTILE_SPAWN_TEXTS if t not in texts
    )


def _has_case(data: Mapping[str, Any], field: str, wanted: Callable[[Mapping[str, Any]], bool]) -> bool:
    cases = (c for c in data.get("invalid", ()) if isinstance(c, dict))
    return any(c.get("field") == field and "msg" in c and wanted(c["msg"]) for c in cases)


def required_case_problems(vectors: Mapping[str, Any]) -> tuple[str, ...]:
    return tuple(
        f"{type_}: needs an invalid case with {label} (field {field})"
        for type_, field, label, wanted in (*REQUIRED_FILE_CASES, *REQUIRED_REC_CASES)
        if isinstance(vectors.get(type_), dict) and not _has_case(vectors[type_], field, wanted)
    )


def _crc_problem(case: Any) -> str:
    msg = case.get("msg") if isinstance(case, dict) else None
    if not isinstance(msg, dict) or _b64_size(msg) is None:
        return ""
    crc = zlib.crc32(base64.b64decode(msg.get("b64") or ""))
    if crc == msg.get("crc"):
        return ""
    return f"file.data: valid case {case.get('name')!r} has crc {msg.get('crc')}, its bytes have crc {crc}"


def file_data_crc_problems(data: Mapping[str, Any] | None) -> tuple[str, ...]:
    if not isinstance(data, dict):
        return ()
    return tuple(p for p in map(_crc_problem, data.get("valid", ())) if p)


def _asks_for_files(flow: Any) -> bool:
    steps = flow.get("steps", ()) if isinstance(flow, dict) else ()
    return any(
        isinstance(s, dict) and s.get("from") == "host" and (s.get("msg") or {}).get("t") in FILE_REQUESTS
        for s in steps
    )


FILES_MISPLACED = "the host asks for files, which the link cores cannot answer; it belongs in flows/rec/"
APP_MISPLACED = f"the device sends {sorted(APP_REQUESTS)}, which only a device app does; it belongs in flows/app/"


def _misplaced(flows: Mapping[str, Any]) -> Iterable[str]:
    for name, data in flows.items():
        if _asks_for_files(data):
            yield f"flows/{name}.json: {FILES_MISPLACED}"
        if sends_app_requests(data):
            yield f"flows/{name}.json: {APP_MISPLACED}"


def flow_problems(contract: Contract, meta: Any, folders: Sequence[Mapping[str, Any]]) -> tuple[str, ...]:
    """folders: the flows found in flows/, flows/rec/ and flows/app/ (FLOW_FOLDERS order)."""
    missing = (
        f"{folder}{name}.json: required flow missing"
        for (folder, _, required), found in zip(FLOW_FOLDERS, folders, strict=True)
        for name in required
        if name not in found
    )

    def one(folder: str, name: str, data: Any) -> tuple[str, ...]:
        label = f"{folder}{name}.json"
        structural = _meta_errors(meta, label, data)
        if structural:
            return structural
        renamed = (f"{label}: name {data['name']!r} does not match the file name",) if data["name"] != name else ()
        return renamed + tuple(f"{label}: {e}" for e in check_flow(contract, data))

    checked = (
        one(folder, n, d) for (folder, _, _), found in zip(FLOW_FOLDERS, folders, strict=True) for n, d in found.items()
    )
    return tuple(chain(missing, _misplaced(folders[0]), chain.from_iterable(checked)))


def check_all(root: Path) -> Report:
    contract = load_contract(root)
    vectors, vector_load_errors = _load_dir(root / MESSAGES)
    loaded = tuple(_load_dir(root / path) for _, path, _ in FLOW_FOLDERS)
    folders = tuple(found for found, _ in loaded)
    message_meta = meta_validator(root, "messages")
    errors = tuple(
        chain(
            vector_load_errors,
            chain.from_iterable(load_errors for _, load_errors in loaded),
            schema_problems(contract),
            limit_mismatches(contract),
            coverage_problems(contract, vectors),
            chain.from_iterable(message_file_problems(contract, message_meta, t, d) for t, d in vectors.items()),
            exec_kind_problems(contract, vectors.get("outbox.item")),
            hostile_text_problems(vectors.get("agent.spawn")),
            required_case_problems(vectors),
            file_data_crc_problems(vectors.get("file.data")),
            flow_problems(contract, meta_validator(root, "flow"), folders),
        )
    )
    counts = tuple(
        (t, len(d.get("valid", ())), len(d.get("invalid", ())))
        for t, d in sorted(vectors.items())
        if isinstance(d, dict)
    )
    prefixes = (folder.removeprefix("flows/") for folder, _, _ in FLOW_FOLDERS)
    every_flow = chain.from_iterable(
        sorted((f"{prefix}{n}", d) for n, d in found.items()) for prefix, found in zip(prefixes, folders, strict=True)
    )
    steps = tuple((n, len(d.get("steps", ()))) for n, d in every_flow if isinstance(d, dict))
    return Report(errors, counts, steps)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate protocol v0 schemas and vectors.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    report = check_all(parser.parse_args(argv).root)
    for type_, valid, invalid in report.type_counts:
        print(f"{type_:<20} valid={valid:<3} invalid={invalid}")
    for name, steps in report.flow_counts:
        print(f"flow {name:<28} steps={steps}")
    for error in report.errors:
        print(f"ERROR {error}")
    if report.errors:
        print(f"FAILED: {len(report.errors)} problems")
        return 1
    print(f"OK: {len(report.type_counts)} message types, {len(report.flow_counts)} flows")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
