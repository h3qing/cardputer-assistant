"""The settings.json change `cpa hooks install` makes, and how it is written (#33).

Every test writes under tmp_path; the real ~/.claude is never touched.
"""

from __future__ import annotations

import json
import os
import stat

import pytest

from cpad.agents.claude_code import hook_settings as hset

CPA = "/Users/u/Repos/Cardputer Assistant/daemon/.venv/bin/cpa"
OURS = {"type": "command", "command": CPA, "args": ["hook"], "timeout": 5}
FOREIGN = {"type": "command", "command": "afplay /System/Library/Sounds/Glass.aiff"}


def test_snippet_lists_every_event_with_the_exec_form_handler():
    hooks = hset.snippet(CPA)["hooks"]
    assert list(hooks) == [event for event, _ in hset.EVENTS]
    assert hooks["Stop"] == [{"hooks": [OURS]}]
    assert hooks["PermissionRequest"] == [{"matcher": "*", "hooks": [OURS]}]
    assert hooks["PreToolUse"] == [{"matcher": "AskUserQuestion|ExitPlanMode", "hooks": [OURS]}]
    assert "idle_prompt" in hooks["Notification"][0]["matcher"]


def test_snippet_passes_a_custom_home():
    group = hset.snippet(CPA, home="/tmp/cpa home")["hooks"]["Stop"][0]
    assert group["hooks"][0]["args"] == ["hook", "--home", "/tmp/cpa home"]


def test_install_keeps_everything_else_and_is_idempotent():
    settings = {"model": "opus", "hooks": {"Stop": [{"hooks": [FOREIGN]}]}, "permissions": {"allow": ["Bash(ls)"]}}
    once = hset.install(settings, CPA)
    assert once["model"] == "opus" and once["permissions"] == {"allow": ["Bash(ls)"]}
    assert once["hooks"]["Stop"] == [{"hooks": [FOREIGN]}, {"hooks": [OURS]}]
    assert hset.install(once, CPA) == once
    assert settings["hooks"]["Stop"] == [{"hooks": [FOREIGN]}], "the input is never mutated"


def test_install_replaces_an_older_cpa_path():
    old = hset.install({}, "/old/venv/bin/cpa")
    new = hset.install(old, CPA)
    assert new == hset.install({}, CPA)


def test_uninstall_removes_only_cpa_handlers():
    shared = {"matcher": "*", "hooks": [FOREIGN, {**OURS, "command": "/elsewhere/cpa"}]}
    settings = {"hooks": {"PermissionRequest": [shared], "Stop": [{"hooks": []}]}}
    removed = hset.uninstall(hset.install(settings, CPA))
    assert removed == {"hooks": {"PermissionRequest": [{"matcher": "*", "hooks": [FOREIGN]}], "Stop": [{"hooks": []}]}}


def test_uninstall_restores_a_file_that_had_no_hooks():
    assert hset.uninstall(hset.install({"model": "opus"}, CPA)) == {"model": "opus"}
    assert hset.uninstall({"hooks": {}}) == {"hooks": {}}


def test_handlers_that_merely_look_similar_are_not_ours():
    lookalikes = [{**OURS, "command": "/bin/cpa-other"}, {**OURS, "args": ["status"]}, {**OURS, "type": "http"},
                  {"type": "command", "command": f"{CPA} hook"}, "cpa hook"]  # fmt: skip
    settings = {"hooks": {"Stop": [{"hooks": lookalikes}]}}
    assert hset.uninstall(settings) == settings


def test_shapes_cpa_does_not_understand_are_left_alone():
    odd = {"hooks": {"Stop": ["not a group", {"matcher": "*", "hooks": "x"}], "Custom": {"x": 1}}}
    assert hset.uninstall(hset.install(odd, CPA)) == odd


@pytest.mark.parametrize("doc", [[], "x", {"hooks": []}, {"hooks": {"Stop": {"hooks": []}}}])
def test_malformed_settings_are_refused(doc):
    with pytest.raises(ValueError):
        hset.install(doc, CPA)


# --- files ----------------------------------------------------------------------------------


def test_load_missing_file_is_empty(tmp_path):
    loaded = hset.load(tmp_path / ".claude" / "settings.json")
    assert loaded.doc == {} and loaded.raw is None


@pytest.mark.parametrize("text", ["{not json", "[1]", "// comment\n{}"])
def test_load_refuses_what_it_cannot_round_trip(tmp_path, text):
    path = tmp_path / "settings.json"
    path.write_text(text)
    with pytest.raises(ValueError):
        hset.load(path)


