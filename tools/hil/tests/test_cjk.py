"""The cjk scenario's sessions as cpad reads them: its inbox_counts_waiting check expects the device's
need count, which is cpad's (cpad.inbox.need), to be exactly the sessions whose status is waiting."""

import json

from cpad.agents.claude_code.parse import parse_sessions
from cpad.inbox.need import waiting

from scenarios.cjk import NAMES, _sessions


def test_only_the_waiting_sessions_need_you():
    infos = parse_sessions(json.dumps(_sessions("/p")))
    assert [i.name for i in infos] == [name for name, _, _ in NAMES]
    assert [i.name for i in infos if waiting(i)] == [name for name, _, status in NAMES if status == "waiting"]
