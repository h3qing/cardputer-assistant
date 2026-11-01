"""cpad, the daemon process (05 §7.1): one asyncio loop, one writer of HubState.

Wiring: PortFinder -> DeviceLink (link handlers below) -> hub.reduce -> EffectRunner.
Tasks: supervisor (ports, links, pause), poller (agents --json only while a device is Ready, merged with the
Claude Code hook events `cpa hook` sends over the control socket, #33),
housekeeping (projects.json / config.json / events.jsonl reloads every 3 s, agent probe every 60 s).
v0.3 recordings (docs/11 §6): Recordings pulls file.* on the link, its ResultWatcher runs in housekeeping.
v0.4.1 (docs/13 §11.1): the host hello offers caps.recs / caps.todo and this Mac's host_id / host_label;
rec.list and rec.act go to Recordings.on_device_request, whose reply goes back with re = the request id.
Lines another process appended (`cpa rec unhide`) reach the hub and Recordings, whichever append or poll reads them.
"""

from __future__ import annotations

import asyncio
import fcntl
import logging
import os
import time
from collections.abc import Awaitable, Callable
from dataclasses import replace
from typing import Any

import serial

from cpad import __version__
from cpad.agents.claude_code import hook_state
from cpad.agents.claude_code.adapter import ClaudeCodeAdapter
from cpad.agents.claude_code.parse import AgentError
from cpad.agents.claude_code.process import run as run_process
from cpad.config.host import host_id, id_key
from cpad.config.settings import Paths, load_config, replace_config, save_config, secure_home
from cpad.control.server import API, ControlServer
from cpad.device.link import DeviceLink, HostIds, LinkTiming
from cpad.device.port_finder import Backoff, PortInfo, choose, list_ports
from cpad.events import records
from cpad.events.log import EventLog
from cpad.hub import inputs
from cpad.hub.reduce import failure_reply, reduce
from cpad.hub.runner import EffectRunner
from cpad.hub.state import HubState
from cpad.logging import setup
from cpad.notes.writer import NotesWriter
from cpad.projects.registry import ProjectsError, load_registry
from cpad.projects.resolve import Resolver, run_git
from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.protocol import rec_messages as r
from cpad.protocol.limits import LIMITS
from cpad.recordings.service import Recordings, notes_warnings

log = logging.getLogger("cpad.app")
HOLD = frozenset({"foreign", "version", "no_reply", "open_failed", "internal_error"})
REQUESTS = (m.OutboxItem, m.AgentSpawn, m.AgentReply, m.QueueDone, m.InboxDismiss)
REC_REQUESTS = (r.RecList, r.RecAct)
PROBE_S = 60.0


def device_status(serial: str | None, hello: m.DeviceHello | None, pending: int | None) -> dict[str, Any]:
    """The `device` block of `cpa status`; store / launcher stay None for a v0.1 device (09 §7)."""
    return {
        "serial": serial,
        "dev": hello.dev if hello else None,
        "fw": hello.fw if hello else None,
        "boot": hello.boot if hello else None,
        "pending": pending,
        "store": hello.store if hello else None,
        "launcher": hello.launcher if hello else None,
    }


class AlreadyRunning(RuntimeError):
    """Another cpad holds $CPA_HOME/cpad.lock."""


