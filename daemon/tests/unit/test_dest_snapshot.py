"""dests.snapshot from destinations.json (docs/14 §7.2, §7.6): usable rows by slot, content rev, one line.

Held to the protocol contract: the vectors in protocol/vectors/v0/messages/dests.snapshot.json and
line_max, for any line budget.
"""

from __future__ import annotations

import json
from dataclasses import replace

import pytest

from cpad.dest import catalog as c
from cpad.dest import model, specs
from cpad.dest import snapshot as s
from cpad.dest.config import FILE_NAME
from cpad.dest.model import AppOpts, Dest, FileOpts, UrlOpts
from cpad.protocol import codec
from cpad.protocol.messages import to_wire
from tests.support.protocol_files import PROTOCOL, VECTORS

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

OPTIN = "2026-10-22T09:14:03.120+00:00"
SCHEMA = PROTOCOL / "schema" / "v0" / "dests.snapshot.schema.json"


def file_dest(name: str = "sb", slot: int = 7, **changes) -> Dest:
    base = Dest(f"file.{name}", "file", "reading-list", slot, key="o", leaves_mac=True, optin=OPTIN,
                file=FileOpts("~/Notes/reading-list", "inbox/cardputer.md"))  # fmt: skip
    return replace(base, **changes)


def with_dests(*extra: Dest) -> c.Catalog:
    return c.validate((*c.defaults().dests, *extra))


def vector(name: str) -> dict:
    doc = json.loads((VECTORS / "messages" / "dests.snapshot.json").read_text(encoding="utf-8"))
    return next(case["msg"] for case in doc["valid"] if case["name"].startswith(name))


def wire(snap: s.DestsSnapshot, msg_id: str = "h12") -> dict:
    return to_wire(replace(snap, id=msg_id))


def line_size(snap: s.DestsSnapshot) -> int:
    return len(codec.serialize(wire(snap, s.WORST_ID)))


# --- rows -------------------------------------------------------------------------------------


def test_defaults_give_the_five_builtins_with_their_flags():
    rows = s.dest_rows(c.defaults())
    assert [(r.id, r.slot, r.key) for r in rows] == [
        ("daily", 1, "n"), ("todo", 2, "t"), ("queue", 3, "q"), ("session", 4, "s"), ("clip", 5, "c"),
    ]  # fmt: skip
    assert [r.fl for r in rows] == ["", "", "p", "p", "l"]


def test_every_row_carries_its_fl_even_when_empty():
    """Firmware reads a row without fl as lck (docs/25 §4.6), so cpad always sends it."""
    rows = s.dest_rows(with_dests(file_dest(), file_dest("local", 8, key=None, leaves_mac=False, optin=None)))
    assert {r.id: r.fl for r in rows}["file.local"] == "" and {r.id: r.fl for r in rows}["file.sb"] == "c"
    assert all("fl" in row for row in wire(s.dests_snapshot(with_dests(file_dest())))["dests"])
    assert next(r for r in rows if r.id == "file.local").key is None


def test_url_and_app_rows_say_only_what_the_device_needs():
    chat = Dest(
        "url.chat",
        "url",
        "Chat",
        8,
        key="x",
        leaves_mac=True,
        optin=OPTIN,
        url=UrlOpts("test-scheme://a/b?text={text}", "invalid.example.chat", "ABCDE12345", effect="draft"),
    )
    notes = Dest("app.notes", "app", "Notes", 9, key="u", app=AppOpts("invalid.example.notes"))
    verified = replace(chat, url=replace(chat.url, verified="draft"))
    assert [r.fl for r in s.dest_rows(with_dests(chat, notes))][-2:] == ["lck", "l"]
    assert [r.fl for r in s.dest_rows(with_dests(verified))][-1] == "lc"
    line = codec.encode(wire(s.dests_snapshot(with_dests(chat, notes)), s.WORST_ID))
    assert b"test-scheme" not in line and b"invalid.example" not in line and b"ABCDE" not in line


