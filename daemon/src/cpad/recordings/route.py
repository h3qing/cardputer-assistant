"""Daily-note lines for recordings (docs/11 §6.7). Pure: rec_entry(event) -> (file name, line, block id).

Like notes.render.note_entry, each line is a function of one event (rec.result / rec.stale carry the
session context), so NotesWriter.backfill can replay them. Date and time are the session start; '~'
marks an uncertain time. Text: control characters become spaces, '[' and ']' are escaped inside link
text, titles are cut to 80 characters and memo text to 280 (first sentence + a link to the full text).
v0.3 never routes anything to a queue: this module only renders lines.
v0.4.1 (docs/13 §9.3): a finished memo whose segment 0 carried dest "todo" is a '- [ ] ' line whatever
WhisperWoof's intent says; any other dest (daily, unknown) renders as before.
docs/23 §2.4: rec.filed (cpad filed the session after its line was written) adds one more line,
'- HH:MM #<key> ↪ Filed: <title or Voice/Meeting> ^cpa-<sid>-f<n>-<key>', n the session's n-th rec.filed, so filing
back to an earlier project still adds a line; the old lines are never changed.
The words (Voice:, Meeting:, min, to-dos, [audio], ...; in Chinese 语音：, 会议：, 分钟, 待办, [音频], ...) are in the
language of the moment the line is written (cpad.i18n). A line already written is never rewritten (the notes writer
appends by block id), and readers parse the box, the time, the #tag and the block id, never the words.
"""

from __future__ import annotations

from collections.abc import Mapping
from datetime import datetime, tzinfo
from typing import Any

from cpad.i18n import T
from cpad.notes.render import block_id, clean
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import is_int, matches

TITLE_MAX = 80
MEMO_MAX = 280
STOPS = "。！？!?."
LABEL = {"meeting": ("会议", "Meeting"), "memo": ("语音", "Voice")}  # (zh, en)


def _link_text(text: str) -> str:
    return clean(text)[:TITLE_MAX].replace("[", "\\[").replace("]", "\\]")


def _minutes(dur_ms: Any) -> int:
    return max(1, (int(dur_ms or 0) + 30000) // 60000)


def _memo(text: str, folder: str) -> str:
    text = clean(text).strip()
    if len(text) <= MEMO_MAX:
        return text
    stop = next((i for i, ch in enumerate(text[:MEMO_MAX]) if ch in STOPS), None)
    head = text[: stop + 1] if stop is not None else text[: MEMO_MAX - 1] + "…"
    return f"{head} {T('[全文]', '[full text]')}({folder}/summary.md)"


def is_todo(e: Mapping[str, Any]) -> bool:
    """A finished memo rendered as a checkbox: sent to todo by the user, or a todo for WhisperWoof."""
    if e.get("mode") != "memo" or e.get("state") != "done":
        return False
    return e.get("dest") == "todo" or (e.get("level") == "summary" and e.get("memo_intent") == "todo")


def _done(e: Mapping[str, Any], folder: str) -> str:
    label, n = T(*LABEL[e["mode"]]), _minutes(e.get("dur_ms"))
    if e.get("level") == "transcript":
        return T(f"{label}（仅转写）：[转写]({folder}/transcript.md) {n} 分钟",
                 f"{label} (transcript only): [transcript]({folder}/transcript.md) {n} min")  # fmt: skip
    if e["mode"] == "meeting":
        title, todos = _link_text(e.get("title") or label), e.get("actions_n", 0)
        return T(f"会议：[{title}]({folder}/summary.md) {n} 分钟 · 待办 {todos}",
                 f"Meeting: [{title}]({folder}/summary.md) {n} min · to-dos: {todos}")  # fmt: skip
    text = _memo(e.get("memo_text") or e.get("title") or "", folder)
    audio = f"{T('[音频]', '[audio]')}({folder}/seg-000.wav)"
    return f"{text} {audio}" if is_todo(e) else T(f"语音：{text} {audio}", f"Voice: {text} {audio}")


def _body(e: Mapping[str, Any], folder: str) -> tuple[str, str] | None:
    """(line body, block id suffix)."""
    if e.get("ev") == "rec.stale" or e.get("state") == "failed":
        label = T(*LABEL[e["mode"]])
        return T(f"{label}（未转写）[音频]({folder}/)", f"{label} (not transcribed) [audio]({folder}/)"), "-u"
    return (_done(e, folder), "") if e.get("state") == "done" else None


def _filed(e: Mapping[str, Any]) -> tuple[str, str] | None:
    """(line body, block id suffix) of a rec.filed; its number and key are part of the block id, so they must be a
    positive integer and a key."""
    n = e.get("n")
    if not matches(LIMITS["project_key_pattern"], e.get("project")) or not (is_int(n) and n >= 1):
        return None
    title = clean(e.get("title") or "").strip()[:TITLE_MAX] or T(*LABEL[e["mode"]])
    return T(f"↪ 归档：{title}", f"↪ Filed: {title}"), f"-f{n}-{e['project']}"


def rec_entry(e: Mapping[str, Any], tz: tzinfo | None = None) -> tuple[str, str, str] | None:
    """(daily note file name, line, block id) for rec.result / rec.stale / rec.filed; None for anything else."""
    if e.get("ev") not in ("rec.result", "rec.stale", "rec.filed") or e.get("mode") not in LABEL:
        return None
    rendered = _filed(e) if e["ev"] == "rec.filed" else _body(e, str(e["dir"]))
    if rendered is None:
        return None
    body, suffix = rendered
    when = datetime.fromtimestamp(int(e["started_ms"]) / 1000, tz)
    mark = "~" if e.get("uncertain") else ""
    tag = f"#{e['project']} " if e.get("project") else ""
    box = "[ ] " if is_todo(e) else ""
    marker = block_id(e["rec"]) + suffix
    return f"{when:%Y-%m-%d}.md", f"- {box}{mark}{when:%H:%M} {tag}{body} {marker}", marker
