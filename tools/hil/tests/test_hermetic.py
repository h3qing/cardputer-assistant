"""#71: the HIL harness tests run on a temp HOME with stub programs first on PATH, like the daemon tests."""

import os
import shutil
from pathlib import Path

import cpa_hermetic as hermetic


def test_home_is_a_temp_dir_and_the_programs_are_stubs():
    assert os.path.realpath(Path.home()) != os.path.realpath(hermetic.original_home())
    assert [name for name in hermetic.SCRUB if name in os.environ] == []
    for name in hermetic.STUBS:
        assert shutil.which(name) == str(hermetic.stub_dir() / name)


def test_the_guard_watches_the_real_home():
    claim = os.path.join(hermetic.original_home(), "CardputerNotes", "recordings", ".transcriber")
    assert hermetic.guard().refused("open", (claim, "r", 0))