def test_only_usable_dests_are_sent():
    cat = with_dests(
        file_dest(),
        file_dest("pending", 8, key="u", optin=None),  # leaves the Mac, not opted in yet
        file_dest("off", 9, key="x", enabled=False),
        Dest("paste.ed", "paste", "Editor", 10, key="y"),  # no executor in this cpad
    )  # fmt: skip
    cat = c.validate(replace(d, enabled=False) if d.id == "clip" else d for d in cat.dests)
    assert [r.id for r in s.dest_rows(cat)] == ["daily", "todo", "queue", "session", "file.sb"]


def test_rows_follow_slots_not_catalog_order():
    cat = with_dests(file_dest("b", 11, key="b"), file_dest("a", 8, key="f"))
    assert [r.slot for r in s.dest_rows(cat)] == [1, 2, 3, 4, 5, 8, 11]
    assert [r.slot for r in s.dest_rows(replace(cat, dests=cat.dests[::-1]))] == [1, 2, 3, 4, 5, 8, 11]


def test_c_follows_leaves_mac():
    lan = file_dest("lan", 8, key="u", leaves_mac=False, optin=None)
    rows = {r.id: r for r in s.dest_rows(with_dests(file_dest(), lan))}
    assert rows["file.sb"].fl == "c" and rows["file.lan"].fl == ""


# --- rev --------------------------------------------------------------------------------------


def test_rev_depends_only_on_the_rows():
    a = s.dests_snapshot(with_dests(file_dest()))
    b = s.dests_snapshot(with_dests(file_dest()))
    assert a.rev == b.rev == s.content_rev(a.dests)
    assert wire(a, "h1")["rev"] == wire(b, "h99")["rev"]


@pytest.mark.parametrize(
    "change",
    [{"label": "SB"}, {"key": "u"}, {"slot": 8}, {"leaves_mac": False, "optin": None}],
    ids=["label", "key", "slot", "fl"],
)
def test_rev_moves_when_a_row_changes(change):
    before = s.dests_snapshot(with_dests(file_dest()))
    after = s.dests_snapshot(with_dests(file_dest(**change)))
    assert after.rev != before.rev


def test_rev_ignores_what_the_device_never_sees():
    hidden = file_dest("hidden", 9, key="x", optin=None)
    assert s.dests_snapshot(with_dests(hidden)).rev == s.dests_snapshot(c.defaults()).rev
    moved = with_dests(replace(file_dest(), file=FileOpts("~/Notes/other", "inbox.md")))
    assert s.dests_snapshot(moved).rev == s.dests_snapshot(with_dests(file_dest())).rev


def test_rev_is_never_zero(monkeypatch):
    monkeypatch.setattr(s.zlib, "crc32", lambda _data: 0)
    assert s.content_rev(()) == 1


def test_rev_is_a_uint32():
    assert 1 <= s.dests_snapshot(c.defaults()).rev <= 0xFFFFFFFF


# --- the message ------------------------------------------------------------------------------


def test_defaults_snapshot_is_the_contract_vector():
    assert wire(s.dests_snapshot(c.defaults())) == vector("built-in defaults")


def test_design_example_rows_match_the_contract_vector():
    expected = vector("dests.snapshot (docs/14")
    assert wire(s.dests_snapshot(with_dests(file_dest())))["dests"] == expected["dests"]


def test_snapshot_line_decodes_and_carries_no_file_paths():
    line = codec.encode(wire(s.dests_snapshot(with_dests(file_dest())), s.WORST_ID))
    decoded = codec.decode_line(line)
    assert decoded.ok, decoded
    assert b"~/Notes" not in line and b"cardputer.md" not in line and OPTIN.encode() not in line


