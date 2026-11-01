"""Presets (docs/25 §2.3): the shipped files, the public contract in protocol/dest-preset/v1, your own presets."""

from __future__ import annotations

import json
from dataclasses import replace
from pathlib import Path
from urllib.parse import parse_qsl, urlsplit

import jsonschema
import pytest

from cpad.dest.handoff import handed_text, plan
from cpad.dest.model import Capture, UrlOpts
from cpad.dest.presets import (
    Preset,
    PresetError,
    load_deny,
    load_presets,
    map_entry,
    parse_preset,
    read_user_preset,
    shipped_presets,
)
from tests.support.protocol_files import PROTOCOL

CONTRACT = PROTOCOL / "dest-preset" / "v1"
SCHEMA = json.loads((CONTRACT / "preset.schema.json").read_text(encoding="utf-8"))
VECTORS = json.loads((CONTRACT / "vectors.json").read_text(encoding="utf-8"))
DATA = Path(__file__).resolve().parents[2] / "src" / "cpad" / "dest" / "presets" / "v1"
DENY = load_deny()


def schema_errors(doc: object) -> list[str]:
    return [e.message for e in jsonschema.Draft202012Validator(SCHEMA).iter_errors(doc)]


# --- shipped presets ------------------------------------------------------------------------------


def test_the_shipped_presets():
    found = {p.preset: p for p in shipped_presets()}
    assert set(found) == {"grok-bot", "cursor", "claude", "codex", "obsidian"}
    assert {p.kind for p in found.values()} == {"url", "file"}
    assert all(p.shipped and p.leaves_mac for p in found.values())
    assert len({p.key_hint for p in found.values()}) == len(found)


@pytest.mark.parametrize("path", sorted(p for p in DATA.glob("*.json") if p.name != "deny.json"
                                        and not p.name.endswith(".vectors.json")), ids=lambda p: p.name)  # fmt: skip
def test_every_shipped_preset_follows_the_public_schema(path):
    assert schema_errors(json.loads(path.read_text(encoding="utf-8"))) == []


def test_url_presets_claim_nothing_until_this_macs_test():
    """D12: effect absent means act until cpa dest verify --draft on this Mac."""
    urls = [p for p in shipped_presets() if p.kind == "url"]
    assert urls and all(p.effect is None and p.may_be_draft for p in urls)


def test_the_grok_bot_preset():
    grok = next(p for p in shipped_presets() if p.preset == "grok-bot")
    assert (grok.name, grok.label, grok.key_hint, grok.overflow) == ("grok", "Grok", "x", "clip")
    assert (grok.max_text_utf16, grok.max_url) == (1000, 2048)
    assert grok.template.endswith("?text={text}&id={p}") and grok.focus.endswith("agent?id={p}")
    assert map_entry(grok, "AbC_12-x", DENY) == "AbC_12-x"
    assert map_entry(grok, grok.focus.replace("{p}", "bot_7") + "&x=1", DENY) == "bot_7"
    for bad in ("xai-abcdef", "has space", "x" * 129):
        with pytest.raises(PresetError):
            map_entry(grok, bad, DENY)


# --- the Grok vectors: what Grok's own link checks accept -----------------------------------------

GROK_VECTORS = json.loads((DATA / "grok-bot.vectors.json").read_text(encoding="utf-8"))


def grok_opts(**changes) -> UrlOpts:
    grok = next(p for p in shipped_presets() if p.preset == "grok-bot")
    opts = UrlOpts(grok.template, grok.bundle, None, focus=grok.focus, max_text_utf16=grok.max_text_utf16,
                   max_url=grok.max_url, overflow=grok.overflow)  # fmt: skip
    return replace(opts, **changes)


def grok_accepts(url: str) -> bool:
    """Grok Bot's checks as docs/25 §2.3 lists them: printable ASCII, no # or \\, <= 2048, decoded values
    without control characters and with something besides space, empty parameters dropped."""
    if not all(0x21 <= ord(c) <= 0x7E for c in url) or "#" in url or "\\" in url or len(url) > 2048:
        return False
    pairs = parse_qsl(urlsplit(url).query, keep_blank_values=True)
    return all(v.strip() and not any(ord(c) < 0x20 or 0x7F <= ord(c) < 0xA0 for c in v) for _, v in pairs)


