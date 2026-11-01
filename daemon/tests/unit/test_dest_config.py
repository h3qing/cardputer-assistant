"""destinations.json and the catalog (docs/14 §4, §2.2, §3.1): strict parsing, defaults, immutable edits."""

from __future__ import annotations

import json
from dataclasses import replace

import pytest

from cpad.config.settings import Paths
from cpad.dest import catalog as c
from cpad.dest.config import FILE_NAME, dests_path, dump_dests, load_catalog, load_dests, parse_dests, save_dests
from cpad.dest.model import BUILTIN_IDS, FIXED_KEYS, AppOpts, Dest, FileOpts, UrlOpts, flags, kind_of

SB = {"id": "file.sb", "slot": 7, "key": "o", "label": "reading-list", "enabled": True, "leaves_mac": True,
      "optin": "2026-10-22T09:14:03.120+00:00",
      "file": {"root": "~/Notes/reading-list", "path": "inbox/cardputer.md"}}  # fmt: skip
URL = {
    "template": "test-scheme://app/v1/prompt?text={text}&id={p}",
    "bundle": "invalid.example.chat",
    "team": "ABCDE12345",
    "effect": "draft",
    "verified": None,
    "focus": "test-scheme://app/v1/agent?id={p}",
    "max_text_utf16": 1000,
    "max_url": 2048,
    "overflow": "clip",
    "map": {"*": "bot_1", "cpa": "bot-2"},
    "preset": "test-chat",
}
CHAT = {
    "id": "url.chat",
    "slot": 8,
    "key": "x",
    "label": "Chat",
    "enabled": True,
    "leaves_mac": True,
    "optin": "2026-10-22T09:14:03.120+00:00",
    "profiles": ["home"],
    "url": URL,
}
NOTES = {
    "id": "app.notes",
    "slot": 9,
    "key": None,
    "label": "Notes",
    "enabled": True,
    "leaves_mac": False,
    "optin": None,
    "app": {"bundle": "invalid.example.notes", "team": None},
}


def doc(*dests: dict, version: int = 1) -> dict:
    return {"version": version, "dests": list(dests)}


def file_dest(**changes) -> Dest:
    base = Dest("file.sb", "file", "reading-list", 7, key="o", leaves_mac=True, optin="2026-10-22T09:14:03.120+00:00",
                file=FileOpts("~/Notes/reading-list", "inbox/cardputer.md"))  # fmt: skip
    return replace(base, **changes)


# --- ids -------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("dest_id", "kind"),
    [("daily", "daily"), ("clip", "clip"), ("file.sb", "file"), ("url.grok", "url"), ("app.notion", "app"),
     ("paste", "paste"), ("paste.ed", "paste"), ("file.0-a", "file"), ("file." + "a" * 12, "file"),
     ("file", None), ("url", None), ("app", None), ("daily.x", None), ("clip.x", None), ("mail.x", None),
     ("plugin.todoist", None), ("File.sb", None), ("file.-a", None), ("file." + "a" * 13, None), ("", None),
     ("url.", None), ("unknown", None)],
)  # fmt: skip
def test_kind_of(dest_id, kind):
    assert kind_of(dest_id) == kind


# --- defaults --------------------------------------------------------------------------------


def test_defaults_are_the_builtins_sorted_by_slot():
    cat = c.defaults()
    assert [d.id for d in cat.dests] == list(BUILTIN_IDS)
    assert [d.slot for d in cat.dests] == [1, 2, 3, 4, 5, 6]
    assert {d.id: d.key for d in cat.dests} == {**FIXED_KEYS, "clip": "c", "paste": None}
    assert cat.problem is None
    assert not any(d.leaves_mac for d in cat.dests)


def test_only_daily_todo_queue_session_clip_are_usable_by_default():
    assert [d.id for d in c.usable(c.defaults())] == ["daily", "todo", "queue", "session", "clip"]
    assert c.state_of(c.find(c.defaults(), "paste")) == "unavailable"


