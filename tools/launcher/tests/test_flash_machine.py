"""The READY/ACK/OK state machine of `flash firmware` (pure; time is passed in)."""

import pytest

from cpalauncher.flash_machine import (
    Phase,
    SendChunk,
    Status,
    Timeouts,
    on_line,
    on_lost,
    on_tick,
    start,
)

T = Timeouts(ready_s=30.0, ack_s=15.0, result_s=60.0)


def run(size: int, lines: list[str], chunk: int = 2048):
    """Feed lines at t=0,1,2,... and collect the chunks the machine asks for."""
    state, sent = start(size, 0.0, T, chunk), []
    for t, line in enumerate(lines, start=1):
        state, action = on_line(state, line, float(t), T)
        if action:
            sent.append(action)
    return state, sent


def test_start_waits_for_ready() -> None:
    state = start(5000, 10.0, T)
    assert (state.phase, state.sent, state.acked, state.deadline) == (Phase.AWAIT_READY, 0, 0, 40.0)


@pytest.mark.parametrize(("size", "chunk"), [(0, 2048), (-1, 2048), (10, 0)])
def test_start_rejects_nonsense(size: int, chunk: int) -> None:
    with pytest.raises(ValueError):
        start(size, 0.0, T, chunk)


def test_happy_path_sends_one_chunk_per_ack() -> None:
    state, sent = run(5000, ["READY 5000", "ACK 2048/5000", "ACK 4096/5000", "ACK 5000/5000", "OK flashed, rebooting"])
    assert sent == [SendChunk(0, 2048), SendChunk(2048, 2048), SendChunk(4096, 904)]
    assert state.outcome.status is Status.OK
    assert (state.phase, state.acked) == (Phase.DONE, 5000)


def test_after_the_last_ack_it_waits_for_the_result() -> None:
    state, _ = run(100, ["READY 100", "ACK 100/100"])
    assert (state.phase, state.deadline, state.outcome) == (Phase.AWAIT_RESULT, 62.0, None)


def test_partial_acks_wait_for_the_whole_chunk() -> None:
    state, sent = run(5000, ["READY 5000", "ACK 1024/5000", "ACK 2048/5000"])
    assert sent == [SendChunk(0, 2048), SendChunk(2048, 2048)]
    assert (state.acked, state.sent) == (2048, 4096)


def test_a_partial_ack_counts_as_progress_for_the_deadline() -> None:
    state, _ = run(5000, ["READY 5000", "ACK 1024/5000"])
    assert state.deadline == 2.0 + 15.0


def test_noise_is_ignored_and_does_not_move_the_deadline() -> None:
    state, sent = run(5000, ["READY 5000", BANNER_LIKE, "I (123) wifi: init", "Launcher 2.9.1"])
    assert sent == [SendChunk(0, 2048)]
    assert (state.phase, state.deadline) == (Phase.STREAMING, 1.0 + 15.0)


BANNER_LIKE = "Press the button to enter the Launcher!"


def test_err_before_ready_is_an_error_with_the_device_text() -> None:
    state, sent = run(5000, ["ERR no space for app: too big"])
    assert sent == []
    assert (state.outcome.status, state.outcome.detail) == (Status.ERROR, "ERR no space for app: too big")


def test_ready_for_another_size_is_an_error() -> None:
    state, _ = run(5000, ["READY 4096"])
    assert state.outcome.status is Status.ERROR
    assert "4096" in state.outcome.detail


@pytest.mark.parametrize(
    "bad",
    ["ACK 4096/5000", "ACK 2048/9999", "ACK 1/5000 extra", "ACK x/5000", "READY 5000", "OK flashed, rebooting"],
)
def test_impossible_replies_while_streaming_are_errors(bad: str) -> None:
    state, _ = run(5000, ["READY 5000", "ACK 1024/5000", bad])
    assert state.outcome.status is Status.ERROR


def test_an_ack_going_backwards_is_an_error() -> None:
    state, _ = run(5000, ["READY 5000", "ACK 1024/5000", "ACK 512/5000"])
    assert state.outcome.status is Status.ERROR


def test_err_while_streaming_reports_progress() -> None:
    state, _ = run(5000, ["READY 5000", "ACK 2048/5000", "ERR Write failed"])
    assert state.outcome.status is Status.ERROR
    assert state.outcome.detail == "ERR Write failed"
    assert state.acked == 2048


def test_err_after_all_bytes_is_an_error() -> None:
    state, _ = run(100, ["READY 100", "ACK 100/100", "ERR flash failed: image verify"])
    assert state.outcome.status is Status.ERROR


def test_lines_after_done_change_nothing() -> None:
    state, _ = run(100, ["READY 100", "ACK 100/100", "OK flashed, rebooting"])
    later, action = on_line(state, "ERR late", 99.0, T)
    assert (later, action) == (state, None)


def test_ticks_before_the_deadline_change_nothing() -> None:
    state = start(100, 0.0, T)
    assert on_tick(state, 29.9) == state


@pytest.mark.parametrize(
    ("lines", "now", "status"),
    [
        ([], 30.0, Status.TIMEOUT),  # no READY
        (["READY 5000"], 16.0, Status.TIMEOUT),  # no ACK
        (["READY 100", "ACK 100/100"], 62.0, Status.UNCONFIRMED),  # every byte acked, no OK
    ],
)
def test_deadlines(lines: list[str], now: float, status: Status) -> None:
    state, _ = run(5000 if lines == ["READY 5000"] else 100, lines)
    assert on_tick(state, now).outcome.status is status


def test_timeout_detail_says_where_it_stopped() -> None:
    state, _ = run(5000, ["READY 5000", "ACK 2048/5000"])
    detail = on_tick(state, 100.0).outcome.detail
    assert "2048/5000" in detail


def test_losing_the_port_after_every_ack_is_unconfirmed() -> None:
    state, _ = run(100, ["READY 100", "ACK 100/100"])
    outcome = on_lost(state).outcome
    assert outcome.status is Status.UNCONFIRMED
    assert "reboot" in outcome.detail


@pytest.mark.parametrize("lines", [[], ["READY 5000"], ["READY 5000", "ACK 2048/5000"]])
def test_losing_the_port_earlier_is_lost(lines: list[str]) -> None:
    state, _ = run(5000, lines)
    assert on_lost(state).outcome.status is Status.LOST


def test_lost_or_tick_after_done_keeps_the_outcome() -> None:
    state, _ = run(100, ["READY 100", "ACK 100/100", "OK flashed, rebooting"])
    assert on_lost(state) == state
    assert on_tick(state, 1e9) == state


def test_state_is_immutable() -> None:
    state = start(100, 0.0, T)
    with pytest.raises(AttributeError):
        state.sent = 5  # type: ignore[misc]


def test_noise_while_waiting_for_ready_or_the_result_is_ignored() -> None:
    state = start(100, 0.0, T)
    assert on_line(state, "I (5) boot: ESP-IDF", 1.0, T) == (state, None)
    state, _ = run(100, ["READY 100", "ACK 100/100"])
    assert on_line(state, BANNER_LIKE, 3.0, T) == (state, None)
