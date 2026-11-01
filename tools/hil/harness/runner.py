"""run_e2e.py: parse arguments, build the rig, run the selected scenarios in order, write the report.

A scenario that raises or times out is recorded as failed and the run goes on; before each
scenario the rig is brought back to Ready (a failed offline scenario may leave cpad paused or the
tap holding the device's items back). With --expect-launcher a device whose hello does not say
launcher=true fails the whole run before any scenario. The run ends with debug.lang and the language the
device showed when it started (docs/21 §9.6), after a failure too, so a run never changes it.
mic_cal's (and mic_verify's, rec_cold_start's) preflight runs
before the port check: it refuses (exit 2) unless the real cpad shows an empty card, then pauses it; after such a
run the last lines say how to get back to the release build. Exit code 0 only when every selected scenario
passed.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import platform
import shutil
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

from cpad import __version__ as cpad_version

from harness.device import Device
from harness.expect import launcher_mismatch, launcher_of, store_of
from harness.lang import FALLBACK_LANG, restore_lang, start_lang
from harness.report import build_report, error_result, summary_lines, write_report
from harness.rig import Rig, create_rig, port_holders
from harness.select import SPEAKER, TIMEOUT_S, SelectionError, parse_scenarios, validate
from harness.wire import not_ready
from scenarios import SCENARIOS
from scenarios.common import Ctx, Options
from scenarios.mic_cal import preflight as mic_cal_preflight

SHORT_TIMEOUT_S = 300.0
LATENCY_TIMEOUT_S = 1800.0
WIRE_TAIL = 60
CALIBRATION_AFTER = (
    "[hil] the device still runs the test build and the real cpad stays paused for up to 30 min. When you are"
    " done: cd firmware/app && pio run -e cardputer-adv -t upload, then `cpa pause 1`, then `cpa status` must"
    " show link ready. If an error says `cleanup FAILED`, read docs/gates/m3-device-gates.md §5.2 before"
    " `cpa pause 1`."
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    tmp = Path(os.environ.get("TMPDIR", "/tmp"))
    p = argparse.ArgumentParser(prog="run_e2e.py", description="Cardputer Assistant HIL (05 §9.3, 06 WP5)")
    p.add_argument("--scenarios", default="all", help="comma list, `all` (the ten short ones), soak, real_claude_smoke")
    p.add_argument("--report", type=Path, default=tmp / "hil.json", help="combined JSON report")
    p.add_argument("--minutes", type=float, default=60.0, help="soak duration (default 60)")
    p.add_argument("--flips", type=int, default=100, help="latency: fake-claude state flips (default 100)")
    p.add_argument("--taps", type=int, default=30, help="latency: G0 taps (default 30)")
    p.add_argument("--project-dir", type=Path, help="real_claude_smoke only: an empty temporary directory")
    p.add_argument("--state", type=Path, help="migrate_seed / migrate_check / migrate_drained: the seed file")
    p.add_argument("--expect-launcher", action="store_true", help="the device was installed from the Launcher")
    p.add_argument("--keep", action="store_true", help="keep the temporary CPA_HOME / notes / fake-claude dir")
    return p.parse_args(argv)


def _timeout(name: str, opts: Options) -> float:
    if name == "soak":
        return opts.minutes * 60 + 600
    if name in TIMEOUT_S:
        return TIMEOUT_S[name]
    return LATENCY_TIMEOUT_S if name == "latency" else SHORT_TIMEOUT_S


async def _recover(rig: Rig) -> None:
    rig.tap.drop_next("outbox.ack", 0)
    rig.tap.hold_rx(None)
    if rig.daemon.status()["link"] == "paused":
        await rig.resume()
    await rig.ready(timeout=45)


def _evidence(rig: Rig, mark: int) -> dict[str, Any]:
    """For a failed scenario: daemon status and the last wire lines (raw, as the tap saw them)."""
    recs = rig.tap.since(mark)
    t0 = recs[0].t if recs else 0.0
    tail = [f"{r.t - t0:9.3f} {r.dir:5} {r.raw.decode('utf-8', 'replace')[:160].rstrip()}" for r in recs[-WIRE_TAIL:]]
    status = rig.daemon.status() if rig.daemon else {}
    keep = ("link", "reboots", "last_release", "device")
    return {"daemon": {k: status.get(k) for k in keep}, "wire_tail": tail}


async def _scenario(ctx: Ctx, name: str) -> dict[str, Any]:
    started, mark = time.monotonic(), ctx.rig.tap.size()
    try:
        await _recover(ctx.rig)
        res = await asyncio.wait_for(SCENARIOS[name](ctx), _timeout(name, ctx.opts))
    except Exception as exc:  # recorded in the report; the run goes on
        res = {**error_result(exc), **_evidence(ctx.rig, mark)}
    # Evidence for every scenario: the device answering E_NOT_READY means it fell out of Ready
    # while cpad still was (cpad re-handshakes by itself; see docs/07-hil-checklist.md).
    not_ready_n = not_ready(ctx.rig.tap.since(mark))
    return {**res, "device_not_ready": not_ready_n, "duration_s": round(time.monotonic() - started, 1)}


async def _put_lang_back(rig: Rig, lang: str) -> None:
    """docs/21 §9.6: debug.lang before the port is let go, so no run leaves the user's device switched."""
    if rig.link is None:
        return
    problem = await restore_lang(Device(rig), lang)
    if problem:
        print(f"[hil] could not put the language back to {lang} ({problem}): Opt+M . Enter on the device",
              file=sys.stderr)  # fmt: skip


