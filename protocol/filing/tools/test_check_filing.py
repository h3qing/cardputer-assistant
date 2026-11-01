"""Tests for the filing contract v1 (docs/23-mac-inbox.md §3.2, §3.3) and check_filing.py."""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
from collections.abc import Callable
from pathlib import Path

import check_filing as cf
import pytest

V1 = Path(__file__).resolve().parents[1] / "v1"
VALID = sorted((V1 / "vectors" / "valid").glob("*.json"))
INVALID = sorted((V1 / "vectors" / "invalid").glob("*.json"))
SID = "3QZK1M7-00042-0A1C"


@pytest.fixture(scope="module")
def contract() -> cf.FilingContract:
    return cf.load(V1)


@pytest.fixture()
def tree(tmp_path: Path) -> Path:
    root = tmp_path / "v1"
    shutil.copytree(V1, root)
    return root


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _vector(name: str) -> dict:
    return copy.deepcopy(_load(V1 / "vectors" / "valid" / f"{name}.json")["data"])


def _rewrite(path: Path, change: Callable[[dict], dict]) -> None:
    path.write_text(
        json.dumps(change(_load(path)), indent=2) + "\n", encoding="utf-8"
    )  # escaped: some vectors hold lone surrogates


def _row(**over) -> dict:
    return {**_vector("ls_new_memo")["rows"][0], **over}


def _ls(*rows: dict) -> dict:
    return {**_vector("ls_new_memo"), "rows": list(rows)}


# --- the repo contract ----------------------------------------------------------------


def test_repo_contract_passes() -> None:
    report = cf.check_all(V1)
    assert report.errors == ()
    assert {doc for doc, valid, _ in report.counts if valid >= 1} == set(cf.DOCS)


def test_main_prints_counts_and_exits_zero(capsys: pytest.CaptureFixture[str]) -> None:
    assert cf.main(["--root", str(V1)]) == 0
    out = capsys.readouterr().out
    assert "ls" in out and "OK" in out


def test_main_exits_one_and_lists_errors(tree: Path, capsys: pytest.CaptureFixture[str]) -> None:
    (tree / "vectors" / "valid" / VALID[0].name).write_text("{", encoding="utf-8")
    assert cf.main(["--root", str(tree)]) == 1
    assert VALID[0].name in capsys.readouterr().out


@pytest.mark.parametrize("path", VALID, ids=[p.stem for p in VALID])
def test_valid_vector_is_accepted(contract: cf.FilingContract, path: Path) -> None:
    case = _load(path)
    assert cf.validate(contract, case["doc"], case["data"]) == cf.Verdict("ok")


@pytest.mark.parametrize("path", INVALID, ids=[p.stem for p in INVALID])
def test_invalid_vector_is_rejected_for_its_reason(contract: cf.FilingContract, path: Path) -> None:
    case = _load(path)
    reason = case["reason"]
    assert cf.validate(contract, case["doc"], case["data"]) == cf.Verdict(reason["code"], (reason["field"],))


@pytest.mark.parametrize(("doc", "code", "field", "label", "wanted"), cf.REQUIRED_INVALID, ids=lambda v: str(v))
def test_required_invalid_case_exists(doc: str, code: str, field: str, label: str, wanted: Callable) -> None:
    cases = [_load(p) for p in INVALID]
    reason = {"code": code, "field": field}
    assert any(c["doc"] == doc and c["reason"] == reason and wanted(c["data"]) for c in cases), label


# --- version, forward compatibility, the WhisperWoof keyword subset ----------------------


@pytest.mark.parametrize("doc", ["ls", "show", "result", "error", "projects"])
@pytest.mark.parametrize("version", [2, 0, "1", None, True])
def test_only_v1_is_read(contract: cf.FilingContract, doc: str, version: object) -> None:
    name = next(p for p in VALID if _load(p)["doc"] == doc)
    data = {**_load(name)["data"], "v": version}
    assert cf.validate(contract, doc, data) == cf.Verdict("E_VERSION", ("v",))


