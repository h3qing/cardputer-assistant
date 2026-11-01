"""Command flows (I/O shell): each takes the parsed args and Deps, says what happens, returns an exit code.

Every flow that touches the port pauses cpad first and resumes it after (daemon.py), refuses a port
another process holds (lsof), and opens it the safe way (link.py). Output is progress and device
facts only (versions, partition labels, sizes): never note content or secrets.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import time
import urllib.request
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import serial

from cpalauncher import images
from cpalauncher.console import READ_ONLY, Table, parse_partitions, read_only_command
from cpalauncher.daemon import DaemonPause, pause_seconds, wait_for_daemon
from cpalauncher.esptool import find_pio, flash_command, write_flash
from cpalauncher.flash_machine import Status
from cpalauncher.flasher import FlashReport, progress_line, run_flash, steps_reached
from cpalauncher.link import Console, Opener, PortInfo, PortLost, busy_pids, find_port, list_ports
from cpalauncher.release import LAUNCHER, FetchError, Release, default_cache_dir, fetch, verify_file
from cpalauncher.verify import expected_fw, verify_partitions
from cpalauncher.waiter import wait_for_launcher

EXIT_OK, EXIT_FAILED, EXIT_SETUP, EXIT_UNCONFIRMED, EXIT_NO_LAUNCHER = 0, 1, 2, 3, 4
REPO_ROOT = Path(__file__).resolve().parents[3]
PORT_FREE_WAIT_S = 10.0  # cpad lets go within a scan period after `cpa pause`
TABLE_WAIT_S = 5.0
CMD_WAIT_S = 2.0
PRESS = "press RST, then Enter on the boot screen"
NOT_REACHED = (
    "error: the Launcher console did not answer in time. Press RST, then Enter on the boot screen, and run "
    "again. If the screen stays dark after RST, the chip may be stuck in download mode: needs power cycle "
    "(switch OFF, unplug USB, wait a few seconds, plug in again; docs/03 §2)"
)
FAILED_HINT = (
    "nothing new was selected to boot; the Launcher keeps what it had. If the transfer stopped, the boot "
    "screen may have timed out (press Enter on it) or the cable moved; run the same command again"
)


@dataclass(frozen=True)
class Deps:
    say: Callable[[str], None] = print
    clock: Callable[[], float] = time.monotonic
    sleep: Callable[[float], None] = time.sleep
    run: Callable[..., Any] = subprocess.run
    which: Callable[[str], str | None] = shutil.which
    ports: Callable[[], Iterable[PortInfo]] = list_ports
    serial_factory: Callable[[], Any] = serial.Serial
    urlopen: Callable[..., Any] = urllib.request.urlopen
    home: Path = field(default_factory=Path.home)
    env: Mapping[str, str] = field(default_factory=lambda: dict(os.environ))
    repo_root: Path = REPO_ROOT
    known: Mapping[str, images.Known] = field(default_factory=lambda: dict(images.KNOWN))
    launcher: Release = LAUNCHER


# ---- shared steps -----------------------------------------------------------------------------------


def _cache_dir(args: argparse.Namespace, deps: Deps) -> Path:
    return Path(args.cache_dir).expanduser() if args.cache_dir else default_cache_dir(deps.env, deps.home)


def _pause(args: argparse.Namespace, deps: Deps, seconds: int) -> DaemonPause:
    return DaemonPause(seconds, run=deps.run, which=deps.which, say=deps.say, stay=args.stay_paused)


def _opener(args: argparse.Namespace, deps: Deps, device: str | None = None) -> Opener:
    return Opener(deps.ports, deps.run, deps.serial_factory, deps.clock, deps.say, device=device or args.port)


def _reach(args: argparse.Namespace, deps: Deps, timeout_s: float, how: str) -> tuple[Console, str] | None:
    deps.say(f"waiting up to {timeout_s:.0f} s for the Launcher menu: {how}")
    found = wait_for_launcher(_opener(args, deps), deps.clock, deps.sleep, timeout_s, deps.say)
    if found is None:
        deps.say(NOT_REACHED)
    return found


def _free_port(args: argparse.Namespace, deps: Deps) -> str | None:
    """The Cardputer's port once no other process holds it, or None (and why)."""
    port = args.port or find_port(deps.ports())
    if port is None:
        deps.say("error: no Cardputer port (USB 303A:1001) found; plug it in and run again")
        return None
    deadline = deps.clock() + PORT_FREE_WAIT_S
    while busy := busy_pids(port, deps.run):
        if deps.clock() >= deadline:
            deps.say(f"error: {port} is held by pid {', '.join(map(str, busy))}; close it (lsof {port}) and retry")
            return None
        deps.sleep(0.5)
    return port


