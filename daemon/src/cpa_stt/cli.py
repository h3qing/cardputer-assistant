"""cpa-stt: use [FILE] | off | setup [MODEL...] | status [--json] | test WAV | retry | run [--once] |
watch [--interval S] | service install|uninstall (docs/22 §4.11).

Off by default. `cpa-stt use` turns it on: it fills the whisper.cpp example (or your FILE), checks every
path, runs the self-test in the sandbox and only then saves $CPA_HOME/stt/config.json. Nothing runs in the
background unless you start it: `cpa-stt run --once` handles what is waiting and exits, `cpa-stt watch`
keeps polling, `cpa-stt service install` keeps `watch` running at login. Output is English, like cpa, and
never shows transcript text except `cpa-stt test` (your file, in your terminal).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import threading
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path
from types import MappingProxyType
from typing import Any

from cpa_stt import (
    APP,
    claim,
    cli_setup,
    cli_status,
    config,
    engine,
    gate,
    loop,
    manifest,
    models,
    process,
    runner,
    scan,
    service,
    state,
    template,
)
from cpad import installed_version
from cpad.config.settings import DEFAULT_NOTES, ConfigError, Paths, cpa_home, load_config
from cpad.projects.registry import ProjectsError, load_registry
from cpad.service import launchd

EXTRA_PATH = ("/opt/homebrew/bin", "/usr/local/bin")


def default_which(name: str) -> str | None:
    return shutil.which(name, path=os.pathsep.join((os.environ.get("PATH", ""), *EXTRA_PATH)))


def ask_user(prompt: str) -> str:
    try:
        return input(prompt)
    except EOFError:
        return ""


def capture(argv: list[str]) -> str:
    """stdout of a helper program (getconf, mdfind); empty when it cannot run. stderr is dropped."""
    try:
        done = subprocess.run(argv, capture_output=True, text=True, timeout=30, check=False, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return done.stdout


@dataclass(frozen=True)
class CliDeps:
    """Seams for tests: the outside programs cpa-stt calls and how `watch` stops."""

    sandbox_exec: str = runner.SANDBOX_EXEC
    which: Callable[[str], str | None] = default_which
    run_cmd: Callable[[list[str]], str] = capture
    launchctl: launchd.Runner = launchd.run
    agents_dir: Path | None = None
    uid: int | None = None
    alive: Callable[[int], bool] = claim.pid_alive
    stop: Callable[[], bool] | None = None
    ask: Callable[[str], str] = ask_user
    model_dirs: tuple[Path, ...] = field(default_factory=models.known_dirs)


@dataclass(frozen=True)
class Ctx:
    home: Path
    paths: config.SttPaths
    notes: Path
    deps: CliDeps

    def env(self, cfg: config.SttConfig) -> runner.Env:
        cache = runner.darwin_cache_dir(self.deps.run_cmd)
        return runner.Env(self.deps.sandbox_exec, cache, cfg.sandbox_mach, str(Path.home()))

    def loop_deps(self) -> loop.Deps:
        cfg = config.load_config(self.paths.config)
        return loop.Deps(self.paths, self.notes, self.env(cfg), alive=self.deps.alive)

    def agents(self) -> Path:
        return self.deps.agents_dir or launchd.default_agents_dir()


def _notes_dir(home: Path) -> Path:
    try:
        return load_config(Paths.at(home).config).notes_path()
    except ConfigError:
        return Path(DEFAULT_NOTES).expanduser()


def _projects(home: Path) -> tuple[str, ...]:
    try:
        return tuple(p.name for p in load_registry(Paths.at(home).projects))
    except (ProjectsError, OSError):
        return ()


def _kept(path: Path) -> dict[str, Any]:
    """Your saved settings, except what the example decides: the command and its sandbox services."""
    try:
        old = config.load_config(path) if path.exists() else None
    except config.SttConfigError:
        old = None
    skip = ("provider", "argv", "sandbox_mach")
    return {k: v for k, v in config.dump_config(old).items() if k not in skip} if old else {}


def _use(ctx: Ctx, args: argparse.Namespace) -> int:
    doc = json.loads(Path(args.file).read_text(encoding="utf-8")) if args.file else template.example()
    if not isinstance(doc, dict):
        raise config.SttConfigError("the config must be a JSON object")
    filled = template.fill(doc, template.Inputs(ctx.paths.models, _projects(ctx.home), ctx.deps.which))
    kept = {} if args.file else _kept(ctx.paths.config)
    cfg = config.parse_config({**filled, **kept, "provider": "command"})
    missing = config.missing_paths(cfg.argv or ())
    if missing:
        raise config.SttConfigError(f"not found: {', '.join(missing)}")
    problem = engine.selftest(cfg, ctx.env(cfg))
    if problem:
        print(f"error: self-test failed ({problem}); the configuration was not changed", file=sys.stderr)
        return 1
    config.save_config(ctx.paths.config, cfg)
    st = replace(state.load(ctx.paths.state), blocked=None, selftest_ok=True, selftest_at=loop.iso(time.time()),
                 config_sig=None, failures=MappingProxyType({}))  # fmt: skip
    state.save(ctx.paths.state, st)
    print(f"cpa-stt is on: {Path((cfg.argv or ('',))[0]).name}, recordings in {config.recordings_root(cfg, ctx.notes)}")
    print("Only one transcriber may write recordings/: if WhisperWoof's Cardputer inbox is on, turn it off first.")
    print("Now: cpa-stt run --once   Keep polling: cpa-stt watch   At login: cpa-stt service install")
    return 0


def _off(ctx: Ctx, args: argparse.Namespace) -> int:
    try:
        cfg = config.load_config(ctx.paths.config)
    except config.SttConfigError:
        cfg = config.SttConfig()
    config.save_config(ctx.paths.config, replace(cfg, provider="off"))
    service.uninstall(ctx.agents(), runner=ctx.deps.launchctl, uid=ctx.deps.uid)
    root = config.recordings_root(cfg, ctx.notes)
    if root.is_dir():
        claim.release_dead(root, ctx.deps.alive)
    print("cpa-stt is off (a running watch stops before its next recording and releases its claim)")
    return 0


def _summary(report: loop.Report) -> str:
    texts = {
        "off": "cpa-stt is off: turn it on with cpa-stt use",
        "bad_config": "the configuration is invalid: run cpa-stt use again",
        "no_recordings": "no recordings folder yet",
        "busy": f"another transcriber is running ({report.by}); nothing written",
        "stopped": "stopped",
        "blocked": f"blocked ({report.reason}): recordings wait; fix it, then cpa-stt retry",
    }
    if report.state == "blocked" and report.missing:
        return (f"blocked (model_missing): not found: {', '.join(report.missing)}; link it again (cpa-stt setup "
                "[PATH], then cpa-stt use); recordings wait and go on by themselves once it is back")  # fmt: skip
    others = f", another transcriber writes {len(report.foreign)} folder(s)" if report.foreign else ""
    return texts.get(report.state, f"done {report.done}, failed {report.failed}, waiting {report.waiting}{others}")


def _watch_line(report: loop.Report) -> str:
    if report.state != "ok":
        return _summary(report)
    others = f"; another transcriber writes {len(report.foreign)} folder(s)" if report.foreign else ""
    wait = f"{report.waiting} recording(s) wait to be tried again" if report.waiting else "nothing waits"
    return f"watching: {wait}{others}"


def _printer() -> Callable[[loop.Report], None]:
    """What watch prints: one line per transcribed recording (id and timings, never text), and how it is
    doing whenever that changes."""
    last = ""

    def show(report: loop.Report) -> None:
        nonlocal last
        for r in report.finished:
            print(f"transcribed {r.session}: {r.dur_ms / 1000:.1f} s of audio in {r.took_ms / 1000:.2f} s", flush=True)
        if report.failed:
            print(f"failed: {report.failed} recording(s); see cpa-stt status", flush=True)
        line = _watch_line(report)
        if line != last:
            print(line, flush=True)
        last = line

    return show


def _run(ctx: Ctx, args: argparse.Namespace) -> int:
    if not args.once:
        return _watch(ctx, args)
    report = loop.once(ctx.loop_deps())
    print(_summary(report))
    return 1 if report.state in ("blocked", "bad_config") else 0


def signal_stop() -> tuple[Callable[[], bool], Callable[[float], None]]:
    """SIGTERM / Ctrl-C stop watch (a running command is killed within 0.2 s); a second Ctrl-C ends it at once."""
    event = threading.Event()

    def handler(signum: int, frame: object) -> None:
        event.set()
        signal.signal(signal.SIGINT, signal.SIG_DFL)

    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, handler)
    return event.is_set, event.wait


def _watch(ctx: Ctx, args: argparse.Namespace) -> int:
    stop, sleep = (ctx.deps.stop, time.sleep) if ctx.deps.stop is not None else signal_stop()
    deps = ctx.loop_deps()
    root = config.recordings_root(config.load_config(ctx.paths.config), ctx.notes)
    print(f"cpa-stt watch: watching {root} every {args.interval:g} s; Ctrl-C stops", flush=True)
    loop.watch(deps, interval=args.interval, stop=stop, sleep=sleep, report=_printer())
    return 0


def _device_wav(path: Path) -> manifest.Run:
    size = os.lstat(path).st_size
    with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb") as fh:
        header = fh.read(44)
    if size < 44 or size % 2 or header != scan.wav_header(size - 44):
        raise ValueError(f"{path} is not a 16 kHz / 16-bit / mono WAV with a 44-byte header")
    return manifest.Run((manifest.Seg(0, path.name, size, (size - 44) // 2, ""),), 0, 0)


def _test(ctx: Ctx, args: argparse.Namespace) -> int:
    cfg = config.load_config(ctx.paths.config)
    if cfg.argv is None:
        raise config.SttConfigError("no command is configured: run cpa-stt use first")
    wav = Path(args.wav).absolute()
    run = _device_wav(wav)
    env = ctx.env(cfg)
    problem = engine.env_problem(cfg, env)
    if problem:
        raise config.SttConfigError(f"the command cannot run here ({problem}): see cpa-stt status")
    pctx = process.Ctx(cfg, env, now=lambda: loop.iso(time.time()), tick=lambda: True)
    heard = process.hear(wav.parent, run, pctx)
    if heard.failed is not None:
        print(f"the command failed: exit {heard.failed.exit}, timed out {heard.failed.timed_out}")
        return 1
    said = gate.keep(heard.sentences, (*config.prompts(cfg.argv), *cfg.drop_lines))
    print(f"took {heard.took_ms / 1000:.2f} s for {run.duration_s:.1f} s of audio")
    for s in said:
        print(f"[{s.t_ms // 60000:02d}:{s.t_ms // 1000 % 60:02d}] {s.text}")
    print("" if said else "(nothing heard)")
    return 0


def _retry(ctx: Ctx, args: argparse.Namespace) -> int:
    removed = loop.retry(ctx.loop_deps())
    print(f"removed {removed} failed result(s) written by {APP}; cleared blocked and the retry counts")
    return 0


def _service(ctx: Ctx, args: argparse.Namespace) -> int:
    if args.action == "uninstall":
        removed = service.uninstall(ctx.agents(), runner=ctx.deps.launchctl, uid=ctx.deps.uid)
        print("LaunchAgent removed" if removed else "no LaunchAgent was installed")
        return 0
    path = service.install(ctx.agents(), ctx.paths.dir / "log", str(ctx.home), runner=ctx.deps.launchctl,
                           uid=ctx.deps.uid)  # fmt: skip
    print(f"LaunchAgent installed: {path} (runs cpa-stt watch at login)")
    if not config.load_config(ctx.paths.config).on:
        print("note: cpa-stt is off; it waits until you run cpa-stt use")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cpa-stt", description="Transcribe Cardputer recordings on this Mac")
    parser.add_argument("--version", action="version", version=f"cpa-stt {installed_version()}")
    sub = parser.add_subparsers(dest="command", required=True)
    use = sub.add_parser("use", help="turn it on with the whisper.cpp example or your config FILE")
    use.add_argument("file", nargs="?")
    use.set_defaults(fn=_use)
    sub.add_parser("off", help="turn it off, drop a dead claim, remove the LaunchAgent").set_defaults(fn=_off)
    setup = sub.add_parser("setup", help="link existing models into $CPA_HOME/models (no copy, no download)")
    setup.add_argument("models", nargs="*")
    setup.add_argument("--yes", action="store_true", help="link what it finds without asking")
    setup.set_defaults(fn=cli_setup.setup)
    status = sub.add_parser("status", help="on/off, claim, command, sandbox, self-test, recent timings")
    status.add_argument("--json", action="store_true")
    status.set_defaults(fn=cli_status.status)
    test = sub.add_parser("test", help="run the command on one WAV in the sandbox and print the text")
    test.add_argument("wav")
    test.set_defaults(fn=_test)
    sub.add_parser("retry", help="unblock and redo cpa-stt's own failed recordings").set_defaults(fn=_retry)
    run = sub.add_parser("run", help="--once: handle waiting recordings and exit (without it: watch)")
    run.add_argument("--once", action="store_true")
    watch = sub.add_parser("watch", help="keep polling the recordings (every 2 s)")
    for p, fn in ((run, _run), (watch, _watch)):
        p.add_argument("--interval", type=float, default=2.0)
        p.set_defaults(fn=fn)
    svc = sub.add_parser("service", help="optional LaunchAgent that runs cpa-stt watch at login")
    svc.add_argument("action", choices=("install", "uninstall"))
    svc.set_defaults(fn=_service)
    return parser


def main(argv: Sequence[str] | None = None, deps: CliDeps | None = None) -> int:
    args = build_parser().parse_args(sys.argv[1:] if argv is None else list(argv))
    home = cpa_home()
    ctx = Ctx(home, config.SttPaths.at(home), _notes_dir(home), deps or CliDeps())
    try:
        return args.fn(ctx, args)
    except (config.SttConfigError, template.TemplateError, models.ModelError, ValueError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