def test_a_row_alone_has_no_version(contract: cf.FilingContract) -> None:
    assert cf.validate(contract, "row", _row()) == cf.Verdict("ok")
    assert cf.validate(contract, "row", {**_row(), "v": 7}) == cf.Verdict("ok"), "an unknown field, not a version"


def test_unknown_fields_and_unknown_extensible_values_are_accepted(contract: cf.FilingContract) -> None:
    row = _row(kind="capture", why=["unfiled", "later"], tx="queued_remote", x_future={"a": [1]},
               env={"host": "家", "link": "other"}, prof="work", clips=[{"id": SID}])  # fmt: skip
    assert cf.validate(contract, "ls", {**_ls(row), "x_new": 1}) == cf.Verdict("ok")


def test_non_object_document_is_invalid(contract: cf.FilingContract) -> None:
    assert cf.validate(contract, "ls", [1]) == cf.Verdict("E_INVALID", ("(root)",))


def test_schemas_use_only_keywords_the_whisperwoof_validator_knows(contract: cf.FilingContract) -> None:
    assert cf.keyword_problems(contract) == ()


def test_a_forbidden_keyword_is_reported(tree: Path) -> None:
    path = tree / "schema" / "error.schema.json"
    _rewrite(path, lambda s: {**s, "additionalProperties": False})
    assert any("additionalProperties" in e and "error" in e for e in cf.check_all(tree).errors)


def test_a_property_named_like_a_keyword_is_not_a_keyword(contract: cf.FilingContract) -> None:
    schema = {"type": "object", "properties": {"format": {"type": "string"}, "not": {"type": "string"}}}
    assert cf.unknown_keywords(schema) == ()
    assert cf.unknown_keywords({"anyOf": [{"not": {}}]}) == ("not",)


@pytest.mark.parametrize(
    "keyword", ["default", "examples", "$comment", "uniqueItems", "minProperties", "propertyNames", "const2"]
)
def test_any_keyword_outside_the_whisperwoof_list_is_reported(tree: Path, keyword: str) -> None:
    """contract-schema.js allows a fixed list and throws at start-up on anything else, x- annotations aside."""
    path = tree / "schema" / "row.schema.json"
    _rewrite(path, lambda s: {**s, "properties": {**s["properties"], "tx": {**s["properties"]["tx"], keyword: 1}}})
    assert any(f"uses {keyword}" in e and "row" in e for e in cf.check_all(tree).errors)


def test_the_walk_follows_every_place_whisperwoof_looks(contract: cf.FilingContract) -> None:
    nested = {"allOf": [{"if": {"x": 1}, "then": {"items": {"$defs": {"d": {"y": 1}}}}}], "x-mine": {"z": 1}}
    assert cf.unknown_keywords(nested) == ("x", "y")
    assert set(cf.FORBIDDEN).isdisjoint(cf.KEYWORDS), "the keywords it throws on stay refused"


# --- row rules -----------------------------------------------------------------------


