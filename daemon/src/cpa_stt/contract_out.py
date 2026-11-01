"""The files cpa-stt writes, in the recording contract v1 transcriber role (docs/22 §4.7). Pure.

status.json carries by:"cpa-stt" so a restarted cpa-stt recognises its own; result.json is level
"transcript" (cpa-stt has no LLM): memos get memo:{text, intent:"note"} with the whole text cut to 2000
characters, meetings never do (their text is never an instruction); nothing heard = no sentences, no memo, and a
transcript.md of one line, "(nothing heard)" or "（没听到）" in the language of the moment (cpad.i18n).
"""

from __future__ import annotations

import json
import os
from collections.abc import Iterable, Mapping
from typing import Any

from cpa_stt import APP
from cpa_stt.parse import Sentence
from cpad.i18n import T

MEMO_MAX = 2000
LABEL_MAX = 64
NOTHING_HEARD = ("（没听到）\n", "(nothing heard)\n")  # (zh, en), like the device's 没听到 / Huh?
OUTPUT_FILES = {"transcript_json": "transcript.json", "transcript_md": "transcript.md"}


def stt_label(argv0: str) -> str:
    return (os.path.basename(argv0) or APP)[:LABEL_MAX]


def status(session: str, state: str, at: str, reason: str | None = None) -> dict[str, Any]:
    doc = {"v": 1, "session": session, "attempt": 1, "state": state, "by": APP, "at": at}
    return {**doc, "reason": reason} if reason else doc


def transcript(session: str, lead_ms: int, stt: str, sentences: Iterable[Sentence]) -> dict[str, Any]:
    return {"v": 1, "session": session, "attempt": 1, "lead_ms": lead_ms, "engine": {"stt": stt},
            "sentences": [{"t_ms": s.t_ms, "text": s.text} for s in sentences]}  # fmt: skip


def _clock(t_ms: int) -> tuple[str, str]:
    """('MM:SS' or 'H:MM:SS', 'HHMMSS')."""
    secs = t_ms // 1000
    h, m, s = secs // 3600, secs // 60 % 60, secs % 60
    shown = f"{h}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"
    return shown, f"{h:02d}{m:02d}{s:02d}"


def transcript_md(sentences: Iterable[Sentence]) -> str:
    lines, seen = [], {}
    for s in sentences:
        shown, block = _clock(s.t_ms)
        seen[block] = seen.get(block, 0) + 1
        suffix = f"-{seen[block]}" if seen[block] > 1 else ""
        lines.append(f"- [{shown}] {s.text} ^t{block}{suffix}\n")
    return "".join(lines) or T(*NOTHING_HEARD)


def memo_text(sentences: Iterable[Sentence]) -> str | None:
    text = " ".join(s.text for s in sentences)[:MEMO_MAX]
    return text or None


def _pipeline(stt: str | None) -> dict[str, Any]:
    return {"app": APP, "stt": stt or APP, "llm": None, "prompt": None}


def _base(session: str, mode: str, at: str) -> dict[str, Any]:
    return {"v": 1, "session": session, "attempt": 1, "mode": mode, "title": None, "memo": None, "actions": [],
            "finished_at": at}  # fmt: skip


def result_done(session: str, mode: str, outputs: Mapping[str, str], stt: str, sentences: tuple[Sentence, ...],
                at: str) -> dict[str, Any]:  # fmt: skip
    text = memo_text(sentences) if mode == "memo" else None
    files = {k: {"path": OUTPUT_FILES[k], "sha256": digest} for k, digest in outputs.items()}
    return {**_base(session, mode, at), "state": "done", "level": "transcript", "outputs": files,
            "memo": {"text": text, "intent": "note"} if text else None, "pipeline": _pipeline(stt),
            "error": None}  # fmt: skip


def result_failed(session: str, mode: str, code: str, retryable: bool, at: str, stt: str | None = None) -> dict:
    return {**_base(session, mode, at), "state": "failed", "level": None, "outputs": {},
            "pipeline": _pipeline(stt), "error": {"code": code, "retryable": retryable}}  # fmt: skip


def encode(doc: Any) -> bytes:
    return (json.dumps(doc, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
