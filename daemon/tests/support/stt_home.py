"""A temp $CPA_HOME and notes folder for cpa-stt command-line tests, with every outside program faked:
sandbox-exec (a stand-in), mdfind (answers from `spotlight`), launchctl (recorded), the y/N question
(answers from `answers`) and the known model folders."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from cpa_stt import cli, config
from cpad.config.settings import Config, save_config
from cpad.service.launchd import Completed
from tests.support import stt_tree
from tests.support.stt_tree import fake_argv

DEAD_PID = 2**22 + 4321


class Home:
    def __init__(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        self.answers: list[str] = []
        self.known = tmp_path / "known-models"
        self.home = tmp_path / "cpa-home"
        self.notes = tmp_path / "notes"
        self.root = self.notes / "recordings"
        self.root.mkdir(parents=True)
        self.home.mkdir()
        monkeypatch.setenv("CPA_HOME", str(self.home))
        save_config(self.home / "config.json", Config(notes_dir=str(self.notes)))
        self.paths = config.SttPaths.at(self.home)
        self.sandbox = stt_tree.fake_sandbox(tmp_path)
        self.calls: list[list[str]] = []
        self.spotlight: dict[str, str] = {}
        self.agents = tmp_path / "LaunchAgents"
        self.tmp = tmp_path

    def deps(self, **kw) -> cli.CliDeps:
        def launchctl(argv, **_):
            self.calls.append(list(argv))
            return Completed(0, "")

        def run_cmd(argv):
            self.calls.append(list(argv))
            return self.spotlight.get(argv[-1], "") if argv[0].endswith("mdfind") else ""

        base = {"sandbox_exec": str(self.sandbox), "which": lambda name: None, "run_cmd": run_cmd,
                "launchctl": launchctl, "agents_dir": self.agents, "uid": 501,
                "alive": lambda pid: pid != DEAD_PID, "ask": lambda prompt: self.answers.pop(0) if self.answers else "",
                "model_dirs": (self.known,)}  # fmt: skip
        return cli.CliDeps(**{**base, **kw})

    def main(self, *argv: str, **kw) -> int:
        return cli.main(list(argv), self.deps(**kw))

    def turn_on(self, *flags: str) -> None:
        argv = fake_argv(*flags)
        config.save_config(self.paths.config, config.parse_config({"provider": "command", "argv": argv}))

    def config_file(self, *flags: str, **doc) -> Path:
        path = self.tmp / "mine.json"
        path.write_text(json.dumps({"argv": fake_argv(*flags), **doc}), encoding="utf-8")
        return path