def read_table(console: Console) -> Table:
    """`partitions`, parsed; an empty, incomplete table when the port drops."""
    try:
        console.drain()
        console.send("partitions")
        lines = console.collect(TABLE_WAIT_S, stop=lambda line: line.startswith("Flash size:"))
    except PortLost:
        return Table()
    return parse_partitions(lines)


def _summary(table: Table) -> str:
    apps = ", ".join(p.label for p in table.ota_apps()) or "none"
    boot = table.boot_app()
    return f"apps: {apps}; boot: {boot.label if boot else '-'}; free {sum(s for _, s in table.free)} B"


class ProgressPrinter:
    """One progress line per tenth of the image."""

    def __init__(self, say: Callable[[str], None]) -> None:
        self._say, self._last = say, 0

    def __call__(self, acked: int, size: int, seconds: float) -> None:
        if steps_reached(self._last, acked, size):
            self._say("  " + progress_line(acked, size, seconds))
        self._last = acked


# ---- fetch / flash-launcher -------------------------------------------------------------------------


def cmd_fetch(args: argparse.Namespace, deps: Deps) -> int:
    try:
        fetch(deps.launcher, _cache_dir(args, deps), deps.urlopen, deps.say)
    except FetchError as exc:
        deps.say(f"error: {exc}")
        return EXIT_SETUP
    return EXIT_OK


def cmd_flash_launcher(args: argparse.Namespace, deps: Deps) -> int:
    pio = find_pio(deps.which, deps.home)
    if pio is None:
        deps.say("error: pio not found (uv tool install platformio, docs/03 §4)")
        return EXIT_SETUP
    try:
        image = fetch(deps.launcher, _cache_dir(args, deps), deps.urlopen, deps.say)
    except FetchError as exc:
        deps.say(f"error: {exc}")
        return EXIT_SETUP
    with _pause(args, deps, pause_seconds(args.timeout, deps.launcher.size)):
        port = _free_port(args, deps)
        if port is None:
            return EXIT_SETUP
        return _write_launcher(args, deps, pio, port, image)


def _write_launcher(args: argparse.Namespace, deps: Deps, pio: str, port: str, image: Path) -> int:
    problem = verify_file(image, deps.launcher)  # once more, right before it is written
    if problem:
        deps.say(f"error: {image}: {problem}")
        return EXIT_SETUP
    deps.say(f"writing Launcher {deps.launcher.version} at 0x0 on {port} (esptool, no G0 needed)")
    code = write_flash(flash_command(pio, port, image), deps.run)
    if code != 0:
        deps.say(f"error: esptool exited {code}. If the Cardputer screen stays dark it may be stuck in download "
                 "mode: needs power cycle (switch OFF, unplug USB, wait, plug in; docs/03 §2)")  # fmt: skip
        return EXIT_FAILED
    found = _reach(args, deps, args.timeout, "no key needed, it starts by itself after the USB reset")
    if found is None:
        return EXIT_NO_LAUNCHER
    console, version = found
    try:
        table = read_table(console)
    finally:
        console.close()
    deps.say(f"Launcher {version} is running; {_summary(table)}")
    if version != deps.launcher.version:
        deps.say(f"warn: expected Launcher {deps.launcher.version}")
    return EXIT_OK


# ---- wait / cmd -------------------------------------------------------------------------------------


def cmd_wait(args: argparse.Namespace, deps: Deps) -> int:
    with _pause(args, deps, pause_seconds(args.timeout, 0)):
        found = _reach(args, deps, args.timeout, PRESS)
        if found is None:
            return EXIT_NO_LAUNCHER
        console, version = found
        console.close()
    deps.say(f"Launcher {version} menu is open")
    return EXIT_OK


def cmd_console(args: argparse.Namespace, deps: Deps) -> int:
    line = " ".join(args.command)
    if not read_only_command(line):
        deps.say(f"error: only read-only commands are sent: {', '.join(sorted(READ_ONLY))}")
        return EXIT_SETUP
    with _pause(args, deps, pause_seconds(0, 0)):
        port = _free_port(args, deps)
        console = _opener(args, deps, port)() if port else None
        if console is None:
            return EXIT_SETUP
        try:
            console.drain()
            console.send(line)
            lines = console.collect(CMD_WAIT_S, stop=lambda reply: reply.startswith("Flash size:"))
        except PortLost:
            lines = ()
        finally:
            console.close()
    for reply in lines:
        deps.say(reply)
    if not lines:
        deps.say(f"no reply: is the Launcher menu open? ({PRESS})")
        return EXIT_NO_LAUNCHER
    return EXIT_OK