def test_missing_file_means_defaults(tmp_path):
    assert load_dests(tmp_path / FILE_NAME) == c.defaults()
    assert load_catalog(tmp_path / FILE_NAME) == c.defaults()


def test_dests_path_is_in_cpa_home(tmp_path):
    assert dests_path(Paths.at(tmp_path)) == tmp_path / "destinations.json"


# --- parse / dump ----------------------------------------------------------------------------


def test_example_document_parses_and_fills_in_builtins():
    cat = parse_dests(doc({"id": "clip", "slot": 5, "key": "c", "enabled": True}, SB))
    assert [d.id for d in cat.dests] == [*BUILTIN_IDS, "file.sb"]
    assert c.find(cat, "file.sb") == file_dest()
    assert c.by_key(cat, "o").id == "file.sb" and c.by_key(cat, "O").id == "file.sb"


def test_round_trip():
    cat = parse_dests(doc(SB, CHAT, NOTES))
    assert parse_dests(dump_dests(cat)) == cat
    assert dump_dests(cat)["dests"][-2:] == [CHAT, NOTES]
    assert parse_dests(json.loads(json.dumps(dump_dests(c.defaults())))) == c.defaults()


def test_custom_without_slot_takes_the_first_free_slot_from_seven():
    entry = {k: v for k, v in SB.items() if k != "slot"}
    other = {**entry, "id": "file.b", "key": None, "slot": 7}
    cat = parse_dests(doc(other, entry))
    assert c.find(cat, "file.sb").slot == 8


def test_custom_label_defaults_to_its_name():
    entry = {k: v for k, v in SB.items() if k != "label"}
    assert c.find(parse_dests(doc(entry)), "file.sb").label == "sb"


def test_url_and_app_entries():
    cat = parse_dests(doc(CHAT, NOTES))
    chat, notes = c.find(cat, "url.chat"), c.find(cat, "app.notes")
    assert chat.url == UrlOpts(URL["template"], "invalid.example.chat", "ABCDE12345", "draft", None, URL["focus"],
                               1000, 2048, "clip", (("*", "bot_1"), ("cpa", "bot-2")), "test-chat")  # fmt: skip
    assert chat.profiles == ("home",) and flags(chat) == "lck"  # not verified on this Mac yet
    assert notes.app == AppOpts("invalid.example.notes") and flags(notes) == "l"
    assert c.state_of(chat) == c.state_of(notes) == "on"


def test_a_custom_destination_leaves_the_mac_unless_told_otherwise():
    entry = {k: v for k, v in NOTES.items() if k not in ("leaves_mac", "optin")}
    notes = c.find(parse_dests(doc(entry)), "app.notes")
    assert notes.leaves_mac and c.state_of(notes) == "needs_optin"


def test_paste_may_be_named_and_waits_for_its_executor():
    ed = c.find(parse_dests(doc({"id": "paste.ed", "slot": 9})), "paste.ed")
    assert not ed.leaves_mac and flags(ed) == "l" and c.state_of(ed) == "unavailable"


@pytest.mark.parametrize(
    "bad",
    [
        None, [], {"version": 1}, {"dests": []},
        {"version": 2, "dests": []}, {"version": True, "dests": []}, {"version": "1", "dests": []},
        {"version": 1, "dests": {}},
        {"version": 1, "dests": [], "token": "x"},
        {"version": 1, "dests": [], "api_key": "x"},
    ],
)  # fmt: skip
def test_bad_documents_are_rejected(bad):
    with pytest.raises(c.DestsError):
        parse_dests(bad)


def test_newer_version_is_named_in_the_error():
    with pytest.raises(c.DestsError, match="version 2"):
        parse_dests(doc(version=2))


