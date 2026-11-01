"""agent.result `why` (#25) is additive: valid under the reference schema and still within host_reply_max."""

from __future__ import annotations

from dataclasses import replace

import pytest

from cpad.agents.claude_code.blockers import WHYS
from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.protocol.limits import LIMITS


@pytest.mark.parametrize("why", WHYS)
def test_spawn_failure_with_reason_is_valid_and_small(reference_contract, why):
    schemas, contract = reference_contract
    wire = m.to_wire(replace(m.AgentResult(re="d" + "9" * 23, ok=False, code="E_SPAWN", why=why), id="h" + "9" * 23))
    assert schemas.field_errors(contract, wire) == ()
    line = codec.encode(wire)
    assert codec.decode_line(line).ok and len(line) <= LIMITS["host_reply_max"]


def test_a_malformed_reason_is_refused():
    line = b'{"v":0,"t":"agent.result","id":"h1","re":"d1","ok":false,"code":"E_SPAWN","why":"Not Trusted!"}\n'
    decoded = codec.decode_line(line)
    assert decoded.outcome == "E_BAD_FIELD" and decoded.fields == ("why",)