class Daemon:
    def __init__(self, paths: Paths, *, port_lister: Callable[[], tuple[PortInfo, ...]] = list_ports,
                 serial_factory: Callable[[], Any] = serial.Serial, timing: LinkTiming | None = None,
                 runner: Callable[..., Any] = run_process, git: Callable[[str], str | None] = run_git,
                 clock: Callable[[], float] = time.monotonic, wall: Callable[[], float] = time.time,
                 housekeeping_s: float = 3.0):  # fmt: skip
        self.paths = paths
        self.state = HubState()
        self.link: DeviceLink | None = None
        self.config = load_config(paths.config)
        self._lister, self._factory, self._timing = port_lister, serial_factory, timing or LinkTiming()
        self._proc_runner, self._resolver = runner, Resolver(git=git, clock=clock)
        self._clock, self._wall, self._housekeeping_s = clock, wall, housekeeping_s
        self._ids, self._seen, self._backoff = HostIds(), None, Backoff()
        self._paused_until, self._reboots, self._pending = 0.0, 0, None
        self._last_release: str | None = None
        self._host_id: str | None = None
        self.projects_error: str | None = None
        self._stop = asyncio.Event()
        self._poll_now = asyncio.Event()
        self._lock_fd: int | None = None
        self._mtimes: dict[str, float] = {}
        self.hooks: hook_state.Book = hook_state.EMPTY
        self._listing: tuple[tuple, float] = ((), 0.0)

    # --- lifecycle ----------------------------------------------------------------------------

    def _startup(self) -> None:
        fixed = secure_home(self.paths)
        self.config = load_config(self.paths.config)
        self._acquire_lock()
        try:
            self._open(fixed)
        except BaseException:
            self._release_lock()
            raise

    def _open(self, fixed: tuple[str, ...]) -> None:
        setup.configure(self.paths.logs, content=os.environ.get("CPA_LOG_CONTENT") == "1")
        if fixed:
            log.warning("tightened permissions", extra={"fields": {"paths": list(fixed)}})
        self._mtimes = {"config": self._mtime(self.paths.config)}
        self._host_id = host_id(self.paths.host_id)
        id_key(self.paths.id_key)  # docs/23 §3.2: created once here, read by cpa filing and the export
        self.events = EventLog(self.paths.events)
        history = self.events.load()
        self.notes = NotesWriter(self.config.notes_path())
        self.notes.backfill(history)
        self.adapter = ClaudeCodeAdapter(self._claude_bin(), self.config.spawn_env, runner=self._proc_runner,
                                         reply_enabled=self.config.experimental_bg_reply)  # fmt: skip
        self.runner = EffectRunner(events=self.events, notes=self.notes, adapter=self.adapter,
                                   get_link=lambda: self.link, get_state=lambda: self.state, dispatch=self.dispatch,
                                   absorb=lambda others: self.recordings.absorb(others),
                                   clock=self._clock, wall=self._wall)  # fmt: skip
        self.state = HubState(idle_s=self.config.inbox_idle_s)
        self.dispatch(inputs.Appended(history, self._clock()))
        self.recordings = Recordings(events=self.events, notes=self.notes, staging=self.paths.home / "rec-staging",
                                     config=lambda: self.config, projects=lambda: self.state.projects, history=history,
                                     on_appended=lambda recs: self.dispatch(inputs.Appended(recs, self._clock())),
                                     clock=self._clock, wall=self._wall)  # fmt: skip
        self._reload_projects()
        self._control = ControlServer(self.paths.sock, self._control_request)

    def _claude_bin(self) -> str:
        from shutil import which  # noqa: PLC0415

        return self.config.claude_bin or which("claude") or "claude"

    def _acquire_lock(self) -> None:
        fd = os.open(self.paths.lock, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            os.close(fd)
            raise AlreadyRunning("cpad is already running") from exc
        self._lock_fd = fd

    def _release_lock(self) -> None:
        if self._lock_fd is not None:
            os.close(self._lock_fd)
            self._lock_fd = None

    async def run(self, stop: asyncio.Event) -> None:
        self._stop = stop
        self._startup()
        tasks: list[asyncio.Task] = []
        try:
            await self._probe()
            await self._control.start()
            log.info("cpad started", extra={"fields": {"version": __version__}})
            tasks = [asyncio.create_task(c) for c in (self._supervise(), self._housekeeping(), self._poller())]
            await stop.wait()
        finally:
            await self._shutdown(tasks)

    async def _shutdown(self, tasks: list[asyncio.Task]) -> None:
        if self.link is not None:
            self.link.close("closed")
        await asyncio.gather(*tasks, return_exceptions=True)
        await self.recordings.link_down()
        await self.runner.drain()
        await self._control.close()
        self._release_lock()
        log.info("cpad stopped")
        setup.reset(logging.getLogger(setup.LOGGER))

    async def _sleep(self, seconds: float, wake: asyncio.Event | None = None) -> None:
        waiters = [asyncio.ensure_future(self._stop.wait())]
        waiters += [asyncio.ensure_future(wake.wait())] if wake else []
        _, pending = await asyncio.wait(waiters, timeout=max(0.0, seconds), return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()

    async def _survive(self, task: str, step: Callable[[], Awaitable[None]], retry_s: float) -> None:
        """One loop iteration. An unexpected error is logged (type only) and retried, never fatal (E14):
        a dead task would leave the process alive, so launchd would never restart it."""
        try:
            await step()
        except Exception:
            log.exception("background step failed", extra={"fields": {"task": task}})
            await self._sleep(retry_s)

    # --- hub ----------------------------------------------------------------------------------

    def dispatch(self, event: Any) -> None:
        self.state, effects = reduce(self.state, event)
        self.runner.run(effects)

    # --- supervisor ---------------------------------------------------------------------------

    async def _supervise(self) -> None:
        while not self._stop.is_set():
            await self._survive("supervisor", self._supervise_once, self._timing.backoff_s)

    async def _supervise_once(self) -> None:
        now = self._clock()
        if self._paused_until > now:
            await self._sleep(min(self._timing.scan_s, self._paused_until - now))
            return
        port = choose(self._lister(), self.config.device_serial, self._backoff, now)
        if port is None:
            await self._sleep(self._timing.scan_s)
            return
        await self._connect(port)

    async def _connect(self, port: PortInfo) -> None:
        link = DeviceLink(port, self, serial_factory=self._factory, timing=self._timing, ids=self._ids,
                          seen=self._seen, clock=self._clock)  # fmt: skip
        self.link = link
        reason = await self._run_link(link)
        self.link, self._seen = None, link.seen
        await self.recordings.link_down()
        self._reboots += link.reboots
        self._last_release = reason
        self.dispatch(inputs.LinkDown())
        if reason in HOLD:
            self._backoff = self._backoff.hold(port, self._clock(), self._timing.backoff_s)
        log.info("link closed", extra={"fields": {"port": port.device, "reason": reason}})

    @staticmethod
    async def _run_link(link: DeviceLink) -> str:
        """The release reason; a crashing link handler releases the port (link.run closes it) as internal_error."""
        try:
            return (await link.run()).reason
        except Exception:
            log.exception("link handler crashed", extra={"fields": {"port": link.port.device}})
            return "internal_error"

    # --- link handlers ------------------------------------------------------------------------

    def host_hello(self) -> m.HostHello:
        caps = replace(self.state.agent.caps, recs=True, todo=True)
        return m.HostHello(app=f"cpad/{__version__}", agent=self.state.agent.status, caps=caps,
                           host_id=self._host_id, host_label=self.config.host_label)  # fmt: skip

    async def on_ready(self, link: DeviceLink, hello: m.DeviceHello, reboot: bool) -> None:
        self._bind(link.port, hello)
        tz_min = max(LIMITS["tz_min_min"], min(LIMITS["tz_min_max"], time.localtime().tm_gmtoff // 60))
        link.request(m.TimeSet(epoch_ms=int(self._wall() * 1000), tz_min=tz_min))
        self._pending = hello.pending
        self.dispatch(inputs.LinkReady(hello.dev, self._clock()))
        self._poll_now.set()
        await self.recordings.on_ready(link, hello)
        log.info("device ready", extra={"fields": {"dev": hello.dev, "boot": hello.boot, "reboot": reboot}})

    def _bind(self, port: PortInfo, hello: m.DeviceHello) -> None:
        if self.config.device_serial is None and port.serial:
            self.config = replace_config(self.config, device_serial=port.serial)
            save_config(self.paths.config, self.config)
            self._mtimes = {**self._mtimes, "config": self._mtime(self.paths.config)}
        try:
            others, _ = self.events.append((records.device_seen(hello.dev, port.serial, hello.fw,
                                                                records.iso(self._wall())),))  # fmt: skip
        except OSError as exc:
            log.error("events.jsonl append failed", extra={"fields": {"err": exc.errno}})
            return
        if others:
            self.recordings.absorb(others)
            self.dispatch(inputs.Appended(others, self._clock()))

    async def on_request(self, link: DeviceLink, decoded: codec.Decoded) -> None:
        msg = m.from_wire(decoded.msg) if decoded.ok else None
        if isinstance(msg, REC_REQUESTS):
            link.reply(await self._rec_reply(msg))
            return
        if not isinstance(msg, REQUESTS):
            link.reply(failure_reply(decoded))
            log.info("request refused", extra={"fields": {"code": decoded.outcome, "field": list(decoded.fields)}})
            return
        duplicate = isinstance(msg, m.OutboxItem) and self.events.is_duplicate(self.state.dev or "", msg.id)
        self.dispatch(inputs.DeviceRequest(msg, self._clock(), self._wall(), duplicate))

    async def _rec_reply(self, msg: r.RecList | r.RecAct) -> Any:
        """rec.items, ack or error from Recordings; a failure is E_INTERNAL, logged by type only (no content)."""
        try:
            return replace(await self.recordings.on_device_request(msg), re=msg.id)
        except Exception as exc:
            log.error("rec request failed", extra={"fields": {"t": msg.T, "err": type(exc).__name__}})
            return m.ErrorMsg(msg.id, "E_INTERNAL")

    async def on_pong(self, link: DeviceLink, pong: m.Pong) -> None:
        self._pending = pong.pending
        self.dispatch(inputs.Reconcile(pong.revs, self._clock()))
        self.recordings.on_pong(link, pong)

    # --- poller and housekeeping --------------------------------------------------------------

    async def _poller(self) -> None:
        while not self._stop.is_set():
            self._poll_now.clear()
            link = self.link
            if link is not None and link.ready and self.state.agent.present:
                await self._survive("poller", self._poll_once, 0.0)
            await self._sleep(self.config.poll_s, wake=self._poll_now)

    async def _poll_once(self) -> None:
        started = self._clock()
        try:
            sessions = await self.adapter.list_sessions()
        except AgentError:
            log.warning("agents --json failed")
            self._agent_errored(True)
            return
        self._agent_errored(False)
        resolved = await asyncio.to_thread(self._resolve, self.state.projects, sessions)
        self._listing = (resolved, started)
        self.hooks = hook_state.observe(self.hooks, sessions, started)
        self.dispatch(inputs.Polled(hook_state.merge_pairs(*self._listing, self.hooks), self._clock()))

    def _on_hook(self, event: Any) -> dict[str, Any]:
        """Applied to the last listing right away; the next poll confirms or overrules it."""
        self.hooks = hook_state.record(self.hooks, event, self._clock())
        if self._listing[0] and self.link is not None and self.link.ready:
            self.dispatch(inputs.Polled(hook_state.merge_pairs(*self._listing, self.hooks), self._clock()))
        return {"ok": True}

    def _resolve(self, projects: tuple, sessions: tuple) -> tuple:
        """(session, project key or None) pairs; may run git, so it runs in a worker thread."""
        found = ((s, self._resolver.project_for(projects, s.cwd)) for s in sessions)
        return tuple((s, p.key if p else None) for s, p in found)

    def _agent_errored(self, errored: bool) -> None:
        agent = self.state.agent
        if agent.errored != errored:
            self.dispatch(inputs.AgentProbed(agent.present, agent.version, errored, agent.reply))

    async def _probe(self) -> None:
        caps = await self.adapter.probe()
        await self.adapter.check_login()
        self.dispatch(inputs.AgentProbed(caps.present, caps.version, False, caps.reply))
        self._next_probe = self._clock() + PROBE_S

    async def _housekeeping(self) -> None:
        while not self._stop.is_set():
            await self._sleep(self._housekeeping_s)
            if self._stop.is_set():
                return
            await self._survive("housekeeping", self._housekeep_once, 0.0)

    async def _housekeep_once(self) -> None:
        self._reload_projects()
        self._reload_config()
        fresh = self.events.poll()
        if fresh:
            self.dispatch(inputs.Appended(fresh, self._clock()))
            self.recordings.absorb(fresh)
        await self.recordings.housekeep()
        if self._clock() >= self._next_probe:
            await self._probe()

    def _reload_projects(self) -> None:
        try:
            projects = load_registry(self.paths.projects)
        except ProjectsError as exc:
            if self.projects_error != str(exc):
                log.error("projects.json rejected", extra={"fields": {"error": str(exc)}})
            self.projects_error = str(exc)
            return
        self.projects_error = None
        if projects != self.state.projects:
            self.dispatch(inputs.ProjectsLoaded(projects, self._clock()))

    @staticmethod
    def _mtime(path) -> float:
        try:
            return path.stat().st_mtime
        except FileNotFoundError:
            return 0.0

    def _reload_config(self) -> None:
        mtime = self._mtime(self.paths.config)
        if mtime == self._mtimes.get("config"):
            return
        self._mtimes = {**self._mtimes, "config": mtime}
        try:
            self.config = load_config(self.paths.config)
        except ValueError as exc:
            log.error("config.json rejected", extra={"fields": {"error": str(exc)}})

    # --- control ------------------------------------------------------------------------------

    async def _control_request(self, req: dict[str, Any]) -> dict[str, Any]:
        if req["cmd"] == "status":
            return self.status()
        if req["cmd"] == "rec":
            return {"ok": True, "api": API, "sessions": list(self.recordings.rows())}
        if req["cmd"] == "hook":
            return self._on_hook(req["event"])
        self._paused_until = self._clock() + req["seconds"]
        if self.link is not None:
            self.link.close("paused")
        log.info("paused", extra={"fields": {"seconds": req["seconds"]}})
        return {"ok": True, "paused_s": req["seconds"]}

    def _link_state(self, paused_s: float) -> str:
        if paused_s > 0:
            return "paused"
        if self.link is None:
            return "offline"
        return "ready" if self.link.ready else "handshaking"

    def status(self) -> dict[str, Any]:
        now = self._clock()
        paused_s = max(0.0, self._paused_until - now)
        hello = self.link.hello if self.link is not None else None
        agent = self.state.agent
        return {
            "ok": True,
            "running": True,
            "version": __version__,
            "api": API,
            "link": self._link_state(paused_s),
            "paused_s": round(paused_s, 1),
            "device": device_status(self.config.device_serial, hello, self._pending),
            "agent": {
                "status": agent.status,
                "version": agent.version,
                "spawn": agent.caps.spawn,
                "reply": agent.caps.reply,
                "login": self.adapter.logged_in,
            },  # fmt: skip
            "projects": [{"key": p.key, "slot": p.slot, "ok": p.ok} for p in self.state.projects],
            "projects_error": self.projects_error,
            "items_stored": self.events.items_stored,
            "queue_open": len(self.state.queue.items),
            "reboots": self._reboots + (self.link.reboots if self.link is not None else 0),
            "last_release": self._last_release,
            "backoff_s": round(max((u - now for u in self._backoff.until.values()), default=0.0), 1),
            "recordings": self.recordings.status(),
            "hooks": hook_state.summary(self.hooks, now),
            "warnings": [*notes_warnings(self.config.notes_path()), *self.adapter.warnings(self.state.projects)],
        }
