"""Example configs with fill-in tokens (docs/22 §4.4). Pure apart from the model folder lookup.

An example is a config document whose argv may hold tokens and optional groups:
  <WHICH:name>     the program found on PATH (plus /opt/homebrew/bin and /usr/local/bin)
  <MODEL:pattern>  a file in $CPA_HOME/models matching a bare glob pattern (the last one in name order)
  <PROJECTS>       the project names from projects.json, joined with ", " (a prompt hint)
A nested list is an optional group: it is kept only when every token in it resolves; a token outside a
group that does not resolve is an error. cpa-stt knows no engine: whisper.cpp lives only in
examples/whisper-cpp.json.
"""

from __future__ import annotations

import fnmatch
import json
import re
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from importlib import resources
from pathlib import Path
from typing import Any

TOKEN = re.compile(r"<([A-Z]+)(?::([^<>]*))?>")
BARE_GLOB = re.compile(r"^[A-Za-z0-9*?._-]{1,128}$")
EXAMPLE = ("examples", "whisper-cpp.json")  # package data, read with importlib.resources (gh #57)


class TemplateError(ValueError):
    """The example cannot be filled on this Mac (the message says what is missing)."""


@dataclass(frozen=True, slots=True)
class Inputs:
    models_dir: Path
    projects: tuple[str, ...]
    which: Callable[[str], str | None]


def example() -> dict[str, Any]:
    return json.loads(resources.files("cpa_stt").joinpath(*EXAMPLE).read_text(encoding="utf-8"))


def _model(pattern: str, inputs: Inputs) -> str | None:
    if not BARE_GLOB.match(pattern) or pattern.startswith("."):
        raise TemplateError(f"model pattern must be a bare file name: {pattern!r}")
    names = sorted(p.name for p in inputs.models_dir.glob("*") if fnmatch.fnmatchcase(p.name, pattern))
    found = [n for n in names if (inputs.models_dir / n).exists()]
    return str(inputs.models_dir / found[-1]) if found else None


def _resolve(name: str, arg: str | None, inputs: Inputs) -> str | None:
    if name == "MODEL" and arg:
        return _model(arg, inputs)
    if name == "WHICH" and arg:
        return inputs.which(arg)
    if name == "PROJECTS" and arg is None:
        return ", ".join(inputs.projects) or None
    raise TemplateError(f"unknown token <{name}{':' + arg if arg is not None else ''}>")


def _missing(name: str, arg: str | None, inputs: Inputs) -> str:
    if name == "MODEL":
        return f"{arg} is not in {inputs.models_dir}: link or copy it there, or run `cpa-stt setup`"
    return f"{arg or name} was not found on this Mac"


def _element(text: str, inputs: Inputs) -> tuple[str | None, str | None]:
    """(filled element or None, why it could not be filled)."""
    out = text
    for match in TOKEN.finditer(text):
        value = _resolve(match.group(1), match.group(2), inputs)
        if value is None:
            return None, _missing(match.group(1), match.group(2), inputs)
        out = out.replace(match.group(0), value, 1)
    return out, None


def _fill_argv(argv: Any, inputs: Inputs) -> list[str]:
    if not isinstance(argv, list):
        raise TemplateError("argv must be a list")
    out: list[str] = []
    for item in argv:
        group = item if isinstance(item, list) else None
        parts = [_element(p, inputs) if isinstance(p, str) else (None, "bad") for p in (group or [item])]
        if any(why == "bad" for _, why in parts):
            raise TemplateError("argv holds strings and optional groups of strings")
        if group is None and parts[0][1] is not None:
            raise TemplateError(str(parts[0][1]))
        out += [p for p, _ in parts] if all(p is not None for p, _ in parts) else []
    return out


def fill(doc: Mapping[str, Any], inputs: Inputs) -> dict[str, Any]:
    if "argv" not in doc or doc["argv"] is None:
        return dict(doc)
    return {**doc, "argv": _fill_argv(doc["argv"], inputs)}


def model_patterns(doc: Mapping[str, Any]) -> tuple[str, ...]:
    """The <MODEL:pattern> file patterns an example needs, in order (what `cpa-stt setup` looks for)."""
    text = json.dumps(doc.get("argv"))
    found = (m.group(2) for m in TOKEN.finditer(text) if m.group(1) == "MODEL" and m.group(2))
    return tuple(dict.fromkeys(found))
