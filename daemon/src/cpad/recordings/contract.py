"""cpad's reader of result.json and status.json (recording file contract v1, protocol/recording/v1).

Hand-written from the v1 schemas with the protocol validators (no jsonschema at runtime). The verdict
order is the contract's: not an object -> E_INVALID (root); v is not the integer 1 -> E_VERSION v and
nothing else is read; over 64 KiB -> E_INVALID (size); then field faults -> E_INVALID <paths>.
Unknown fields are always ignored (forward compatibility).
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import (
    NO_CONTROL,
    Check,
    Errors,
    Path,
    array,
    boolean,
    format_path,
    integer,
    is_int,
    nullable,
    obj,
    one_of,
    string,
)

RESULT_MAX = 65536
MONOID = string(LIMITS["monoid_pattern"])
SHA256 = string(LIMITS["sha256_pattern"])
ATTEMPT = integer(1, 1000)
MODE = one_of("memo", "meeting")
SESSION_MS = integer(0, 4294967295)
TIMESTAMP = string(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,9})?(Z|[+-][0-9]{2}:[0-9]{2})$")
BARE_NAME = string(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
LABEL = string(NO_CONTROL, min_len=1, max_len=64)
ERROR_CODES = ("E_VERSION", "E_INVALID", "E_AUDIO", "E_STT", "E_LLM", "E_IO")
OUTPUTS = {
    "transcript": ("transcript_json", "transcript_md"),
    "summary": ("transcript_json", "transcript_md", "summary"),
}


@dataclass(frozen=True, slots=True)
class Verdict:
    code: str
    fields: tuple[str, ...] = ()

    @property
    def ok(self) -> bool:
        return self.code == "ok"


def _result_rules(doc: Any, path: Path) -> Errors:
    """The schema's if/then: done needs a level and a null error; failed an error object; level -> outputs."""
    if not isinstance(doc, dict):
        return ()
    out: list[Path] = []
    state, level = doc.get("state"), doc.get("level")
    if state == "done":
        out += [(*path, "level")] if level not in OUTPUTS else []
        out += [(*path, "error")] if doc.get("error") is not None else []
    if state == "failed" and not isinstance(doc.get("error"), dict):
        out.append((*path, "error"))
    outputs = doc.get("outputs")
    if level in OUTPUTS and isinstance(outputs, dict):
        out += [(*path, "outputs", k) for k in OUTPUTS[level] if k not in outputs]
    return tuple(out)


def _result() -> Check:
    output = obj({"path": BARE_NAME, "sha256": SHA256})
    action = obj(
        {"n": integer(1, 2**53), "text": string(min_len=1, max_len=200), "confident": boolean()},
        {"owner": nullable(string(max_len=40)), "due": nullable(string(max_len=40)), "t_ms": nullable(SESSION_MS)},
    )
    pipeline = obj({}, {"app": LABEL, "stt": LABEL, "llm": nullable(LABEL), "prompt": nullable(LABEL)})
    return obj(
        {"session": MONOID, "attempt": ATTEMPT, "state": one_of("done", "failed"), "mode": MODE,
         "outputs": obj({}, {k: output for k in OUTPUTS["summary"]}), "error": nullable(
             obj({"code": one_of(*ERROR_CODES), "retryable": boolean()})), "finished_at": TIMESTAMP},
        {"level": nullable(one_of(*OUTPUTS)), "title": nullable(string(min_len=1, max_len=80)),
         "memo": nullable(obj({"text": string(min_len=1, max_len=2000), "intent": one_of("note", "todo")})),
         "actions": array(action, max_items=20), "pipeline": nullable(pipeline)},
        rules=(_result_rules,),
    )  # fmt: skip


BLOCKED_REASONS = ("busy", "model_missing", "disabled", "engine")  # engine: docs/22 §4.6
WRITER = string(r"^[a-z0-9-]{1,16}$")  # status.by: the program that wrote it (docs/22 §4.7)


def _status_rules(doc: Any, path: Path) -> Errors:
    blocked = isinstance(doc, dict) and doc.get("state") == "blocked"
    return ((*path, "reason"),) if blocked and doc.get("reason") not in BLOCKED_REASONS else ()


STATUS = obj(
    {"session": MONOID, "attempt": ATTEMPT, "state": one_of("queued", "transcribing", "summarizing", "blocked"),
     "at": TIMESTAMP},
    {"reason": nullable(one_of(*BLOCKED_REASONS)), "by": WRITER},
    rules=(_status_rules,),
)  # fmt: skip
RESULT = _result()


def _is_v1(doc: dict) -> bool:
    return is_int(doc.get("v")) and doc["v"] == 1


def _size(doc: dict) -> int:
    return len(json.dumps(doc, ensure_ascii=False, separators=(",", ":")).encode("utf-8", "surrogatepass"))


def _verdict(check: Check, doc: Any, max_bytes: int | None) -> Verdict:
    if not isinstance(doc, dict):
        return Verdict("E_INVALID", ("(root)",))
    if not _is_v1(doc):
        return Verdict("E_VERSION", ("v",))
    if max_bytes is not None and _size(doc) > max_bytes:
        return Verdict("E_INVALID", ("(size)",))
    fields = tuple(sorted({format_path(p) for p in check(doc, ())}))
    return Verdict("E_INVALID", fields) if fields else Verdict("ok")


def check_result(doc: Any) -> Verdict:
    return _verdict(RESULT, doc, RESULT_MAX)


def check_status(doc: Any) -> Verdict:
    return _verdict(STATUS, doc, None)
