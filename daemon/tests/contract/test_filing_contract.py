"""Filing contract v1 (protocol/filing/v1) against the reference checker (docs/23 §7.2): every JSON document
`cpa filing ... --json`, `cpa project ls --json` and `cpa host name --json` print, successes and errors (an argv
argparse refuses included), every line of the export's data/filing.jsonl, and the structured results of the MCP
tools list_unfiled and get_filing_item (§3.5), gets an ok from protocol/filing/tools/check_filing.py. The MCP output
schemas name every field of the contract's documents: an MCP client compiles outputSchema by itself and cannot follow
a $ref into protocol/, so the shape is inlined and pinned here."""

from __future__ import annotations

import json
import sys

import pytest

from cpad.archive import export
from cpad.archive.fs import Root
from cpad.cli.main import main as cpa
from cpad.config import host
from cpad.config.settings import load_config
from cpad.mcp import backend, catalog, tools
from tests.support import archive_tree as at
from tests.support import filing_home
from tests.support.protocol_files import PROTOCOL

V1 = PROTOCOL / "filing" / "v1"
IDS = (at.MEETING, at.MEMO, at.OLD, at.FAILED, at.BLOCKED)


@pytest.fixture(scope="module")
def reference():
    tools = str(PROTOCOL / "filing" / "tools")
    sys.path.insert(0, tools)
    try:
        import check_filing  # noqa: PLC0415

        yield lambda doc, data: check_filing.validate(check_filing.load(V1), doc, data).code
    finally:
        sys.path.remove(tools)


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    host.id_key(paths.id_key)  # as cpad does at start: rows carry short device ids
    return paths


def _out(capsys, argv: list[str]) -> dict:
    cpa(argv)
    return json.loads(capsys.readouterr().out)


@pytest.mark.parametrize(
    "argv",
    [
        [],
        ["--orphans", "--filing", "all"],
        ["--filing", "hidden"],
        ["--no-text", "--limit", "1"],
        ["--text", "报价", "--filing", "all"],
        ["--filing", "done"],
    ],
    ids=["default", "all", "hidden", "page", "text", "empty"],
)
def test_ls_is_valid(home, capsys, reference, argv):
    assert reference("ls", _out(capsys, ["filing", "ls", "--json", *argv])) == "ok"


@pytest.mark.parametrize("rec", IDS)
def test_show_is_valid(home, capsys, reference, rec):
    assert reference("show", _out(capsys, ["filing", "show", rec, "--json"])) == "ok"


def test_write_results_are_valid(home, capsys, reference):
    steps = [["file", at.MEMO, "cpa"], ["file", at.MEMO, "cpa"], ["file", at.MEMO, "--none"], ["done", at.OLD],
             ["hide", at.OLD], ["reopen", at.OLD], ["reopen", at.OLD], ["file", at.FAILED, "blog", "--other"],
             ["reopen", at.BLOCKED], ["hand", at.MEMO, "cpa"], ["reopen", at.MEMO],
             ["hand", at.MEMO, "blog", "--spawn"]]  # fmt: skip
    for step in steps:
        assert reference("result", _out(capsys, ["filing", *step, "--json"])) == "ok", step


@pytest.mark.parametrize(
    "argv",
    [
        ["ls", "--from", "nope"],
        ["ls", "--project", "acme"],
        ["show", "3QZK1M7-00099-0000"],
        ["file", at.MEMO],
        ["file", at.FAILED, "cpa"],
        ["reopen", at.MEETING],
        ["hide", "x"],
        ["hand", at.MEETING, "cpa"],
        ["hand", at.MEMO],
    ],
)
def test_errors_are_valid(home, capsys, reference, argv):
    assert reference("error", _out(capsys, ["filing", *argv, "--json"])) == "ok"


def test_projects_and_their_error_are_valid(home, capsys, reference):
    assert reference("projects", _out(capsys, ["project", "ls", "--json"])) == "ok"
    home.projects.write_text("{", encoding="utf-8")
    assert reference("error", _out(capsys, ["project", "ls", "--json"])) == "ok"


