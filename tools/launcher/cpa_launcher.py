#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""cpa-launcher: install and drive bmorcelli Launcher 2.9.1 on the Cardputer ADV (M3-L).

Run from the repository root:
  uv run tools/launcher/cpa_launcher.py fetch                  download Launcher 2.9.1, verify size + SHA-256
  uv run tools/launcher/cpa_launcher.py flash-launcher         write it at 0x0 with esptool (replaces a full flash)
  uv run tools/launcher/cpa_launcher.py wait                   wait until RST, then Enter, opened the menu
  uv run tools/launcher/cpa_launcher.py cmd partitions         read-only: version, partitions, whoami, help
  uv run tools/launcher/cpa_launcher.py install factory        the factory app from backups/ (as factory-v0.9)
  uv run tools/launcher/cpa_launcher.py install dist/cpa-v0.2.0-app.bin   our app (as cpa-v0.2.0)

cpad is paused while the tool works (`cpa pause`) and resumed after (`cpa pause 1`), unless --stay-paused.
Exit codes: 0 done, 1 failed, 2 setup problem, 3 sent but not confirmed, 4 Launcher console not reached.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence

from cpalauncher import flows


def _install_parser(sub: argparse._SubParsersAction) -> None:
    install = sub.add_parser("install", help="install an app image through the console (flash firmware)")
    install.add_argument("target", help="'factory' (backups/) or a path to an app-only image (cpa-<tag>-app.bin)")
    install.add_argument("--name", help="Launcher name (default: factory-v0.9, or the file name without -app.bin)")
    install.add_argument("--timeout", type=float, default=1500.0, help="seconds to wait for the menu (1500)")
    install.add_argument(
        "--verify",
        choices=("auto", "daemon", "partitions", "none"),
        default="auto",
        help="how to confirm when the OK line is lost (auto: cpad for cpa-*, else partitions)",
    )
    install.add_argument("--verify-timeout", type=float, default=90.0, help="seconds to wait for cpad (90)")
    install.set_defaults(fn=flows.cmd_install)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cpa_launcher.py", description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="serial port (default: the first USB 303A:1001 port)")
    parser.add_argument("--cache-dir", help="where the Launcher image is kept (~/.cache/cardputer-assistant/launcher)")
    parser.add_argument("--stay-paused", action="store_true", help="leave cpad paused at the end")
    sub = parser.add_subparsers(dest="command_name", required=True)
    sub.add_parser("fetch", help="download and verify Launcher 2.9.1").set_defaults(fn=flows.cmd_fetch)
    flash = sub.add_parser("flash-launcher", help="write Launcher 2.9.1 at 0x0 (esptool), then check it answers")
    flash.add_argument("--timeout", type=float, default=60.0, help="seconds to wait for its console (60)")
    flash.set_defaults(fn=flows.cmd_flash_launcher)
    wait = sub.add_parser("wait", help="poll `version` until the Launcher menu is open")
    wait.add_argument("--timeout", type=float, default=1500.0, help="seconds (1500)")
    wait.set_defaults(fn=flows.cmd_wait)
    cmd = sub.add_parser("cmd", help="send one read-only console command and print the reply")
    cmd.add_argument("command", nargs="+", help="version | partitions | whoami | help")
    cmd.set_defaults(fn=flows.cmd_console)
    _install_parser(sub)
    return parser


def main(argv: Sequence[str] | None = None, deps: flows.Deps | None = None) -> int:
    args = build_parser().parse_args(argv)
    deps = deps or flows.Deps()
    try:
        return args.fn(args, deps)
    except KeyboardInterrupt:
        deps.say("interrupted")
        return 130


if __name__ == "__main__":
    sys.exit(main())
