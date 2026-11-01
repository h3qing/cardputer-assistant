"""The dest-cli contract v1 checks itself (check_dest_cli.check_all), and its checker catches a broken contract."""

from __future__ import annotations

import hashlib
import json
import shutil
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_dest_cli as c  # noqa: E402

V1 = Path(__file__).resolve().parents[1] / "v1"
CONTRACT = c.load(V1)


def copy(tmp_path: Path) -> Path:
    root = tmp_path / "v1"
    shutil.copytree(V1, root)
    return root


def redigest(root: Path) -> None:
    lines = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(root)}"
             for p in sorted((root / "vectors").glob("*/*.json"))]  # fmt: skip
    (root / "VECTORS.sha256").write_text("\n".join(lines) + "\n", encoding="utf-8")


def test_the_contract_is_clean_and_main_says_ok(capsys):
    report = c.check_all(V1)
    assert report.errors == ()
    assert {doc for doc, valid, _ in report.counts if valid} == set(c.DOCS)
    assert c.main(["--root", str(V1)]) == 0 and "OK:" in capsys.readouterr().out


@pytest.mark.parametrize(("data", "verdict"), [([], ("E_INVALID", ("(root)",))), ({"v": "1"}, ("E_VERSION", ("v",))),
                                               ({"ok": False, "v": 1}, ("E_INVALID", ("error",)))])  # fmt: skip
def test_verdicts(data, verdict):
    got = c.validate(CONTRACT, "error", data)
    assert (got.code, got.fields) == verdict


def test_a_vector_that_no_longer_matches_is_reported(tmp_path, capsys):
    root = copy(tmp_path)
    path = root / "vectors" / "valid" / "dropped.json"
    case = json.loads(path.read_text(encoding="utf-8"))
    path.write_text(json.dumps({**case, "data": {**case["data"], "dest": "Nope"}}), encoding="utf-8")
    errors = c.check_all(root).errors
    assert any("valid/dropped.json: expected ok" in e for e in errors)
    assert any("does not match VECTORS.sha256" in e for e in errors)
    assert c.main(["--root", str(root)]) == 1 and "FAILED" in capsys.readouterr().out


def test_missing_cases_and_a_forbidden_keyword_are_reported(tmp_path):
    root = copy(tmp_path)
    (root / "vectors" / "invalid" / "tested_marker_5.json").unlink()
    (root / "vectors" / "valid" / "pending_unknown_values.json").unlink()
    for name in ("dropped.json",):
        (root / "vectors" / "valid" / name).unlink()
    (root / "vectors" / "invalid" / "odd.json").write_text(json.dumps({"doc": "nope", "name": "x", "data": {}}))
    schema = root / "schema" / "usual.schema.json"
    schema.write_text(json.dumps({**json.loads(schema.read_text()), "additionalProperties": False}))
    redigest(root)
    errors = c.check_all(root).errors
    assert "needs an invalid tested case (E_INVALID marker)" in errors
    assert "needs a valid dropped case" in errors
    assert any(e.startswith("needs a valid pending case whose row has an unknown field") for e in errors)
    assert any("odd.json: doc must be one of" in e for e in errors)
    assert "schema usual: uses additionalProperties" in errors


def test_wrapper_problems():
    assert c._wrapper_problem("valid", {"doc": "sent", "name": ""}) == "needs a non-empty name and data"
    assert c._wrapper_problem("invalid", {"doc": "sent", "name": "x", "data": {}}).startswith("reason must be")
