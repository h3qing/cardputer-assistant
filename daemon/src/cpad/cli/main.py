"""cpa: daemon | status | project add|rename|move|folder|rm|ls | queue ls|add|done|reopen | rec ls|show|unhide |
filing [ls]|show|file|done|hide|reopen|hand|triage | pause | service install|uninstall | device forget | host name |
lang | setup check |
hook | hooks print|install|uninstall | export | mcp serve|tools|setup|audit."""

from __future__ import annotations

import argparse
import asyncio
import json
import shutil
import signal
import sys
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from cpad import __version__
from cpad.archive import cli as archive_cli
from cpad.cli import commands, filing, hooks, projects, rec, triage
from cpad.config.settings import ConfigError, Paths, cpa_home, load_config, merge_config, secure_home
from cpad.control import client
from cpad.control.server import API
from cpad.dest import cli as dest_cli
from cpad.dest import cli_send
from cpad.events.log import EventLog
from cpad.filing.args import E_BAD_ARG, FilingError
from cpad.i18n import T
from cpad.mcp import cli as mcp_cli
from cpad.projects.registry import ProjectsError, load_registry
from cpad.queue.fold import fold_queue
from cpad.recordings import transcriber
from cpad.service import launchd
from cpad.setup import cli as setup_cli
from cpad.setup.facts import SetupDeps

CONTRACT = ("filing", "project", "host", "dest")  # their --json errors: protocol/filing/v1 and dest-cli/v1 error


@dataclass(frozen=True)
class CliDeps:
    """Seams for tests: how to run launchctl / zsh, where LaunchAgents live, how to find claude."""

    runner: Callable[..., launchd.Completed] = launchd.run
    agents_dir: Path | None = None
    uid: int | None = None
    which: Callable[..., str | None] = field(default=shutil.which)
    setup: SetupDeps | None = None
    clock: launchd.Clock = field(default_factory=launchd.Clock)


def _run_daemon(paths: Paths, args: argparse.Namespace, deps: CliDeps) -> int:
    from cpad.app import AlreadyRunning, Daemon  # noqa: PLC0415

    async def serve() -> None:
        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGTERM, signal.SIGINT):
            loop.add_signal_handler(sig, stop.set)
        await Daemon(paths).run(stop)

    try:
        asyncio.run(serve())
    except AlreadyRunning as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def _offline_status(paths: Paths) -> dict[str, Any]:
    cfg = load_config(paths.config)
    try:
        projects = [{"key": p.key, "slot": p.slot, "ok": p.ok} for p in load_registry(paths.projects)]
        problem = None
    except ProjectsError as exc:
        projects, problem = [], str(exc)
    log = EventLog(paths.events)
    queue = fold_queue(log.load())
    return {"running": False, "version": __version__, "device": {"serial": cfg.device_serial},
            "projects": projects, "projects_error": problem, "items_stored": log.items_stored,
            "queue_open": len(queue.items), "recordings": rec.offline_summary(paths)}  # fmt: skip


def _shown(value: Any) -> str:
    """'-' when the device did not say (v0.1 firmware or no device); yes / no for a flag."""
    if value is None:
        return "-"
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def _print_status(doc: dict[str, Any]) -> None:
    print(f"cpad {doc.get('version')}: {'running' if doc.get('running') else 'not running'}")
    device = doc.get("device") or {}
    if doc.get("running"):
        print(f"link: {doc['link']}  dev={device.get('dev')} fw={device.get('fw')} pending={device.get('pending')}"
              f" store={_shown(device.get('store'))} launcher={_shown(device.get('launcher'))}")  # fmt: skip
        agent = doc["agent"]
        print(f"claude: {agent['status']} {agent['version']} spawn={agent['spawn']} reply={agent['reply']}"
              f" login={_shown(agent.get('login'))}")  # fmt: skip
    print(f"bound serial: {device.get('serial') or '-'}")
    for p in doc.get("projects", []):
        print(f"project {p['slot']} {p['key']}: {'ok' if p['ok'] else 'MISSING ROOT'}")
    if doc.get("projects_error"):
        print(f"projects.json: {doc['projects_error']}")
    print(f"items stored: {doc.get('items_stored')}  open queue items: {doc.get('queue_open')}")
    recs = doc.get("recordings")
    if recs:
        counts = " ".join(f"{k}={_shown(recs.get(k))}" for k in ("on_device", "pulling", "waiting", "processing",
                                                                  "blocked", "failed"))  # fmt: skip
        print(f"recordings: {counts} last_result={_shown(recs.get('last_result_at'))}")
    if doc.get("transcriber"):
        print(f"transcriber: {transcriber.line(doc['transcriber'])}")
    for warning in doc.get("warnings") or ():
        print(f"warning: {warning}")


