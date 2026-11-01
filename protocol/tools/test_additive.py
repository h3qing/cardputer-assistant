"""Additive-only guard (docs/11-m3-design.md §5, §11; docs/13 §12): released vector cases stay unchanged.

fixtures/v02_cases.sha256 holds one digest per v0.2 message case and per v0.2 flow, taken before M3 touched
the vectors. fixtures/v031_cases.sha256 holds the same for every case released up to v0.3.1 (v0.1-v0.3.1),
flows/rec/ included, taken before v0.4.0 touched the vectors; fixtures/v040_cases.sha256 does the same for
every case up to v0.4.0 (v0.3.2's debug.codec and v0.4.0's debug.state ui cases appended), taken before
v0.4.1 touched them. A case's digest is sha256 over its canonical JSON (sorted keys, compact, ASCII escapes),
so the guard is independent of indentation but sees any change to a name, a byte of line_b64 or a field
value. New cases may be appended; existing ones may not be edited, reordered or removed.
"""

from __future__ import annotations

import hashlib
import json
import shutil
from collections.abc import Mapping
from pathlib import Path
from typing import Any

import pytest

PROTOCOL = Path(__file__).resolve().parents[1]
VECTORS = Path("vectors/v0")
V02_FIXTURE = Path(__file__).resolve().parent / "fixtures" / "v02_cases.sha256"
V031_FIXTURE = Path(__file__).resolve().parent / "fixtures" / "v031_cases.sha256"
V040_FIXTURE = V02_FIXTURE.with_name("v040_cases.sha256")


def digest(value: Any) -> str:
    canonical = json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("ascii")).hexdigest()