@pytest.mark.parametrize(
    ("over", "field"),
    [
        ({"text": "x" * 281}, "text"),
        ({"text": ""}, "text"),
        ({"title": "a\u202eb"}, "title"),
        ({"title": "a\u2028b"}, "title"),
        ({"text": "a\x85b"}, "text"),
        ({"text": "a\x1b[31mb"}, "text"),
        ({"device": "02:00:00:00:00:01"}, "device"),
        ({"filing": "archived"}, "filing"),
        ({"project": "Acme Corp"}, "project"),
        ({"at": {"wall_ms": 1, "tz_min": 1440, "uncertain": False}}, "at.tz_min"),
        ({"queue": {"qid": "12", "intent": "todo", "open": True}}, "queue.qid"),
        ({"filing": "handed", "queue": None}, "queue"),
        ({"filing": "done", "asked": "acme"}, "filing"),
        ({"why": "unfiled"}, "why"),
        ({"tx": "Done"}, "tx"),
        ({"text": "a\ud83db"}, "text"),
        ({"env": {"host": "公司", "link": "HERE"}}, "env.link"),
        ({"env": {"host": "a" * 25, "link": "here"}}, "env.host"),
        ({"env": {"host": "公司\u202e", "link": "here"}}, "env.host"),
        ({"env": {"host": "公司"}}, "env.link"),
        ({"env": {"link": "here"}}, "env.host"),
        ({"asked": "ACME"}, "asked"),
    ],
    ids=[
        "text-281",
        "text-empty",
        "bidi",
        "line-sep",
        "c1",
        "escape",
        "mac",
        "filing",
        "project",
        "tz",
        "qid",
        "handed-queue",
        "asked-not-new",
        "why-string",
        "tx-case",
        "lone-surrogate",
        "link-case",
        "host-25",
        "host-bidi",
        "env-no-link",
        "env-no-host",
        "asked-case",
    ],
)
def test_row_rules(contract: cf.FilingContract, over: dict, field: str) -> None:
    assert cf.validate(contract, "ls", _ls(_row(**over))) == cf.Verdict("E_INVALID", (f"rows[0].{field}",))


def test_every_row_field_is_required(contract: cf.FilingContract) -> None:
    row = {k: v for k, v in _row().items() if k != "env"}
    assert cf.validate(contract, "row", row) == cf.Verdict("E_INVALID", ("env",))


def test_lengths_count_code_points_also_beyond_the_basic_plane(contract: cf.FilingContract) -> None:
    """A UTF-16 reader counts U+20000 and an emoji as two: 280 / 80 code points must still pass."""
    row = _row(text="\U00020000" * 140 + "长" * 140, title="🙂" * 80)
    assert cf.validate(contract, "row", row) == cf.Verdict("ok")
    assert cf.validate(contract, "row", {**row, "text": row["text"] + "长"}) == cf.Verdict("E_INVALID", ("text",))


def test_a_handed_row_with_its_queue_item_is_valid(contract: cf.FilingContract) -> None:
    row = _row(filing="handed", why=[], project="cpa", queue={"qid": "q12", "intent": "spawn", "open": False})
    assert cf.validate(contract, "row", row) == cf.Verdict("ok")


# --- the other documents ----------------------------------------------------------------


@pytest.mark.parametrize(
    ("name", "change", "field"),
    [
        ("show_memo", lambda d: {**d, "audio": ["/tmp/seg-000.wav"]}, "audio[0]"),
        ("show_memo", lambda d: {**d, "queue_text": "x" * 501}, "queue_text"),
        ("show_memo", lambda d: {**d, "todos": [{**d["todos"][0], "owner": "a\x00b"}]}, "todos[0].owner"),
        ("show_memo", lambda d: {**d, "sentences": [{"t_s": -1, "text": "x"}]}, "sentences[0].t_s"),
        ("result_file", lambda d: {**d, "undo": {**d["undo"], "id": "not-an-id"}}, "undo.id"),
        ("result_file", lambda d: {**d, "undo": {**d["undo"], "args": {"project": "A"}}}, "undo.args.project"),
        ("result_file", lambda d: {**d, "undo": {**d["undo"], "args": {}}}, "undo.args.project"),
        ("host_named", lambda d: {**d, "host": "a\x00b"}, "host"),
        ("host_named", lambda d: {**d, "host": "a" * 25}, "host"),
        ("result_file", lambda d: {**d, "ok": False}, "ok"),
        ("error_unknown_id", lambda d: {**d, "field": "id; rm -rf"}, "field"),
        ("error_unknown_id", lambda d: {**d, "ok": True}, "ok"),
        ("projects_two", lambda d: {**d, "projects": [{**d["projects"][0], "slot": 10}]}, "projects[0].slot"),
        ("projects_two", lambda d: {**d, "projects": [{**d["projects"][0], "name": "a\u202eb"}]}, "projects[0].name"),
    ],
    ids=[
        "audio-path",
        "queue-text",
        "owner-control",
        "t-negative",
        "undo-id",
        "undo-key",
        "undo-file-without-project",
        "host-control",
        "host-25",
        "result-ok",
        "field-echo",
        "error-ok",
        "slot",
        "name-bidi",
    ],
)
def test_document_rules(contract: cf.FilingContract, name: str, change: Callable[[dict], dict], field: str) -> None:
    case = _load(V1 / "vectors" / "valid" / f"{name}.json")
    data = change(copy.deepcopy(case["data"]))
    assert cf.validate(contract, case["doc"], data) == cf.Verdict("E_INVALID", (field,))


