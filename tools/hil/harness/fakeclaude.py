"""The harness's side of daemon/tests/fixtures/bin/fake-claude (its state directory, $FAKE_CLAUDE_DIR).

state.json holds the `agents --json` list the fake prints; every invocation is appended to
calls.jsonl as {"argv", "cwd", "env"}. Writes are atomic (temp file + rename) because the
fake reads the file from the daemon's poller at any moment.
"""

from __future__ import annotations

import json
import os
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

BIN = Path(__file__).resolve().parents[3] / "daemon" / "tests" / "fixtures" / "bin" / "fake-claude"
# Without it a CoreFoundation-linked python3 reads ~/.CFUserTextEncoding from the account's real home (#71).
CF_ENCODING = os.environ.get("__CF_USER_TEXT_ENCODING") or f"0x{os.getuid():X}:0x0:0x0"


def tty_session(sid: str, name: str, cwd: str, status: str = "idle") -> dict[str, Any]:
    return {"pid": 2, "cwd": cwd, "kind": "interactive", "name": name, "startedAt": 1792455000000,
            "sessionId": f"{sid}-4c2b-4a6e-9f10-2b7c5d8e3a61", "status": status}  # fmt: skip


def bg_session(short: str, name: str, cwd: str, status: str = "idle", state: str = "blocked") -> dict[str, Any]:
    return {"pid": 1, "id": short, "cwd": cwd, "kind": "background", "startedAt": 1792455500000,
            "sessionId": f"{short}-d3b8-4e21-a6f5-7c0e9b1d2f38", "name": name, "status": status,
            "state": state}  # fmt: skip


@dataclass(frozen=True)
class FakeClaudeDir:
    path: Path

    @classmethod
    def create(cls, path: Path) -> FakeClaudeDir:
        path.mkdir(mode=0o700, parents=True, exist_ok=True)
        fake = cls(path)
        fake.write_state({"sessions": []})
        return fake

    def env(self, path_var: str) -> dict[str, str]:
        """spawn_env for config.json: fixtures/bin first on PATH, HOME inside the temp dir."""
        return {"PATH": f"{BIN.parent}:{path_var}", "HOME": str(self.path), "LANG": "en_US.UTF-8",
                "FAKE_CLAUDE_DIR": str(self.path), "__CF_USER_TEXT_ENCODING": CF_ENCODING}  # fmt: skip

    def state(self) -> dict[str, Any]:
        return json.loads((self.path / "state.json").read_text(encoding="utf-8"))

    def write_state(self, doc: Mapping[str, Any]) -> None:
        tmp = self.path / "state.json.tmp"
        tmp.write_text(json.dumps(doc, ensure_ascii=False), encoding="utf-8")
        os.replace(tmp, self.path / "state.json")

    def set_sessions(self, sessions: Iterable[Mapping[str, Any]]) -> None:
        self.write_state({**self.state(), "sessions": [dict(s) for s in sessions]})

    def calls(self) -> tuple[dict[str, Any], ...]:
        path = self.path / "calls.jsonl"
        lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
        return tuple(json.loads(line) for line in lines if line.strip())

    def spawns(self) -> tuple[dict[str, Any], ...]:
        return tuple(c for c in self.calls() if "--bg" in c["argv"] and "--resume" not in c["argv"])
