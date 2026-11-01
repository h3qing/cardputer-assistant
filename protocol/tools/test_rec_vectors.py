"""Tests for the v0.4.1 vectors (docs/13 §12.2) and the rules check_vectors.py enforces on them."""

from __future__ import annotations

import json
import shutil
from collections.abc import Callable
from pathlib import Path

import check_vectors
import pytest
import rec_rules

PROTOCOL = Path(__file__).resolve().parents[1]
MESSAGES = Path("vectors/v0/messages")
FLOWS = Path("vectors/v0/flows")
APP_FLOWS = FLOWS / "app"
REC_TYPES = ("rec.list", "rec.items", "rec.act")
LINK_FLOWS = ("caps_absent", "old_daemon_ignores_dest", "new_daemon_old_firmware")
APP_FLOW_NAMES = ("rec_list_paging", "rec_act_hide", "rec_list_old_daemon")


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


def _sent(flow: dict, side: str) -> list[dict]:
    return [s["msg"] for s in flow["steps"] if s.get("from") == side and "msg" in s]


# --- coverage --------------------------------------------------------------------


def test_every_rec_type_has_valid_and_invalid_cases() -> None:
    counts = {t: (v, i) for t, v, i in check_vectors.check_all(PROTOCOL).type_counts}
    assert all(counts[t][0] >= 1 and counts[t][1] >= 1 for t in REC_TYPES)


def test_v041_flows_are_required_and_placed() -> None:
    assert set(LINK_FLOWS) <= set(check_vectors.REQUIRED_FLOWS)
    assert set(check_vectors.REQUIRED_APP_FLOWS) == set(APP_FLOW_NAMES)
    for name in APP_FLOW_NAMES:
        assert (PROTOCOL / APP_FLOWS / f"{name}.json").is_file(), name
        assert not (PROTOCOL / FLOWS / f"{name}.json").exists(), name


def test_a_flow_with_device_rec_requests_outside_flows_app_is_an_error(tree: Path) -> None:
    (tree / APP_FLOWS / "rec_act_hide.json").rename(tree / FLOWS / "rec_act_hide.json")
    errors = _errors(tree)
    assert _mentions(errors, "flows/rec_act_hide.json", "flows/app/")
    assert _mentions(errors, "flows/app/rec_act_hide.json", "missing")


def test_main_lists_the_rec_types_and_app_flows(capsys: pytest.CaptureFixture[str]) -> None:
    assert check_vectors.main(["--root", str(PROTOCOL)]) == 0
    out = capsys.readouterr().out
    assert all(t in out for t in REC_TYPES) and all(f"app/{f}" in out for f in APP_FLOW_NAMES)


@pytest.mark.parametrize(("type_", "field", "label"), [(t, f, d) for t, f, d, _ in rec_rules.REQUIRED_REC_CASES])
def test_missing_required_invalid_case_is_an_error(tree: Path, type_: str, field: str, label: str) -> None:
    def drop(data: dict) -> dict:
        return {**data, "invalid": [c for c in data["invalid"] if c.get("field") != field]}

    _edit_json(tree / MESSAGES / f"{type_}.json", drop)
    assert _mentions(_errors(tree), type_, label)


def test_required_invalid_cases_include_the_acceptance_list() -> None:
    labels = {(t, f) for t, f, _, _ in rec_rules.REQUIRED_REC_CASES}
    assert labels >= {
        ("rec.list", "max"),
        ("rec.act", "op"),
        ("rec.list", "cursor"),
        ("rec.items", "items[0].title"),
        ("rec.items", "items"),
        ("rec.act", "project"),
        ("outbox.item", "body.dest"),
        ("file.items", "items[0].dest"),
        ("hello", "host_id"),
    }
    ops = [label for t, f, label, _ in rec_rules.REQUIRED_REC_CASES if (t, f) == ("rec.act", "op")]
    cursors = [label for t, f, label, _ in rec_rules.REQUIRED_REC_CASES if (t, f) == ("rec.list", "cursor")]
    assert len(ops) == 2 and len(cursors) == 2


# --- host reply size ---------------------------------------------------------------


def test_rec_items_is_the_only_host_reply_above_host_reply_max() -> None:
    assert rec_rules.LARGE_HOST_REPLIES == frozenset({"rec.items"})


def test_device_rx_budget_still_holds(tree: Path) -> None:
    limits = _load(tree / "limits.json")
    small = limits["device_outbox_inflight_max"] + limits["device_agent_inflight_max"]
    large = limits["device_queue_inbox_inflight_max"]  # rec.list shares the one misc slot (docs/13 §10.2)
    worst = limits["host_inflight_max"] + small * limits["host_reply_max"] + large * limits["line_max"]
    assert worst == 7424 and worst <= limits["device_rx_buffer"]


