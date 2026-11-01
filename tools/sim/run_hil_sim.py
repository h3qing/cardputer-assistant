#!/usr/bin/env python3
"""The HIL scenarios (tools/hil) on the virtual device instead of the Cardputer.

    uv run --project daemon python tools/sim/run_hil_sim.py --scenarios handshake,capture,board \\
        --report "$TMPDIR/hil-sim.json"

It starts `cpa-sim serve` (store, USB, battery and mic from --sim-args), points the harness at its pty
(simtools.hil_patch) and runs tools/hil's own runner, with its temporary CPA_HOME, notes and fake-claude.
Only the scenarios in SUPPORTED run: each passed on the sim. rec_nosd needs --sim-args "--store none";
nav, menu and lock (v0.4.0) pass with a card and with --store none; so does lang (docs/21).
latency runs, but its numbers are the simulator's, not the device's. Not run: mic_cal, mic_verify and
rec_cold_start (they pause the real cpad and play through the speakers; the sim's codec is never cold),
launcher_boot and migrate_* (an installed or flashed image), real_claude_smoke (a real Claude Code
session); sd_bulk, rec_rotation, rec_soak and soak have not been tried on the sim.
"""

from __future__ import annotations

import argparse
import shlex
import sys
import tempfile
from pathlib import Path

sys.path.insert(1, str(Path(__file__).resolve().parents[1] / "hil"))  # harness, scenarios

from harness.runner import main as hil_main  # noqa: E402
from simtools.hil_patch import harness_on_sim  # noqa: E402
from simtools.serve import SimError, SimServe  # noqa: E402

SUPPORTED = ("sd_empty", "sd_store", "handshake", "capture", "offline_replay", "dup_ack", "reboot", "board", "spawn",
             "offline_spawn", "latency", "cjk", "back_keys", "rec_memo", "rec_meeting", "rec_resume", "rec_live_hold",
             "rec_reboot", "rec_nosd", "nav", "menu", "lock", "lang", "rec_memo_grace",
             "rec_memo_handsfree")  # fmt: skip
DEFAULT = "handshake,capture,board"


def split_args(argv: list[str]) -> tuple[argparse.Namespace, list[str]]:
    p = argparse.ArgumentParser(prog="run_hil_sim.py", add_help=False)
    p.add_argument("--scenarios", default=DEFAULT)
    p.add_argument("--sim-args", default="", help='options for cpa-sim serve, e.g. "--store none --bat 3600"')
    p.add_argument("--shots", type=Path, help="where serve writes screenshots (default: a temp dir)")
    p.add_argument("-h", "--help", action="store_true")
    return p.parse_known_args(argv)


def main(argv: list[str]) -> int:
    ours, rest = split_args(argv)
    if ours.help:
        print(__doc__)
        return 0
    names = [n.strip() for n in ours.scenarios.split(",") if n.strip()]
    unsupported = [n for n in names if n not in SUPPORTED]
    if unsupported:
        print(f"error: not on the simulator: {', '.join(unsupported)} (supported: {', '.join(SUPPORTED)})",
              file=sys.stderr)  # fmt: skip
        return 2
    shots = ours.shots or Path(tempfile.mkdtemp(prefix="cpa-sim-shots-"))
    try:
        sim = SimServe.start(shots, shlex.split(ours.sim_args))
    except SimError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(f"[sim] cpa-sim serve on {sim.path}, shots in {shots}", file=sys.stderr)
    try:
        with harness_on_sim(sim.path):
            return hil_main(["--scenarios", ",".join(names), *rest])
    finally:
        sim.stop()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
