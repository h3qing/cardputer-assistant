"""ResultWatcher (docs/11 §6.6) and the cpad reader of result.json / status.json (protocol/recording/v1)."""

from __future__ import annotations

import json
import os

from cpad.recordings import contract, index
from cpad.recordings import records as r
from cpad.recordings.results import ResultWatcher

SID = "K3M9Q2T-00042-0A1C"
DIR = f"recordings/2026-10-21/1402-{SID.lower()}"
ARCHIVED_AT = "2026-10-21T06:45:10.000+00:00"
ARCHIVED_MS = 1792565110000
HOUR_MS = 3_600_000
SHA = "a" * 64


def result_doc(**over) -> dict:
    doc = {"v": 1, "session": SID, "attempt": 1, "state": "done", "level": "summary", "mode": "meeting",
           "title": "客户同步", "memo": None, "actions": [{"n": 1, "text": "x", "confident": True}],
           "outputs": {"transcript_json": {"path": "transcript.json", "sha256": SHA},
                       "transcript_md": {"path": "transcript.md", "sha256": SHA},
                       "summary": {"path": "summary.md", "sha256": SHA}},
           "error": None, "finished_at": "2026-10-21T14:49:02.000+08:00"}  # fmt: skip
    return {**doc, **over}


FAILED = {"state": "failed", "level": None, "error": {"code": "E_STT", "retryable": False}}


def status_doc(**over) -> dict:
    return {"v": 1, "session": SID, "attempt": 1, "state": "transcribing", "at": "2026-10-21T14:46:00+08:00", **over}


def archived_index() -> index.RecIndex:
    return index.fold([r.archived(dev="020000000001", rec=SID, dir=DIR, mode="meeting", project="cpa", segments=3,
                                  dur_ms=2520000, started_ms=1792620120000, uncertain=False, gaps=[], consent="ack",
                                  at=ARCHIVED_AT)])  # fmt: skip


def write(tmp_path, name: str, doc) -> None:
    path = tmp_path / DIR / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(doc if isinstance(doc, str) else json.dumps(doc), encoding="utf-8")


# --- the reader ---------------------------------------------------------------------------


def test_unknown_fields_are_ignored_and_v2_is_a_version_error():
    assert contract.check_result(result_doc(x_new={"a": 1}, pipeline={"app": "WW/9", "gpu": True})).ok
    assert contract.check_result(result_doc(v=2)) == contract.Verdict("E_VERSION", ("v",))
    assert contract.check_result(result_doc(v="1")).code == "E_VERSION"
    assert contract.check_result([1]) == contract.Verdict("E_INVALID", ("(root)",))


def test_malicious_results_are_rejected_on_the_offending_field():
    outputs = result_doc()["outputs"]
    cases = {
        "outputs.summary.path": result_doc(
            outputs={**outputs, "summary": {"path": "../../2026-10-21.md", "sha256": SHA}}
        ),  # fmt: skip
        "outputs.transcript_md.path": result_doc(
            outputs={**outputs, "transcript_md": {"path": "/etc/passwd", "sha256": SHA}}
        ),  # fmt: skip
        "title": result_doc(title="x" * 81),
        "actions": result_doc(actions=[{"n": 1, "text": "x", "confident": True}] * 21),
        "memo.text": result_doc(mode="memo", memo={"text": "y" * 2001, "intent": "note"}),
        "error": result_doc(error={"code": "E_STT", "retryable": False}),
        "session": result_doc(session="../x"),
        "actions[0].confident": result_doc(actions=[{"n": 1, "text": "x", "confident": "yes"}]),
    }
    for field, doc in cases.items():
        assert contract.check_result(doc) == contract.Verdict("E_INVALID", (field,)), field
    assert contract.check_result(result_doc(**FAILED)).ok
    assert contract.check_result(result_doc(state="failed", level=None, error=None)).fields == ("error",)


def test_status_reader():
    assert contract.check_status(status_doc()).ok
    assert contract.check_status(status_doc(state="blocked", reason="model_missing")).ok
    assert contract.check_status(status_doc(state="blocked")).fields == ("reason",)
    assert contract.check_status(status_doc(state="done")).fields == ("state",)


# --- the watcher --------------------------------------------------------------------------


def test_failed_then_done_after_a_manual_rerun_appends_each_state_once(tmp_path):
    watcher, idx = ResultWatcher(tmp_path, wait_h=24), archived_index()
    assert watcher.scan(idx, ARCHIVED_MS) == ()
    write(tmp_path, "result.json", result_doc(**FAILED))
    out = watcher.scan(idx, ARCHIVED_MS)
    assert [(e["ev"], e["state"], e["error_code"], e["dir"]) for e in out] == [("rec.result", "failed", "E_STT", DIR)]
    idx = index.fold(out, idx)
    assert ResultWatcher(tmp_path, wait_h=24).scan(idx, ARCHIVED_MS) == ()  # a restart does not repeat it
    os.unlink(tmp_path / DIR / "result.json")
    assert watcher.scan(idx, ARCHIVED_MS) == ()
    write(tmp_path, "result.json", result_doc())
    os.utime(tmp_path / DIR / "result.json", (1, 2))
    done = watcher.scan(idx, ARCHIVED_MS)
    assert [(e["state"], e["level"], e["actions_n"], e["title"]) for e in done] == [("done", "summary", 1, "客户同步")]
    idx = index.fold(done, idx)
    write(tmp_path, "result.json", result_doc(**FAILED))
    os.utime(tmp_path / DIR / "result.json", (3, 4))
    assert watcher.scan(idx, ARCHIVED_MS) == ()  # done sessions are no longer watched


