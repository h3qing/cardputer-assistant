"""Tests for check_vectors.py: the repo contract passes, and each coverage rule bites."""

from __future__ import annotations

import json
import shutil
from collections.abc import Callable
from pathlib import Path

import check_vectors
import pytest

PROTOCOL = Path(__file__).resolve().parents[1]
MESSAGES = Path("vectors/v0/messages")
FLOWS = Path("vectors/v0/flows")
SCHEMAS = Path("schema/v0")


@pytest.fixture()
def tree(tmp_path: Path) -> Path:
    root = tmp_path / "protocol"
    shutil.copytree(PROTOCOL, root, ignore=shutil.ignore_patterns("tools", "__pycache__"))
    return root


def _edit_json(path: Path, change: Callable[[dict], dict]) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    path.write_text(json.dumps(change(data), ensure_ascii=False, indent=2), encoding="utf-8")


def _errors(root: Path) -> tuple[str, ...]:
    return check_vectors.check_all(root).errors


def _mentions(errors: tuple[str, ...], *words: str) -> bool:
    return any(all(w in e for w in words) for e in errors)


# --- the real contract ---------------------------------------------------------


def test_repo_contract_passes() -> None:
    report = check_vectors.check_all(PROTOCOL)
    assert report.errors == ()
    assert {name for name, _, _ in report.type_counts} == set(check_vectors.load_types(PROTOCOL))
    assert all(valid >= 1 and invalid >= 1 for _, valid, invalid in report.type_counts)
    assert set(check_vectors.REQUIRED_FLOWS) <= {name for name, _ in report.flow_counts}


def test_hello_vectors_cover_store_and_launcher() -> None:
    data = json.loads((PROTOCOL / MESSAGES / "hello.json").read_text(encoding="utf-8"))
    devices = [c["msg"] for c in data["valid"] if c["msg"].get("role") == "device"]
    assert any({"store", "launcher"} <= set(msg) for msg in devices), "a v0.2 device hello"
    assert any(not {"store", "launcher"} & set(msg) for msg in devices), "a v0.1 device hello"
    bad = {(c.get("field"), c["msg"].get(c.get("field", ""))) for c in data["invalid"] if "msg" in c}
    assert {("store", "fat"), ("launcher", "yes")} <= bad


def test_main_prints_counts_and_exits_zero(capsys: pytest.CaptureFixture[str]) -> None:
    assert check_vectors.main(["--root", str(PROTOCOL)]) == 0
    out = capsys.readouterr().out
    assert "outbox.item" in out and "valid=" in out and "invalid=" in out
    assert "OK" in out


def test_main_exits_one_and_lists_errors(tree: Path, capsys: pytest.CaptureFixture[str]) -> None:
    (tree / MESSAGES / "ping.json").unlink()
    assert check_vectors.main(["--root", str(tree)]) == 1
    assert "ping" in capsys.readouterr().out


# --- coverage rules ------------------------------------------------------------


def test_type_without_invalid_cases_is_an_error(tree: Path) -> None:
    _edit_json(tree / MESSAGES / "ping.json", lambda d: {**d, "invalid": []})
    assert _mentions(_errors(tree), "ping", "invalid")


def test_type_without_valid_cases_is_an_error(tree: Path) -> None:
    _edit_json(tree / MESSAGES / "ack.json", lambda d: {**d, "valid": []})
    assert _mentions(_errors(tree), "ack", "valid")


def test_schema_without_vectors_is_an_error(tree: Path) -> None:
    (tree / MESSAGES / "queue.done.json").unlink()
    assert _mentions(_errors(tree), "queue.done")


def test_vectors_without_schema_is_an_error(tree: Path) -> None:
    (tree / SCHEMAS / "inbox.dismiss.schema.json").unlink()
    assert _mentions(_errors(tree), "inbox.dismiss")


def test_missing_required_flow_is_an_error(tree: Path) -> None:
    (tree / FLOWS / "credit_window.json").unlink()
    assert _mentions(_errors(tree), "credit_window")


def _with_valid_exec_kind(data: dict) -> dict:
    case = json.loads(json.dumps(data["valid"][0]))
    case["name"] = "exec kind smuggled in"
    case["msg"]["kind"] = "spawn"
    return {**data, "valid": [*data["valid"], case]}


