"""An installed cpad needs no checkout (gh #57): protocol/limits.json ships inside the package."""

from __future__ import annotations

import ast
import json
from importlib import resources
from pathlib import Path

import pytest

import cpad
from cpad.protocol import limits
from tests.support.protocol_files import PROTOCOL

SOURCES = Path(cpad.__file__).parent
PATH_LOOKUPS = frozenset({"__file__", "__spec__", "getfile", "getsourcefile"})


def path_lookups(source: str) -> set[str]:
    """The names and attributes in source through which a module could find files next to itself."""
    nodes = list(ast.walk(ast.parse(source)))
    names = {n.id for n in nodes if isinstance(n, ast.Name)}
    attrs = {n.attr for n in nodes if isinstance(n, ast.Attribute)}
    return (names | attrs) & PATH_LOOKUPS


def test_the_package_ships_protocol_limits_json_byte_for_byte():
    packaged = resources.files("cpad.protocol") / "limits.json"
    assert packaged.read_bytes() == (PROTOCOL / "limits.json").read_bytes()


def test_limits_are_read_from_the_package_by_default(monkeypatch):
    monkeypatch.delenv("CPA_PROTOCOL_DIR", raising=False)
    assert limits.limits_source() == resources.files("cpad.protocol") / "limits.json"
    assert dict(limits.load_limits()) == json.loads((PROTOCOL / "limits.json").read_text(encoding="utf-8"))


def test_cpa_protocol_dir_still_overrides_the_packaged_table(monkeypatch, tmp_path):
    (tmp_path / "limits.json").write_text('{"line_max": 7}', encoding="utf-8")
    monkeypatch.setenv("CPA_PROTOCOL_DIR", str(tmp_path))
    assert dict(limits.load_limits()) == {"line_max": 7}


def test_no_cpad_module_finds_files_through_its_own_path():
    """__file__ points into the checkout only when run from one; package data goes through importlib.resources."""
    for path in sorted(SOURCES.rglob("*.py")):
        assert not path_lookups(path.read_text(encoding="utf-8")), path.relative_to(SOURCES)


@pytest.mark.parametrize(
    "source",
    [
        "p = __file__",
        "import cpad\np = Path(cpad.__file__).parent",
        "p = sys.modules[__name__].__file__",
        "p = __spec__.origin",
        "p = mod.__spec__.origin",
        "import inspect\np = inspect.getfile(f)",
        "from inspect import getsourcefile\np = getsourcefile(f)",
    ],
)
def test_the_path_lookup_scan_sees_every_spelling(source):
    assert path_lookups(source)


def test_the_path_lookup_scan_passes_ordinary_code():
    assert not path_lookups("from importlib import resources\np = resources.files('cpad.protocol') / 'limits.json'")
