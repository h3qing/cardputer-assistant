"""display_safe() (docs/23 §3.3 rule 7): text shown to a person, from a transcriber, an LLM title or summary.md.

C0, DEL, C1 (U+0080-009F, U+009B included) and U+2028 / U+2029 become spaces, so one text is one line and no
terminal escape survives; so does a lone surrogate (U+D800-DFFF: half an emoji a transcriber cut, valid JSON as
"\\ud83d", which no UTF-8 output can encode; a Python str never holds a pair, so real emoji stay); bidirectional
controls (U+202A-202E, U+2066-2069) are dropped, so a title cannot reorder what is around it. NO_CONTROL in
protocol.validate covers only C0 and DEL.
queue_text() (docs/23 §3.4) is the text of a queue item made from a transcript, the one rule docs/14 §2.3 and
docs/22 §5.3 share: display_safe, trimmed, cut to text_max UTF-8 bytes on a character boundary, no suffix.
Pure.
"""

from __future__ import annotations

import re

from cpad.agents.claude_code.argv import check_text
from cpad.hub.snapshots import truncate_utf8
from cpad.protocol.limits import LIMITS

SPACE = re.compile("[\\u0000-\\u001f\\u007f-\\u009f\\u2028\\u2029\\ud800-\\udfff]")
BIDI = re.compile("[\\u202a-\\u202e\\u2066-\\u2069]")


def display_safe(text: str) -> str:
    return BIDI.sub("", SPACE.sub(" ", text))


def excerpt(text: str | None, limit: int) -> str | None:
    """display_safe, trimmed and cut to `limit` characters; None when nothing is left."""
    if text is None:
        return None
    return display_safe(text).strip()[:limit].strip() or None


def queue_text(text: str | None) -> str | None:
    """None when nothing is left or it could never be spawned (check_text: empty, or a leading '-')."""
    cut = truncate_utf8(display_safe(text or "").strip(), LIMITS["text_max"]).strip()
    return cut if check_text(cut) is None else None
