"""docs/27 §5.3 (O0, #57): the version comes from the package metadata, which `uv version` sets from the tag, so a new
release is a new wheel to uv's cache and `cpa --version` / `cpa-stt --version` tell old from new."""

from __future__ import annotations

import re
import tomllib
from importlib import metadata
from pathlib import Path

import pytest

import cpad
from cpa_stt import cli as stt_cli
from cpad.cli.main import main as cpa

PYPROJECT = Path(__file__).resolve().parents[2] / "pyproject.toml"


def project_version() -> str:
    return tomllib.loads(PYPROJECT.read_text(encoding="utf-8"))["project"]["version"]


def test_version_is_read_from_the_installed_metadata():
    assert cpad.__version__ == metadata.version("cpad")
    assert re.fullmatch(r"\d+\.\d+\.\d+", cpad.__version__)


def test_the_metadata_is_what_pyproject_says():
    assert metadata.version("cpad") == project_version()


def test_the_version_is_not_written_into_the_source():
    source = (Path(cpad.__file__)).read_text(encoding="utf-8")
    assert project_version() not in source


def test_without_metadata_the_version_says_so(monkeypatch):
    def missing(name: str) -> str:
        raise metadata.PackageNotFoundError(name)

    monkeypatch.setattr(metadata, "version", missing)
    assert cpad.installed_version() == "0+unknown"


def test_cpa_version(capsys):
    with pytest.raises(SystemExit) as done:
        cpa(["--version"])
    assert done.value.code == 0
    assert capsys.readouterr().out.strip() == f"cpa {cpad.__version__}"


def test_cpa_stt_version(capsys):
    with pytest.raises(SystemExit) as done:
        stt_cli.main(["--version"])
    assert done.value.code == 0
    assert capsys.readouterr().out.strip() == f"cpa-stt {cpad.__version__}"