def test_largest_catalog_fits_one_line():
    # paste keeps slot 6 without an executor, so 11 rows is the most this cpad can send.
    custom = tuple(
        file_dest(f"{n:02d}" + "z" * 10, slot, key=key, label="一二三四五六七八")
        for n, (slot, key) in enumerate(zip(range(7, 13), "bfjkou", strict=True))
    )
    snap = s.dests_snapshot(with_dests(*custom))
    line = codec.encode(wire(snap, s.WORST_ID))
    assert len(snap.dests) == 11
    assert codec.decode_line(line).ok
    assert len(line) <= codec.LINE_MAX


def test_rows_that_do_not_fit_are_dropped_from_the_tail(monkeypatch):
    cat = with_dests(file_dest())
    full = s.dests_snapshot(cat)
    monkeypatch.setattr(codec, "LINE_MAX", line_size(replace(full, rev=0xFFFFFFFF, dests=full.dests[:3])))
    fitted = s.dests_snapshot(cat)
    assert fitted.dests == full.dests[:3]
    assert fitted.rev == s.content_rev(full.dests[:3])


@pytest.mark.parametrize("rows", range(1, 7))
def test_the_line_fits_whatever_rev_the_rows_hash_to(monkeypatch, rows):
    # The rev is only known after fitting, so the fit must measure with the longest rev (10 digits).
    cat = with_dests(file_dest())
    full = s.dests_snapshot(cat)
    budget = line_size(replace(full, rev=0, dests=full.dests[:rows]))
    monkeypatch.setattr(codec, "LINE_MAX", budget)
    assert line_size(s.dests_snapshot(cat)) <= budget


# --- from destinations.json -------------------------------------------------------------------


def test_load_snapshot_without_a_file_sends_the_defaults(tmp_path):
    assert s.load_snapshot(tmp_path / FILE_NAME) == s.dests_snapshot(c.defaults())


def test_load_snapshot_reads_destinations_json(tmp_path):
    path = tmp_path / FILE_NAME
    sb = {"id": "file.sb", "slot": 7, "key": "o", "label": "reading-list", "enabled": True, "leaves_mac": True,
          "optin": OPTIN, "file": {"root": "~/Notes/reading-list", "path": "inbox/cardputer.md"}}  # fmt: skip
    path.write_text(json.dumps({"version": 1, "dests": [sb]}), encoding="utf-8")
    snap = s.load_snapshot(path)
    assert wire(snap) == {**vector("dests.snapshot (docs/14"), "rev": snap.rev}


def test_broken_destinations_json_offers_only_the_fixed_four(tmp_path):
    path = tmp_path / FILE_NAME
    path.write_text('{"version": 1, "dests": [{"id": "clip", "token": "secret"}]}', encoding="utf-8")
    snap = s.load_snapshot(path)
    assert snap == s.dests_snapshot(c.fail_closed())
    assert [(r.id, r.key, r.fl) for r in snap.dests] == [("daily", "n", ""), ("todo", "t", ""),
                                                         ("queue", "q", "p"), ("session", "s", "p")]  # fmt: skip


# --- validator ↔ schema ↔ model ---------------------------------------------------------------


def test_validator_constants_match_the_schema():
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    dests = schema["properties"]["dests"]
    row = dests["items"]["properties"]
    assert dests["maxItems"] == specs.DESTS_MAX == model.DESTS_MAX
    assert row["slot"]["maximum"] == specs.SLOT_MAX
    assert schema["$defs"]["label"]["x-maxBytes"] == specs.LABEL_MAX == model.LABEL_MAX
    assert schema["$defs"]["destId"]["pattern"] == specs.DEST_ID_PATTERN == model.DEST_ID
    assert schema["$defs"]["label"]["pattern"] == specs.LABEL_PATTERN
    assert row["kind"]["pattern"] == specs.KIND_PATTERN
    assert row["key"]["anyOf"][0]["pattern"] == specs.KEY_PATTERN
    assert row["fl"]["anyOf"][0]["pattern"] == specs.FLAGS_PATTERN
    assert not {"live", "cloud", "proj"} & set(row)
