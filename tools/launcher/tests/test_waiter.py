"""Wait for the Launcher menu: poll `version` until it answers and the boot screen is gone."""

import serial

from cpalauncher.link import Console, Opener, PortInfo
from cpalauncher.waiter import QUIET_S, Presence, observe, ready, wait_for_launcher
from fakes import BANNER, FakeClock, FakeLauncher, FakeRun

# ---- Presence (pure) ----------------------------------------------------------------------------


def feed(events: list[tuple[float, str]], opened_at: float = 0.0) -> tuple[Presence, list[str]]:
    p, notices = Presence(opened_at=opened_at), []
    for now, line in events:
        p, notice = observe(p, line, now)
        if notice:
            notices.append(notice)
    return p, notices


def test_nothing_seen_is_not_ready() -> None:
    assert not ready(Presence(opened_at=0.0))


def test_menu_already_open_is_ready_after_the_quiet_time() -> None:
    p, notices = feed([(0.1, "Launcher 2.9.1"), (1.0, "Launcher 2.9.1")])
    assert not ready(p)
    p, _ = observe(p, "Launcher 2.9.1", QUIET_S + 0.1)
    assert ready(p) and p.version == "2.9.1"
    assert notices == ["Launcher 2.9.1 is answering"]


def test_boot_screen_is_not_ready_until_the_banners_stop() -> None:
    events = [(t / 2, BANNER) for t in range(10)] + [(t / 2 + 0.1, "Launcher 2.9.1") for t in range(10)]
    p, notices = feed(sorted(events))
    assert not ready(p)
    assert sum("press Enter" in n for n in notices) == 1
    p, _ = observe(p, "Launcher 2.9.1", 4.5 + QUIET_S)
    assert ready(p)


def test_a_banner_after_the_version_resets_the_quiet_time() -> None:
    p, _ = feed([(0.0, "Launcher 2.9.1"), (2.5, "Launcher 2.9.1"), (2.6, BANNER)])
    assert not ready(p)


def test_other_lines_change_nothing() -> None:
    p = Presence(opened_at=1.0)
    assert observe(p, '{"v":0,"t":"hello"}', 2.0) == (p, None)


# ---- wait_for_launcher over fake ports ----------------------------------------------------------


class Script:
    """open_console stand-in: a list of (available_from, FakeLauncher or None)."""

    def __init__(self, clock: FakeClock, plan: list[tuple[float, FakeLauncher | None]]) -> None:
        self.clock, self.plan, self.opened = clock, plan, []

    def __call__(self) -> Console | None:
        fake = next((f for start, f in reversed(self.plan) if self.clock() >= start), None)
        if fake is None or fake.lost:
            return None
        self.opened.append(fake)
        return Console(fake, self.clock)


def wait(clock: FakeClock, plan, timeout: float = 60.0):
    said: list[str] = []
    script = Script(clock, plan)
    got = wait_for_launcher(script, clock, clock.sleep, timeout, said.append)
    return got, said, script


def test_waits_for_the_port_then_the_menu() -> None:
    clock = FakeClock(0.0)
    got, said, _ = wait(clock, [(0.0, None), (3.0, FakeLauncher(clock))])
    console, version = got
    assert version == "2.9.1" and isinstance(console, Console)
    assert 3.0 + QUIET_S <= clock() < 3.0 + QUIET_S + 2.0
    assert said == ["Launcher 2.9.1 is answering"]


def test_boot_screen_then_enter() -> None:
    clock = FakeClock(0.0)
    fake = FakeLauncher(clock, banner_until=5.0)
    got, said, _ = wait(clock, [(0.0, fake)])
    assert got is not None
    assert clock() >= 5.0 + QUIET_S
    assert any("press Enter" in s for s in said)


def test_a_firmware_that_ignores_version_times_out() -> None:
    clock = FakeClock(0.0)
    silent = FakeLauncher(clock, responsive=False)
    got, _, script = wait(clock, [(0.0, silent)], timeout=10.0)
    assert got is None
    assert clock() >= 10.0 and silent.closed


class Rebooting(FakeLauncher):
    """Answers once, then the port drops (RST pressed) and stays gone."""

    def _command(self, tokens):
        super()._command(tokens)
        self.lost = True


def test_a_port_that_drops_is_reopened() -> None:
    clock = FakeClock(0.0)
    first = Rebooting(clock)
    got, _, script = wait(clock, [(0.0, first), (4.0, FakeLauncher(clock))])
    assert got is not None and len(script.opened) == 2


# ---- Opener -------------------------------------------------------------------------------------

CARDPUTER = PortInfo("/dev/cu.usbmodem1101", 0x303A, 0x1001)


def test_opener_finds_the_port_and_opens_it_safely() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock)
    opener = Opener(lambda: (CARDPUTER,), FakeRun(), lambda: fake, clock, lambda _m: None)
    console = opener()
    assert isinstance(console, Console)
    assert fake.port == "/dev/cu.usbmodem1101"


def test_opener_without_a_port_returns_none() -> None:
    assert Opener(lambda: (), FakeRun(), lambda: None, FakeClock(), lambda _m: None)() is None


def test_opener_uses_the_given_port_even_when_not_listed() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock)
    opener = Opener(lambda: (), FakeRun(), lambda: fake, clock, lambda _m: None, device="/dev/cu.other")
    assert opener() is not None and fake.port == "/dev/cu.other"


def test_opener_reports_a_busy_port_once() -> None:
    said: list[str] = []
    opener = Opener(lambda: (CARDPUTER,), FakeRun(lsof=["4242\n"]), lambda: None, FakeClock(), said.append)
    assert opener() is None and opener() is None
    assert len(said) == 1 and "4242" in said[0]


def test_opener_survives_an_open_error() -> None:
    class Refuses(FakeLauncher):
        def open(self):
            raise serial.SerialException("Resource busy")

    clock = FakeClock()
    opener = Opener(lambda: (CARDPUTER,), FakeRun(), lambda: Refuses(clock), clock, lambda _m: None)
    assert opener() is None
