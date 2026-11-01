"""The environment stamp in the filing list and over MCP (docs/23 §3.2, §3.5, §4.2): a memo the real Recordings
service pulled over FakeFiles carries env{host, link} from its rec.archived into `cpa filing ls / show --json`,
list_unfiled, get_filing_item, list_recordings and get_recording, and every filing document stays valid v1."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

from cpad.archive.fs import Root
from cpad.cli.main import main as cpa
from cpad.config.files import write_json
from cpad.config.settings import Config, Paths
from cpad.mcp import backend, tools
from tests.support import fake_files as ff
from tests.support.protocol_files import PROTOCOL
from tests.support.rec_shell import Shell, until

MEMO = "K3M9Q2T-00042-0A1F"
V1 = PROTOCOL / "filing" / "v1"


@pytest.fixture(scope="module")
def reference():
    tools_dir = str(PROTOCOL / "filing" / "tools")
    sys.path.insert(0, tools_dir)
    try:
        import check_filing  # noqa: PLC0415

        yield lambda doc, data: check_filing.validate(check_filing.load(V1), doc, data).code
    finally:
        sys.path.remove(tools_dir)


async def _pulled(tmp: Path, *, host_label: str | None, linked: bool) -> Paths:
    """One unfiled memo pulled by cpad: recorded at the desk while linked (after the hello, before a pong), or in the
    pocket (before the hello). Then the config.json and projects.json `cpa` reads."""
    files = ff.FakeFiles()
    shell = Shell(tmp, files, config=Config(notes_dir=str(tmp / "notes"), host_label=host_label))
    if not linked:
        files.add(ff.memo(MEMO, up_ms=435000, project=None))
    async with shell.connected():  # hello at up_ms 900000
        if linked:
            files.add(ff.memo(MEMO, up_ms=905000, project=None))
            shell.sim.pong(up_ms=910000)
        await until(lambda: shell.archived())
    paths = Paths.at(shell.home)
    write_json(paths.config, {"notes_dir": shell.config.notes_dir})
    write_json(paths.projects, {"version": 1, "projects": [{"key": "cpa", "name": "Cardputer", "slot": 1,
                                                            "root": "/r"}]})  # fmt: skip
    return paths


def _cli(capsys, argv: list[str]) -> dict:
    cpa(argv)
    return json.loads(capsys.readouterr().out)


def _mcp(paths: Paths, name: str, raw: dict) -> dict:
    out = tools.call(backend.context(paths, Root.at(Path(json.loads(paths.config.read_text())["notes_dir"]))), name,
                     raw)  # fmt: skip
    assert out.error is None, out.error.body()
    return out.body


async def test_a_memo_recorded_while_linked_shows_this_macs_name_and_here_everywhere(
    short_tmp, monkeypatch, capsys, reference
):
    paths = await _pulled(short_tmp, host_label="公司", linked=True)
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    want = {"host": "公司", "link": "here"}
    ls, show = _cli(capsys, ["filing", "ls", "--json"]), _cli(capsys, ["filing", "show", MEMO, "--json"])
    assert [row["env"] for row in ls["rows"]] == [want] and show["row"]["env"] == want
    assert (reference("ls", ls), reference("show", show)) == ("ok", "ok")
    assert [row["env"] for row in _mcp(paths, "list_unfiled", {"link": "here"})["rows"]] == [want]
    assert _mcp(paths, "get_filing_item", {"id": MEMO})["row"]["env"] == want
    assert [row["env"] for row in _mcp(paths, "list_recordings", {})["recordings"]] == [want]
    assert _mcp(paths, "get_recording", {"id": MEMO})["meta"]["env"] == want


async def test_a_pocket_memo_on_an_unnamed_mac_is_unknown_and_has_no_name(short_tmp, monkeypatch, capsys, reference):
    paths = await _pulled(short_tmp, host_label=None, linked=False)
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    want = {"host": None, "link": "unknown"}
    unknown = _cli(capsys, ["filing", "ls", "--json", "--link", "unknown"])
    assert [row["env"] for row in unknown["rows"]] == [want] and reference("ls", unknown) == "ok"
    assert _cli(capsys, ["filing", "ls", "--json", "--link", "here"])["rows"] == []
    assert [row["env"] for row in _mcp(paths, "list_unfiled", {})["rows"]] == [want]
    assert _mcp(paths, "get_recording", {"id": MEMO})["meta"]["env"] == want
