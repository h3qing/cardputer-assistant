"""`python -m cpad mcp serve` as a real child process (docs/15 §3.2, §8; ADR 0004 rules 2, 4, 6).

Both golden transcripts over stdio against a temporary $CPA_HOME and archive: every stdout line is
JSON, the child holds no inet socket, it exits within 2 s of stdin closing, it writes nothing outside
$CPA_HOME/logs/, and the canary placed in notes, summaries, captures and queries never reaches the audit
log or stderr (while it does reach the client, so the data really was read).
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

from tests.support import mcp_home
from tests.support.mcp_home import CANARY

DAEMON = Path(__file__).resolve().parents[2]


def _snapshot(*roots: Path, skip: Path) -> dict[str, tuple[int, int, int]]:
    out = {}
    for root in roots:
        for path in sorted(root.rglob("*")):
            if skip in path.parents or path == skip:
                continue
            st = path.lstat()
            out[str(path)] = (st.st_mode, st.st_size, st.st_mtime_ns)
    return out


@pytest.fixture
def paths(short_tmp):
    return mcp_home.build(short_tmp)


def _spawn(paths):
    env = {**os.environ, "CPA_HOME": str(paths.home), "PYTHONUNBUFFERED": "1"}
    return subprocess.Popen([sys.executable, "-m", "cpad", "mcp", "serve"], cwd=DAEMON, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)  # fmt: skip


def _requests(transcript: str) -> list[dict]:
    steps = mcp_home.golden(transcript)
    search = {"jsonrpc": "2.0", "id": 99, "method": "tools/call",
              "params": {"name": "search", "arguments": {"query": CANARY}}}  # fmt: skip
    if transcript == "golden-modern.jsonl":
        search["params"]["_meta"] = steps[0]["request"]["params"]["_meta"]
    return [s["request"] for s in steps] + [search]


def _inet_sockets(pid: int) -> str:
    lsof = shutil.which("lsof") or "/usr/sbin/lsof"
    assert Path(lsof).exists(), "lsof is needed to prove the server opens no inet socket"
    done = subprocess.run([lsof, "-a", "-p", str(pid), "-i"], capture_output=True, text=True, check=False)
    return done.stdout.strip()


def _write_and_close(stdin, data: bytes) -> float:
    stdin.write(data)
    stdin.close()
    return time.monotonic()


@pytest.mark.parametrize("transcript", ["golden-legacy.jsonl", "golden-modern.jsonl"])
def test_golden_session_over_stdio(paths, transcript):
    before = _snapshot(paths.home, paths.home.parent / "notes", skip=paths.logs)
    requests = _requests(transcript)
    child = _spawn(paths)
    child.stdin.write(json.dumps(requests[0]).encode() + b"\n")
    child.stdin.flush()
    first = json.loads(child.stdout.readline())
    assert first["id"] == requests[0]["id"]
    assert _inet_sockets(child.pid) == ""
    tail = b"".join(json.dumps(r, ensure_ascii=False).encode() + b"\n" for r in requests[1:])
    # Feed stdin while reading stdout: the ~44 KB of replies can outgrow a pipe macOS left small, and a write that
    # waits for the child to drain stdin deadlocks against a child blocked writing those replies.
    with ThreadPoolExecutor(max_workers=1) as pool:
        closing = pool.submit(_write_and_close, child.stdin, tail)
        rest = child.stdout.read()
        closed = closing.result(timeout=5)
    child.wait(timeout=5)
    assert child.returncode == 0 and time.monotonic() - closed < 2.0
    replies = [first] + [json.loads(line) for line in rest.splitlines()]
    assert [r["id"] for r in replies] == [r["id"] for r in requests if "id" in r]
    assert CANARY.encode() in rest
    stderr = child.stderr.read()
    audit = (paths.logs / "mcp-audit.jsonl").read_bytes()
    assert CANARY.encode() not in stderr and CANARY.encode() not in audit
    assert _snapshot(paths.home, paths.home.parent / "notes", skip=paths.logs) == before
    assert (paths.logs / "mcp-audit.jsonl").stat().st_mode & 0o777 == 0o600
    assert paths.logs.stat().st_mode & 0o777 == 0o700


def test_audit_names_the_client_and_never_the_query(paths):
    child = _spawn(paths)
    out, _ = child.communicate(b"".join(json.dumps(r).encode() + b"\n" for r in _requests("golden-legacy.jsonl")),
                               timeout=10)  # fmt: skip
    assert out
    records = [json.loads(line) for line in (paths.logs / "mcp-audit.jsonl").read_text().splitlines()]
    search = next(r for r in records if r.get("tool") == "search" and r.get("qlen") == len(CANARY))
    assert search["client"]["name"] == "golden-legacy" and search["era"] == "legacy" and search["args"] == ["query"]
    assert records[0]["event"] == "start" and records[0]["write_tools"] == [] and records[-1]["event"] == "stop"


def test_bad_mcp_json_refuses_to_start(paths):
    (paths.home / "mcp.json").write_text('{"version": 1, "write_tools": ["delete_everything"]}')
    child = _spawn(paths)
    out, err = child.communicate(b"", timeout=10)
    assert child.returncode == 2 and out == b"" and b"unknown tool" in err


def test_stray_prints_cannot_reach_the_protocol_channel(paths):
    env = {**os.environ, "CPA_HOME": str(paths.home)}
    code = ("import sys; from cpad.mcp import server; out = server._protocol_out(); print('stray'); "
            "sys.stdout.flush(); import os; os.write(1, b'fd1\\n'); out.write(b'proto\\n'); out.flush()")  # fmt: skip
    done = subprocess.run([sys.executable, "-c", code], cwd=DAEMON, env=env, capture_output=True, check=False)
    assert done.stdout == b"proto\n" and b"stray" in done.stderr and b"fd1" in done.stderr
