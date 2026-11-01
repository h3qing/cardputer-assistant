"""The Mac side's language (English first, Chinese supported): CPA_LANG, then config.json "lang", then the locale."""

from __future__ import annotations

import json

import pytest

from cpad import i18n
from cpad.config.settings import Config, ConfigError, dump_config, parse_config


def _none() -> None:
    return None


@pytest.mark.parametrize(
    ("value", "expected"),
    [("zh", "zh"), ("zh_CN.UTF-8", "zh"), ("ZH_tw", "zh"), ("zh-Hans", "zh"), ("en_US.UTF-8", "en"), ("C", "en"),
     ("POSIX", "en"), ("fr_FR.UTF-8", "en"), ("en", "en"), ("auto", "en"), (" zh", "zh")],
)  # fmt: skip
def test_a_value_starting_with_zh_is_chinese_anything_else_english(value, expected):
    assert i18n.of(value) == expected


@pytest.mark.parametrize("value", [None, "", "   "])
def test_an_empty_value_says_nothing(value):
    assert i18n.of(value) is None


def test_nothing_set_is_english():
    assert i18n.resolve({}, _none) == ("en", "default")


@pytest.mark.parametrize(
    ("env", "configured", "expected"),
    [
        ({"CPA_LANG": "zh", "LANG": "en_US.UTF-8"}, "en", ("zh", "CPA_LANG")),
        ({"CPA_LANG": "en", "LC_ALL": "zh_CN.UTF-8"}, "zh", ("en", "CPA_LANG")),
        ({"LC_ALL": "en_US.UTF-8"}, "zh", ("zh", "config.json")),
        ({"LANG": "zh_CN.UTF-8"}, "en", ("en", "config.json")),
        ({"LC_ALL": "zh_CN.UTF-8", "LC_MESSAGES": "en_US", "LANG": "en_US"}, None, ("zh", "LC_ALL")),
        ({"LC_ALL": "C", "LANG": "zh_CN.UTF-8"}, None, ("en", "LC_ALL")),
        ({"LC_MESSAGES": "zh_CN", "LANG": "en_US"}, None, ("zh", "LC_MESSAGES")),
        ({"LC_ALL": "", "LANG": "zh_CN.UTF-8"}, None, ("zh", "LANG")),
        ({"LC_CTYPE": "zh_CN.UTF-8"}, None, ("en", "default")),
    ],
)
def test_the_order_is_cpa_lang_config_lc_all_lc_messages_lang(env, configured, expected):
    assert i18n.resolve(env, lambda: configured) == expected


def test_the_config_is_only_read_when_cpa_lang_does_not_decide():
    def boom() -> str:
        raise AssertionError("config.json read although CPA_LANG was set")

    assert i18n.resolve({"CPA_LANG": "zh"}, boom) == ("zh", "CPA_LANG")


def test_t_picks_the_pair_by_the_language_at_the_call(monkeypatch):
    monkeypatch.setenv("CPA_LANG", "zh")
    assert i18n.T("会议", "Meeting") == "会议" and i18n.lang() == "zh"
    monkeypatch.setenv("CPA_LANG", "en_US")
    assert i18n.T("会议", "Meeting") == "Meeting" and i18n.lang() == "en"
    monkeypatch.delenv("CPA_LANG")
    assert i18n.T("会议", "Meeting") == "Meeting", "the tests run with no language set: English"


def _home(tmp_path, monkeypatch, doc) -> None:
    monkeypatch.setenv("CPA_HOME", str(tmp_path))
    (tmp_path / "config.json").write_text(doc if isinstance(doc, str) else json.dumps(doc), encoding="utf-8")


def test_config_lang_is_read_at_each_call(tmp_path, monkeypatch):
    monkeypatch.setenv("LANG", "en_US.UTF-8")
    _home(tmp_path, monkeypatch, {"lang": "zh"})
    assert i18n.T("会议", "Meeting") == "会议"
    _home(tmp_path, monkeypatch, {"lang": "en"})
    assert i18n.T("会议", "Meeting") == "Meeting"


@pytest.mark.parametrize("doc", ["{not json", "[]", {"lang": "fr"}, {"lang": 1}, {"lang": None}, {}])
def test_a_config_without_a_usable_lang_falls_through_to_the_locale(tmp_path, monkeypatch, doc):
    monkeypatch.setenv("LANG", "zh_CN.UTF-8")
    _home(tmp_path, monkeypatch, doc)
    assert i18n.configured() is None
    assert i18n.lang() == "zh"


def test_no_config_file_falls_through(tmp_path, monkeypatch):
    monkeypatch.setenv("CPA_HOME", str(tmp_path / "missing"))
    assert i18n.configured() is None and i18n.lang() == "en"


# --- config.json "lang" -------------------------------------------------------------------------------


@pytest.mark.parametrize("value", ["zh", "en", None])
def test_config_lang_takes_zh_en_or_nothing(value):
    assert parse_config({"lang": value}).lang == value


@pytest.mark.parametrize("value", ["fr", "", "ZH", 1, True, ["zh"]])
def test_config_lang_refuses_anything_else(value):
    with pytest.raises(ConfigError):
        parse_config({"lang": value})


def test_config_lang_is_written_only_when_set():
    assert "lang" not in dump_config(Config())
    assert dump_config(Config(lang="zh"))["lang"] == "zh"
    assert parse_config(dump_config(Config(lang="en"))) == Config(lang="en")