OLDER = "cpad is older than this cpa; restart it: cpa service install"


def _checked(doc: dict[str, Any]) -> dict[str, Any]:
    """docs/23 §1.3: a running cpad with a lower api (none: 0) than this cpa gets a restart warning."""
    if not doc.get("running") or (doc.get("api") if isinstance(doc.get("api"), int) else 0) >= API:
        return doc
    return {**doc, "warnings": [*(doc.get("warnings") or ()), OLDER]}


def _transcriber(paths: Paths) -> dict[str, Any]:
    """Who claims recordings/ (docs/22 §4.10): read from the folder, the daemon is not involved. A config
    that cannot be read shows no transcriber rather than failing cpa status."""
    try:
        root = load_config(paths.config).notes_path() / "recordings"
    except (ConfigError, OSError):
        return transcriber.describe(None, time.time())
    return transcriber.describe(root, time.time())


def _status(paths: Paths, args: argparse.Namespace, deps: CliDeps) -> int:
    try:
        doc = _checked(client.request(paths.sock, {"cmd": "status"}))
    except client.DaemonNotRunning:
        doc = _offline_status(paths)
    doc = {**doc, "transcriber": _transcriber(paths)}
    print(json.dumps(doc, ensure_ascii=False)) if args.json else _print_status(doc)
    return 0


def _pause(paths: Paths, args: argparse.Namespace, deps: CliDeps) -> int:
    seconds = args.seconds_opt or args.seconds or 120
    try:
        reply = client.request(paths.sock, {"cmd": "pause", "seconds": seconds})
    except client.DaemonNotRunning:
        print("cpad is not running; the port is free")
        return 0
    if not reply.get("ok"):
        print(f"error: {reply.get('error')}", file=sys.stderr)
        return 2
    print(f"paused for {reply['paused_s']} s")
    return 0


def _service(paths: Paths, args: argparse.Namespace, deps: CliDeps) -> int:
    agents_dir = deps.agents_dir or launchd.default_agents_dir()
    if args.action == "uninstall":
        launchd.uninstall(agents_dir, runner=deps.runner, uid=deps.uid)
        print("LaunchAgent removed")
        return 0
    secure_home(paths)
    env = launchd.capture_login_env(deps.runner)
    claude = deps.which("claude", path=env.get("PATH"))
    load_config(paths.config)  # a broken config.json stops here, before anything is written
    merge_config(paths.config, spawn_env=env, claude_bin=claude)
    program = [sys.executable, "-m", "cpad", "daemon"]
    try:
        done = launchd.install(program, paths.home, agents_dir, runner=deps.runner, uid=deps.uid,
                               cpa_home_env=str(paths.home), clock=deps.clock, env=launchd.lang_env(env))  # fmt: skip
    except launchd.ServiceError as exc:
        print(T(f"错误：{exc}；后台服务没起来，再跑一次 cpa service install",
                f"error: {exc}; try again: cpa service install"), file=sys.stderr)  # fmt: skip
        return 1
    print(f"LaunchAgent installed: {done.path}")
    print(f"cpad running: pid {done.pid}")
    if claude is None:
        print(T("注意：没找到 Claude Code：装好以后再跑一次 cpa service install",
                "warning: claude was not found on the login PATH; spawning stays off"), file=sys.stderr)  # fmt: skip
    return 0


def _setup_check(paths: Paths, args: argparse.Namespace, deps: CliDeps) -> int:
    setup = deps.setup or SetupDeps(agents_dir=deps.agents_dir, runner=deps.runner, which=deps.which)
    return setup_cli.check(paths, args, setup)


