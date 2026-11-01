"""Scenario results, the combined report, and reading it back (06 WP5 acceptance gates)."""

import asyncio
import json

import pytest

from harness.report import (
    ReportError,
    build_report,
    check,
    error_result,
    failed_checks,
    gate,
    load_report,
    result,
    summary_lines,
    write_report,
)


def test_check_carries_detail_and_a_boolean_verdict():
    assert check("handshake_s", 1.2 <= 3, value=1.2, limit=3) == {
        "name": "handshake_s",
        "ok": True,
        "value": 1.2,
        "limit": 3,
    }


def test_check_coerces_truthy_values_to_bool():
    assert check("x", 1)["ok"] is True
    assert check("x", [])["ok"] is False


def test_result_is_ok_only_when_every_check_passes():
    good = result([check("a", True), check("b", True)], p95_ms=12)
    bad = result([check("a", True), check("b", False)])
    assert good == {"ok": True, "checks": [check("a", True), check("b", True)], "p95_ms": 12}
    assert bad["ok"] is False


def test_result_without_checks_is_not_ok():
    assert result([])["ok"] is False


def test_metrics_cannot_shadow_ok_or_checks():
    with pytest.raises(ValueError):
        result([check("a", True)], ok=True)


def test_error_result_keeps_the_error_type_and_message():
    r = error_result(TimeoutError("no debug.state reply"))
    assert r == {"ok": False, "checks": [], "error": "TimeoutError: no debug.state reply"}


def test_error_result_appends_the_notes_a_scenario_added_on_the_way_out():
    exc = RuntimeError("afplay exited 1")
    exc.add_note("cleanup: card emptied")
    assert error_result(exc)["error"] == "RuntimeError: afplay exited 1 [cleanup: card emptied]"


def test_error_result_finds_the_notes_behind_a_scenario_timeout():
    cancelled = asyncio.CancelledError()  # what the runner's wait_for cancelled the scenario with
    cancelled.add_note("cleanup: card emptied")
    try:
        raise TimeoutError from cancelled
    except TimeoutError as exc:
        assert error_result(exc)["error"] == "TimeoutError:  [cleanup: card emptied]"


def test_report_keys_scenarios_by_name_and_is_ok_when_all_are():
    meta = {"started": "2026-10-19T20:00:00"}
    lat = result([check("p95", True)], p95_board_s=3.1, p95_compose_ms=60)
    rep = build_report(meta, {"handshake": result([check("a", True)]), "latency": lat})
    assert rep["ok"] is True
    assert rep["meta"] == meta
    assert rep["scenarios"] == ["handshake", "latency"]
    assert rep["latency"]["p95_board_s"] == 3.1


def test_report_is_not_ok_when_one_scenario_fails_or_none_ran():
    assert build_report({}, {"a": result([check("x", False)])})["ok"] is False
    assert build_report({}, {})["ok"] is False


@pytest.mark.parametrize("name", ["ok", "meta", "scenarios"])
def test_reserved_names_cannot_be_scenarios(name):
    with pytest.raises(ValueError):
        build_report({}, {name: result([check("x", True)])})


def test_gate_reads_a_dotted_metric():
    rep = build_report({}, {"latency": result([check("x", True)], p95_board_s=4.2, p95_compose_ms=301)})
    assert gate(rep, "latency.p95_board_s", 5) is True
    assert gate(rep, "latency.p95_compose_ms", 300) is False


def test_gate_is_false_for_a_missing_or_non_numeric_metric():
    rep = build_report({}, {"latency": result([check("x", True)], note="n/a")})
    assert gate(rep, "latency.p95_board_s", 5) is False
    assert gate(rep, "latency.note", 5) is False
    assert gate(rep, "soak.lost", 0) is False


def test_failed_checks_lists_scenario_and_check():
    rep = build_report({}, {"a": result([check("x", False, value=2)]), "b": error_result(RuntimeError("boom"))})
    assert failed_checks(rep) == ["a: x (value=2)", "b: RuntimeError: boom"]


def test_summary_lines_has_one_line_per_scenario_and_a_verdict():
    rep = build_report({}, {"a": result([check("x", True)]), "b": result([check("y", False)])})
    lines = summary_lines(rep)
    assert lines[0].startswith("PASS a")
    assert lines[1].startswith("FAIL b")
    assert lines[-1] == "HIL: FAIL (1/2 scenarios passed)"


def test_write_then_load_round_trips_utf8(tmp_path):
    rep = build_report({"note": "中文名"}, {"cjk": result([check("rendered", True)], names=["设置页面和字体修复"])})
    path = tmp_path / "hil.json"
    write_report(path, rep)
    assert "设置页面" in path.read_text(encoding="utf-8")
    assert load_report(path) == rep


def test_load_rejects_something_that_is_not_a_report(tmp_path):
    path = tmp_path / "x.json"
    path.write_text(json.dumps({"hello": 1}), encoding="utf-8")
    with pytest.raises(ReportError):
        load_report(path)
    path.write_text("not json", encoding="utf-8")
    with pytest.raises(ReportError):
        load_report(path)


def test_load_rejects_a_listed_scenario_without_a_result(tmp_path):
    path = tmp_path / "x.json"
    path.write_text(json.dumps({"ok": True, "meta": {}, "scenarios": ["a"]}), encoding="utf-8")
    with pytest.raises(ReportError, match="a"):
        load_report(path)
