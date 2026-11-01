"""cpa lang [zh|en|auto]: config.json's "lang", what cpa, cpad and cpa-stt speak and write (cpad.i18n)."""

from __future__ import annotations

import json

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


def test_without_a_setting_it_follows_the_locale(home, monkeypatch, capsys):
    assert cpa(["lang"]) == 0
    assert capsys.readouterr().out.strip() == "language: en (default)"
    monkeypatch.setenv("LANG", "zh_CN.UTF-8")
    assert cpa(["lang"]) == 0
    assert capsys.readouterr().out.strip() == "语言：zh（来自 LANG）"
    assert not (home / "config.json").exists(), "showing it writes nothing"


def test_setting_it_wins_over_the_locale_and_keeps_the_rest(home, monkeypatch, capsys):
    monkeypatch.setenv("LANG", "en_US.UTF-8")
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"host_label": "家", "poll_s": 5})
    assert cpa(["lang", "zh"]) == 0
    assert capsys.readouterr().out.strip() == "语言：zh（来自 config.json）"
    assert _config(home) == {"host_label": "家", "poll_s": 5, "lang": "zh"}
    assert (home / "config.json").stat().st_mode & 0o777 == 0o600
    assert cpa(["lang", "en"]) == 0
    assert capsys.readouterr().out.strip() == "language: en (from config.json)"


def test_auto_forgets_it(home, monkeypatch, capsys):
    monkeypatch.setenv("LC_MESSAGES", "zh_CN")
    assert cpa(["lang", "en"]) == 0 and cpa(["lang", "auto"]) == 0
    assert _config(home)["lang"] is None
    assert capsys.readouterr().out.splitlines()[-1] == "语言：zh（来自 LC_MESSAGES）"


def test_cpa_lang_still_wins_and_it_says_so(home, monkeypatch, capsys):
    monkeypatch.setenv("CPA_LANG", "en")
    assert cpa(["lang", "zh"]) == 0
    assert capsys.readouterr().out.strip() == "language: en (from CPA_LANG)"
    assert _config(home)["lang"] == "zh"


def test_anything_else_is_refused(home, capsys):
    with pytest.raises(SystemExit):
        cpa(["lang", "fr"])
    assert not (home / "config.json").exists()


def test_a_broken_config_is_refused_before_writing(home, capsys):
    home.mkdir(mode=0o700)
    (home / "config.json").write_text('{"poll_s": "fast"}', encoding="utf-8")
    assert cpa(["lang", "zh"]) == 2
    assert (home / "config.json").read_text(encoding="utf-8") == '{"poll_s": "fast"}'