def _wrap(fn: Callable[[Paths, argparse.Namespace], int]) -> Callable[[Paths, argparse.Namespace, CliDeps], int]:
    return lambda paths, args, deps: fn(paths, args)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cpa", description="Cardputer Assistant Mac side")
    parser.add_argument("--version", action="version", version=f"cpa {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("daemon", help="run cpad in the foreground").set_defaults(fn=_run_daemon)
    status = sub.add_parser("status", help="daemon, device, claude and project health (no note text)")
    status.add_argument("--json", action="store_true")
    status.set_defaults(fn=_status)
    pause = sub.add_parser("pause", help="release the serial port for a while (default 120 s)")
    pause.add_argument("seconds", nargs="?", type=int)
    pause.add_argument("--seconds", dest="seconds_opt", type=int)
    pause.set_defaults(fn=_pause)
    projects.add_parsers(sub, _wrap)
    _queue_parsers(sub)
    rec.add_parsers(sub)
    filing_acts = filing.add_parsers(sub)
    triage.add_parser(filing_acts)
    cli_send.add_filing_parser(filing_acts)
    hooks.add_parsers(sub)
    archive_cli.add_parsers(sub)
    mcp_cli.add_parsers(sub)
    dest_cli.add_parsers(sub)
    service = sub.add_parser("service", help="install or remove the LaunchAgent")
    service.add_argument("action", choices=("install", "uninstall"))
    service.set_defaults(fn=_service)
    device = sub.add_parser("device", help="device binding")
    device.add_argument("action", choices=("forget",))
    device.set_defaults(fn=_wrap(commands.device_forget))
    _host_parsers(sub)
    lang = sub.add_parser("lang", help="the language of cpa, cpad and cpa-stt (none: CPA_LANG, then the locale)")
    lang.add_argument("lang", nargs="?", choices=("zh", "en", "auto"), help="auto: follow CPA_LANG / LC_ALL / LANG")
    lang.set_defaults(fn=_wrap(commands.lang))
    setup_cli.add_parsers(sub, _setup_check)
    return parser


def _host_parsers(sub: Any) -> None:
    host = sub.add_parser("host", help="this Mac").add_subparsers(dest="action", required=True)
    name = host.add_parser("name", help="name this Mac on the device and on new recordings: cpa host name -- NAME")
    which = name.add_mutually_exclusive_group()
    which.add_argument("label", nargs="?", help="1-24 bytes; put it after -- (no name: show the current one)")
    which.add_argument("--clear", action="store_true", help="forget the name")
    name.add_argument("--json", action="store_true", help="protocol/filing/v1 host or error")
    name.set_defaults(fn=_wrap(commands.host_name))


def _queue_parsers(sub: Any) -> None:
    queue = sub.add_parser("queue", help="the project queue").add_subparsers(dest="action", required=True)
    ls = queue.add_parser("ls")
    ls.add_argument("key", nargs="?")
    ls.set_defaults(fn=_wrap(commands.queue_ls))
    add = queue.add_parser("add")
    add.add_argument("key")
    add.add_argument("text", nargs="+")
    add.set_defaults(fn=_wrap(commands.queue_add))
    done = queue.add_parser("done")
    done.add_argument("qid")
    done.set_defaults(fn=_wrap(commands.queue_done))
    reopen = queue.add_parser("reopen", help="open a done or spawned item again")
    reopen.add_argument("qid")
    reopen.set_defaults(fn=_wrap(commands.queue_reopen))


def _parse(argv: list[str]) -> argparse.Namespace | None:
    """None when argparse refused argv of a contract command with --json: it said why on stderr, and stdout gets the
    v1 error document, so a reader tells a cpa without that flag or command (update cpa) from a crash."""
    try:
        return build_parser().parse_args(filing.with_ls(argv))
    except SystemExit as exc:
        if exc.code != 2 or not argv or argv[0] not in CONTRACT or "--json" not in argv:
            raise
    print(json.dumps(FilingError(E_BAD_ARG, "argv").doc()))
    return None


def main(argv: Sequence[str] | None = None, deps: CliDeps | None = None) -> int:
    argv = sys.argv[1:] if argv is None else list(argv)
    if argv[:1] == ["hook"]:
        return hooks.hook_main(argv[1:])  # before argparse: its exit 2 would block Claude
    args = _parse(argv)
    if args is None:
        return 2
    paths = Paths.at(cpa_home())
    try:
        return args.fn(paths, args, deps or CliDeps())
    except (ProjectsError, ConfigError, ValueError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
