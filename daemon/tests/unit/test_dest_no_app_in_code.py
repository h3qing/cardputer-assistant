"""No app in code (docs/25 §2.1, D1): connecting an app is a preset file, never a code path.

The names come from the shipped presets themselves (presets/v1/*.json), never from a list written here:
1. nowhere in cpad/ (nor mac/, once it exists): a preset's bundle id or `<scheme>://`, any case;
2. in cpad/dest/ (and mac/**/*.swift): also a preset's id, label, name or destination id (url.<name>),
   case-sensitive, as a whole word.
Exempt: cpad/agents/claude_code/** (ADR 0002's session executor). Known exception, dated 2026-10-22:
cpad/mcp/cli.py's CLIENTS names four MCP clients; it passes 1 and is outside 2, and R3 moves it into data
(docs/25 §5.8). In cpad/mcp/** and cpad/filing/** `cursor` is a paging cursor, hence 2 only checks cpad/dest;
in mac/ (the filing contract's next_cursor) the lower-case word is one too, so there only the label (`Cursor`) is
checked for it.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from cpad.dest.template import scheme_of

SRC = Path(__file__).resolve().parents[2] / "src" / "cpad"
MAC = Path(__file__).resolve().parents[3] / "mac"
DATA = SRC / "dest" / "presets" / "v1"
EXEMPT = (SRC / "agents" / "claude_code",)
# Preset ids that are also ordinary words of mac/'s code: a paging cursor is not an app.
MAC_WORDS = {"cursor"}


def presets() -> list[dict]:
    files = [p for p in sorted(DATA.glob("*.json")) if p.name != "deny.json" and not p.name.endswith(".vectors.json")]
    return [json.loads(p.read_text(encoding="utf-8")) for p in files]


def code(root: Path, pattern: str) -> list[Path]:
    found = sorted(root.rglob(pattern)) if root.is_dir() else []
    return [p for p in found if not any(p.is_relative_to(e) for e in EXEMPT)]


def needles() -> tuple[set[str], set[str]]:
    """(bundle ids and scheme://, case-insensitive), (preset ids, labels, names and kind.name, case-sensitive)."""
    loose, exact = set(), set()
    for doc in presets():
        loose.add(doc["detect"]["bundle"].lower())
        loose.update(f"{scheme_of(doc[k])}://" for k in ("template", "focus") if k in doc)
        exact.update((doc["preset"], doc["label"], doc["name"], f"{doc['kind']}.{doc['name']}"))
    return loose, exact


def test_the_needles_come_from_the_presets():
    loose, exact = needles()
    assert len(presets()) >= 5 and any(n.endswith("://") for n in loose) and exact


@pytest.mark.parametrize(
    "path", code(SRC, "*.py") + code(MAC, "*.swift"), ids=lambda p: str(p.relative_to(p.parents[2]))
)
def test_no_bundle_id_or_scheme_in_code(path):
    text = path.read_text(encoding="utf-8").lower()
    loose, _ = needles()
    assert [n for n in loose if n in text] == []


@pytest.mark.parametrize("path", code(SRC / "dest", "*.py") + code(MAC, "*.swift"), ids=lambda p: p.name)
def test_no_preset_id_or_label_in_destination_code(path):
    text = path.read_text(encoding="utf-8")
    _, exact = needles()
    names = exact - MAC_WORDS if path.is_relative_to(MAC) else exact
    assert [n for n in names if re.search(rf"(?<![\w-]){re.escape(n)}(?![\w-])", text)] == []


def test_the_guard_would_catch_one(tmp_path):
    loose, exact = needles()
    planted = "BUNDLE = " + repr(sorted(loose)[0].upper())
    assert any(n in planted.lower() for n in loose)
    assert any(re.search(rf"(?<![\w-]){re.escape(n)}(?![\w-])", f"label = '{n}'") for n in exact)
