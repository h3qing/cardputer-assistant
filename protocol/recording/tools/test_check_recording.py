"""Tests for the recording file contract v1 (docs/11-m3-design.md §7.2-§7.7) and check_recording.py."""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
from collections.abc import Callable
from pathlib import Path

import check_recording as cr
import pytest

V1 = Path(__file__).resolve().parents[1] / "v1"
VALID = sorted((V1 / "vectors" / "valid").glob("*.json"))
INVALID = sorted((V1 / "vectors" / "invalid").glob("*.json"))
SID = "3QZK1M7-00042-0A1C"
SHA = "0123456789abcdef" * 4


@pytest.fixture(scope="module")
def contract() -> cr.RecordingContract:
    return cr.load(V1)


@pytest.fixture()
def tree(tmp_path: Path) -> Path:
    root = tmp_path / "v1"
    shutil.copytree(V1, root)
    return root


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _vector(name: str) -> dict:
    return _load(V1 / "vectors" / "valid" / f"{name}.json")["data"]


def _rewrite(path: Path, change: Callable[[dict], dict]) -> None:
    path.write_text(json.dumps(change(_load(path)), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _segment(i: int, samples: int = 16000) -> dict:
    return {
        "i": i, "file": f"seg-{i:03d}.wav", "id": SID, "t0_ms": i * 1000, "samples": samples,
        "bytes": 44 + 2 * samples, "sha256": SHA, "recovered": False, "repaired": False,
    }  # fmt: skip


def _manifest(segments: list[dict], gaps: list[int]) -> dict:
    return {**_vector("manifest_meeting"), "segments": segments, "gaps": gaps}


# --- the repo contract ----------------------------------------------------------------


def test_repo_contract_passes() -> None:
    report = cr.check_all(V1)
    assert report.errors == ()
    kinds = {kind for kind, valid, _ in report.counts if valid >= 1}
    assert kinds == {"manifest", "status", "transcript", "result"}


def test_main_prints_counts_and_exits_zero(capsys: pytest.CaptureFixture[str]) -> None:
    assert cr.main(["--root", str(V1)]) == 0
    out = capsys.readouterr().out
    assert "manifest" in out and "OK" in out


def test_main_exits_one_and_lists_errors(tree: Path, capsys: pytest.CaptureFixture[str]) -> None:
    (tree / "vectors" / "valid" / VALID[0].name).write_text("{", encoding="utf-8")
    assert cr.main(["--root", str(tree)]) == 1
    assert VALID[0].name in capsys.readouterr().out


@pytest.mark.parametrize("path", VALID, ids=[p.stem for p in VALID])
def test_valid_vector_is_accepted(contract: cr.RecordingContract, path: Path) -> None:
    case = _load(path)
    assert cr.validate(contract, case["doc"], case["data"]) == cr.Verdict("ok")


@pytest.mark.parametrize("path", INVALID, ids=[p.stem for p in INVALID])
def test_invalid_vector_is_rejected_for_its_reason(contract: cr.RecordingContract, path: Path) -> None:
    case = _load(path)
    reason = case["reason"]
    assert cr.validate(contract, case["doc"], case["data"]) == cr.Verdict(reason["code"], (reason["field"],))


def test_result_with_unknown_top_level_field_is_valid(contract: cr.RecordingContract) -> None:
    unknown = [
        p for p in VALID if _load(p)["doc"] == "result" and set(_load(p)["data"]) - cr.known_fields(contract, "result")
    ]
    assert unknown, "a valid result.json vector must carry an unknown top-level field"


@pytest.mark.parametrize(("doc", "code", "field", "label", "wanted"), cr.REQUIRED_INVALID, ids=lambda v: str(v))
def test_required_invalid_case_exists(doc: str, code: str, field: str, label: str, wanted: Callable) -> None:
    cases = [_load(p) for p in INVALID]
    reason = {"code": code, "field": field}
    assert any(c["doc"] == doc and c["reason"] == reason and wanted(c["data"]) for c in cases), label


# --- version and forward compatibility ------------------------------------------------


@pytest.mark.parametrize("version", [2, 0, "1", None])
def test_only_v1_is_read(contract: cr.RecordingContract, version: object) -> None:
    data = {**_vector("result_meeting_summary"), "v": version, "state": "whatever"}
    assert cr.validate(contract, "result", data) == cr.Verdict("E_VERSION", ("v",))


def test_missing_v_is_a_version_error(contract: cr.RecordingContract) -> None:
    data = {k: v for k, v in _vector("status_queued").items() if k != "v"}
    assert cr.validate(contract, "status", data).code == "E_VERSION"


@pytest.mark.parametrize("doc", ["manifest_meeting", "status_queued", "transcript_meeting", "result_meeting_summary"])
def test_unknown_fields_are_ignored_everywhere(contract: cr.RecordingContract, doc: str) -> None:
    case = _load(V1 / "vectors" / "valid" / f"{doc}.json")
    data = {**case["data"], "x_future": {"anything": [1, 2]}}
    assert cr.validate(contract, case["doc"], data) == cr.Verdict("ok")


def test_non_object_document_is_invalid(contract: cr.RecordingContract) -> None:
    assert cr.validate(contract, "manifest", [1]) == cr.Verdict("E_INVALID", ("(root)",))


def test_result_over_64_kib_is_invalid(contract: cr.RecordingContract) -> None:
    data = {**_vector("result_meeting_summary"), "x_padding": "a" * 65536}
    assert cr.validate(contract, "result", data) == cr.Verdict("E_INVALID", ("(size)",))


# --- manifest rules beyond the schema -------------------------------------------------


def test_gaps_complete_the_index_range(contract: cr.RecordingContract) -> None:
    data = _manifest([_segment(0), _segment(2)], [1, 3])
    assert cr.validate(contract, "manifest", data) == cr.Verdict("ok")


@pytest.mark.parametrize(
    ("segments", "gaps", "field"),
    [
        ([_segment(0), _segment(2)], [], "gaps"),
        ([_segment(0), _segment(1)], [1], "gaps[0]"),
        ([_segment(1), _segment(0)], [], "segments[1].i"),
        ([_segment(0), _segment(0)], [], "segments[1].i"),
        ([_segment(0), _segment(3)], [2, 1], "gaps[1]"),
        ([{**_segment(0), "file": "seg-001.wav"}], [], "segments[0].file"),
        ([{**_segment(0), "bytes": 44 + 2 * 16000 + 1}], [], "segments[0].bytes"),
    ],
    ids=["missing-gap", "gap-is-a-segment", "unordered", "duplicate", "unordered-gaps", "name-index", "bytes"],
)
def test_manifest_index_rules(
    contract: cr.RecordingContract, segments: list[dict], gaps: list[int], field: str
) -> None:
    assert cr.validate(contract, "manifest", _manifest(segments, gaps)) == cr.Verdict("E_INVALID", (field,))


@pytest.mark.parametrize("name", ["seg-000.wav\n", "seg-000.WAV", "./seg-000.wav", "seg-0.wav", "seg-000.wav.part"])
def test_segment_file_must_be_a_bare_seg_name(contract: cr.RecordingContract, name: str) -> None:
    data = _manifest([{**_segment(0), "file": name}], [])
    assert cr.validate(contract, "manifest", data) == cr.Verdict("E_INVALID", ("segments[0].file",))


def test_segment_index_above_999_uses_four_digits(contract: cr.RecordingContract) -> None:
    data = _manifest([_segment(i) for i in range(1001)], [])
    assert cr.validate(contract, "manifest", data) == cr.Verdict("ok")


# --- the env stamp (docs/23 §4.2): optional, additive ------------------------------------


def test_env_is_optional_and_may_be_null(contract: cr.RecordingContract) -> None:
    data = _vector("manifest_memo")
    assert "env" not in data and cr.validate(contract, "manifest", data) == cr.Verdict("ok")
    assert cr.validate(contract, "manifest", {**data, "env": None}) == cr.Verdict("ok")


@pytest.mark.parametrize("link", ["here", "unknown", "other"])
def test_env_link_is_an_extensible_enum(contract: cr.RecordingContract, link: str) -> None:
    data = {**_vector("manifest_memo"), "env": {"host": "八个汉字的主机名", "link": link}}
    assert cr.validate(contract, "manifest", data) == cr.Verdict("ok")


@pytest.mark.parametrize(
    ("env", "field"),
    [
        ({"host": "", "link": "here"}, "env.host"),
        ({"host": "a" * 25, "link": "here"}, "env.host"),
        ({"host": "九个汉字的主机名称", "link": "here"}, "env.host"),
        ({"host": "a\u007fb", "link": "here"}, "env.host"),
        ({"link": "here"}, "env.host"),
        ({"host": None, "link": "Here"}, "env.link"),
        ({"host": None}, "env.link"),
        ("here", "env"),
    ],
    ids=["empty-host", "host-25-bytes", "host-27-utf8-bytes", "host-del", "no-host", "link-case", "no-link", "string"],
)
def test_env_rules(contract: cr.RecordingContract, env: object, field: str) -> None:
    data = {**_vector("manifest_memo"), "env": env}
    assert cr.validate(contract, "manifest", data) == cr.Verdict("E_INVALID", (field,))


# --- result and status rules -----------------------------------------------------------


@pytest.mark.parametrize(
    ("change", "field"),
    [
        (lambda d: {**d, "state": "failed", "error": None}, "error"),
        (lambda d: {k: v for k, v in d.items() if k != "level"}, "level"),
        (lambda d: {**d, "outputs": {k: v for k, v in d["outputs"].items() if k != "summary"}}, "outputs.summary"),
        (
            lambda d: {**d, "outputs": {**d["outputs"], "summary": {"path": "..", "sha256": SHA}}},
            "outputs.summary.path",
        ),
        (lambda d: {**d, "actions": [{**d["actions"][0], "text": "x" * 201}]}, "actions[0].text"),
        (lambda d: {**d, "finished_at": "2026-10-21 14:49:02"}, "finished_at"),
    ],
    ids=["failed-needs-error", "done-needs-level", "summary-needs-file", "dotdot", "action-text", "timestamp"],
)
def test_result_rules(contract: cr.RecordingContract, change: Callable[[dict], dict], field: str) -> None:
    data = change(copy.deepcopy(_vector("result_meeting_summary")))
    assert cr.validate(contract, "result", data) == cr.Verdict("E_INVALID", (field,))


def test_blocked_status_needs_a_reason(contract: cr.RecordingContract) -> None:
    data = {k: v for k, v in _vector("status_blocked").items() if k != "reason"}
    assert cr.validate(contract, "status", data) == cr.Verdict("E_INVALID", ("reason",))


# --- check_all bites --------------------------------------------------------------------


def _errors(root: Path) -> tuple[str, ...]:
    return cr.check_all(root).errors


def _mentions(errors: tuple[str, ...], *words: str) -> bool:
    return any(all(w in e for w in words) for e in errors)


def test_edited_vector_breaks_the_digest(tree: Path) -> None:
    _rewrite(tree / "vectors" / "valid" / "status_queued.json", lambda c: {**c, "name": "renamed"})
    assert _mentions(_errors(tree), "status_queued.json", "VECTORS.sha256")


def test_unlisted_vector_is_an_error(tree: Path) -> None:
    shutil.copy(tree / "vectors" / "valid" / "status_queued.json", tree / "vectors" / "valid" / "status_copy.json")
    assert _mentions(_errors(tree), "status_copy.json", "VECTORS.sha256")


def test_valid_vector_that_fails_is_an_error(tree: Path) -> None:
    path = tree / "vectors" / "valid" / "status_queued.json"
    _rewrite(path, lambda c: {**c, "data": {**c["data"], "state": "sleeping"}})
    digests = (tree / "VECTORS.sha256").read_text(encoding="utf-8")
    fresh = hashlib.sha256(path.read_bytes()).hexdigest()
    old = next(line.split()[0] for line in digests.splitlines() if line.endswith("status_queued.json"))
    (tree / "VECTORS.sha256").write_text(digests.replace(old, fresh), encoding="utf-8")
    errors = _errors(tree)
    assert _mentions(errors, "status_queued.json", "state") and not _mentions(errors, "VECTORS.sha256")


def test_invalid_vector_with_wrong_reason_is_an_error(tree: Path) -> None:
    path = next(p for p in (tree / "vectors" / "invalid").glob("*.json") if _load(p)["reason"]["code"] == "E_INVALID")
    _rewrite(path, lambda c: {**c, "reason": {"code": "E_INVALID", "field": "nowhere"}})
    assert _mentions(_errors(tree), path.name, "nowhere")


def test_missing_required_invalid_case_is_an_error(tree: Path) -> None:
    path = next(p for p in (tree / "vectors" / "invalid").glob("*.json") if _load(p)["reason"]["code"] == "E_VERSION")
    path.unlink()
    assert _mentions(_errors(tree), "v=2")


def test_malformed_vector_wrapper_is_an_error(tree: Path) -> None:
    _rewrite(tree / "vectors" / "valid" / "status_queued.json", lambda c: {**c, "doc": "nonsense"})
    assert _mentions(_errors(tree), "status_queued.json", "doc")
