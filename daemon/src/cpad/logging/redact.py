"""Log redaction (05 §7.5 "logs", §7.9): bodies never reach the log unless CPA_LOG_CONTENT=1.

Text-like fields (text, body, prompt) become `<text len=N sha8=xxxxxxxx>`; argv elements after `--`
are replaced the same way; stderr keeps its first 200 bytes with known secrets masked.
"""

from __future__ import annotations

import hashlib
import json
import logging
from collections.abc import Iterable, Sequence
from typing import Any

BODY_FIELDS = frozenset({"text", "body", "prompt"})
STDERR_MAX = 200


def redact_text(value: Any) -> str:
    raw = json.dumps(value, ensure_ascii=False, sort_keys=True) if not isinstance(value, str) else value
    data = raw.encode("utf-8", "surrogatepass")
    return f"<text len={len(data)} sha8={hashlib.sha256(data).hexdigest()[:8]}>"


def redact_argv(argv: Sequence[str]) -> list[str]:
    if "--" not in argv:
        return list(argv)
    cut = list(argv).index("--") + 1
    return [*argv[:cut], *(redact_text(a) for a in argv[cut:])]


def redact_stderr(stderr: str, secrets: Iterable[str] = ()) -> str:
    head = stderr.encode("utf-8", "replace")[:STDERR_MAX].decode("utf-8", "ignore")
    for secret in secrets:
        if secret:
            head = head.replace(secret, redact_text(secret))
    return head


def redact_fields(fields: dict[str, Any]) -> dict[str, Any]:
    return {k: (redact_text(v) if k in BODY_FIELDS and v is not None else v) for k, v in fields.items()}


class RedactFilter(logging.Filter):
    """Replaces body-like `extra` fields on every record before it is formatted."""

    def filter(self, record: logging.LogRecord) -> bool:
        fields = getattr(record, "fields", None)
        if isinstance(fields, dict):
            record.fields = redact_fields(fields)
        for name in BODY_FIELDS & set(vars(record)):
            setattr(record, name, redact_text(getattr(record, name)))
        return True


class JsonFormatter(logging.Formatter):
    """One JSON object per line: ts, level, logger, msg and the structured `fields`."""

    def format(self, record: logging.LogRecord) -> str:
        doc = {
            "ts": self.formatTime(record, "%Y-%m-%dT%H:%M:%S"),
            "level": record.levelname,
            "logger": record.name,
            "msg": record.getMessage(),
            **(getattr(record, "fields", None) or {}),
        }
        if record.exc_info:
            doc["exc"] = record.exc_info[0].__name__ if record.exc_info[0] else None
        return json.dumps(doc, ensure_ascii=False, default=str)
