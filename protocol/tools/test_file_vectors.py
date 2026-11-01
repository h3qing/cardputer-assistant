"""Tests for the v0.3 recording-transfer vectors and the rules check_vectors.py enforces on them."""

from __future__ import annotations

import base64
import json
import re
import shutil
import zlib
from collections.abc import Callable
from functools import reduce
from pathlib import Path

import check_vectors
import pytest

PROTOCOL = Path(__file__).resolve().parents[1]
MESSAGES = Path("vectors/v0/messages")
FLOWS = Path("vectors/v0/flows")
REC_FLOWS = FLOWS / "rec"
FILE_TYPES = ("file.list", "file.items", "file.get", "file.data", "file.del", "debug.rec")
# The host asks for files in these, so only a device with the recording engine can answer them.
REC_FILE_FLOWS = (
    "file_pull_resume",
    "file_del_idempotent",
    "files_rev_relist",
    "file_list_paging",
    "file_busy_while_live",
)
FILE_FLOWS = (*REC_FILE_FLOWS, "old_daemon_ignores_files", "old_device_no_files")
CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
BOOT_ID = re.compile(r"\b([0-9A-HJKMNP-TV-Z]{7})-[0-9A-HJKMNP-TV-Z]{5}\b")


@pytest.fixture()
def tree(tmp_path: Path) -> Path:
    root = tmp_path / "protocol"
    shutil.copytree(PROTOCOL, root, ignore=shutil.ignore_patterns("tools", "__pycache__"))
    return root


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _edit_json(path: Path, change: Callable[[dict], dict]) -> None:
    path.write_text(json.dumps(change(_load(path)), ensure_ascii=False, indent=2), encoding="utf-8")


def _errors(root: Path) -> tuple[str, ...]:
    return check_vectors.check_all(root).errors


def _mentions(errors: tuple[str, ...], *words: str) -> bool:
    return any(all(w in e for w in words) for e in errors)


# --- coverage --------------------------------------------------------------------


def test_every_file_type_has_valid_and_invalid_cases() -> None:
    counts = {t: (v, i) for t, v, i in check_vectors.check_all(PROTOCOL).type_counts}
    assert all(counts[t][0] >= 1 and counts[t][1] >= 1 for t in FILE_TYPES)


def test_file_flows_are_required() -> None:
    assert set(FILE_FLOWS) <= set(check_vectors.REQUIRED_FLOWS) | set(check_vectors.REQUIRED_REC_FLOWS)


def test_flows_that_ask_for_files_live_in_flows_rec() -> None:
    # core/test_link_flows replays every flow in flows/ through the v0.2 link cores, which cannot answer file.*.
    assert set(check_vectors.REQUIRED_REC_FLOWS) == set(REC_FILE_FLOWS)
    for name in REC_FILE_FLOWS:
        assert (PROTOCOL / REC_FLOWS / f"{name}.json").is_file(), name
        assert not (PROTOCOL / FLOWS / f"{name}.json").exists(), name


def test_a_flow_asking_for_files_outside_flows_rec_is_an_error(tree: Path) -> None:
    (tree / REC_FLOWS / "file_del_idempotent.json").rename(tree / FLOWS / "file_del_idempotent.json")
    errors = _errors(tree)
    assert _mentions(errors, "flows/file_del_idempotent.json", "flows/rec/")
    assert _mentions(errors, "flows/rec/file_del_idempotent.json", "missing")


def test_missing_rec_flow_is_an_error(tree: Path) -> None:
    (tree / REC_FLOWS / "file_list_paging.json").unlink()
    assert _mentions(_errors(tree), "flows/rec/file_list_paging.json", "missing")


def _epoch(digits: str) -> int:
    return reduce(lambda value, c: value * 32 + CROCKFORD.index(c), digits, 0)


def test_every_id_in_the_vectors_has_a_device_valid_epoch() -> None:
    # 05 §5.9: the epoch is 32 bits, so no device issues one above 3ZZZZZZ (the firmware's mono_parse rejects it).
    files = [
        *(PROTOCOL / "vectors" / "v0").rglob("*.json"),
        *(PROTOCOL / "recording" / "v1" / "vectors").rglob("*.json"),
    ]
    found = {(str(p.relative_to(PROTOCOL)), e) for p in files for e in BOOT_ID.findall(p.read_text(encoding="utf-8"))}
    assert found and sorted(f"{path}: {e}" for path, e in found if _epoch(e) >= 2**32) == []


def test_main_lists_the_file_types(capsys: pytest.CaptureFixture[str]) -> None:
    assert check_vectors.main(["--root", str(PROTOCOL)]) == 0
    out = capsys.readouterr().out
    assert all(t in out for t in FILE_TYPES) and all(f in out for f in FILE_FLOWS)


@pytest.mark.parametrize(("type_", "field", "label"), [(t, f, d) for t, f, d, _ in check_vectors.REQUIRED_FILE_CASES])
def test_missing_required_invalid_case_is_an_error(tree: Path, type_: str, field: str, label: str) -> None:
    def drop(data: dict) -> dict:
        return {**data, "invalid": [c for c in data["invalid"] if c.get("field") != field]}

    _edit_json(tree / MESSAGES / f"{type_}.json", drop)
    assert _mentions(_errors(tree), type_, label)