@pytest.mark.parametrize(
    "entry",
    [
        {**SB, "token": "secret"},
        {**SB, "api_key": "secret"},
        {**SB, "file": {**SB["file"], "token": "x"}},
        {"id": "plugin.todoist", "slot": 9, "plugin": {"name": "todoist"}},
        {**CHAT, "url": {**URL, "api_key": "x"}},
        {**CHAT, "url": {k: v for k, v in URL.items() if k != "focus"}},
        {**CHAT, "url": {**URL, "template": "test-scheme://x?text={text}&token=abc"}},
        {**CHAT, "url": {**URL, "template": "test-scheme://x?q=1"}},
        {**CHAT, "url": {**URL, "template": "test-scheme://x?a={text}&b={text}"}},
        {**CHAT, "url": {**URL, "template": "test-scheme://{text}"}},
        {**CHAT, "url": {**URL, "template": "test-scheme://x?text={text}#frag"}},
        {**CHAT, "url": {**URL, "template": "Bad://x?text={text}"}},
        {**CHAT, "url": {**URL, "template": "test-scheme://x?text={text}&v=abcdefghijklmnopq"}},
        {**CHAT, "url": {**URL, "focus": "test-scheme://x?text={text}"}},
        {**CHAT, "url": {**URL, "effect": "maybe"}},
        {**CHAT, "url": {**URL, "verified": "yes"}},
        {**CHAT, "url": {**URL, "overflow": "drop"}},
        {**CHAT, "url": {**URL, "max_url": 0}},
        {**CHAT, "url": {**URL, "max_text_utf16": True}},
        {**CHAT, "url": {**URL, "max_url": "2048"}},
        {**CHAT, "url": {**URL, "map": {"*": "has space"}}},
        {**CHAT, "url": {**URL, "map": {"Bad Key": "x"}}},
        {**CHAT, "url": {**URL, "map": {"*": 5}}},
        {**CHAT, "url": {**URL, "map": ["x"]}},
        {**CHAT, "url": {**URL, "bundle": "not a bundle"}},
        {**CHAT, "url": {**URL, "team": "short"}},
        {**CHAT, "url": {**URL, "template": 5}},
        {**CHAT, "leaves_mac": False, "optin": None},
        {**CHAT, "profiles": "home"},
        {**CHAT, "profiles": ["Home"]},
        {**CHAT, "profiles": ["home", "home"]},
        {**CHAT, "app": NOTES["app"]},
        {**NOTES, "app": {"bundle": "x.y", "team": 5}},
        {**NOTES, "app": {"bundle": "x.y"}},
        {"id": "clip", "profiles": ["home"]},
        {"id": "url.x", "slot": 7},
        {"id": "app.x", "slot": 7},
        {"id": "clip", "leaves_mac": True},
        {"id": "paste.ed", "leaves_mac": True},
        {"id": "clip", "file": {"root": "/tmp", "path": "a.md"}},
        {"id": "file.sb", "slot": 7},
        {"id": "file.sb", "slot": 7, "url": URL, "file": SB["file"]},
        {"id": "nope", "slot": 7},
        {"id": "clip.x", "slot": 7},
        {"slot": 7},
        "clip",
        {"id": "clip", "slot": "5"},
        {"id": "clip", "slot": True},
        {"id": "clip", "slot": 0},
        {"id": "clip", "slot": 13},
        {"id": "clip", "enabled": "yes"},
        {"id": "clip", "label": 5},
        {"id": "clip", "key": 3},
        {"id": "clip", "key": "C"},
        {"id": "clip", "key": "cc"},
        {**SB, "leaves_mac": "yes"},
        {**SB, "optin": "yesterday"},
        {**SB, "optin": 5},
        {**SB, "file": {"root": "~/x", "path": 5}},
        {**SB, "file": "~/x/a.md"},
    ],
)  # fmt: skip
def test_bad_entries_are_rejected(entry):
    with pytest.raises(c.DestsError):
        parse_dests(doc(entry))


def test_more_than_twelve_dests_are_rejected():
    entries = [{**SB, "id": f"file.n{i}", "slot": 7 + i % 6, "key": None} for i in range(13)]
    with pytest.raises(c.DestsError):
        parse_dests(doc(*entries))


