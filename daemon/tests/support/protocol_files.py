"""Readers for protocol/: the same files the firmware contract tests read."""

from __future__ import annotations

import base64
import json
from pathlib import Path

PROTOCOL = Path(__file__).resolve().parents[3] / "protocol"
VECTORS = PROTOCOL / "vectors" / "v0"


def canonical(msg: dict) -> bytes:
    text = json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n"
    return text.encode("utf-8", "surrogatepass")


def raw_line(case: dict) -> bytes:
    return base64.b64decode(case["line_b64"]) if "line_b64" in case else canonical(case["msg"])


def message_cases() -> list[tuple[str, str, dict]]:
    out = []
    for path in sorted((VECTORS / "messages").glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        out += [(doc["type"], "valid", case) for case in doc["valid"]]
        out += [(doc["type"], "invalid", case) for case in doc["invalid"]]
    return out


def flows() -> list[dict]:
    return [json.loads(p.read_text(encoding="utf-8")) for p in sorted((VECTORS / "flows").glob("*.json"))]
