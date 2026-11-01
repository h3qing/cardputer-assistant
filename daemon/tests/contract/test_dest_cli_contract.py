"""dest-cli contract v1 (protocol/dest-cli/v1) against its reference checker: every JSON document `cpa dest pending |
send | drop | usual | test --send` and `cpa filing send` print with --json, successes, failures and refusals (an argv
argparse refuses included), gets an ok from protocol/dest-cli/tools/check_dest_cli.py."""

from __future__ import annotations

import json
import sys

import pytest

from cpad.cli.main import main as cpa
from tests.support import archive_tree as at
from tests.support import send_home as sh
from tests.support.protocol_files import PROTOCOL

V1 = PROTOCOL / "dest-cli" / "v1"


@pytest.fixture(scope="module")
def reference():
    tools = str(PROTOCOL / "dest-cli" / "tools")
    sys.path.insert(0, tools)
    try:
        import check_dest_cli  # noqa: PLC0415

        contract = check_dest_cli.load(V1)
        yield lambda doc, data: check_dest_cli.validate(contract, doc, data)
    finally:
        sys.path.remove(tools)


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = sh.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths


@pytest.fixture
def mac(monkeypatch):
    return sh.install(monkeypatch)


def check(reference, capsys, argv: list[str], doc: str) -> dict:
    capsys.readouterr()
    cpa([*argv, "--json"])
    data = json.loads(capsys.readouterr().out)
    name = "error" if data.get("ok") is False else doc
    verdict = reference(name, data)
    assert verdict.code == "ok", (argv, data, verdict)
    return data


def test_every_document_of_a_send_from_the_mac(reference, home, mac, capsys):
    fake, _ = mac
    sh.grok(verified=False)
    check(reference, capsys, ["dest", "test", "url.grok", "--send"], "tested")
    assert cpa(["dest", "verify", "url.grok", "--draft"]) == 0
    rec = sh.fresh_memo(home)
    check(reference, capsys, ["dest", "usual", "blog", "url.grok"], "usual")
    check(reference, capsys, ["dest", "usual"], "usual")
    assert check(reference, capsys, ["dest", "pending"], "pending")["rows"][0]["why"] == "usual"
    assert check(reference, capsys, ["dest", "send", rec], "sent")["outcome"] == "handed"
    assert check(reference, capsys, ["filing", "send", at.MEMO, "url.grok"], "sent")["why"] == "draft_busy"
    app = fake.apps.pop(sh.GROK.bundle)
    assert check(reference, capsys, ["filing", "send", at.MEMO, "url.grok"], "error")["error"] == "E_ALREADY"
    assert check(reference, capsys, ["filing", "send", at.MEMO, "url.grok", "--again"], "sent")["outcome"] == "failed"
    assert check(reference, capsys, ["dest", "test", "url.grok", "--send"], "tested")["handed"] is False
    fake.apps[sh.GROK.bundle] = app
    assert check(reference, capsys, ["dest", "pending"], "pending")["rows"][0]["st"] == "failed"
    check(reference, capsys, ["dest", "drop", at.MEMO], "dropped")
    assert check(reference, capsys, ["dest", "pending", "--expired"], "pending")["rows"] == []


@pytest.mark.parametrize(
    "argv",
    [["filing", "send", at.MEETING, "url.grok"], ["filing", "send", at.MEMO, "url.nope"],
     ["filing", "send", "bad id", "clip"], ["dest", "send", "3QZK1M7-00099-0000"], ["dest", "drop", at.MEMO],
     ["dest", "usual", "acme", "clip"], ["dest", "usual", "cpa", "daily"], ["dest", "send", at.MEMO, "--nope"],
     ["filing", "send", at.MEMO, "url.grok"]],
)  # fmt: skip
def test_every_refusal_is_an_error_document(reference, home, mac, capsys, argv):
    sh.grok(verified=False)
    assert check(reference, capsys, argv, "error")["ok"] is False