# --- whole-catalog rules ---------------------------------------------------------------------


@pytest.mark.parametrize(
    "entries",
    [
        [SB, {**SB, "key": None}],  # duplicate id
        [SB, {**SB, "id": "file.b", "key": None}],  # duplicate slot
        [SB, {**SB, "id": "file.b", "slot": 8}],  # duplicate key
        [{**SB, "key": "c"}],  # the default clip key
        [{"id": "daily", "key": "x"}],  # fixed key changed
        [{"id": "daily", "key": None}],
        [{"id": "todo", "enabled": False}],  # fixed dest disabled
        [{**SB, "key": "a"}],  # reserved
        [{**SB, "key": "n"}],  # fixed letter
        [{**SB, "label": "x" * 25}],
        [{**SB, "label": "名" * 9}],
        [{**SB, "label": ""}],
        [{**SB, "label": 'say "hi"'}],
        [{**SB, "label": "back\\slash"}],
        [{**SB, "label": "tab\there"}],
        [{**SB, "label": "del\x7f"}],
        [{**SB, "file": {**SB["file"], "path": "/etc/a.md"}}],
        [{**SB, "file": {**SB["file"], "path": "../a.md"}}],
        [{**SB, "file": {**SB["file"], "path": "inbox/../../a.md"}}],
        [{**SB, "file": {**SB["file"], "path": ".obsidian/a.md"}}],
        [{**SB, "file": {**SB["file"], "path": ".zshrc"}}],
        [{**SB, "file": {**SB["file"], "path": "run.sh"}}],
        [{**SB, "file": {**SB["file"], "path": "a.md/"}}],
        [{**SB, "file": {**SB["file"], "path": ""}}],
        [{**SB, "file": {"root": "~/Documents/notes", "path": "a.md"}}],
        [{**SB, "file": {"root": "~/Library/Mobile Documents", "path": "a.md"}}],
        [{**SB, "file": {"root": "~", "path": "Desktop/a.md"}}],
        [{**SB, "file": {"root": "~/downloads", "path": "a.md"}}],
        [{**SB, "file": {"root": "relative/dir", "path": "a.md"}}],
        [{**SB, "file": {"root": "~/.config", "path": "a.md"}}],
        [{**SB, "file": {"root": "~other/x", "path": "a.md"}}],
        [{"id": "clip", "optin": "2026-10-22T09:14:03.120+00:00"}],  # optin on a local dest
        [{"id": "clip", "slot": 1}],  # takes daily's slot
    ],
)
def test_catalog_rules(entries):
    with pytest.raises(c.DestsError):
        parse_dests(doc(*entries))


def test_leaves_mac_without_optin_needs_optin():
    cat = parse_dests(doc({**SB, "optin": None}))
    assert c.state_of(c.find(cat, "file.sb")) == "needs_optin"
    assert "file.sb" not in [d.id for d in c.usable(cat)]


def test_local_file_needs_no_optin():
    cat = parse_dests(doc({**SB, "leaves_mac": False, "optin": None}))
    assert c.state_of(c.find(cat, "file.sb")) == "on"


def test_disabled_clip_is_off():
    cat = parse_dests(doc({"id": "clip", "enabled": False}))
    assert c.state_of(c.find(cat, "clip")) == "off"


# --- immutable edits -------------------------------------------------------------------------


def test_put_adds_or_replaces_without_touching_the_original():
    base = c.defaults()
    added = c.put(base, file_dest())
    assert c.find(base, "file.sb") is None and c.find(added, "file.sb") == file_dest()
    renamed = c.put(added, file_dest(label="sb2"))
    assert c.find(renamed, "file.sb").label == "sb2" and c.find(added, "file.sb").label == "reading-list"


def test_put_validates():
    with pytest.raises(c.DestsError):
        c.put(c.defaults(), file_dest(key="c"))


