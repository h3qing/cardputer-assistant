"""The MCP audit log (docs/15 §3.9): one content-free line per call, 0600, rotated, fail closed."""

from __future__ import annotations

import json
import os
import subprocess

import pytest

from cpad.mcp import audit
from cpad.mcp.audit import AuditError, AuditLog, Call, Proc
from cpad.mcp.era import ClientInfo

PROC = Proc(pid=10, ppid=9, parent="codex")


def test_record_shape():
    call = Call(method="tools/call", era="legacy", client=ClientInfo("codex-mcp-client", "0.98.0"),
                tool="list_recordings", args=("limit", "from"), ids=("2026-10-01",), n=12, bytes=5310,
                sizes={"qlen": 4})  # fmt: skip
    rec = audit.record(call, PROC, at=0.0, ms=18.4)
    assert rec["pid"] == 10 and rec["ppid"] == 9 and rec["parent"] == "codex"
    assert rec["client"] == {"name": "codex-mcp-client", "version": "0.98.0"}
    assert rec["args"] == ["from", "limit"] and rec["ids"] == ["2026-10-01"]
    assert (rec["n"], rec["bytes"], rec["ms"], rec["error"], rec["qlen"]) == (12, 5310, 18, None, 4)
    assert rec["at"].startswith("1970-01-01T") or rec["at"].startswith("1969-12-31T")


def test_record_caps_ids():
    rec = audit.record(Call("tools/call", ids=tuple(f"2026-10-{d:02d}" for d in range(1, 20))), PROC, 0.0, 1.0)
    assert len(rec["ids"]) == audit.MAX_IDS


def test_start_and_stop_records():
    start = audit.start_record(PROC, 0.0, "0.1.0", frozenset({"queue_add"}), frozenset())
    assert start["event"] == "start" and start["write_tools"] == ["queue_add"] and start["version"] == "0.1.0"
    stop = audit.stop_record(PROC, 1.0, calls=7)
    assert stop["event"] == "stop" and stop["calls"] == 7


def test_append_creates_private_dir_and_file(tmp_path):
    log = AuditLog(tmp_path / "logs" / "mcp-audit.jsonl")
    log.append({"a": 1})
    log.append({"a": "汉"})
    lines = log.path.read_text().splitlines()
    assert [json.loads(x) for x in lines] == [{"a": 1}, {"a": "汉"}]
    assert log.path.stat().st_mode & 0o777 == 0o600
    assert log.path.parent.stat().st_mode & 0o777 == 0o700


def test_append_tightens_a_loose_file(tmp_path):
    log = AuditLog(tmp_path / "mcp-audit.jsonl")
    log.path.write_text("")
    log.path.chmod(0o644)
    log.append({"a": 1})
    assert log.path.stat().st_mode & 0o777 == 0o600


def test_rotation_keeps_five(tmp_path):
    log = AuditLog(tmp_path / "mcp-audit.jsonl", max_bytes=200, keep=5)
    for i in range(60):
        log.append({"i": i, "pad": "x" * 40})
    names = sorted(p.name for p in tmp_path.iterdir())
    assert names == ["mcp-audit.jsonl", *[f"mcp-audit.jsonl.{n}" for n in range(1, 6)]]
    assert all(p.stat().st_size <= 200 for p in tmp_path.iterdir())
    assert json.loads(log.path.read_text().splitlines()[-1])["i"] == 59


def test_unwritable_audit_raises(tmp_path):
    (tmp_path / "logs").write_text("not a directory")
    with pytest.raises(AuditError):
        AuditLog(tmp_path / "logs" / "mcp-audit.jsonl").append({"a": 1})


def test_symlinked_audit_file_is_refused(tmp_path):
    target = tmp_path / "elsewhere.jsonl"
    target.write_text("")
    (tmp_path / "mcp-audit.jsonl").symlink_to(target)
    with pytest.raises(AuditError):
        AuditLog(tmp_path / "mcp-audit.jsonl").append({"a": 1})
    assert target.read_text() == ""


def test_tail_reads_the_last_lines_across_one_rotation(tmp_path):
    log = AuditLog(tmp_path / "mcp-audit.jsonl", max_bytes=120)
    for i in range(6):
        log.append({"i": i, "pad": "x" * 30})
    assert [r["i"] for r in audit.tail(log.path, 3)] == [3, 4, 5]
    assert [r["i"] for r in audit.tail(log.path, 100)][-1] == 5
    assert audit.tail(tmp_path / "missing.jsonl", 5) == ()


def test_tail_skips_unreadable_lines(tmp_path):
    path = tmp_path / "mcp-audit.jsonl"
    path.write_text('{"i": 1}\nnot json\n[1]\n{"i": 2}\n')
    assert audit.tail(path, 10) == ({"i": 1}, {"i": 2})


def test_proc_info_names_the_parent(monkeypatch):
    def runner(cmd, **kwargs):
        assert cmd == ["ps", "-o", "comm=", "-p", str(os.getppid())]
        return subprocess.CompletedProcess(cmd, 0, stdout="/Applications/Codex.app/Contents/MacOS/codex\n")

    info = audit.proc_info(runner)
    assert info == Proc(os.getpid(), os.getppid(), "codex")


def test_proc_info_survives_a_failing_ps():
    def runner(cmd, **kwargs):
        raise OSError("no ps")

    assert audit.proc_info(runner).parent is None


def test_proc_info_bounds_the_name():
    def runner(cmd, **kwargs):
        return subprocess.CompletedProcess(cmd, 0, stdout="x\n" + "y" * 500)

    assert len(audit.proc_info(runner).parent) <= 64