def test_project_writes_and_their_errors_are_valid(home, tmp_path, capsys, reference):
    """docs/25 §8: add / rename / move / folder / rm print the projects document after the change, or an error."""
    folder = str(tmp_path)
    writes = (
        ["add", "--json", "--folder", folder, "--", "博客"],
        ["add", "--json", "--key", "loose", "--", "零散"],
        ["rename", "--json", "loose", "--", "零散的"],
        ["move", "--json", "loose", "9"],
        ["folder", "--json", "loose", folder],
        ["folder", "--json", "loose", "--none"],
        ["rm", "--json", "loose"],
    )
    for argv in writes:
        assert reference("projects", _out(capsys, ["project", *argv])) == "ok", argv
    for argv in (["add", "--json", "--", "-x"], ["move", "--json", "nope", "1"], ["folder", "--json", "x"]):
        assert reference("error", _out(capsys, ["project", *argv])) == "ok", argv


def test_host_name_and_its_error_are_valid(home, capsys, reference):
    for argv in ([], ["--", "公司"], [], ["--clear"]):
        assert reference("host", _out(capsys, ["host", "name", "--json", *argv])) == "ok", argv
    assert reference("error", _out(capsys, ["host", "name", "--json", "--", "-x"])) == "ok"


@pytest.mark.parametrize("argv", [["filing", "ls", "--json", "--net", "x"], ["project", "ls", "--json", "x"]])
def test_an_argv_this_cpa_does_not_know_is_a_valid_error(home, capsys, reference, argv):
    assert reference("error", _out(capsys, argv)) == "ok"


def test_export_rows_are_valid(home, tmp_path, reference):
    root = Root.at(load_config(home.config).notes_path())
    export.export_dir(home, root, tmp_path / "out", export.ExportOptions(audio=False))
    lines = (tmp_path / "out" / "data" / "filing.jsonl").read_bytes().split(b"\n")
    rows = [json.loads(line) for line in lines if line]
    assert {row["id"] for row in rows} == set(IDS)
    assert all(reference("row", row) == "ok" for row in rows)


@pytest.mark.parametrize(
    "raw",
    [{}, {"filing": "all", "orphans": True}, {"limit": 1}, {"query": "报价", "filing": "all"}, {"filing": "done"}],
    ids=["default", "all", "page", "text", "empty"],
)
def test_mcp_list_unfiled_is_valid(home, reference, raw):
    ctx = backend.context(home, Root.at(load_config(home.config).notes_path()))
    out = tools.call(ctx, "list_unfiled", raw)
    assert out.error is None and reference("ls", out.body) == "ok"


@pytest.mark.parametrize("rec", [at.MEETING, at.MEMO, at.OLD, at.FAILED])
def test_mcp_get_filing_item_is_valid(home, reference, rec):
    ctx = backend.context(home, Root.at(load_config(home.config).notes_path()))
    out = tools.call(ctx, "get_filing_item", {"id": rec})
    assert out.error is None and reference("show", out.body) == "ok"


def _contract(name: str) -> dict:
    return json.loads((V1 / "schema" / f"{name}.schema.json").read_text(encoding="utf-8"))


def _outputs() -> dict:
    return {t.name: t.output_schema for t in catalog.READ_TOOLS}


@pytest.mark.parametrize(("tool", "doc"), [("list_unfiled", "ls"), ("get_filing_item", "show")])
def test_mcp_output_schemas_name_every_field_of_the_contract(tool, doc):
    mcp, contract = _outputs()[tool], _contract(doc)
    assert set(contract["properties"]) <= set(mcp["properties"])
    assert set(mcp["required"]) == {"trust", *contract["required"]}


def test_mcp_rows_name_every_field_of_the_contract_row():
    outputs, row = _outputs(), _contract("row")
    listed, shown = outputs["list_unfiled"]["properties"]["rows"]["items"], outputs["get_filing_item"]["properties"]
    for mcp_row in (listed, shown["row"]):
        assert set(mcp_row["properties"]) == set(row["properties"]) and set(mcp_row["required"]) == set(row["required"])
        for nested in ("at", "env"):
            assert set(mcp_row["properties"][nested]["properties"]) == set(row["properties"][nested]["properties"])
