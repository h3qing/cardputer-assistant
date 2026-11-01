"""The guard keeps the simulator's cpad off the user's data: $CPA_HOME and notes_dir must be temporary."""

from pathlib import Path

import pytest
from cpad.config.settings import DEFAULT_HOME, Config, Paths, load_config

from simtools.guard import GuardError, check_home, check_notes, is_temp, prepare_home


def test_temporary_directories_are_recognised(tmp_path):
    assert is_temp(tmp_path)
    assert is_temp(tmp_path / "home" / "deeper")
    assert not is_temp(Path.home())
    assert not is_temp(Path(DEFAULT_HOME).expanduser())


def test_the_temp_root_itself_is_not_a_home(tmp_path):
    import tempfile

    assert not is_temp(Path(tempfile.gettempdir()))


def test_check_home_needs_cpa_home(tmp_path):
    with pytest.raises(GuardError, match="set CPA_HOME"):
        check_home(None)
    with pytest.raises(GuardError, match="set CPA_HOME"):
        check_home("")


def test_check_home_refuses_the_real_home():
    with pytest.raises(GuardError, match="not a temporary directory"):
        check_home(DEFAULT_HOME)
    with pytest.raises(GuardError, match="not a temporary directory"):
        check_home(str(Path.home() / "cpa-sim-home"))


def test_check_home_accepts_a_temp_dir(tmp_path):
    assert check_home(str(tmp_path / "home")) == (tmp_path / "home").resolve()


def test_notes_must_be_temporary(tmp_path):
    with pytest.raises(GuardError, match="notes_dir"):
        check_notes(Config())  # the default is ~/CardputerNotes
    check_notes(Config(notes_dir=str(tmp_path / "notes")))


def test_prepare_home_writes_a_safe_config(tmp_path):
    home = tmp_path / "home"
    paths = prepare_home(home, path_var="/usr/bin:/bin")
    config = load_config(paths.config)
    assert paths == Paths.at(home)
    assert config.notes_path() == home / "notes"
    assert config.claude_bin is not None and config.claude_bin.endswith("fake-claude")
    assert config.spawn_env["FAKE_CLAUDE_DIR"] == str(home / "fake-claude")
    assert config.spawn_env["HOME"] == str(home / "fake-claude")
    assert (home / "fake-claude" / "state.json").read_text(encoding="utf-8") == '{"sessions": []}'


def test_prepare_home_keeps_an_existing_config(tmp_path):
    home = tmp_path / "home"
    prepare_home(home, path_var="/usr/bin")
    paths = Paths.at(home)
    paths.config.write_text('{"notes_dir": "%s", "poll_s": 1}' % (tmp_path / "n"), encoding="utf-8")
    prepare_home(home, path_var="/usr/bin")
    assert load_config(paths.config).poll_s == 1.0
