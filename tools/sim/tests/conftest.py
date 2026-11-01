"""The tests that start cpa-sim use the `built_sim` fixture: skipped without a build (failed under
CPA_SIM_REQUIRED=1, as in CI), failed with a build older than its sources."""

from pathlib import Path

import pytest

from simtools.serve import sim_bin, sim_check


@pytest.fixture(scope="session")
def built_sim() -> Path:
    action, why = sim_check()
    if action == "fail":
        pytest.fail(why)
    if action == "skip":
        pytest.skip(why)
    return sim_bin()
