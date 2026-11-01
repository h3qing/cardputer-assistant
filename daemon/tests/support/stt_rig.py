"""The cpa-stt test rig (docs/22 §4): a temp notes folder with recordings/, a temp $CPA_HOME, the fake
transcriber command behind a stand-in for sandbox-exec, and a clock the tests move."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path

from cpa_stt import config, loop, runner
from cpad.recordings import contract
from tests.support import stt_tree
from tests.support.stt_tree import fake_argv, read_json

T0 = 1_792_700_000.0


@dataclass
class Clock:
    t: float = T0

    def __call__(self) -> float:
        return self.t


@dataclass
class Rig:
    root: Path
    paths: config.SttPaths
    deps: loop.Deps
    clock: Clock
    log: Path
    calls: list = field(default_factory=list)

    def configure(self, *flags, **doc):
        argv = fake_argv("--log", str(self.log), *flags)
        config.save_config(self.paths.config, config.parse_config({"provider": "command", "argv": argv, **doc}))

    def once(self):
        return loop.once(self.deps)

    def runs(self) -> list[int]:
        """Samples per command call, in order (the self-test is 16000 samples of silence)."""
        return [int(x) for x in self.log.read_text().split()]


def make_rig(tmp_path: Path) -> Rig:
    notes = tmp_path / "notes"
    root = notes / "recordings"
    root.mkdir(parents=True)
    paths = config.SttPaths.at(tmp_path / "home")
    env = runner.Env(str(stt_tree.fake_sandbox(tmp_path)), None, (), str(tmp_path))
    clock = Clock()
    deps = loop.Deps(paths=paths, notes_dir=notes, env=env, clock=clock, pid=os.getpid(), alive=lambda pid: True)
    (tmp_path / "calls.log").touch()  # an absolute path in argv must exist (cpa-stt checks them)
    return Rig(root, paths, deps, clock, tmp_path / "calls.log")


def put_claim(root: Path, app: str, beat_age: float, pid: int = 77) -> None:
    """Another transcriber's claim whose last beat was `beat_age` seconds before T0."""
    stamp = loop.iso(T0 - beat_age)
    doc = {"v": 1, "app": app, "pid": pid, "since": stamp, "beat_at": stamp}
    (root / ".transcriber").write_text(json.dumps(doc))


def failed(folder: Path) -> dict:
    doc = read_json(folder / "result.json")
    assert contract.check_result(doc).ok and doc["state"] == "failed"
    return doc["error"]
