"""run_flash drives the state machine over a Console; the fake Launcher plays the device."""

import os

import pytest

from cpalauncher.flash_machine import Status, Timeouts
from cpalauncher.flasher import progress_line, run_flash, steps_reached
from cpalauncher.link import Console
from fakes import BANNER, FakeClock, FakeLauncher

DATA = os.urandom(10_000)


def flash(fake: FakeLauncher, clock: FakeClock, data: bytes = DATA, name: str = "cpa-v0.2.0", **kw):
    seen: list[tuple[int, int]] = []
    report = run_flash(Console(fake, clock), name, data, clock,
                       on_progress=lambda acked, size, _s: seen.append((acked, size)), **kw)  # fmt: skip
    return report, seen


def test_happy_path_streams_every_byte_and_reports_ok() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock)
    report, seen = flash(fake, clock)
    assert report.outcome.status is Status.OK
    assert bytes(fake.payload) == DATA
    assert fake.commands == ["flash firmware cpa-v0.2.0 10000"]
    assert fake.reset_input_calls == 1  # stale lines dropped before the command
    assert (report.acked, report.size) == (10_000, 10_000)
    assert seen[-1] == (10_000, 10_000) and [a for a, _ in seen] == sorted(a for a, _ in seen)


def test_split_acks_still_finish() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock, ack_step=512)
    report, _ = flash(fake, clock)
    assert report.outcome.status is Status.OK and bytes(fake.payload) == DATA


def test_reboot_before_ok_is_unconfirmed() -> None:
    clock = FakeClock()
    report, _ = flash(FakeLauncher(clock, say_ok=False), clock)
    assert report.outcome.status is Status.UNCONFIRMED
    assert report.acked == report.size


def test_ok_then_reboot_is_ok() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock, reboot=True)
    report, _ = flash(fake, clock)
    assert report.outcome.status is Status.OK


def test_no_ok_and_no_reboot_is_unconfirmed_after_the_result_timeout() -> None:
    clock = FakeClock()
    t0 = clock()
    report, _ = flash(FakeLauncher(clock, say_ok=False, reboot=False), clock, timeouts=Timeouts(result_s=5.0))
    assert report.outcome.status is Status.UNCONFIRMED
    assert clock() - t0 >= 5.0


def test_a_stall_times_out() -> None:
    clock = FakeClock()
    report, _ = flash(FakeLauncher(clock, stall_at=4096), clock)
    assert report.outcome.status is Status.TIMEOUT
    assert report.acked == 2048


def test_a_device_error_mid_stream() -> None:
    clock = FakeClock()
    report, _ = flash(FakeLauncher(clock, fail_at=5000), clock)
    assert (report.outcome.status, report.outcome.detail) == (Status.ERROR, "ERR Write failed")


def test_no_space_is_reported_before_any_byte_is_sent() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock, no_space=True)
    report, _ = flash(fake, clock)
    assert report.outcome.status is Status.ERROR
    assert "no space" in report.outcome.detail and not fake.payload


def test_a_silent_device_times_out_waiting_for_ready() -> None:
    clock = FakeClock()
    report, _ = flash(FakeLauncher(clock, responsive=False), clock)
    assert report.outcome.status is Status.TIMEOUT
    assert "READY" in report.outcome.detail


def test_banners_do_not_disturb_the_transfer() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock, banner_until=clock() + 3600)
    report, _ = flash(fake, clock)
    assert report.outcome.status is Status.OK
    assert BANNER not in fake.commands


class Unplugged(FakeLauncher):
    def _payload_byte(self, byte: int) -> None:
        super()._payload_byte(byte)
        if len(self.payload) == 3000:
            self.lost = True


def test_unplugged_mid_stream_is_lost() -> None:
    clock = FakeClock()
    report, _ = flash(Unplugged(clock), clock)
    assert report.outcome.status is Status.LOST


def test_a_bad_name_is_refused_before_touching_the_port() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock)
    with pytest.raises(ValueError):
        flash(fake, clock, name="two words")
    assert fake.commands == []


def test_empty_data_is_refused() -> None:
    clock = FakeClock()
    with pytest.raises(ValueError):
        flash(FakeLauncher(clock), clock, data=b"")


def test_progress_line() -> None:
    line = progress_line(1_300_352, 2_600_704, 121.0)
    assert line.startswith(" 50%") and "1300352/2600704 B" in line and "10.5 KB/s" in line and "eta 121 s" in line
    assert "KB/s" in progress_line(0, 10, 0.0)


def test_steps_reached_reports_each_tenth_once() -> None:
    assert steps_reached(0, 2048, 20_480) is True
    assert steps_reached(2048, 3000, 20_480) is False
    assert steps_reached(3000, 20_480, 20_480) is True


def test_a_port_gone_before_the_command_is_lost() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock)
    fake.lost = True
    report, _ = flash(fake, clock)
    assert (report.outcome.status, report.acked) == (Status.LOST, 0)