def test_remove():
    cat = c.put(c.defaults(), file_dest())
    assert c.find(c.remove(cat, "file.sb"), "file.sb") is None
    assert c.find(cat, "file.sb") is not None
    off = c.remove(cat, "clip")
    assert c.state_of(c.find(off, "clip")) == "off" and c.state_of(c.find(cat, "clip")) == "on"
    for fixed in FIXED_KEYS:
        with pytest.raises(c.DestsError, match="fixed"):
            c.remove(cat, fixed)
    with pytest.raises(c.DestsError):
        c.remove(cat, "file.nope")


def test_bind_never_steals_a_key():
    cat = c.put(c.defaults(), file_dest())
    with pytest.raises(c.DestsError, match="file.sb"):
        c.bind(cat, "clip", "o")
    moved = c.bind(c.bind(cat, "file.sb", None), "clip", "O")
    assert c.find(moved, "clip").key == "o" and c.find(moved, "file.sb").key is None
    assert c.find(cat, "clip").key == "c"


@pytest.mark.parametrize(("dest_id", "key"), [("daily", "x"), ("daily", None), ("clip", "a"), ("clip", "n"),
                                              ("clip", "1"), ("file.nope", "x")])  # fmt: skip
def test_bind_refuses(dest_id, key):
    with pytest.raises(c.DestsError):
        c.bind(c.defaults(), dest_id, key)


def test_bind_fixed_to_its_own_letter_is_a_no_op():
    assert c.bind(c.defaults(), "daily", "n") == c.defaults()


def test_free_slot():
    cat = c.put(c.defaults(), file_dest())
    assert c.free_slot(c.defaults()) == 7 and c.free_slot(cat) == 8
    full = cat
    for i, slot in enumerate(range(8, 13)):
        full = c.put(full, file_dest(id=f"file.n{i}", slot=slot, key=None))
    with pytest.raises(c.DestsError):
        c.free_slot(full)


# --- files -----------------------------------------------------------------------------------


def test_save_is_0600_and_loads_back(tmp_path):
    path = tmp_path / FILE_NAME
    cat = c.put(c.defaults(), file_dest())
    save_dests(path, cat)
    assert oct(path.stat().st_mode & 0o777) == "0o600"
    assert load_dests(path) == cat


BROKEN = [
    "{not json", '{"version": 9, "dests": []}', '{"version": 1, "dests": [{"id": "x"}]}',
    json.dumps(doc({"id": "clip", "slot": 5, "key": None, "enabled": False}, SB | {"key": "c"}, version=2)),
    json.dumps(doc({"id": "clip", "slot": 5, "key": None, "enabled": False, "typo": 1}, SB | {"key": "c"})),
]  # fmt: skip


@pytest.mark.parametrize("content", BROKEN)
def test_broken_file_falls_back_to_the_fixed_four_with_the_problem(tmp_path, content):
    path = tmp_path / FILE_NAME
    path.write_text(content, encoding="utf-8")
    with pytest.raises(c.DestsError):
        load_dests(path)
    cat = load_catalog(path)
    assert cat.dests == c.fail_closed().dests and cat.problem
    assert [d.id for d in c.usable(cat)] == ["daily", "todo", "queue", "session"]


@pytest.mark.parametrize("content", BROKEN)
def test_a_broken_file_never_makes_a_live_kind_usable(tmp_path, content):
    """The user turned clip off and gave c to a file; a newer or mistyped file must not turn clip back on."""
    path = tmp_path / FILE_NAME
    path.write_text(content, encoding="utf-8")
    cat = load_catalog(path)
    live = [d for d in cat.dests if "l" in flags(d)]
    assert live and all(c.state_of(d) != "on" and d.key is None for d in live)
    assert c.by_key(cat, "c") is None


def test_fail_closed_keeps_every_builtin_so_it_validates():
    assert c.validate(c.fail_closed().dests) == c.fail_closed()
    assert [d.id for d in c.fail_closed().dests] == list(BUILTIN_IDS)