# --- check_all bites --------------------------------------------------------------------


def _errors(root: Path) -> tuple[str, ...]:
    return cf.check_all(root).errors


def _mentions(errors: tuple[str, ...], *words: str) -> bool:
    return any(all(w in e for w in words) for e in errors)


def test_edited_vector_breaks_the_digest(tree: Path) -> None:
    _rewrite(tree / "vectors" / "valid" / "ls_new_memo.json", lambda c: {**c, "name": "renamed"})
    assert _mentions(_errors(tree), "ls_new_memo.json", "VECTORS.sha256")


def test_unlisted_vector_is_an_error(tree: Path) -> None:
    shutil.copy(tree / "vectors" / "valid" / "ls_new_memo.json", tree / "vectors" / "valid" / "ls_copy.json")
    assert _mentions(_errors(tree), "ls_copy.json", "VECTORS.sha256")


def test_valid_vector_that_fails_is_an_error(tree: Path) -> None:
    path = tree / "vectors" / "valid" / "ls_new_memo.json"
    _rewrite(path, lambda c: {**c, "data": {**c["data"], "counts": {}}})
    digests = (tree / "VECTORS.sha256").read_text(encoding="utf-8")
    fresh = hashlib.sha256(path.read_bytes()).hexdigest()
    old = next(line.split()[0] for line in digests.splitlines() if line.endswith("ls_new_memo.json"))
    (tree / "VECTORS.sha256").write_text(digests.replace(old, fresh), encoding="utf-8")
    errors = _errors(tree)
    assert _mentions(errors, "ls_new_memo.json", "counts") and not _mentions(errors, "VECTORS.sha256")


def test_invalid_vector_with_wrong_reason_is_an_error(tree: Path) -> None:
    path = next(
        p for p in sorted((tree / "vectors" / "invalid").glob("*.json")) if _load(p)["reason"]["code"] == "E_INVALID"
    )
    _rewrite(path, lambda c: {**c, "reason": {"code": "E_INVALID", "field": "nowhere"}})
    assert _mentions(_errors(tree), path.name, "nowhere")


def test_missing_required_invalid_case_is_an_error(tree: Path) -> None:
    path = next(
        p for p in sorted((tree / "vectors" / "invalid").glob("*.json")) if _load(p)["reason"]["code"] == "E_VERSION"
    )
    path.unlink()
    assert _mentions(_errors(tree), "v=2")


def test_missing_unknown_value_case_is_an_error(tree: Path) -> None:
    for path in (tree / "vectors" / "valid").glob("*.json"):
        if cf.has_unknown_values(cf.load(tree), _load(path)):
            path.unlink()
    assert _mentions(_errors(tree), "unknown")


def test_malformed_vector_wrapper_is_an_error(tree: Path) -> None:
    _rewrite(tree / "vectors" / "valid" / "ls_new_memo.json", lambda c: {**c, "doc": "nonsense"})
    assert _mentions(_errors(tree), "ls_new_memo.json", "doc")


def test_invalid_vector_without_a_reason_is_an_error(tree: Path) -> None:
    path = sorted((tree / "vectors" / "invalid").glob("*.json"))[0]
    _rewrite(path, lambda c: {k: v for k, v in c.items() if k != "reason"})
    assert _mentions(_errors(tree), path.name, "reason")


def test_missing_schema_is_an_error(tree: Path) -> None:
    (tree / "schema" / "projects.schema.json").unlink()
    assert _mentions(_errors(tree), "projects", "missing")
