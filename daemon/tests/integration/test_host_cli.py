"""cpa host name (docs/23 §4.2, D10): name this Mac in one line. The name is config.json's host_label: it goes on
the host hello (shown on the device) and into the env stamp of new recordings. Unset by default; never the macOS
computer name. Runs against a temporary $CPA_HOME."""

from __future__ import annotations

import json
import stat

import pytest

from cpad.cli.main import main as cpa
from cpad.config.files import write_json


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "home"
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


def _config(home) -> dict:
    return json.loads((home / "config.json").read_text(encoding="utf-8"))


def test_naming_this_mac_takes_one_line(home, capsys):
    assert cpa(["host", "name", "--", "公司"]) == 0
    assert _config(home)["host_label"] == "公司"
    assert "公司" in capsys.readouterr().out
    assert stat.S_IMODE((home / "config.json").stat().st_mode) == 0o600


def test_a_rename_replaces_the_name_without_the_dashes(home):
    assert cpa(["host", "name", "家"]) == 0 and cpa(["host", "name", "八个汉字的主机名"]) == 0
    assert _config(home)["host_label"] == "八个汉字的主机名"


def test_emoji_count_by_bytes_like_the_device(home):
    assert cpa(["host", "name", "--", "🏠家"]) == 0
    assert _config(home)["host_label"] == "🏠家"
    assert cpa(["host", "name", "--", "🏠" * 7]) == 2, "28 bytes"


def test_clear_forgets_the_name(home, capsys):
    assert cpa(["host", "name", "家"]) == 0 and cpa(["host", "name", "--clear"]) == 0
    assert "host_label" not in _config(home)


def test_other_settings_are_kept(home):
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"notes_dir": "/n", "device": {"serial": "28:84"}, "rec_pull": False})
    assert cpa(["host", "name", "家"]) == 0
    doc = _config(home)
    assert (doc["notes_dir"], doc["device"]["serial"], doc["rec_pull"], doc["host_label"]) == ("/n", "28:84", False,
                                                                                               "家")  # fmt: skip


def test_without_a_name_it_shows_the_current_one(home, capsys):
    assert cpa(["host", "name"]) == 0
    assert "cpa host name" in capsys.readouterr().out, "says how to name it"
    assert cpa(["host", "name", "家"]) == 0
    capsys.readouterr()
    assert cpa(["host", "name"]) == 0 and "家" in capsys.readouterr().out
    assert _config(home)["host_label"] == "家"


@pytest.mark.parametrize("bad", ["", "九个汉字的主机名称", "a" * 25, "Studio\tMac", "a\u007fb", "line\nbreak", "-x",
                                 "--clear", "🏠" * 7, "Mac \udcff"])  # fmt: skip
def test_a_name_the_device_would_refuse_is_refused_before_writing(home, capsys, bad):
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"host_label": "家"})
    assert cpa(["host", "name", "--", bad]) == 2
    assert _config(home)["host_label"] == "家"
    err = capsys.readouterr().err
    assert err.startswith("error: ") and (not bad or bad not in err), "the input is never echoed"


@pytest.mark.parametrize("bad", ["公司\u202e", "a\u0085b", "a\u2028b", "\u2066家"])
def test_a_name_the_filing_rows_would_show_otherwise_is_refused(home, capsys, bad):
    """C1, U+2028/9 and bidi controls: display_safe strips them from every row, so the stored name would differ."""
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"host_label": "家"})
    assert cpa(["host", "name", "--", bad]) == 2
    assert _config(home)["host_label"] == "家" and bad not in capsys.readouterr().err


def test_a_name_and_clear_together_are_refused(home):
    with pytest.raises(SystemExit):
        cpa(["host", "name", "家", "--clear"])
    assert not (home / "config.json").exists()


def test_json_reads_sets_and_clears_the_name_for_whisperwoof(home, capsys):
    """protocol/filing/v1 host: the home / work banner asks whether this Mac has a name, then sets one."""

    def doc(argv: list[str], code: int = 0) -> dict:
        assert cpa(["host", "name", "--json", *argv]) == code
        return json.loads(capsys.readouterr().out)

    assert doc([]) == {"v": 1, "host": None}
    assert doc(["--", "公司"]) == {"v": 1, "host": "公司"}
    assert doc([]) == {"v": 1, "host": "公司"}
    assert doc(["--clear"]) == {"v": 1, "host": None} and "host_label" not in _config(home)


def test_json_refuses_a_bad_name_with_an_error_document(home, capsys):
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"host_label": "家"})
    assert cpa(["host", "name", "--json", "--", "公司\u202e"]) == 2
    captured = capsys.readouterr()
    assert json.loads(captured.out) == {"ok": False, "v": 1, "error": "E_BAD_ARG", "field": "name"}
    assert captured.err == "" and _config(home)["host_label"] == "家"
