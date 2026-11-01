"""Every code cpa dest can answer the Mac app has Chinese words for (mac/.../DestFeedback.swift), so none shows raw.
Read as text, like the no-app-in-code guard; skipped where mac/ is not checked out."""

from __future__ import annotations

import re
from pathlib import Path

import pytest

SRC = Path(__file__).resolve().parents[2] / "src" / "cpad" / "dest"
MAC = Path(__file__).resolve().parents[3] / "mac" / "Sources" / "CardputerCore"
SWIFT = MAC / "Destinations" / "DestFeedback.swift"
DEVICE_ONLY = {"E_BAD_FIELD", "E_UNKNOWN_ITEM"}  # dest.send replies to the device (docs/14 §7.4), never cpa's


def codes() -> set[str]:
    found = {c for p in SRC.glob("*.py") for c in re.findall(r'"(E_[A-Z_]+)"', p.read_text(encoding="utf-8"))}
    return found - DEVICE_ONLY


@pytest.mark.skipif(not SWIFT.exists(), reason="mac/ is not here")
def test_every_code_has_words_in_the_mac_app():
    words = SWIFT.read_text(encoding="utf-8")
    assert len(codes()) > 15
    assert sorted(c for c in codes() if f'case "{c}"' not in words) == []
