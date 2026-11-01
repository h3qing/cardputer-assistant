"""Regression for a CI flake: the test helper read state.json while fake-claude was rewriting it."""

import subprocess
import sys
import threading

from tests.support.fake_claude import FakeClaude


def _hammer(fake: FakeClaude, runs: int, errors: list) -> None:
    env = {**fake.env}
    for _ in range(runs):
        done = subprocess.run([sys.executable, fake.bin, "agents", "--json"], env=env, capture_output=True,
                              check=False)  # fmt: skip
        if done.returncode != 0:
            errors.append(done.stderr.decode(errors="replace"))


def test_state_reads_and_updates_survive_concurrent_fake_claude_writes(tmp_path):
    fake = FakeClaude(tmp_path / "fake")
    errors: list = []
    worker = threading.Thread(target=_hammer, args=(fake, 40, errors))
    worker.start()
    flips = 0
    while worker.is_alive():
        fake.update(marker=flips)  # read-modify-write while fake-claude rewrites the same file
        assert isinstance(fake.state()["sessions"], list)
        flips += 1
    worker.join()
    assert errors == []
    assert fake.state()["marker"] == flips - 1
