"""cpa-stt use with the whisper.cpp example and cpa-stt setup (docs/22 §4.4, §4.13): filling the example,
finding, checking and linking models. Never copies or downloads; outside programs are fakes."""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from cpa_stt import config
from tests.support.stt_home import Home


@pytest.fixture
def home(tmp_path, monkeypatch):
    return Home(tmp_path, monkeypatch)


def _whisper_stand_in(home: Home) -> Path:
    path = home.tmp / "bin" / "whisper-cli"
    path.parent.mkdir()
    path.write_text("#!/bin/sh\nexit 0\n")
    path.chmod(0o755)
    return path


def _model_file(directory: Path, name: str, size: int = 10) -> Path:
    """A ggml model as far as cpa-stt checks: a regular file of ours, not writable by others, magic lmgg."""
    directory.mkdir(parents=True, exist_ok=True)
    directory.chmod(0o755)
    (directory / name).write_bytes(b"lmgg" + b"m" * size)
    (directory / name).chmod(0o644)
    return directory / name


def _example_inputs(home: Home) -> Path:
    whisper = _whisper_stand_in(home)
    _model_file(home.paths.models, "ggml-large-v3-turbo.bin")
    return whisper


def test_use_without_a_file_fills_the_whisper_example(home):
    whisper = _whisper_stand_in(home)
    home.paths.models.mkdir(parents=True)
    (home.paths.models / "ggml-large-v3-turbo.bin").write_bytes(b"m")
    projects = {"version": 1, "projects": [{"key": "cpa", "name": "Cardputer", "slot": 1, "root": str(home.tmp)}]}
    (home.home / "projects.json").write_text(json.dumps(projects))
    assert home.main("use", which=lambda name: str(whisper) if name == "whisper-cli" else None) == 0
    argv = list(config.load_config(home.paths.config).argv)
    assert argv[0] == str(whisper) and argv[argv.index("-m") + 1] == str(home.paths.models / "ggml-large-v3-turbo.bin")
    assert argv[argv.index("--prompt") + 1] == "Cardputer" and "--vad" not in argv


def test_use_without_a_model_points_to_setup(home, capsys):
    whisper = _whisper_stand_in(home)
    assert home.main("use", which=lambda name: str(whisper)) == 2
    assert "cpa-stt setup" in capsys.readouterr().err


def test_use_without_a_file_keeps_your_other_settings(home, capsys):
    whisper = _example_inputs(home)
    mine = {"provider": "off", "meetings": False, "timeout_s": 60, "drop_lines": ["嗯嗯"],
            "recordings_dir": str(home.root)}  # fmt: skip
    config.save_config(home.paths.config, config.parse_config(mine))
    assert home.main("use", which=lambda name: str(whisper)) == 0
    cfg = config.load_config(home.paths.config)
    assert cfg.on and cfg.argv[0] == str(whisper) and cfg.sandbox_mach == config.DEFAULT_MACH
    assert (cfg.meetings, cfg.timeout_s, cfg.drop_lines, cfg.recordings_dir) == (False, 60, ("嗯嗯",), str(home.root))


def test_setup_links_models_found_by_spotlight(home, capsys):
    big = _model_file(home.tmp / "a", "ggml-large-v3-turbo.bin", 50)
    small = _model_file(home.tmp / "b", "ggml-large-v3-turbo.bin", 5)
    vad = _model_file(home.tmp / "c", "ggml-silero-v5.1.2.bin")
    other = _model_file(home.tmp / "d", "for-tests-ggml-large-v3-turbo.bin")
    (home.tmp / "e").mkdir()
    (home.tmp / "e" / "ggml-large-v3-turbo.bin").symlink_to(big)
    linked_twice = home.tmp / "e" / "ggml-large-v3-turbo.bin"
    home.spotlight = {"ggml-large-v3-turbo.bin": "\n".join(map(str, [small, other, linked_twice, big])),
                      "ggml-silero-": str(vad)}  # fmt: skip
    assert home.main("setup", "--yes") == 0
    linked = home.paths.models / "ggml-large-v3-turbo.bin"
    assert linked.is_symlink() and os.readlink(linked) == str(big.resolve())
    assert os.readlink(home.paths.models / "ggml-silero-v5.1.2.bin") == str(vad.resolve())
    assert "linked" in capsys.readouterr().out
    assert ["/usr/bin/mdfind", "-name", "ggml-large-v3-turbo.bin"] in home.calls


def test_setup_with_a_path_and_what_it_refuses(home, capsys):
    model = _model_file(home.tmp / "x", "ggml-large-v3-turbo.bin")
    assert home.main("setup", str(model)) == 0
    assert os.readlink(home.paths.models / "ggml-large-v3-turbo.bin") == str(model.resolve())
    assert home.main("setup", str(_model_file(home.tmp / "y", "notes.bin"))) == 2
    (home.paths.models / "ggml-silero-v5.1.2.bin").write_bytes(b"real")
    assert home.main("setup", str(_model_file(home.tmp / "z", "ggml-silero-v5.1.2.bin"))) == 2
    assert (home.paths.models / "ggml-silero-v5.1.2.bin").read_bytes() == b"real"


def test_setup_says_when_nothing_was_found(home, capsys):
    assert home.main("setup") == 0
    out = capsys.readouterr().out
    assert "not found" in out and "never downloads" in out


def test_setup_finds_models_in_known_folders_and_asks_before_linking(home, capsys):
    found = _model_file(home.known, "ggml-large-v3-turbo.bin", 50)
    home.answers = ["n"]
    assert home.main("setup") == 0
    out = capsys.readouterr().out
    assert f"found {found} (0.0 MB)" in out and not (home.paths.models / "ggml-large-v3-turbo.bin").exists()
    home.answers = ["y"]
    assert home.main("setup") == 0
    assert os.readlink(home.paths.models / "ggml-large-v3-turbo.bin") == str(found.resolve())
    assert ["/usr/bin/mdfind", "-name", "ggml-large-v3-turbo.bin"] not in home.calls
    assert "another app" in capsys.readouterr().out


@pytest.mark.parametrize("spoil", ["magic", "group_writable", "folder_writable", "owner"])
def test_setup_refuses_a_model_it_cannot_trust(home, capsys, monkeypatch, spoil):
    model = _model_file(home.tmp / "x", "ggml-large-v3-turbo.bin")
    if spoil == "magic":
        model.write_bytes(b"GGUF" + b"m" * 10)
    elif spoil == "group_writable":
        model.chmod(0o664)
    elif spoil == "folder_writable":
        model.parent.chmod(0o777)
    else:
        monkeypatch.setattr(os, "getuid", lambda: os.stat(model).st_uid + 1)
    assert home.main("setup", str(model)) == 2
    assert not os.path.lexists(home.paths.models / "ggml-large-v3-turbo.bin")


def test_setup_refuses_models_on_external_volumes(home, capsys):
    assert home.main("setup", "/Volumes/Stick/ggml-large-v3-turbo.bin") == 2
    assert "/Volumes" in capsys.readouterr().err
