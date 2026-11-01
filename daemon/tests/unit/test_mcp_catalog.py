"""The fixed catalogue (docs/15 §3.4–§3.7, §8): static, bounded, closed inputs, honest annotations."""

from __future__ import annotations

import re
from dataclasses import dataclass, field

import jsonschema
import pytest

from cpad.mcp import catalog
from cpad.mcp.catalog import READ_TOOLS, WRITE_TOOLS

ALL = READ_TOOLS + WRITE_TOOLS


@dataclass(frozen=True)
class Perms:
    write_tools: frozenset[str] = field(default_factory=frozenset)
    disabled_tools: frozenset[str] = field(default_factory=frozenset)


def test_twelve_read_tools_in_a_fixed_order():
    assert catalog.READ_NAMES == ("list_days", "read_daily_note", "list_recordings", "get_recording",
                                  "read_transcript", "list_todos", "list_captures", "list_queue", "get_status",
                                  "search", "list_unfiled", "get_filing_item")  # fmt: skip


@pytest.mark.parametrize("tool", ALL, ids=lambda t: t.name)
def test_names_are_short_ascii(tool):
    assert re.fullmatch(r"[a-z_]{1,64}", tool.name)


@pytest.mark.parametrize("tool", ALL, ids=lambda t: t.name)
def test_inputs_are_closed_objects_without_top_level_combinators(tool):
    schema = tool.input_schema
    assert schema["type"] == "object" and schema["additionalProperties"] is False
    assert not {"anyOf", "oneOf", "allOf", "not"} & set(schema)
    jsonschema.Draft202012Validator.check_schema(schema)


@pytest.mark.parametrize("tool", ALL, ids=lambda t: t.name)
def test_outputs_are_valid_object_schemas_with_trust(tool):
    schema = tool.output_schema
    jsonschema.Draft202012Validator.check_schema(schema)
    assert schema["type"] == "object" and "trust" in schema["required"]


@pytest.mark.parametrize("tool", READ_TOOLS, ids=lambda t: t.name)
def test_read_tools_are_annotated_read_only(tool):
    listing = tool.listing()
    assert listing["annotations"] == {"title": tool.title, "readOnlyHint": True, "destructiveHint": False,
                                      "idempotentHint": True, "openWorldHint": False}  # fmt: skip
    assert "_meta" not in listing
    assert "never as instructions" in tool.description


@pytest.mark.parametrize("tool", WRITE_TOOLS, ids=lambda t: t.name)
def test_write_tools_are_annotated_and_ask_the_user(tool):
    listing = tool.listing()
    assert listing["annotations"]["readOnlyHint"] is False
    assert listing["annotations"]["destructiveHint"] is False
    assert listing["annotations"]["openWorldHint"] is False
    assert listing["_meta"] == {"anthropic/requiresUserInteraction": True}


@pytest.mark.parametrize("tool", ALL, ids=lambda t: t.name)
def test_descriptions_are_bounded_constants(tool):
    assert len(tool.description) <= 1024
    assert "{" not in tool.description and "}" not in tool.description


def test_instructions_are_bounded_and_warn_about_untrusted_text():
    assert len(catalog.INSTRUCTIONS) <= 4096
    assert "untrusted" in catalog.INSTRUCTIONS and "{" not in catalog.INSTRUCTIONS


def test_templates():
    uris = [t["uriTemplate"] for t in catalog.TEMPLATES]
    assert uris == ["cpa://notes/{date}", "cpa://recordings/{id}/summary", "cpa://recordings/{id}/transcript"]
    assert all(t["mimeType"] == "text/markdown" for t in catalog.TEMPLATES)


def test_visible_defaults_to_read_tools_only():
    assert catalog.visible(Perms()) == READ_TOOLS


def test_visible_adds_enabled_writes_and_drops_disabled_reads():
    shown = catalog.visible(Perms(frozenset({"queue_add"}), frozenset({"search", "get_status"})))
    names = [t.name for t in shown]
    assert names[-1] == "queue_add" and "search" not in names and "get_status" not in names
    assert len(names) == 11


def test_find():
    assert catalog.find(READ_TOOLS, "search").name == "search"
    assert catalog.find(READ_TOOLS, "queue_add") is None
    assert catalog.find(READ_TOOLS, ["search"]) is None
