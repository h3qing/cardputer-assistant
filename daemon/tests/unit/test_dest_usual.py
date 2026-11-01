"""A project's usual destination (docs/25 §3.4, D21): `usual` in destinations.json, a project key -> a destination id.

Only a preselection in the Mac's waiting list, never a send: it may name a destination with an executor on the Mac
(clip, file, url, app), and it goes away with the destination it names."""

from __future__ import annotations

from dataclasses import replace

import pytest

from cpad.dest.audit import audit_rows
from cpad.dest.catalog import DestsError, bind, defaults, put, remove, usual_of, with_usual
from cpad.dest.config import dump_dests, parse_dests
from cpad.dest.model import Dest, FileOpts
from cpad.dest.records import dest_usual

SB = Dest("file.sb", "file", "sb", 7, file=FileOpts("~/Notes/sb", "inbox.md"))
AT = "2026-10-22T09:00:00.000+00:00"


def cat_with(*pairs: tuple[str, str]):
    cat = put(defaults(), SB)
    for project, dest in pairs:
        cat = with_usual(cat, project, dest)
    return cat


def test_usual_round_trips_through_destinations_json_and_is_left_out_when_empty():
    cat = cat_with(("acmeshop", "file.sb"), ("cpa", "clip"))
    doc = dump_dests(cat)
    assert doc["usual"] == {"acmeshop": "file.sb", "cpa": "clip"}
    assert parse_dests(doc) == cat
    assert "usual" not in dump_dests(defaults())
    assert usual_of(cat, "acmeshop") == "file.sb" and usual_of(cat, "blog") is None and usual_of(cat, None) is None


def test_setting_none_forgets_a_project():
    cat = with_usual(cat_with(("cpa", "clip")), "cpa", None)
    assert cat.usual == ()
    assert with_usual(cat, "cpa", None) == cat


@pytest.mark.parametrize(
    ("project", "dest", "message"),
    [("Cpa", "clip", "project key"), ("cpa", "daily", "executor"), ("cpa", "queue", "executor"),
     ("cpa", "file.gone", "no destination"), ("cpa", "paste", "executor")],
)  # fmt: skip
def test_what_cannot_be_a_usual_destination(project, dest, message):
    with pytest.raises(DestsError, match=message):
        with_usual(put(defaults(), SB), project, dest)


@pytest.mark.parametrize(
    "usual",
    [[], {"cpa": 3}, {"Cpa": "clip"}, {"cpa": "file.gone"}, {"cpa": "todo"}, "clip"],
)
def test_a_bad_usual_rejects_the_whole_file(usual):
    doc = {**dump_dests(put(defaults(), SB)), "usual": usual}
    with pytest.raises(DestsError):
        parse_dests(doc)


def test_edits_keep_usual_and_removing_its_destination_forgets_it():
    cat = cat_with(("acmeshop", "file.sb"), ("cpa", "clip"))
    assert bind(cat, "file.sb", "o").usual == cat.usual
    assert put(cat, replace(SB, label="Second brain")).usual == cat.usual
    assert remove(cat, "file.sb").usual == (("cpa", "clip"),)
    assert remove(cat, "clip").usual == cat.usual  # turned off, not gone: the list skips it while it is off


def test_the_change_is_audited_without_text():
    record = dest_usual("cpa", "url.grok", by="cli", at=AT)
    assert record == {"ev": "dest.changed", "at": AT, "op": "usual", "id": "url.grok", "project": "cpa", "by": "cli"}
    (row,) = audit_rows([record])
    assert (row.dest, row.detail) == ("url.grok", "usual cpa -> url.grok")
    (gone,) = audit_rows([dest_usual("cpa", None, by="app", at=AT)])
    assert gone.detail == "usual cpa -> -"