def test_outbox_item_with_exec_kind_marked_valid_is_an_error(tree: Path) -> None:
    _edit_json(tree / MESSAGES / "outbox.item.json", _with_valid_exec_kind)
    assert _mentions(_errors(tree), "outbox.item", "spawn")


def test_schema_accepting_exec_kind_is_an_error(tree: Path) -> None:
    def loosen(schema: dict) -> dict:
        props = schema["properties"]
        kind = {**props["kind"], "enum": [*props["kind"]["enum"], "reply"]}
        return {**schema, "properties": {**props, "kind": kind}}

    _edit_json(tree / SCHEMAS / "outbox.item.schema.json", loosen)
    assert _mentions(_errors(tree), "outbox.item", "reply")


def test_missing_exec_kind_invalid_case_is_an_error(tree: Path) -> None:
    def drop_reply(data: dict) -> dict:
        keep = [c for c in data["invalid"] if c.get("msg", {}).get("kind") != "reply"]
        return {**data, "invalid": keep}

    _edit_json(tree / MESSAGES / "outbox.item.json", drop_reply)
    assert _mentions(_errors(tree), "outbox.item", "reply")


def test_missing_hostile_spawn_text_is_an_error(tree: Path) -> None:
    def drop_subshell(data: dict) -> dict:
        def keep(case: dict) -> bool:
            return case.get("msg", {}).get("text") != "$(rm -rf ~)"

        return {
            **data,
            "valid": [c for c in data["valid"] if keep(c)],
            "invalid": [c for c in data["invalid"] if keep(c)],
        }

    _edit_json(tree / MESSAGES / "agent.spawn.json", drop_subshell)
    assert _mentions(_errors(tree), "agent.spawn", "$(rm -rf ~)")


# --- case verification ---------------------------------------------------------


def test_valid_case_failing_schema_is_an_error(tree: Path) -> None:
    def break_first(data: dict) -> dict:
        first = json.loads(json.dumps(data["valid"][0]))
        first["msg"]["tz_min"] = 9999
        return {**data, "valid": [first, *data["valid"][1:]]}

    _edit_json(tree / MESSAGES / "time.set.json", break_first)
    assert _mentions(_errors(tree), "time.set", "tz_min")


def test_invalid_case_with_wrong_expected_code_is_an_error(tree: Path) -> None:
    def wrong_code(data: dict) -> dict:
        first = {**data["invalid"][0], "code": "E_VERSION"}
        first.pop("field", None)
        return {**data, "invalid": [first, *data["invalid"][1:]]}

    _edit_json(tree / MESSAGES / "ping.json", wrong_code)
    assert _mentions(_errors(tree), "ping", "E_VERSION")


def test_case_direction_must_match_schema(tree: Path) -> None:
    def flip(data: dict) -> dict:
        first = {**data["valid"][0], "dir": "d2h"}
        return {**data, "valid": [first, *data["valid"][1:]]}

    _edit_json(tree / MESSAGES / "time.set.json", flip)
    assert _mentions(_errors(tree), "time.set", "dir")


def test_malformed_vector_file_is_an_error(tree: Path) -> None:
    def unnamed(data: dict) -> dict:
        first = {k: v for k, v in data["valid"][0].items() if k != "name"}
        return {**data, "valid": [first, *data["valid"][1:]]}

    _edit_json(tree / MESSAGES / "pong.json", unnamed)
    assert _mentions(_errors(tree), "pong", "name")


def test_limits_drift_is_an_error(tree: Path) -> None:
    _edit_json(tree / "limits.json", lambda d: {**d, "session_name_max": 31})
    assert _mentions(_errors(tree), "session_name_max")


def test_broken_flow_is_an_error(tree: Path) -> None:
    def drop_last_reply(data: dict) -> dict:
        steps = [s for s in data["steps"] if not (s.get("from") == "device" and s.get("msg", {}).get("t") == "pong")]
        return {**data, "steps": steps}

    _edit_json(tree / FLOWS / "handshake.json", drop_last_reply)
    assert _mentions(_errors(tree), "handshake")
