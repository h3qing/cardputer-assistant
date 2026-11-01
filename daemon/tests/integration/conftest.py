"""Integration fixtures: every test gets a fresh rig and always tears it down."""

from __future__ import annotations

import pytest

from tests.support.rig import make_rig


@pytest.fixture
async def rigs(tmp_path, short_tmp):
    made = []

    def build(**kwargs):
        index = len(made)
        rig = make_rig(tmp_path / f"r{index}", short_tmp / f"h{index}", **kwargs)
        made.append(rig)
        return rig

    yield build
    for rig in made:
        await rig.close()
