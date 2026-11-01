"""The language of cpa, cpad and cpa-stt: English first, Chinese supported.

lang() is decided at each call, so text is in the language of the moment it is shown or written: CPA_LANG, then
config.json's "lang" (`cpa lang zh|en|auto`), then LC_ALL, LC_MESSAGES and LANG. A value that starts with "zh"
gives Chinese, any other value English; an empty one says nothing, and nothing at all is English. T(zh, en) is the
one of a pair in that language. The LaunchAgents carry the login shell's CPA_LANG and locale (service.launchd),
so cpad and cpa-stt write in the language of the terminal that installed them. Text already written (daily-note
lines, transcripts) stays as it was; whatever reads it reads either language.
"""

from __future__ import annotations

import os
from collections.abc import Callable, Mapping

from cpad.config.files import read_json
from cpad.config.settings import cpa_home

LANGS = ("en", "zh")
LOCALE = ("LC_ALL", "LC_MESSAGES", "LANG")
KEYS = ("CPA_LANG", *LOCALE)  # what a LaunchAgent must be given to follow this terminal


def of(value: str | None) -> str | None:
    """'zh' for a value that starts with zh, 'en' for any other, None for an empty one."""
    text = (value or "").strip().lower()
    if not text:
        return None
    return "zh" if text.startswith("zh") else "en"


def configured() -> str | None:
    """config.json's "lang" when it is zh or en; anything else (no file, a broken one) is None, never an error."""
    try:
        doc = read_json(cpa_home() / "config.json")
    except (OSError, ValueError):
        return None
    value = doc.get("lang") if isinstance(doc, dict) else None
    return value if value in LANGS else None


def resolve(env: Mapping[str, str], config: Callable[[], str | None]) -> tuple[str, str]:
    """(language, what decided it); the config is read only when CPA_LANG does not decide."""
    found = of(env.get("CPA_LANG"))
    if found:
        return found, "CPA_LANG"
    found = config()
    if found:
        return found, "config.json"
    for key in LOCALE:
        found = of(env.get(key))
        if found:
            return found, key
    return "en", "default"


def lang() -> str:
    return resolve(os.environ, configured)[0]


def T(zh: str, en: str) -> str:  # noqa: N802 (the one-letter name every caller uses)
    return zh if lang() == "zh" else en
