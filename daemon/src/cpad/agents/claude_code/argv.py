"""argv construction and input guards for the claude CLI (05 §7.5, §8; docs/gates/cli-gates.md G3, G8).

The text always goes after `--` as a single argv element, and a text that starts with '-' (after
trimming) is refused anyway. --permission-mode is always explicit; bypassPermissions is impossible.
Reply resumes with `--resume <sessionId> -- <text>` and NO other flag: any flag makes the CLI start
a copy of the session (G3).
"""

from __future__ import annotations

import re
import unicodedata
from itertools import islice

from cpad.projects.registry import MODES
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import NO_CONTROL, matches, utf8_len

SLUG_MAX = 24
NAME_MAX = LIMITS["session_name_max"]
NOT_SLUG = re.compile(r"[^a-z0-9-]")
HEAD_CHARS = 8
HEAD_TOKEN = re.compile(r"[a-z0-9]+|[^\W_]")  # an ASCII word, or any one other letter or digit


def check_text(text: str) -> str | None:
    """None when the body may be passed to the CLI, else the protocol error code."""
    size_ok = isinstance(text, str) and LIMITS["text_min"] <= utf8_len(text) <= LIMITS["text_max"]
    if not size_ok or not matches(NO_CONTROL, text) or text.strip().startswith("-"):
        return "E_BAD_FIELD"
    return None


def _wide(text: str) -> bool:
    return any(unicodedata.east_asian_width(c) in ("W", "F") for c in text)


def _head(text: str, limit: int) -> str:
    """The first 8 characters of a Chinese text, an ASCII word counting as one; a gap becomes '-' (docs/20 U14)."""
    out, end = "", None
    for match in islice(HEAD_TOKEN.finditer(text.lower()), HEAD_CHARS):
        out += ("-" if end is not None and match.start() > end else "") + match.group()
        end = match.end()
    return out.encode()[:limit].decode("utf-8", "ignore").strip("-")


def slug(text: str, n: int, limit: int = SLUG_MAX) -> str:
    if _wide(text) and (head := _head(text, limit)):
        return head
    words = [w for w in (NOT_SLUG.sub("", t.lower()) for t in text.split()) if w.strip("-")][:4]
    joined = re.sub(r"-{2,}", "-", "-".join(w.strip("-") for w in words))[:limit].strip("-")
    return joined or f"task-{n}"[:limit]


def session_name(key: str, text: str, n: int) -> str:
    """<key>/<slug>, at most session_name_max (32) bytes; slug is at most 24 bytes."""
    return f"{key}/{slug(text, n, min(SLUG_MAX, NAME_MAX - len(key) - 1))}"


def _mode(mode: str) -> str:
    if mode not in MODES:
        raise ValueError("permission mode not allowed")
    return mode


def spawn_argv(claude_bin: str, name: str, mode: str, text: str) -> list[str]:
    return [claude_bin, "--bg", "--name", name, "--permission-mode", _mode(mode), "--", text]


def resume_argv(claude_bin: str, session_id: str, text: str) -> list[str]:
    return [claude_bin, "--bg", "--resume", session_id, "--", text]


def stop_argv(claude_bin: str, short: str) -> list[str]:
    return [claude_bin, "stop", short]


def list_argv(claude_bin: str, include_all: bool = False) -> list[str]:
    return [claude_bin, "agents", "--json", *(["--all"] if include_all else [])]


def version_argv(claude_bin: str) -> list[str]:
    return [claude_bin, "--version"]


def auth_status_argv(claude_bin: str) -> list[str]:
    """Read-only; prints JSON by default (2.1.306)."""
    return [claude_bin, "auth", "status"]