def _meta(rig: Rig, names: tuple[str, ...], opts: Options, args: argparse.Namespace) -> dict[str, Any]:
    link = rig.link
    hello = link.hello if link else None
    return {
        "started": datetime.now().astimezone().isoformat(timespec="seconds"),
        "scenarios": list(names),
        "cpad": cpad_version,
        "python": platform.python_version(),
        "port": link.port.device if link else None,
        "fw": hello.fw if hello else None,
        "dev": hello.dev if hello else None,
        "store": store_of(hello),
        "launcher": launcher_of(hello),
        "expect_launcher": opts.expect_launcher,
        "boot_at_start": hello.boot if hello else None,
        "claude": "fake-claude" if rig.fake else rig.daemon.config.claude_bin,
        "minutes": opts.minutes if "soak" in names else None,
        "flips": opts.flips,
        "taps": opts.taps,
        "tmp": str(rig.tmp) if args.keep else None,
    }


def _per_scenario_path(report: Path, name: str) -> Path:
    return report.with_name(f"{report.stem}.{name}{report.suffix or '.json'}")


def _options(args: argparse.Namespace) -> Options:
    return Options(args.minutes, args.flips, args.taps, args.project_dir, args.state, args.expect_launcher, args.report)


async def _run(names: tuple[str, ...], args: argparse.Namespace) -> int:
    opts = _options(args)
    smoke = names == ("real_claude_smoke",)
    claude = shutil.which("claude") if smoke else None
    if smoke and claude is None:
        print("error: claude is not on PATH", file=sys.stderr)
        return 2
    rig = create_rig(claude_bin=claude, project_root=args.project_dir if smoke else None)
    results: dict[str, Any] = {}
    meta: dict[str, Any] = {}
    lang = FALLBACK_LANG  # what the device showed when the run started
    try:
        await rig.start()
        await rig.ready(timeout=60)
        ctx = Ctx(rig, Device(rig), opts)
        lang = await start_lang(ctx.dev)
        meta = _meta(rig, names, opts, args)
        mismatch = launcher_mismatch(rig.link.hello, opts.expect_launcher)
        if mismatch:
            raise RuntimeError(mismatch)
        for name in names:
            print(f"[hil] {name} ...", file=sys.stderr, flush=True)
            results[name] = await _scenario(ctx, name)
            write_report(_per_scenario_path(args.report, name), results[name])
            print(json.dumps({name: results[name]}, ensure_ascii=False), flush=True)
    except Exception as exc:
        meta = {**meta, "rig_error": f"{type(exc).__name__}: {exc}"}
        results = {**results, **{n: error_result(exc) for n in names if n not in results}}
    finally:
        await _put_lang_back(rig, lang)
        await rig.close(keep=args.keep)
    report = build_report({**meta, "finished": datetime.now().astimezone().isoformat(timespec="seconds")}, results)
    write_report(args.report, report)
    for line in summary_lines(report):
        print(line, file=sys.stderr)
    print(f"[hil] report: {args.report}", file=sys.stderr)
    return 0 if report["ok"] else 1


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        names = parse_scenarios(args.scenarios)
        validate(names, args.project_dir, args.state, args.expect_launcher)
    except SelectionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    calibration = bool(set(names) & set(SPEAKER))
    problem = mic_cal_preflight() if calibration else None
    if problem:
        print(f"error: {names[0]} preflight: {problem}", file=sys.stderr)
        return 2
    held = port_holders()
    if held:
        print(f"error: the Cardputer port is held by another process: {held} (cpa pause, close monitors)",
              file=sys.stderr)  # fmt: skip
        return 2
    code = asyncio.run(_run(names, args))
    if calibration:
        print(CALIBRATION_AFTER, file=sys.stderr)
    return code
