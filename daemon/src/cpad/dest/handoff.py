"""What another app receives (docs/25 §2.3, D6, D9). Pure.

Every text handed to another app (url, app, paste.<name>, an overflow to the clipboard) starts with a fixed
header on the same line: `[Cardputer voice·may contain misheard words or other people's speech·#project] text`
(typed: `[Cardputer typed·#project]`; no project: no `·#…`), in Chinese `[Cardputer 语音·可能有错字或别人的话·#project]`
and `[Cardputer 打字·#project]`: the language of the moment it is handed over (cpad.i18n). Its first character is
always `[`, so the `!`, `/`, `@` and `#` an agent's input box treats specially never lead, and the agent knows it is
reading speech recognition. Every C0, DEL and C1
control character (newline and tab included) and U+2028 / U+2029 becomes a space and format characters (Cf: bidi
controls, zero-width) are dropped: some apps silently drop a link whose query carries any control character. A
lone surrogate (JSON allows one) becomes U+FFFD. Lengths are counted in UTF-16 code units, as JavaScript's `.length`
(an emoji counts 2).
"""

from __future__ import annotations

import hashlib
import unicodedata
from dataclasses import dataclass
from typing import Literal

from cpad.dest.model import Capture, UrlOpts
from cpad.dest.template import render
from cpad.i18n import T

VOICE_HEAD = (
    "Cardputer 语音·可能有错字或别人的话",
    "Cardputer voice·may contain misheard words or other people's speech",
)
TYPED_HEAD = ("Cardputer 打字", "Cardputer typed")


def header(source: str, project: str | None) -> str:
    tag = f"·#{project}" if project else ""
    return f"[{T(*(VOICE_HEAD if source == 'voice' else TYPED_HEAD))}{tag}] "


def _keep(ch: str) -> str:
    cat = unicodedata.category(ch)
    if cat in ("Cc", "Zl", "Zp"):
        return " "
    if cat == "Cs":
        return "\ufffd"  # cannot be encoded: quoting a link or writing the clipboard would raise
    return "" if cat == "Cf" else ch


def clean(text: str) -> str:
    """Controls and line / paragraph separators as spaces, format characters dropped, trimmed."""
    return "".join(_keep(ch) for ch in text).strip()


def utf16_len(text: str) -> int:
    return len(text.encode("utf-16-le", "surrogatepass")) // 2


def handed_text(cap: Capture) -> str | None:
    """The header and the cleaned text, or None when nothing is left to hand over."""
    body = clean(cap.text or "")
    return header(cap.source, cap.project) + body if body else None


def map_value(opts: UrlOpts, project: str | None) -> str:
    """The {p} value for this project: its own, else "*", else empty (the parameter is dropped)."""
    values = dict(opts.map)
    return values.get(project or "", values.get("*", ""))


def draft_target(opts: UrlOpts, project: str | None) -> str:
    """The draft a link fills, as dest.handed{to} records it: sha256[:8] of the {p} value (the Bot), never the value."""
    return hashlib.sha256(map_value(opts, project).encode("utf-8")).hexdigest()[:8]


@dataclass(frozen=True, slots=True)
class UrlPlan:
    url: str | None  # the pre-fill link, None when it does not fit
    focus: str | None  # the link that only brings the target forward
    reason: Literal["ok", "too_long"] = "ok"


def plan(opts: UrlOpts, text: str, project: str | None) -> UrlPlan:
    """The link for text (already handed_text), or too_long when the text or the encoded link is over a limit."""
    values = {"{text}": text, "{p}": map_value(opts, project), "{project}": project or ""}
    focus = render(opts.focus, values) if opts.focus else None
    if utf16_len(text) > opts.max_text_utf16:
        return UrlPlan(None, focus, "too_long")
    url = render(opts.template, values)
    return UrlPlan(url, focus) if len(url) <= opts.max_url else UrlPlan(None, focus, "too_long")