def test_required_invalid_cases_include_the_design_list() -> None:
    labels = {(t, f) for t, f, _, _ in check_vectors.REQUIRED_FILE_CASES}
    assert labels >= {
        ("file.get", "len"),
        ("file.get", "fid"),
        ("file.items", "items"),
        ("file.items", "items[0].stars"),
        ("file.data", "b64"),
        ("file.del", "sha256"),
        ("debug.rec", "seg_ms"),
        ("file.items", "items[0].consent"),
    }


# --- file.data payloads ------------------------------------------------------------


def test_valid_file_data_cases_carry_matching_crc() -> None:
    data = _load(PROTOCOL / MESSAGES / "file.data.json")
    for case in data["valid"]:
        msg = case["msg"]
        payload = base64.b64decode(msg.get("b64") or "")
        assert len(payload) == msg["n"] and zlib.crc32(payload) == msg["crc"], case["name"]


def test_wrong_crc_in_a_valid_case_is_an_error(tree: Path) -> None:
    def bump(data: dict) -> dict:
        first = json.loads(json.dumps(data["valid"][0]))
        first["msg"]["crc"] = (first["msg"]["crc"] + 1) % 2**32
        return {**data, "valid": [first, *data["valid"][1:]]}

    _edit_json(tree / MESSAGES / "file.data.json", bump)
    assert _mentions(_errors(tree), "file.data", "crc")


def test_b64_shorter_than_n_in_a_valid_case_is_an_error(tree: Path) -> None:
    def shorten(data: dict) -> dict:
        first = json.loads(json.dumps(data["valid"][0]))
        first["msg"]["n"] += 1
        return {**data, "valid": [first, *data["valid"][1:]]}

    _edit_json(tree / MESSAGES / "file.data.json", shorten)
    assert _mentions(_errors(tree), "file.data", "b64")


# --- flows exercise the host invariants ----------------------------------------------


def _drop_steps(name: str, tree: Path, keep: Callable[[dict], bool]) -> None:
    _edit_json(tree / REC_FLOWS / f"{name}.json", lambda d: {**d, "steps": [s for s in d["steps"] if keep(s)]})


def _is_idle_pong(step: dict) -> bool:
    msg = step.get("msg", {})
    return step.get("from") == "device" and msg.get("t") == "pong" and msg.get("files", {}).get("live") is False


def test_busy_flow_breaks_without_the_live_false_pong(tree: Path) -> None:
    flow = _load(PROTOCOL / REC_FLOWS / "file_busy_while_live.json")
    busy_t = min(s["t"] for s in flow["steps"] if s.get("msg", {}).get("code") == "E_BUSY")
    _drop_steps("file_busy_while_live", tree, lambda s: not (_is_idle_pong(s) and s["t"] > busy_t))
    assert _mentions(_errors(tree), "file_busy_while_live", "E_BUSY")


def test_busy_flow_resumes_at_the_busy_offset() -> None:
    steps = _load(PROTOCOL / REC_FLOWS / "file_busy_while_live.json")["steps"]
    gets = [s["msg"] for s in steps if s.get("msg", {}).get("t") == "file.get"]
    busy_res = {s["msg"]["re"] for s in steps if s.get("msg", {}).get("code") == "E_BUSY"}
    refused = sorted(g["off"] for g in gets if g["id"] in busy_res)
    after = [g["off"] for g in gets if g["id"] not in busy_res][-len(refused) :]
    assert refused and sorted(after) == refused


def test_delete_flow_breaks_when_a_chunk_is_lost(tree: Path) -> None:
    def lose_last_chunk(data: dict) -> dict:
        steps = [dict(s) for s in data["steps"]]
        last = max(i for i, s in enumerate(steps) if s.get("msg", {}).get("t") == "file.data")
        steps[last]["lost"] = True
        return {**data, "steps": steps}

    _edit_json(tree / REC_FLOWS / "file_del_idempotent.json", lose_last_chunk)
    assert _mentions(_errors(tree), "file_del_idempotent", "every byte")


@pytest.mark.parametrize("name", ["old_device_no_files", "old_daemon_ignores_files"])
def test_old_peer_flows_never_carry_file_requests(name: str) -> None:
    steps = _load(PROTOCOL / FLOWS / f"{name}.json")["steps"]
    sent = {s["msg"]["t"] for s in steps if s.get("from") == "host" and "msg" in s}
    assert not sent & set(FILE_TYPES)


def test_old_device_flow_breaks_when_the_host_asks_for_files(tree: Path) -> None:
    def ask(data: dict) -> dict:
        t = data["steps"][-1]["t"] + 10
        listing = {"t": t, "from": "host", "msg": {"v": 0, "t": "file.list", "id": "h99"}}
        refusal = {"v": 0, "t": "error", "id": "d99", "re": "h99", "code": "E_UNKNOWN_TYPE"}
        return {**data, "steps": [*data["steps"], listing, {"t": t + 5, "from": "device", "msg": refusal}]}

    _edit_json(tree / FLOWS / "old_device_no_files.json", ask)
    assert _mentions(_errors(tree), "old_device_no_files", "h99", "files")
