"""Protocol eras (docs/15 §3.3): 2026-07-28 per request through _meta, 2025-11-25 / 2025-06-18 through initialize."""

from __future__ import annotations

import pytest

from cpad.mcp import era
from cpad.mcp.era import ClientInfo, Era, Fault, Session


def _meta(version="2026-07-28", caps=None, client=None):
    meta = {era.META_VERSION: version, era.META_CAPS: caps if caps is not None else {}}
    if client is not None:
        meta[era.META_CLIENT] = client
    return {"_meta": meta}


@pytest.mark.parametrize(
    ("requested", "chosen"),
    [("2025-11-25", "2025-11-25"), ("2025-06-18", "2025-06-18"), ("2024-11-05", "2025-11-25"),
     ("2026-07-28", "2025-11-25"), ("garbage", "2025-11-25")],
)  # fmt: skip
def test_negotiate_table(requested, chosen):
    assert era.negotiate(requested) == chosen


def test_initialize_sets_the_legacy_session():
    params = {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "codex", "version": "0.98"}}
    session, version = era.initialize(Session(), params)
    assert version == "2025-06-18"
    assert session == Session("2025-06-18", ClientInfo("codex", "0.98"))


def test_second_initialize_is_invalid_request():
    session = Session("2025-11-25", ClientInfo("x", "1"))
    fault = era.initialize(session, {"protocolVersion": "2025-11-25"})
    assert fault == Fault(-32600, "Already initialized")


def test_initialize_needs_a_version_string():
    fault = era.initialize(Session(), {"protocolVersion": 5})
    assert isinstance(fault, Fault) and fault.code == -32602


def test_client_info_is_bounded_and_without_control_characters():
    info = era.client_info({"name": "evil\nname" + "x" * 200, "version": 3})
    assert info.name.startswith("evil name") and len(info.name) == era.CLIENT_FIELD_MAX
    assert info.version == ""
    assert era.client_info("nope") == era.UNKNOWN


def test_legacy_session_ignores_meta():
    session = Session("2025-11-25", ClientInfo("cursor", "1"))
    assert era.era_of(session, _meta("1999-01-01")) == Era("legacy", "2025-11-25", ClientInfo("cursor", "1"))


def test_modern_request_with_complete_meta():
    found = era.era_of(Session(), _meta(client={"name": "claude-code", "version": "2.5"}))
    assert found == Era("modern", "2026-07-28", ClientInfo("claude-code", "2.5"))


def test_modern_request_without_client_info_is_unknown():
    assert era.era_of(Session(), _meta()).client == era.UNKNOWN


@pytest.mark.parametrize(
    "params",
    [{}, {"_meta": "x"}, {"_meta": {}}, {"_meta": {era.META_VERSION: "2026-07-28"}},
     {"_meta": {era.META_CAPS: {}}}, {"_meta": {era.META_VERSION: 1, era.META_CAPS: {}}},
     {"_meta": {era.META_VERSION: "2026-07-28", era.META_CAPS: []}}],
)  # fmt: skip
def test_missing_or_bad_meta_is_invalid_params(params):
    fault = era.era_of(Session(), params)
    assert isinstance(fault, Fault) and fault.code == -32602


def test_no_handshake_and_no_meta_names_both_ways_in():
    fault = era.era_of(Session(), {})
    assert "initialize" in fault.message and "2026-07-28" in fault.message
    assert fault.data == {"supported": ["2026-07-28", "2025-11-25", "2025-06-18"]}


def test_unsupported_modern_version_lists_supported_and_requested():
    fault = era.era_of(Session(), _meta("2025-11-25"))
    assert fault == Fault(-32022, "Unsupported protocol version", {"supported": ["2026-07-28"],
                                                                   "requested": "2025-11-25"})  # fmt: skip


def test_requested_version_echo_is_bounded():
    fault = era.era_of(Session(), _meta("9" * 5000))
    assert len(fault.data["requested"]) == era.CLIENT_FIELD_MAX


def test_has_modern_version():
    assert era.has_modern_version(_meta())
    assert not era.has_modern_version({})
    assert not era.has_modern_version({"_meta": []})
