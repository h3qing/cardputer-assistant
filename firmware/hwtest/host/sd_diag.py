"""SD write-failure diagnostics runner for the hwtest firmware (docs/gates/sd-card.md §11).

Safe-opens the port (hwtest_host.open_port), handshakes, then sends each command line in
order and keeps everything the device prints until that command's own reply
("!sdf3 128" -> {"t":"sdf3"}). Prints one short line per reply and saves one JSON report
(default under ../captures/, which git ignores).

    uv run --no-project --with pyserial==3.5 python sd_diag.py --label eio \\
        '!sddiag reset' '!xfermk 9375' '!sddiag' '!sdid' '!sdf3 128' '!sdblk 139808 32 m 4000'
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime

from gate_link import GateLink

CAPTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "captures")
LONG = 40  # summary() leaves out longer strings (register dumps)


def reply_type(cmd):
    return cmd.split()[0].lstrip("!")


def _clean(ev):
    return {k: v for k, v in ev.items() if not k.startswith("_")}


def run_command(link, cmd, timeout):
    events = []
    t0 = link.clock()
    link.send(cmd)
    got = link.collect(reply_type(cmd), 1, timeout, on_event=lambda ev, rx: events.append(_clean(ev)))
    return {
        "cmd": cmd,
        "ms": round((link.clock() - t0) * 1000),
        "events": events,
        "reply": _clean(got[0]) if got else None,
    }


def _scalars(d):
    return [f"{k}={v}" for k, v in d.items() if k != "t" and not isinstance(v, (dict, list))
            and not (isinstance(v, str) and len(v) > LONG)]


def summary(ev):
    parts = [ev.get("t", "?")] + _scalars(ev)
    diag = ev.get("diag")
    if diag:
        parts += [f"{k}={diag[k]}" for k in ("events", "wfail", "rfail", "busy_max_ms") if k in diag]
        last = diag.get("last")
        if last:
            parts.append(f"last={last.get('stage')} cmd{last.get('cmd')} sector={last.get('sector')} "
                         f"busy_ms={last.get('busy_ms')}")
    return " ".join(parts)


def main(argv=None):
    from hwtest_host import find_port, open_port
    from sd_run import handshake

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--label", required=True)
    ap.add_argument("--timeout", type=float, default=900, help="seconds per command")
    ap.add_argument("--out", default=None)
    ap.add_argument("commands", nargs="+")
    args = ap.parse_args(argv)
    device = find_port()
    if not device:
        sys.exit("no 303A:1001 device found")
    ser = open_port(device)
    report = {"label": args.label, "port": device, "steps": []}
    try:
        link = GateLink(ser)
        report["handshake"] = handshake(link)
        if report["handshake"]["ok"]:
            for cmd in args.commands:
                step = run_command(link, cmd, args.timeout)
                report["steps"].append(step)
                print(f"{cmd} -> " + (summary(step["reply"]) if step["reply"] else "NO REPLY"), flush=True)
    finally:
        ser.close()
    path = args.out or os.path.join(CAPTURES, f"sd-diag-{args.label}-{datetime.now():%Y%m%d-%H%M%S}.json")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    print(f"saved {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
