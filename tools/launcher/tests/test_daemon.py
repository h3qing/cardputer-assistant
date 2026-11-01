"""cpa pause while working, resume after; poll `cpa status --json` for the handshake (shell over a fake run)."""

import pytest

from cpalauncher.daemon import DaemonPause, pause_seconds, wait_for_daemon
from fakes import FakeClock, FakeRun


def which(name: str) -> str | None:
    return f"/u/bin/{name}" if name == "cpa" else None


def test_pause_seconds_covers_the_wait_and_a_slow_transfer() -> None:
    assert pause_seconds(1500, 2_600_704) == 1500 + 2_600_704 // 4096 + 1 + 300
    assert pause_seconds(0, 1) == 600
    assert pause_seconds(10**9, 1) == 86400


def test_pauses_then_resumes() -> None:
    run, said = FakeRun(), []
    with DaemonPause(900, run=run, which=which, say=said.append):
        assert run.made("pause") == [["/u/bin/cpa", "pause", "900"]]
    assert run.made("pause")[-1] == ["/u/bin/cpa", "pause", "1"]
    assert said == ["cpad paused for 900 s", "cpad resumed (cpa pause 1)"]


def test_a_longer_pause_is_kept_at_the_start() -> None:
    run, said = FakeRun(status=[{"running": True, "paused_s": 5000.0}]), []
    with DaemonPause(900, run=run, which=which, say=said.append):
        assert run.made("pause") == []
    assert said[0] == "cpad already paused for 5000 s"


def test_unknown_status_still_pauses() -> None:
    run = FakeRun(status=[{"running": False}])
    with DaemonPause(900, run=run, which=which, say=lambda _m: None):
        pass
    assert run.made("pause") == [["/u/bin/cpa", "pause", "900"], ["/u/bin/cpa", "pause", "1"]]


def test_stay_paused_does_not_resume() -> None:
    run, said = FakeRun(), []
    with DaemonPause(900, run=run, which=which, say=said.append, stay=True):
        pass
    assert run.made("pause") == [["/u/bin/cpa", "pause", "900"]]
    assert "cpa pause 1" in said[-1]


def test_resume_is_idempotent() -> None:
    run = FakeRun()
    with DaemonPause(900, run=run, which=which, say=lambda _m: None) as pause:
        pause.resume()
    assert len(run.made("pause")) == 2


def test_without_cpa_nothing_runs() -> None:
    run, said = FakeRun(), []
    with DaemonPause(900, run=run, which=lambda _n: None, say=said.append) as pause:
        assert pause.status() is None
    assert run.calls == [] and "not found" in said[0]


def test_a_failing_pause_is_reported_and_not_fatal() -> None:
    run, said = FakeRun(pause_code=2), []
    with DaemonPause(900, run=run, which=which, say=said.append):
        pass
    assert "exited 2" in said[0]


@pytest.mark.parametrize("error", [OSError("exec failed"), TimeoutError()])
def test_cpa_errors_are_reported_and_not_fatal(error: Exception) -> None:
    said: list[str] = []

    def broken(argv, **_k):
        raise error

    with DaemonPause(900, run=broken, which=which, say=said.append) as pause:
        assert pause.status() is None
    assert any("failed" in s for s in said)


def test_status_with_garbage_output_is_none() -> None:
    class Garbage(FakeRun):
        def __call__(self, argv, **kw):
            result = super().__call__(argv, **kw)
            result.stdout = "not json"
            return result

    assert DaemonPause(1, run=Garbage(), which=which, say=lambda _m: None).status() is None


READY = {"running": True, "link": "ready", "device": {"fw": "cpa-fw/0.2.0", "launcher": True, "store": "sd"}}


def test_wait_for_daemon_polls_until_ready() -> None:
    clock = FakeClock(0.0)
    run = FakeRun(status=[{"running": True, "link": "offline"}, {"running": True, "link": "handshaking"}, READY])
    pause = DaemonPause(1, run=run, which=which, say=lambda _m: None)
    ok, detail = wait_for_daemon(pause, clock, clock.sleep, "cpa-fw/0.2.0", timeout_s=60.0)
    assert ok and "fw=cpa-fw/0.2.0" in detail
    assert 0.0 < clock() < 60.0


def test_wait_for_daemon_gives_up_with_the_last_state() -> None:
    clock = FakeClock(0.0)
    run = FakeRun(status=[{"running": True, "link": "offline"}])
    ok, detail = wait_for_daemon(DaemonPause(1, run=run, which=which, say=lambda _m: None), clock, clock.sleep,
                                 "cpa-fw/0.2.0", timeout_s=10.0)  # fmt: skip
    assert not ok and "link offline" in detail and clock() >= 10.0