# ---- install ----------------------------------------------------------------------------------------


def cmd_install(args: argparse.Namespace, deps: Deps) -> int:
    try:
        image = images.resolve(args.target, deps.repo_root, args.name, deps.known)
    except images.ImageError as exc:
        deps.say(f"error: {exc}")
        return EXIT_SETUP
    deps.say(f"{image.path.name}: {len(image.data)} B, SHA-256 {image.sha256[:16]}..., Launcher name {image.name}")
    with _pause(args, deps, pause_seconds(args.timeout, len(image.data))) as pause:
        found = _reach(args, deps, args.timeout, PRESS)
        if found is None:
            return EXIT_NO_LAUNCHER
        console, _version = found
        try:
            before, report = _flash_in_menu(console, image, deps)
        finally:
            console.close()
        if report is None:
            return EXIT_FAILED
        return _conclude(args, deps, pause, image, before, report)


def _flash_in_menu(console: Console, image: images.AppImage, deps: Deps) -> tuple[Table, FlashReport | None]:
    before = read_table(console)
    if not before.complete:
        deps.say("error: could not read the partition table (is this the Launcher menu?); nothing was sent")
        return before, None
    deps.say(f"before: {_summary(before)}")
    deps.say(f"flash firmware {image.name} {len(image.data)} (2 KB chunks, one ACK each)")
    report = run_flash(console, image.name, image.data, deps.clock, on_progress=ProgressPrinter(deps.say))
    return before, report


def _conclude(args: argparse.Namespace, deps: Deps, pause: DaemonPause, image: images.AppImage,
              before: Table, report: FlashReport) -> int:  # fmt: skip
    outcome = report.outcome
    deps.say(f"result: {outcome.status.value}: {outcome.detail} "
             f"({report.acked}/{report.size} B in {report.seconds:.1f} s)")  # fmt: skip
    if outcome.status in (Status.ERROR, Status.TIMEOUT, Status.LOST):
        deps.say(FAILED_HINT)
        return EXIT_FAILED
    can_use_daemon = deps.which("cpa") is not None and not args.stay_paused
    method = _method(args.verify, outcome.status, image.name, can_use_daemon)
    if method is None and outcome.status is Status.UNCONFIRMED:
        deps.say("not verified (--verify none): every byte was acknowledged but the OK line was lost")
        return EXIT_UNCONFIRMED
    if method is None:
        deps.say(f"installed: {image.name}; the Cardputer reboots into it")
        return EXIT_OK
    ok, detail = _verify(method, args, deps, pause, image, before)
    deps.say(f"{'verified' if ok else 'not verified'}: {detail}")
    if ok and method == "partitions":
        deps.say("the Cardputer is in the Launcher menu now: choose the app there to boot it")
    return EXIT_OK if ok else EXIT_UNCONFIRMED


def _method(verify: str, status: Status, name: str, can_use_daemon: bool) -> str | None:
    """auto: only an unconfirmed install is checked, through cpad for our firmware, else by partitions."""
    if verify == "none" or (verify == "auto" and status is Status.OK):
        return None
    if verify != "auto":
        return verify
    return "daemon" if expected_fw(name) and can_use_daemon else "partitions"


def _verify(method: str, args: argparse.Namespace, deps: Deps, pause: DaemonPause, image: images.AppImage,
            before: Table) -> tuple[bool, str]:  # fmt: skip
    if method == "daemon":
        pause.resume()
        deps.say(f"waiting up to {args.verify_timeout:.0f} s for cpad to link with {image.name}")
        return wait_for_daemon(pause, deps.clock, deps.sleep, expected_fw(image.name), args.verify_timeout)
    deps.say("to confirm, open the Launcher menu once more")
    found = _reach(args, deps, args.timeout, PRESS)
    if found is None:
        return False, "the Launcher menu was not reached"
    console, _version = found
    try:
        after = read_table(console)
    finally:
        console.close()
    verdict = verify_partitions(before, after, image.name, len(image.data))
    return verdict.ok, verdict.detail