def test_large_reply_of_another_type_is_still_an_error(tree: Path) -> None:
    def pad(data: dict) -> dict:
        case = json.loads(json.dumps(data["valid"][0]))
        case["name"] = "padded ack"
        case["msg"] = {"v": 0, "t": "ack", "id": "h9", "re": "d9", "pad": "x" * 300}
        case["dir"] = "h2d"
        return {**data, "valid": [*data["valid"], case]}

    _edit_json(tree / MESSAGES / "ack.json", pad)
    assert _mentions(_errors(tree), "ack.json", "host_reply_max")


def test_rec_items_vectors_include_a_page_above_host_reply_max() -> None:
    sizes = [len(json.dumps(c["msg"], ensure_ascii=False, separators=(",", ":")).encode()) + 1 for c in _cases()]
    assert max(sizes) > 256


def _cases() -> list[dict]:
    return _load(PROTOCOL / MESSAGES / "rec.items.json")["valid"]


# --- the flows say what docs/13 §12.2 says -------------------------------------------


def test_paging_flow_brings_the_cursor_back() -> None:
    flow = _load(PROTOCOL / APP_FLOWS / "rec_list_paging.json")
    nexts = [m["next"] for m in _sent(flow, "host") if m["t"] == "rec.items" and m.get("next")]
    cursors = [m["cursor"] for m in _sent(flow, "device") if m["t"] == "rec.list" and m.get("cursor")]
    assert cursors and cursors == nexts[: len(cursors)]


def test_paging_flow_mixes_two_monoid_epochs_in_wall_time_order() -> None:
    flow = _load(PROTOCOL / APP_FLOWS / "rec_list_paging.json")
    pages = [m for m in _sent(flow, "host") if m["t"] == "rec.items"]
    rows = [r for page in pages[:2] for r in page["items"]]
    times = [r["wall_ms"] for r in rows]
    assert times == sorted(times, reverse=True) and len({r["rec"][:7] for r in rows}) == 2
    assert [r["rec"] for r in rows] != sorted((r["rec"] for r in rows), reverse=True)


def test_hide_flow_hides_and_the_next_list_omits_the_session() -> None:
    flow = _load(PROTOCOL / APP_FLOWS / "rec_act_hide.json")
    hidden = next(m["rec"] for m in _sent(flow, "device") if m["t"] == "rec.act" and m["op"] == "hide")
    pages = [m for m in _sent(flow, "host") if m["t"] == "rec.items"]
    assert any(r["rec"] == hidden for r in pages[0]["items"])
    assert all(r["rec"] != hidden for r in pages[-1]["items"])


def test_old_daemon_flow_answers_e_unknown_type() -> None:
    flow = _load(PROTOCOL / APP_FLOWS / "rec_list_old_daemon.json")
    assert any(m["t"] == "error" and m["code"] == "E_UNKNOWN_TYPE" for m in _sent(flow, "host"))


@pytest.mark.parametrize("name", ["caps_absent", "old_daemon_ignores_dest"])
def test_old_daemon_flows_never_carry_rec_requests_or_caps(name: str) -> None:
    flow = _load(PROTOCOL / FLOWS / f"{name}.json")
    assert not {m["t"] for m in _sent(flow, "device")} & set(REC_TYPES)
    hellos = [m for m in _sent(flow, "host") if m["t"] == "hello"]
    assert hellos and all(not {"recs", "todo"} & set(m["caps"]) for m in hellos)


def test_old_daemon_flow_stores_the_todo_note() -> None:
    flow = _load(PROTOCOL / FLOWS / "old_daemon_ignores_dest.json")
    notes = [m for m in _sent(flow, "device") if m["t"] == "outbox.item"]
    acks = [m for m in _sent(flow, "host") if m["t"] == "outbox.ack"]
    assert [n["body"].get("dest") for n in notes] == ["todo"] and [a["status"] for a in acks] == ["stored"]


def test_new_daemon_flow_sends_the_new_hello_fields_to_old_firmware() -> None:
    flow = _load(PROTOCOL / FLOWS / "new_daemon_old_firmware.json")
    hello = next(m for m in _sent(flow, "host") if m["t"] == "hello")
    device = next(m for m in _sent(flow, "device") if m["t"] == "hello")
    assert {"recs", "todo"} <= set(hello["caps"]) and {"host_id", "host_label"} <= set(hello)
    assert device["fw"] == "cpa-fw/0.4.0"
    assert not {m["t"] for m in _sent(flow, "device")} & set(REC_TYPES)


def test_caps_absent_flow_breaks_when_the_device_asks(tree: Path) -> None:
    def ask(data: dict) -> dict:
        t = data["steps"][-1]["t"] + 10
        listing = {"t": t, "from": "device", "msg": {"v": 0, "t": "rec.list", "id": "d99", "filter": "all", "max": 6}}
        refusal = {"v": 0, "t": "error", "id": "h99", "re": "d99", "code": "E_UNKNOWN_TYPE"}
        return {**data, "steps": [*data["steps"], listing, {"t": t + 5, "from": "host", "msg": refusal}]}

    _edit_json(tree / FLOWS / "caps_absent.json", ask)
    assert _mentions(_errors(tree), "caps_absent", "d99", "caps.recs")
