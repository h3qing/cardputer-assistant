"""The setup contract v1 checks itself (check_setup.check_all), and its checker catches a broken contract."""

from __future__ import annotations

import hashlib
import json
import shutil
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_setup as c  # noqa: E402

V1 = Path(__file__).resolve().parents[1] / "v1"
CONTRACT = c.load(V1)
ROW = {"v": 1, "id": "service", "state": "ok", "required": True, "blocked_by": [], "title": "后台服务在跑",
       "detail": "", "fix": []}  # fmt: skip


def copy(tmp_path: Path) -> Path:
    root = tmp_path / "v1"
    shutil.copytree(V1, root)
    return root


def redigest(root: Path) -> None:
    lines = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(root)}"
             for p in sorted((root / "vectors").glob("*/*.json"))]  # fmt: skip
    (root / "VECTORS.sha256").write_text("\n".join(lines) + "\n", encoding="utf-8")


def test_the_contract_is_clean_and_main_says_ok(capsys):
    assert c.check_all(V1).errors == ()
    assert c.main(["--root", str(V1)]) == 0 and "OK:" in capsys.readouterr().out


@pytest.mark.parametrize(
    ("data", "verdict"),
    [([], ("E_INVALID", ("(root)",))), ({**ROW, "v": "1"}, ("E_VERSION", ("v",))), (ROW, ("ok", ())),
     ({**ROW, "fix": [{"op": "device.bind", "cand": "c0123abcd"}]}, ("ok", ())),
     ({**ROW, "detail": "a\x1b[2Jb"}, ("E_INVALID", ("detail",))),
     ({**ROW, "fix": [{"op": "device.bind", "serial": "AA:BB"}]}, ("E_INVALID", ("serial",))),
     ({**ROW, "extra": {"dev": "020000000001"}}, ("E_INVALID", ("dev",)))],
)  # fmt: skip
def test_verdicts(data, verdict):
    got = c.validate(CONTRACT, data)
    assert (got.code, got.fields) == verdict


def test_unknown_values_are_kept():
    data = {**ROW, "id": "later", "state": "maybe", "fix": [{"op": "new.verb", "x": 1}], "more": True}
    assert c.validate(CONTRACT, data).code == "ok"
    assert c.has_unknown_values(CONTRACT, {"data": data})


def test_a_vector_that_no_longer_matches_is_reported(tmp_path, capsys):
    root = copy(tmp_path)
    path = root / "vectors" / "valid" / "service_ok.json"
    case = json.loads(path.read_text(encoding="utf-8"))
    path.write_text(json.dumps({**case, "data": {**case["data"], "state": "OK"}}), encoding="utf-8")
    errors = c.check_all(root).errors
    assert any("valid/service_ok.json: expected ok" in e for e in errors)
    assert any("does not match VECTORS.sha256" in e for e in errors)
    assert c.main(["--root", str(root)]) == 1 and "FAILED" in capsys.readouterr().out


def test_missing_cases_and_a_forbidden_keyword_are_reported(tmp_path):
    root = copy(tmp_path)
    (root / "vectors" / "invalid" / "host_id.json").unlink()
    (root / "vectors" / "valid" / "unknown_values.json").unlink()
    (root / "vectors" / "valid" / "hooks_warn.json").unlink()
    schema_path = root / "schema" / "row.schema.json"
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    schema_path.write_text(json.dumps({**schema, "additionalProperties": True}), encoding="utf-8")
    redigest(root)
    errors = c.check_all(root).errors
    assert "needs an invalid case (E_INVALID host_id)" in errors
    assert "needs a valid row with an unknown field, id, state and fix op" in errors
    assert "needs a valid hooks row" in errors
    assert "schema row: uses additionalProperties" in errors


def test_a_badly_wrapped_vector_is_reported(tmp_path):
    root = copy(tmp_path)
    (root / "vectors" / "invalid" / "odd.json").write_text(json.dumps({"doc": "row", "name": "x", "data": ROW}))
    redigest(root)
    assert any("invalid/odd.json: reason must be" in e for e in c.check_all(root).errors)