def test_save_creates_the_file_and_its_directory(tmp_path):
    path = tmp_path / ".claude" / "settings.json"
    loaded = hset.load(path)
    backup = hset.save(loaded, hset.install(loaded.doc, CPA), tmp_path / "cpa" / hset.BACKUP_DIR)
    assert backup is None
    assert json.loads(path.read_text())["hooks"]["Stop"] == [{"hooks": [OURS]}]
    assert stat.S_IMODE(path.stat().st_mode) == 0o600


def test_save_keeps_the_mode_and_writes_a_private_backup_away_from_the_file(tmp_path):
    path = tmp_path / "proj" / ".claude" / "settings.local.json"
    path.parent.mkdir(parents=True)
    path.write_text('{"model": "opus", "env": {"KEY": "secret"}}\n')
    path.chmod(0o644)
    backups = tmp_path / "cpa" / hset.BACKUP_DIR
    loaded = hset.load(path)
    backup = hset.save(loaded, hset.install(loaded.doc, CPA), backups)
    assert backup == hset.backup_path(backups, path) and backup.parent == backups
    assert backup.name.endswith("-settings.local.json")
    assert backup.read_text() == '{"model": "opus", "env": {"KEY": "secret"}}\n'
    assert stat.S_IMODE(backup.stat().st_mode) == 0o600
    assert stat.S_IMODE(backups.stat().st_mode) == 0o700
    assert stat.S_IMODE(path.stat().st_mode) == 0o644
    assert json.loads(path.read_text())["model"] == "opus"
    assert [p.name for p in path.parent.iterdir()] == ["settings.local.json"]


def test_backup_names_tell_settings_files_apart(tmp_path):
    user, local = tmp_path / ".claude" / "settings.json", tmp_path / "proj" / ".claude" / "settings.json"
    assert hset.backup_path(tmp_path, user) != hset.backup_path(tmp_path, local)
    assert hset.backup_path(tmp_path, user) == hset.backup_path(tmp_path, user)


def test_save_writes_through_a_symlink(tmp_path):
    real = tmp_path / "dotfiles" / "claude-settings.json"
    real.parent.mkdir()
    real.write_text("{}\n")
    link = tmp_path / ".claude" / "settings.json"
    link.parent.mkdir()
    link.symlink_to(real)
    loaded = hset.load(link)
    hset.save(loaded, hset.install(loaded.doc, CPA), tmp_path / "cpa" / hset.BACKUP_DIR)
    assert link.is_symlink() and "hooks" in json.loads(real.read_text())


def test_save_refuses_when_the_file_changed_underneath(tmp_path):
    path = tmp_path / "settings.json"
    path.write_text("{}\n")
    loaded = hset.load(path)
    path.write_text('{"model": "sonnet"}\n')
    with pytest.raises(hset.SettingsChanged):
        hset.save(loaded, hset.install(loaded.doc, CPA), tmp_path / "cpa" / hset.BACKUP_DIR)
    assert path.read_text() == '{"model": "sonnet"}\n'


def test_default_path_follows_home_and_claude_config_dir(tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.delenv("CLAUDE_CONFIG_DIR", raising=False)
    assert hset.default_path() == tmp_path / ".claude" / "settings.json"
    monkeypatch.setenv("CLAUDE_CONFIG_DIR", str(tmp_path / "cfg"))
    assert hset.default_path() == tmp_path / "cfg" / "settings.json"


def test_find_cpa_prefers_the_running_executable(tmp_path):
    exe = tmp_path / "bin" / "cpa"
    exe.parent.mkdir()
    exe.write_text("#!/bin/sh\n")
    exe.chmod(0o755)
    assert hset.find_cpa(str(exe), lambda _: None) == str(exe)
    assert hset.find_cpa("/usr/bin/python3", lambda _: str(exe)) == str(exe)
    with pytest.raises(ValueError):
        hset.find_cpa("/usr/bin/python3", lambda _: None)


@pytest.mark.parametrize("name", ["relative/cpa", "/abs/not-cpa", "/missing/dir/cpa"])
def test_check_cpa_requires_an_absolute_executable_named_cpa(tmp_path, name):
    with pytest.raises(ValueError):
        hset.check_cpa(name)
    exe = tmp_path / "cpa"
    exe.write_text("#!/bin/sh\n")
    exe.chmod(0o644)
    with pytest.raises(ValueError):
        hset.check_cpa(str(exe))
    exe.chmod(0o755)
    assert hset.check_cpa(str(exe)) == str(exe)
    assert os.path.isabs(hset.check_cpa(str(exe)))
