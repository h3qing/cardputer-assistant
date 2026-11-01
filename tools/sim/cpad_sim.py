#!/usr/bin/env python3
"""Run the real cpad on the virtual device (`cpa-sim serve`), in the foreground, until Ctrl-C.

    firmware/sim/.pio/build/sim/cpa-sim serve             # prints: pty /dev/ttysNNN
    CPA_HOME=$(mktemp -d) uv run --project daemon python tools/sim/cpad_sim.py --port /dev/ttysNNN
    CPA_HOME=<that dir> uv run --project daemon cpa status   # in another terminal

$CPA_HOME must be a temporary directory (simtools.guard); without a config.json one is written there with
notes_dir=$CPA_HOME/notes and fake-claude (state in $CPA_HOME/fake-claude/state.json). cpad sees only the
pty, so it can never open the Cardputer's real port; the user's own cpad never lists a pty.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import signal
import sys

from cpad.app import Daemon
from cpad.config.settings import load_config

from simtools.guard import GuardError, check_home, check_notes, prepare_home
from simtools.serial_pty import PtySerial, sim_lister


async def _run(daemon: Daemon) -> None:
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop.set)
    await daemon.run(stop)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="cpad_sim.py", description="cpad on cpa-sim serve's pty (gh #32)")
    p.add_argument("--port", required=True, help="the pty path cpa-sim serve printed")
    args = p.parse_args(argv)
    try:
        home = check_home(os.environ.get("CPA_HOME"))
        paths = prepare_home(home, os.environ.get("PATH", "/usr/bin:/bin"))
        check_notes(load_config(paths.config))
    except GuardError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    os.environ["CPA_HOME"] = str(home)  # anything in this process that reads it stays in the temp home
    print(f"cpad on {args.port}, CPA_HOME={home}", file=sys.stderr)
    asyncio.run(_run(Daemon(paths, port_lister=sim_lister(args.port), serial_factory=PtySerial)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
