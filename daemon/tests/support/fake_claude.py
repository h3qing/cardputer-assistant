"""Test-side view of tests/fixtures/bin/fake-claude's state directory."""

from __future__ import annotations

import fcntl
import json
import os
from contextlib import contextmanager
from pathlib import Path

BIN = Path(__file__).resolve().parents[1] / "fixtures" / "bin" / "fake-claude"
# Without it a CoreFoundation-linked python3 (python.org's framework build) reads ~/.CFUserTextEncoding from the
# account's real home, found through getpwuid, whatever HOME says (#71).
CF_ENCODING = os.environ.get("__CF_USER_TEXT_ENCODING") or f"0x{os.getuid():X}:0x0:0x0"


class FakeClaude:
    def __init__(self, state_dir: Path):
        self.dir = state_dir
        self.dir.mkdir(parents=True, exist_ok=True)
        self.bin = str(BIN)
        self.write_state({"sessions": []})

    @property
    def env(self) -> dict[str, str]:
        """spawn_env for config.json: what cpad passes to every claude invocation."""
        return {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(self.dir), "LANG": "en_US.UTF-8",
                "FAKE_CLAUDE_DIR": str(self.dir), "__CF_USER_TEXT_ENCODING": CF_ENCODING}  # fmt: skip

    @contextmanager
    def _locked(self):
        # Same lock fake-claude takes, so a read-modify-write cannot interleave with its writes.
        with open(self.dir / "state.lock", "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            yield

    def _read(self) -> dict:
        return json.loads((self.dir / "state.json").read_text(encoding="utf-8"))

    def _write(self, doc: dict) -> None:
        tmp = self.dir / "state.json.tmp"
        tmp.write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")
        os.replace(tmp, self.dir / "state.json")

    def state(self) -> dict:
        with self._locked():
            return self._read()

    def write_state(self, doc: dict) -> None:
        with self._locked():
            self._write(doc)

    def update(self, **changes) -> None:
        with self._locked():
            self._write({**self._read(), **changes})

    def calls(self) -> list[dict]:
        path = self.dir / "calls.jsonl"
        lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
        return [json.loads(line) for line in lines]

    def argvs(self) -> list[list[str]]:
        return [c["argv"][1:] for c in self.calls()]

    def spawns(self) -> list[dict]:
        return [c for c in self.calls() if "--bg" in c["argv"] and "--resume" not in c["argv"]]

    def polls(self) -> int:
        return sum(1 for argv in self.argvs() if argv[:1] == ["agents"])


def bg_session(short: str, name: str, cwd: str, status: str = "idle", state: str = "blocked") -> dict:
    session_id = f"{short}-d3b8-4e21-a6f5-7c0e9b1d2f38"
    return {"pid": 1, "id": short, "cwd": cwd, "kind": "background", "startedAt": 1792455500000,
            "sessionId": session_id, "name": name, "status": status, "state": state}  # fmt: skip


def tty_session(sid: str, name: str, cwd: str, status: str = "idle") -> dict:
    return {"pid": 2, "cwd": cwd, "kind": "interactive", "name": name, "startedAt": 1792455000000,
            "sessionId": f"{sid}-4c2b-4a6e-9f10-2b7c5d8e3a61", "status": status}  # fmt: skip
