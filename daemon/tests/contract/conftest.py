"""Fixtures for contract tests."""

from __future__ import annotations

import sys

import pytest

from tests.support.protocol_files import PROTOCOL


@pytest.fixture(scope="session")
def reference_contract():
    """The jsonschema-based reference checker from protocol/tools (dev dependency only)."""
    sys.path.insert(0, str(PROTOCOL / "tools"))
    try:
        import schemas  # noqa: PLC0415

        return schemas, schemas.load_contract(PROTOCOL)
    finally:
        sys.path.remove(str(PROTOCOL / "tools"))
