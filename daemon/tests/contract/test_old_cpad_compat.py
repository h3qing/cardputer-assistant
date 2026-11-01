"""An old cpad meets a v0.4.0 device (docs/13 §12.1): the v0.3.1 daemon spec accepts the new debug.state reply.

fixtures/specs_v031.py.txt is daemon/src/cpad/protocol/specs.py as released in v0.3.1, byte for byte
(`git show v0.3.1:daemon/src/cpad/protocol/specs.py`); its digest is pinned so it cannot drift. It runs on
today's validate.py and protocol/limits.json, which v0.4.0 leaves unchanged. Unknown fields are ignored, so
the optional ui passes; a new overlay value would not, which is why menu and lock are not overlay values.
"""

from __future__ import annotations

import hashlib
import importlib.machinery
import importlib.util
from pathlib import Path
from types import ModuleType

import pytest

from tests.support.protocol_files import message_cases

FIXTURE = Path(__file__).parent / "fixtures" / "specs_v031.py.txt"
V031_SHA256 = "b5d540a1d0460c23614a5e7be832b910b76c5e3f179e518312ff5d0e904a1c30"
REPLIES = [c for t, kind, c in message_cases() if t == "debug.state" and kind == "valid" and "re" in c["msg"]]
WITH_UI = [c for c in REPLIES if c["msg"].get("ui")]


@pytest.fixture(scope="module")
def v031() -> ModuleType:
    loader = importlib.machinery.SourceFileLoader("cpad_v031_specs", str(FIXTURE))
    module = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
    loader.exec_module(module)
    return module


def test_fixture_is_the_v031_spec_byte_for_byte():
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == V031_SHA256


def test_vectors_carry_v040_replies_with_ui():
    assert len(WITH_UI) >= 2


@pytest.mark.parametrize("case", REPLIES, ids=[c["name"] for c in REPLIES])
def test_v031_spec_accepts_every_debug_state_reply(v031, case):
    assert v031.SPECS["debug.state"](case["msg"], ()) == ()


def test_v031_spec_ignores_ui_entirely(v031):
    """Even a malformed ui passes the old spec: it never looks at the field."""
    msg = {**WITH_UI[0]["msg"], "ui": {"menu": 1}}
    assert v031.SPECS["debug.state"](msg, ()) == ()


def test_v031_spec_rejects_a_new_overlay_value(v031):
    msg = {**WITH_UI[0]["msg"], "overlay": "menu"}
    assert v031.SPECS["debug.state"](msg, ()) == (("overlay",),)
