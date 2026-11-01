"""setup contract v1 (protocol/setup/v1) against its reference checker: every row `cpa setup check --json` prints, in
every state these facts reach, gets an ok from protocol/setup/tools/check_setup.py; the Mac app's copy is the same."""

from __future__ import annotations

import filecmp
import itertools
import json
import sys
from dataclasses import replace

import pytest

from cpad.setup import rows as r
from cpad.setup.facts import DeviceFacts, HooksFacts, NotesFacts, ServiceFacts, SttFacts
from tests.support.protocol_files import PROTOCOL
from tests.unit.test_setup_rows import GOOD, PY

V1 = PROTOCOL / "setup" / "v1"
MAC_COPY = PROTOCOL.parent / "mac" / "Fixtures" / "setup-v1"


@pytest.fixture(scope="module")
def reference():
    tools = str(PROTOCOL / "setup" / "tools")
    sys.path.insert(0, tools)
    try:
        import check_setup  # noqa: PLC0415

        contract = check_setup.load(V1)
        yield lambda data: check_setup.validate(contract, data)
    finally:
        sys.path.remove(tools)


SERVICES = (ServiceFacts(False, None, PY, False, None, "0.5.0"), ServiceFacts(True, "/o", PY, True, "0.5.0", "0.5.0"),
            ServiceFacts(True, PY, PY, False, None, "0.5.0"), ServiceFacts(True, PY, PY, True, "0.4.1", "0.5.0"),
            ServiceFacts(True, PY, PY, True, "9.0.0", "0.5.0"), GOOD.service)  # fmt: skip
NOTES = (NotesFacts("~/a", True, False, False, None), NotesFacts("~/b", True, True, True, None),
         NotesFacts("~/Documents/c", True, True, False, "Documents"),
         NotesFacts("~/d\x1b", False, True, False, None))  # fmt: skip
DEVICES = (DeviceFacts(None, ()), DeviceFacts(None, ("3F7A",)), DeviceFacts(None, ("3F7A", "7C21")),
           DeviceFacts("3F7A", ()), DeviceFacts("3F7A", ("7C21",)), GOOD.device)  # fmt: skip
STTS = (SttFacts(False, (), None), SttFacts(True, (), None), SttFacts(False, ("m",), None),
        SttFacts(True, ("m",), "Downloads"), GOOD.stt)  # fmt: skip
HOOKS = (HooksFacts(False, (), None), HooksFacts(True, (), None), HooksFacts(True, ("/o/cpa",), "/n/cpa"), GOOD.hooks)


def every_facts():
    for svc, notes, dev, stt, hooks, label, fv in itertools.product(SERVICES, NOTES, DEVICES, STTS, HOOKS,
                                                                    (None, "家‮"), (True, False, None)):  # fmt: skip
        yield replace(GOOD, service=svc, notes=notes, device=dev, stt=stt, hooks=hooks, label=label, filevault=fv)


@pytest.mark.parametrize("lang", ["en", "zh"])
def test_every_row_in_every_state_is_valid(reference, monkeypatch, lang):
    monkeypatch.setenv("CPA_LANG", lang)
    seen = set()
    for facts in every_facts():
        for row in r.rows(facts):
            line = json.dumps(row, ensure_ascii=False)
            if line in seen:
                continue
            seen.add(line)
            verdict = reference(json.loads(line))
            assert verdict.code == "ok", (line, verdict)
    assert len(seen) > 30


def test_the_mac_app_copy_matches_the_contract():
    for path in sorted(V1.rglob("*")):
        if path.is_file() and not path.name.startswith("README"):  # README.md and README.zh-CN.md are not copied
            twin = MAC_COPY / path.relative_to(V1)
            assert twin.is_file() and filecmp.cmp(path, twin, shallow=False), twin
