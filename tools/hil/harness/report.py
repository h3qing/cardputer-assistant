"""Scenario results and the combined JSON report written by run_e2e.py --report.

Every scenario produces its own JSON object {"ok", "checks": [{"name", "ok", ...detail}], ...metrics};
the report holds them under the scenario's name, so the 06 WP5 gates read e.g. latency.p95_board_s.
"""

from __future__ import annotations

import json
from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import Any

RESERVED = frozenset({"ok", "meta", "scenarios"})


class ReportError(ValueError):
    """The file is not a run_e2e.py report."""


def check(name: str, ok: Any, **detail: Any) -> dict[str, Any]:
    return {"name": name, "ok": bool(ok), **detail}


def result(checks: Iterable[Mapping[str, Any]], **metrics: Any) -> dict[str, Any]:
    if {"ok", "checks"} & metrics.keys():
        raise ValueError("ok and checks are computed, not metrics")
    listed = [dict(c) for c in checks]
    return {"ok": bool(listed) and all(c["ok"] for c in listed), "checks": listed, **metrics}


def _notes(exc: BaseException) -> list[str]:
    """What a scenario added on the way out (add_note), also on the CancelledError behind the runner's timeout."""
    return [n for e in (exc, exc.__cause__) if e is not None for n in getattr(e, "__notes__", ())]


def error_result(exc: BaseException) -> dict[str, Any]:
    error = " ".join([f"{type(exc).__name__}: {exc}", *(f"[{n}]" for n in _notes(exc))])
    return {"ok": False, "checks": [], "error": error}


def build_report(meta: Mapping[str, Any], results: Mapping[str, Mapping[str, Any]]) -> dict[str, Any]:
    clash = RESERVED & results.keys()
    if clash:
        raise ValueError(f"reserved report keys used as scenario names: {sorted(clash)}")
    ok = bool(results) and all(r["ok"] for r in results.values())
    return {"ok": ok, "meta": dict(meta), "scenarios": list(results), **{k: dict(v) for k, v in results.items()}}


def gate(report: Mapping[str, Any], path: str, limit: float) -> bool:
    """True when the dotted metric (scenario.metric) exists, is a number, and is <= limit."""
    scenario, _, metric = path.partition(".")
    value = (report.get(scenario) or {}).get(metric)
    return isinstance(value, int | float) and not isinstance(value, bool) and value <= limit


def _describe(c: Mapping[str, Any]) -> str:
    detail = ", ".join(f"{k}={v}" for k, v in c.items() if k not in ("name", "ok"))
    return f"{c['name']} ({detail})" if detail else str(c["name"])


def failed_checks(report: Mapping[str, Any]) -> list[str]:
    out: list[str] = []
    for name in report["scenarios"]:
        res = report[name]
        if res.get("error"):
            out.append(f"{name}: {res['error']}")
        out += [f"{name}: {_describe(c)}" for c in res["checks"] if not c["ok"]]
    return out


def summary_lines(report: Mapping[str, Any]) -> list[str]:
    lines = []
    for name in report["scenarios"]:
        res = report[name]
        verdict = "PASS" if res["ok"] else "FAIL"
        passed = sum(c["ok"] for c in res["checks"])
        lines.append(f"{verdict} {name} ({passed}/{len(res['checks'])} checks)")
    total = len(report["scenarios"])
    good = sum(report[n]["ok"] for n in report["scenarios"])
    lines.append(f"HIL: {'PASS' if report['ok'] else 'FAIL'} ({good}/{total} scenarios passed)")
    return lines


def write_report(path: Path, report: Mapping[str, Any]) -> None:
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _valid_result(res: Any) -> bool:
    return isinstance(res, dict) and isinstance(res.get("ok"), bool) and isinstance(res.get("checks"), list)


def load_report(path: Path) -> dict[str, Any]:
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
    except ValueError as exc:
        raise ReportError(f"{path} is not JSON") from exc
    shape_ok = isinstance(doc, dict) and isinstance(doc.get("ok"), bool) and isinstance(doc.get("meta"), dict)
    if not shape_ok or not isinstance(doc.get("scenarios"), list):
        raise ReportError(f"{path} is not a run_e2e.py report")
    broken = [n for n in doc["scenarios"] if not _valid_result(doc.get(n))]
    if broken:
        raise ReportError(f"scenario result missing or malformed: {', '.join(map(str, broken))}")
    return doc
