"""cpa service install on a Mac that already runs cpad (#82): the old job must be gone before the new one is
bootstrapped, and only a running job with a new pid counts as started. launchctl and the clock are fakes."""

from __future__ import annotations

import pytest

from cpad.service import launchd
from tests.support.fake_launchd import PRINT, FakeLaunchd

TARGET = f"gui/501/{launchd.LABEL}"


def _install(tmp_path, fake: FakeLaunchd) -> launchd.Installed:
    return launchd.install(["/py", "-m", "cpad", "daemon"], tmp_path / "home", tmp_path / "LaunchAgents",
                           runner=fake, uid=501, clock=fake.clock)  # fmt: skip


def _bootstrapped_at(fake: FakeLaunchd) -> list[int]:
    return [i for i, c in enumerate(fake.calls) if c[:2] == ["launchctl", "bootstrap"]]


def test_job_reads_state_and_pid_from_launchctl_print():
    running = PRINT.format(label=launchd.LABEL, state="running", pid="\tpid = 4242\n")
    waiting = PRINT.format(label=launchd.LABEL, state="not running", pid="")
    assert launchd.job(lambda argv, **kw: launchd.Completed(0, running), TARGET) == launchd.Job(True, 4242)
    assert launchd.job(lambda argv, **kw: launchd.Completed(0, waiting), TARGET) == launchd.Job(True, None)
    assert launchd.job(lambda argv, **kw: launchd.Completed(113, ""), TARGET) == launchd.Job(False, None)


def test_fresh_install_bootstraps_without_a_bootout_and_reports_the_new_pid(tmp_path):
    fake = FakeLaunchd()
    done = _install(tmp_path, fake)
    assert done.pid == 4000 and fake.pid == 4000
    assert fake.verbs("bootout") == 0 and fake.verbs("bootstrap") == 1


def test_upgrade_waits_for_a_slow_bootout_before_bootstrapping(tmp_path):
    fake = FakeLaunchd(loaded=True, pid=1234, bootout_delay=3.0)
    done = _install(tmp_path, fake)
    assert done.pid not in (None, 1234) and fake.loaded and fake.pid == done.pid
    assert fake.verbs("bootstrap") == 1, "no bootstrap while the old job was still loaded"
    first = _bootstrapped_at(fake)[0]
    before = [c for c in fake.calls[:first] if c[:2] == ["launchctl", "print"]]
    assert len(before) >= 2, "it polled launchctl print until the old label was gone"
    assert fake.now >= 3.0


def test_the_old_pid_alone_is_not_a_start(tmp_path):
    """The bug: the check saw the old process. A job that is gone and comes back with nothing running fails."""
    fake = FakeLaunchd(loaded=True, pid=1234, dead_starts=2)
    with pytest.raises(launchd.ServiceError, match="did not start"):
        _install(tmp_path, fake)
    assert fake.verbs("bootstrap") == 2


def test_a_bootstrap_that_starts_nothing_is_retried_once(tmp_path):
    fake = FakeLaunchd(loaded=True, pid=1234, bootout_delay=1.0, dead_starts=1)
    done = _install(tmp_path, fake)
    assert done.pid == 4000 and fake.verbs("bootstrap") == 2 and fake.verbs("bootout") == 2


def test_an_old_job_that_never_goes_away_fails_clearly_without_a_bootstrap(tmp_path):
    fake = FakeLaunchd(loaded=True, pid=1234, bootout_delay=10_000.0)
    with pytest.raises(launchd.ServiceError, match="old cpad"):
        _install(tmp_path, fake)
    assert fake.verbs("bootstrap") == 0
    assert fake.now <= launchd.WAIT_S + 1


def test_the_plist_is_written_even_when_the_start_fails(tmp_path):
    """So that a second `cpa service install` (or a login) can still pick it up."""
    fake = FakeLaunchd(dead_starts=2)
    with pytest.raises(launchd.ServiceError):
        _install(tmp_path, fake)
    assert launchd.plist_path(tmp_path / "LaunchAgents").exists()