def _load(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def case_digests(root: Path) -> dict[str, str]:
    """Key -> digest for every message case and every flow (flows/rec/, flows/app/ included) under root/vectors/v0."""
    found: dict[str, str] = {}
    for path in sorted((root / VECTORS / "messages").glob("*.json")):
        data = _load(path)
        for kind in ("valid", "invalid"):
            for index, case in enumerate(data.get(kind, ())):
                found[f"messages/{path.name} {kind}[{index}]"] = digest(case)
    flows = root / VECTORS / "flows"
    for path in sorted(flows.rglob("*.json")):
        found[f"flows/{path.relative_to(flows).as_posix()}"] = digest(_load(path))
    return found


def read_fixture(path: Path) -> dict[str, str]:
    lines = (line for line in path.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#"))
    pairs = (line.split("  ", 1) for line in lines)
    return {key: value for value, key in pairs}


def drift(expected: Mapping[str, str], actual: Mapping[str, str]) -> tuple[str, ...]:
    missing = tuple(f"{key}: removed" for key in expected if key not in actual)
    changed = tuple(f"{key}: changed" for key in expected if key in actual and actual[key] != expected[key])
    return missing + changed


def test_fixture_covers_every_v02_case() -> None:
    expected = read_fixture(V02_FIXTURE)
    assert len([k for k in expected if k.startswith("messages/")]) == 217
    assert len([k for k in expected if k.startswith("flows/")]) == 8


def test_every_v02_case_is_unchanged() -> None:
    assert drift(read_fixture(V02_FIXTURE), case_digests(PROTOCOL)) == ()


def test_fixture_covers_every_v031_case() -> None:
    expected = read_fixture(V031_FIXTURE)
    assert len([k for k in expected if k.startswith("messages/")]) == 317
    assert len([k for k in expected if k.startswith("flows/") and not k.startswith("flows/rec/")]) == 10
    assert len([k for k in expected if k.startswith("flows/rec/")]) == 5


def test_v031_fixture_repeats_every_v02_digest() -> None:
    v02, v031 = read_fixture(V02_FIXTURE), read_fixture(V031_FIXTURE)
    assert {key: v031.get(key) for key in v02} == v02


def test_every_v031_case_is_unchanged() -> None:
    assert drift(read_fixture(V031_FIXTURE), case_digests(PROTOCOL)) == ()


def test_v040_fixture_covers_every_case_up_to_v040() -> None:
    expected = read_fixture(V040_FIXTURE)
    assert len([k for k in expected if k.startswith("messages/")]) == 328  # v0.3.1's 317 + debug.codec 6 + ui 5
    assert len([k for k in expected if k.startswith("flows/") and not k.startswith("flows/rec/")]) == 10
    assert len([k for k in expected if k.startswith("flows/rec/")]) == 5
    assert set(read_fixture(V02_FIXTURE).items()) <= set(expected.items())
    assert set(read_fixture(V031_FIXTURE).items()) <= set(expected.items())


def test_every_v040_case_is_unchanged() -> None:
    assert drift(read_fixture(V040_FIXTURE), case_digests(PROTOCOL)) == ()


# --- the guard bites -----------------------------------------------------------


@pytest.fixture()
def tree(tmp_path: Path) -> Path:
    root = tmp_path / "protocol"
    shutil.copytree(PROTOCOL / VECTORS, root / VECTORS)
    return root


def _rewrite(path: Path, change: Any) -> None:
    path.write_text(json.dumps(change(_load(path)), ensure_ascii=False, indent=2), encoding="utf-8")


def test_edited_case_is_reported(tree: Path) -> None:
    def edit(data: dict) -> dict:
        first = {**data["valid"][0], "name": "renamed"}
        return {**data, "valid": [first, *data["valid"][1:]]}

    _rewrite(tree / VECTORS / "messages" / "pong.json", edit)
    assert drift(read_fixture(V02_FIXTURE), case_digests(tree)) == ("messages/pong.json valid[0]: changed",)


def test_removed_case_and_flow_are_reported(tree: Path) -> None:
    _rewrite(tree / VECTORS / "messages" / "ping.json", lambda d: {**d, "invalid": d["invalid"][:-1]})
    (tree / VECTORS / "flows" / "tx_parking.json").unlink()
    assert set(drift(read_fixture(V02_FIXTURE), case_digests(tree))) == {
        "messages/ping.json invalid[9]: removed",
        "flows/tx_parking.json: removed",
    }


def test_edited_rec_flow_is_reported(tree: Path) -> None:
    _rewrite(tree / VECTORS / "flows" / "rec" / "file_list_paging.json", lambda d: {**d, "description": "edited"})
    for fixture in (V031_FIXTURE, V040_FIXTURE):
        assert drift(read_fixture(fixture), case_digests(tree)) == ("flows/rec/file_list_paging.json: changed",)


def test_appended_case_and_reformatting_are_allowed(tree: Path) -> None:
    def append(data: dict) -> dict:
        return {**data, "valid": [*data["valid"], {**data["valid"][0], "name": "new v0.3 case"}]}

    _rewrite(tree / VECTORS / "messages" / "pong.json", append)
    _rewrite(tree / VECTORS / "flows" / "handshake.json", lambda d: d)
    assert drift(read_fixture(V02_FIXTURE), case_digests(tree)) == ()
    assert drift(read_fixture(V031_FIXTURE), case_digests(tree)) == ()
    assert drift(read_fixture(V040_FIXTURE), case_digests(tree)) == ()


def test_ui_added_to_a_released_debug_state_case_is_reported(tree: Path) -> None:
    """v0.4.0 adds debug.state.ui (docs/13 §12.1) as new cases; a released case must not grow the field."""

    def add_ui(data: dict) -> dict:
        case = data["valid"][1]
        edited = {**case, "msg": {**case["msg"], "ui": {"menu": True, "locked": False}}}
        return {**data, "valid": [data["valid"][0], edited, *data["valid"][2:]]}

    _rewrite(tree / VECTORS / "messages" / "debug.state.json", add_ui)
    for fixture in (V02_FIXTURE, V031_FIXTURE, V040_FIXTURE):
        assert drift(read_fixture(fixture), case_digests(tree)) == ("messages/debug.state.json valid[1]: changed",)
