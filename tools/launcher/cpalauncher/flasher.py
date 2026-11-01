"""Install an app image through the Launcher console: drives flash_machine over a Console (I/O shell)."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from cpalauncher import flash_machine as fm
from cpalauncher.console import check_name
from cpalauncher.link import Console, PortLost

ProgressFn = Callable[[int, int, float], None]  # acked, size, seconds since READY was asked for
POLL_S = 0.2  # how often the deadlines are checked while no line arrives
DEFAULT_TIMEOUTS = fm.Timeouts()


@dataclass(frozen=True, slots=True)
class FlashReport:
    outcome: fm.Outcome
    acked: int
    size: int
    seconds: float


def run_flash(console: Console, name: str, data: bytes, clock: Callable[[], float], *,
              timeouts: fm.Timeouts = DEFAULT_TIMEOUTS,
              on_progress: ProgressFn | None = None) -> FlashReport:  # fmt: skip
    """`flash firmware <name> <len(data)>`, then one 2 KB chunk per acknowledgement."""
    problem = check_name(name)
    if problem:
        raise ValueError(problem)
    t0 = clock()
    state = fm.start(len(data), t0, timeouts)
    try:
        console.drain()
        console.send(f"flash firmware {name} {len(data)}")
    except PortLost:
        state = fm.on_lost(state)
    state = _pump(console, state, data, clock, timeouts, lambda a: _notify(on_progress, a, len(data), clock() - t0))
    return FlashReport(state.outcome, state.acked, state.size, clock() - t0)


def _pump(console: Console, state: fm.FlashState, data: bytes, clock: Callable[[], float],
          timeouts: fm.Timeouts, progressed: Callable[[int], None]) -> fm.FlashState:  # fmt: skip
    """Feed lines and ticks until there is an outcome; a lost port is judged on the latest state."""
    try:
        while state.outcome is None:
            for line in console.poll(POLL_S):
                before = state.acked
                state, action = fm.on_line(state, line, clock(), timeouts)
                if action is not None:
                    console.write(data[action.offset : action.offset + action.length])
                if state.acked != before:
                    progressed(state.acked)
            state = fm.on_tick(state, clock())
    except PortLost:
        return fm.on_lost(state)
    return state


def _notify(on_progress: ProgressFn | None, acked: int, size: int, seconds: float) -> None:
    if on_progress is not None:
        on_progress(acked, size, seconds)


def steps_reached(before: int, after: int, size: int, steps: int = 10) -> bool:
    """True when `after` crosses into a new tenth of `size` (for throttled progress lines)."""
    return after * steps // size > before * steps // size


def progress_line(acked: int, size: int, seconds: float) -> str:
    rate = acked / seconds if seconds > 0 else 0.0
    eta = f"{(size - acked) / rate:.0f} s" if rate > 0 else "?"
    return f"{100 * acked / size:3.0f}% {acked}/{size} B  {rate / 1024:.1f} KB/s  eta {eta}"