def test_memo_result_carries_text_and_intent(tmp_path):
    write(tmp_path, "result.json", result_doc(mode="memo", memo={"text": "待办：发报价", "intent": "todo"}, actions=[]))
    (event,) = ResultWatcher(tmp_path, wait_h=24).scan(archived_index(), ARCHIVED_MS)
    assert (event["memo_text"], event["memo_intent"], event["actions_n"]) == ("待办：发报价", "todo", 0)


def test_rejected_results_append_nothing(tmp_path):
    for doc in (result_doc(v=2), result_doc(session="K3M9Q2T-00042-0A1D"), result_doc(title="x" * 81), "{oops", "[]"):
        write(tmp_path, "result.json", doc)
        assert ResultWatcher(tmp_path, wait_h=24).scan(archived_index(), ARCHIVED_MS) == (), doc


def test_oversized_and_symlinked_results_are_not_read(tmp_path):
    write(tmp_path, "result.json", result_doc(pipeline={"app": "a", "pad": "z" * 70000}))
    assert ResultWatcher(tmp_path, wait_h=24).scan(archived_index(), ARCHIVED_MS) == ()
    os.unlink(tmp_path / DIR / "result.json")
    (tmp_path / "elsewhere.json").write_text(json.dumps(result_doc()))
    os.symlink(tmp_path / "elsewhere.json", tmp_path / DIR / "result.json")
    assert ResultWatcher(tmp_path, wait_h=24).scan(archived_index(), ARCHIVED_MS) == ()


def test_status_is_kept_in_memory_only(tmp_path):
    watcher = ResultWatcher(tmp_path, wait_h=24)
    write(tmp_path, "status.json", status_doc(state="blocked", reason="busy"))
    assert watcher.scan(archived_index(), ARCHIVED_MS) == ()
    assert watcher.statuses() == {SID: ("blocked", "busy")}
    os.unlink(tmp_path / DIR / "status.json")
    watcher.scan(archived_index(), ARCHIVED_MS)
    assert watcher.statuses() == {}


def test_stale_after_transcribe_wait_h_without_a_result(tmp_path):
    watcher, idx = ResultWatcher(tmp_path, wait_h=24), archived_index()
    assert watcher.scan(idx, ARCHIVED_MS + 23 * HOUR_MS) == ()
    (stale,) = watcher.scan(idx, ARCHIVED_MS + 24 * HOUR_MS)
    assert (stale["ev"], stale["rec"], stale["dir"], stale["mode"]) == ("rec.stale", SID, DIR, "meeting")
    idx = index.fold([stale], idx)
    assert watcher.scan(idx, ARCHIVED_MS + 48 * HOUR_MS) == ()
    write(tmp_path, "result.json", result_doc())
    assert [e["state"] for e in watcher.scan(idx, ARCHIVED_MS + 49 * HOUR_MS)] == ["done"]


def test_rejections_log_codes_and_paths_never_content(tmp_path, caplog):
    caplog.set_level("WARNING", logger="cpad.rec")
    for doc in (json.dumps("SECRET transcript text"), result_doc(title="SECRET " * 20)):
        write(tmp_path, "result.json", doc)
        assert ResultWatcher(tmp_path, wait_h=24).scan(archived_index(), ARCHIVED_MS) == ()
    fields = [getattr(r, "fields", {}) for r in caplog.records]
    assert [f["field"] for f in fields] == [["(root)"], ["title"]]
    assert "SECRET" not in caplog.text and all("SECRET" not in str(f) for f in fields)


def test_results_and_stale_lines_carry_the_effective_project(tmp_path):
    """docs/23 §2.4: filed before the result, the line the result writes already shows the new project."""
    watcher, idx = ResultWatcher(tmp_path, wait_h=24), archived_index()
    (stale,) = watcher.scan(idx, ARCHIVED_MS + 24 * HOUR_MS, projects={SID: "blog"})
    assert stale["project"] == "blog"
    write(tmp_path, "result.json", result_doc())
    (done,) = watcher.scan(idx, ARCHIVED_MS, projects={SID: None})
    assert done["project"] is None and done["dir"] == DIR
    assert archived_index().archived[SID]["project"] == "cpa", "the archived record is not changed"
    os.utime(tmp_path / DIR / "result.json", (5, 6))
    assert ResultWatcher(tmp_path, wait_h=24).scan(idx, ARCHIVED_MS)[0]["project"] == "cpa", "no mapping: recorded"