@pytest.mark.parametrize("case", GROK_VECTORS["cases"], ids=lambda c: c["name"])
def test_grok_vectors(case, monkeypatch):
    monkeypatch.setenv("CPA_LANG", case["lang"])  # the header is in the language of the moment (cpad.i18n)
    opts = grok_opts(map=tuple(case.get("map", {}).items()))
    cap = Capture("r", case["source"], "url.grok", "pick", case.get("project"), case["text"])
    planned = plan(opts, handed_text(cap), cap.project)
    assert planned.url == case["url"]
    assert planned.reason == ("ok" if case["url"] else "too_long")
    if planned.url is not None:
        assert grok_accepts(planned.url)


def test_the_grok_vectors_cover_both_languages():
    langs = [case["lang"] for case in GROK_VECTORS["cases"]]
    assert set(langs) == {"zh", "en"} and langs.count("en") >= 5


# --- the contract ---------------------------------------------------------------------------------


@pytest.mark.parametrize("case", VECTORS["valid"], ids=lambda c: c["name"])
def test_valid_vectors(case):
    assert schema_errors(case["preset"]) == []
    assert isinstance(parse_preset(case["preset"], shipped=True, deny=DENY), Preset)


@pytest.mark.parametrize("case", VECTORS["invalid"], ids=lambda c: c["name"])
def test_invalid_vectors(case):
    assert bool(schema_errors(case["preset"])) is case["schema"]
    with pytest.raises(PresetError):
        parse_preset(case["preset"], shipped=True, deny=DENY)


@pytest.mark.parametrize(
    "doc",
    [None, {"v": 1, "kind": "fax"}, {"v": 1, "kind": "url", "template": 5}],
)
def test_malformed_documents(doc):
    with pytest.raises(PresetError):
        parse_preset(doc, shipped=True, deny=DENY)


def _vector(name: str) -> dict:
    return next(c["preset"] for c in VECTORS["valid"] if c["name"] == name)


@pytest.mark.parametrize(
    "change",
    [{"leaves_mac": "yes"}, {"note": ""}, {"focus": 5}, {"overflow": "drop"}, {"max_url": 10},
     {"map": {"pattern": "("}}, {"map": {"pattern": 5}}, {"detect": "invalid.example.chat"}],
)  # fmt: skip
def test_more_refusals(change):
    with pytest.raises(PresetError):
        parse_preset({**_vector("url with every field"), **change}, shipped=True, deny=DENY)


@pytest.mark.parametrize("name", ["file", "app", "url with every field"])
def test_a_denied_app_is_refused_for_every_kind(name):
    for bundle in sorted(DENY.handlers):
        with pytest.raises(PresetError, match="may not handle"):
            parse_preset({**_vector(name), "detect": {"bundle": bundle.lower()}}, shipped=False, deny=DENY)


def test_a_file_preset_needs_a_path():
    with pytest.raises(PresetError):
        parse_preset({**_vector("file"), "file": {"path": ""}}, shipped=True, deny=DENY)


def test_a_preset_without_a_map_takes_no_values():
    with pytest.raises(PresetError):
        map_entry(parse_preset(_vector("minimal url"), shipped=True, deny=DENY), "x", DENY)


# --- your presets ---------------------------------------------------------------------------------


def write(folder: Path, name: str, doc: dict, mode: int = 0o600) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / name
    path.write_text(json.dumps(doc), encoding="utf-8")
    path.chmod(mode)
    return path


def test_your_presets_always_act_and_never_take_a_shipped_id(tmp_path):
    folder = tmp_path / "presets"
    write(folder, "a.json", {**_vector("url with every field"), "effect": "draft"})
    write(folder, "b.json", {**_vector("minimal url"), "preset": "grok-bot"})
    write(folder, "c.json", _vector("app"), mode=0o644)
    write(folder, "d.json", {**_vector("minimal url"), "preset": "test-chat"})  # the same id twice
    (folder / "e.json").write_text("{nope", encoding="utf-8")
    found = load_presets(tmp_path)
    mine = [p for p in found.presets if not p.shipped]
    assert [(p.preset, p.effect, p.may_be_draft) for p in mine] == [("test-chat", "act", False)]
    assert len(found.problems) == 4 and any("chmod 600" in p for p in found.problems)
    assert found.find("grok-bot").shipped and found.find("nope") is None


def test_no_cpa_home_means_only_the_shipped_presets(tmp_path):
    assert load_presets(None).presets == load_presets(tmp_path).presets == shipped_presets()


def test_read_user_preset_refuses_what_it_cannot_read(tmp_path):
    with pytest.raises(PresetError):
        read_user_preset(tmp_path / "missing.json", DENY, frozenset())
