"""One pass over the recordings, `run --once` and `watch` (docs/22 §4.6, §4.10). I/O.

A pass: config off -> nothing; someone else's fresh claim -> nothing (busy); else take the claim (link()
when there is none) or renew it (also when idle or blocked), find pending folders whose retry is due, check
the environment (self-test once per process and config, after a change, after any failure), then process
them in order. Before each folder the pass ends when cpa-stt is stopping, was turned off or lost its claim;
during a command run the same check kills the run. An environment problem writes status blocked only;
a file the command fails on while the self-test passes backs off 30 s / 2 min / 8 min (status queued),
then gets result failed / E_STT (retryable); a folder cpa-stt cannot read or write backs off the same way,
then gets failed / E_IO (retryable) when it can be written, and never stops the pass. While blocked by the
engine the self-test runs again at most once a minute; `cpa-stt retry` leaves a file that the next pass of
a running watch consumes. Logs carry ids, lengths, timings and exit codes, never text or paths; a foreign
folder is logged once, and the log is rotated at 1 MiB.
"""

from __future__ import annotations

import contextlib
import json
import os
import time
from collections.abc import Callable
from dataclasses import dataclass, field, replace
from datetime import datetime
from pathlib import Path
from types import MappingProxyType

from cpa_stt import APP, claim, config, contract_out, engine, manifest, process, runner, scan, state, writer
from cpad.config.files import ensure_private_dir


@dataclass(frozen=True, slots=True)
class Deps:
    paths: config.SttPaths
    notes_dir: Path
    env: runner.Env
    clock: Callable[[], float] = time.time
    pid: int = field(default_factory=os.getpid)
    alive: Callable[[int], bool] = claim.pid_alive
    stop: Callable[[], bool] = lambda: False


@dataclass(frozen=True, slots=True)
class Report:
    """state: off | bad_config | no_recordings | busy | blocked | stopped | ok."""

    state: str
    by: str | None = None
    reason: str | None = None
    done: int = 0
    failed: int = 0
    waiting: int = 0
    foreign: tuple[str, ...] = ()
    finished: tuple[state.Recent, ...] = ()  # this pass's transcribed recordings (ids and timings, no text)
    missing: tuple[str, ...] = ()  # blocked by model_missing: the files in the command that are gone


def iso(ts: float) -> str:
    return datetime.fromtimestamp(ts).astimezone().isoformat(timespec="milliseconds")


LOG_MAX = 1 << 20
RETEST_S = 60


def log(path: Path, **fields: object) -> None:
    """One JSON line: ids, lengths, timings, exit codes. Never transcript text. Rotated at 1 MiB (one old file)."""
    ensure_private_dir(path.parent)
    with contextlib.suppress(FileNotFoundError):
        if os.lstat(path).st_size >= LOG_MAX:
            os.replace(path, path.with_name(path.name + ".1"))
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "a", encoding="utf-8") as fh:
        fh.write(json.dumps({"at": iso(time.time()), **fields}) + "\n")


def heartbeat(root: Path, pid: int, since: str, now: float) -> bool:
    """Renew our claim when its beat is a minute old; False when the claim is no longer ours, or cannot be
    renewed (others would take it over after 5 minutes, so nothing more may be written)."""
    seen = claim.read(root)
    if seen is None or seen.claim is None or (seen.claim.app, seen.claim.pid) != (APP, pid):
        return False
    if now - seen.beat < claim.BEAT_S:
        return True
    try:
        claim.write(root, pid, since=since, now=iso(now))
    except OSError:
        return False
    return True


def _root(deps: Deps, cfg: config.SttConfig) -> Path:
    return config.recordings_root(cfg, deps.notes_dir)


def _rank(folder: scan.Folder) -> tuple:
    doc = folder.doc
    return (0, *manifest.order_key(doc)) if manifest.check(doc).ok else (1, 0, 0, folder.rel)


def _pending(root: Path, st: state.SttState, now_ms: int) -> tuple[tuple[scan.Folder, ...], int]:
    found = scan.folders(root)
    due = [f for f in found if state.due(st, (process.named(f.doc) or ("",))[0], now_ms)]
    return tuple(sorted(due, key=_rank)), len(found) - len(due)


def _ready(deps: Deps, cfg: config.SttConfig, st: state.SttState) -> tuple[state.SttState, str | None]:
    problem = engine.env_problem(cfg, deps.env)
    if problem:
        return replace(st, blocked=problem, selftest_ok=False, config_sig=None), problem
    sig = engine.signature(cfg, deps.pid)
    if st.config_sig == sig and (st.blocked is None or not _retest_due(st, deps.clock())):
        return st, st.blocked
    return _selftest(deps, cfg, st)


def _retest_due(st: state.SttState, now: float) -> bool:
    """Blocked by the engine: test it again at most once a minute (it may have been fixed meanwhile)."""
    try:
        return now - datetime.fromisoformat(st.selftest_at or "").timestamp() >= RETEST_S
    except ValueError:
        return True


def _selftest(deps: Deps, cfg: config.SttConfig, st: state.SttState) -> tuple[state.SttState, str | None]:
    problem = engine.selftest(cfg, deps.env)
    log(deps.paths.log, ev="selftest", ok=problem is None, reason=problem)
    sig = engine.signature(cfg, deps.pid)
    done = replace(st, blocked=problem, selftest_ok=problem is None, selftest_at=iso(deps.clock()), config_sig=sig)
    return done, problem


def _mark_blocked(pending: tuple[scan.Folder, ...], reason: str, now: float, meetings: bool) -> None:
    """status blocked in each folder cpa-stt would transcribe (never one it would leave alone)."""
    for folder in pending:
        found = process.named(folder.doc)
        if found is None or (found[1] == "meeting" and not meetings):
            continue
        if scan.foreign(folder.path, now) or not manifest.check(folder.doc).ok:
            continue
        if not scan.regular_segments(folder.path, folder.doc):
            continue
        current = scan.read_json(folder.path / "status.json", scan.STATUS_MAX)
        if isinstance(current, dict) and (current.get("state"), current.get("reason")) == ("blocked", reason):
            continue
        _status(folder, found[0], "blocked", iso(now), reason)


def _status(folder: scan.Folder, session: str, state_: str, now: str, reason: str | None = None) -> None:
    """status.json by cpa-stt; a folder it cannot write is left as it is (the pass goes on)."""
    doc = contract_out.encode(contract_out.status(session, state_, now, reason))
    with contextlib.suppress(OSError):
        writer.write_file(folder.path, "status.json", doc)


def _final(folder: scan.Folder, ctx: process.Ctx, code: str) -> bool:
    """result.json failed / code (retryable); False when it could not be placed."""
    try:
        return process.fail_retryable(folder, ctx, code).kind == "failed"
    except OSError:
        return False


def _failed_run(deps: Deps, cfg: config.SttConfig, st: state.SttState, report: Report, folder: scan.Folder,
                step: process.Step, ctx: process.Ctx) -> tuple[state.SttState, Report]:  # fmt: skip
    """The command failed on this file: an environment problem blocks; otherwise back off, then fail."""
    session = step.session or ""
    st = state.add_recent(st, state.Recent(session, step.dur_ms, step.took_ms, step.exit))
    st, problem = _selftest(deps, cfg, st)
    if problem:
        return st, replace(report, state="blocked", reason=problem)
    st, exhausted = state.fail(st, session, int(deps.clock() * 1000))
    if not exhausted:
        _status(folder, session, "queued", iso(deps.clock()))
        return st, replace(report, waiting=report.waiting + 1)
    return st, replace(report, failed=report.failed + _final(folder, ctx, "E_STT"))


def _io_failed(deps: Deps, st: state.SttState, report: Report, folder: scan.Folder, exc: OSError,
               ctx: process.Ctx) -> tuple[state.SttState, Report]:  # fmt: skip
    """cpa-stt could not read or write this folder: back off like a failing command, never stop the pass."""
    session = (process.named(folder.doc) or ("",))[0]
    log(deps.paths.log, ev="io_error", session=session, errno=exc.errno)
    st, exhausted = state.fail(st, session, int(deps.clock() * 1000))
    if not exhausted:
        return st, replace(report, waiting=report.waiting + 1)
    return st, replace(report, failed=report.failed + _final(folder, ctx, "E_IO"))


def _account(deps: Deps, cfg: config.SttConfig, st: state.SttState, report: Report, folder: scan.Folder,
             step: process.Step, ctx: process.Ctx) -> tuple[state.SttState, Report]:  # fmt: skip
    if step.kind != "skipped" and not (step.kind == "foreign" and folder.rel in st.foreign):
        log(deps.paths.log, ev=step.kind, session=step.session, dur_ms=step.dur_ms, took_ms=step.took_ms,
            exit=step.exit)  # fmt: skip
    if step.kind == "command_failed":
        return _failed_run(deps, cfg, st, report, folder, step, ctx)
    if step.kind == "foreign":
        return st, replace(report, foreign=(*report.foreign, folder.rel))
    if step.kind == "done":
        recent = state.Recent(step.session or "", step.dur_ms, step.took_ms, step.exit)
        done = replace(report, done=report.done + 1, finished=(*report.finished, recent))
        return state.clear(state.add_recent(st, recent), step.session or ""), done
    if step.kind == "failed":
        return state.clear(st, step.session or ""), replace(report, failed=report.failed + 1)
    return st, report


def _foreign_results(deps: Deps, root: Path, st: state.SttState, since: str) -> tuple[str, ...]:
    """Folders whose result.json another program placed while we held the claim (a second writer)."""
    found = scan.foreign_results(root, datetime.fromisoformat(since).timestamp(), since[:10])
    for rel in (r for r in found if r not in st.foreign):
        log(deps.paths.log, ev="foreign_result", folder=rel)
    return found


def _still_on(deps: Deps) -> bool:
    try:
        return config.load_config(deps.paths.config).on
    except config.SttConfigError:
        return False


def _halt(deps: Deps, root: Path, since: str) -> Report | None:
    """Why the pass must end before the next folder: stopping, turned off, or the claim is no longer ours."""
    if deps.stop():
        return Report("stopped")
    if not _still_on(deps):
        return Report("off")
    if not heartbeat(root, deps.pid, since, deps.clock()):
        return Report("busy", by=_holder(root))
    return None


def _ended(st: state.SttState, report: Report, halt: Report) -> tuple[state.SttState, Report]:
    done = replace(report, state=halt.state, by=halt.by)
    return replace(st, foreign=report.foreign, busy_by=halt.by), done


def _work(deps: Deps, cfg: config.SttConfig, root: Path, st: state.SttState,
          since: str) -> tuple[state.SttState, Report]:  # fmt: skip
    pending, waiting = _pending(root, st, int(deps.clock() * 1000))
    report = Report("ok", waiting=waiting, foreign=_foreign_results(deps, root, st, since))
    if not pending:
        return replace(st, foreign=report.foreign), report
    st, problem = _ready(deps, cfg, st)
    if problem:
        _mark_blocked(pending, problem, deps.clock(), cfg.meetings)
        missing = config.missing_paths(cfg.argv or ()) if problem == "model_missing" else ()
        return st, replace(report, state="blocked", reason=problem, missing=missing)
    ctx = process.Ctx(cfg, deps.env, now=lambda: iso(deps.clock()),
                      tick=lambda: not deps.stop() and heartbeat(root, deps.pid, since, deps.clock()),
                      clock=deps.clock)  # fmt: skip
    for folder in pending:
        halt = _halt(deps, root, since)
        if halt is not None:
            return _ended(st, report, halt)
        try:
            step = process.process(folder, ctx)
        except OSError as exc:
            st, report = _io_failed(deps, st, report, folder, exc, ctx)
            continue
        st, report = _account(deps, cfg, st, report, folder, step, ctx)
        if report.state == "blocked":
            _mark_blocked(pending, report.reason or "engine", deps.clock(), cfg.meetings)
            break
    halt = _halt(deps, root, since) if report.state == "ok" else None
    return _ended(st, report, halt) if halt is not None else (replace(st, foreign=report.foreign), report)


def _holder(root: Path) -> str:
    seen = claim.read(root)
    return seen.claim.app if seen is not None and seen.claim is not None else "unknown"


def _retried(deps: Deps, st: state.SttState) -> state.SttState:
    """`cpa-stt retry` ran since the last pass: forget blocked, the self-test and the retry counts."""
    try:
        os.unlink(deps.paths.retry)
    except FileNotFoundError:
        return st
    return replace(st, blocked=None, selftest_ok=None, config_sig=None, failures=MappingProxyType({}))


def run_pass(deps: Deps, st: state.SttState, since: str) -> tuple[state.SttState, Report]:
    st = _retried(deps, st)
    try:
        cfg = config.load_config(deps.paths.config)
    except config.SttConfigError:
        return st, Report("bad_config")
    root = _root(deps, cfg)
    if not cfg.on:
        return st, Report("off")
    if not root.is_dir():
        return st, Report("no_recordings")
    seen = claim.read(root)
    verdict = claim.decide(seen, me=deps.pid, now=deps.clock(), alive=deps.alive)
    if verdict == "take":
        took = claim.take(root, deps.pid, since=since, now=iso(deps.clock()), stale=seen is not None)
        verdict = "mine" if took else "busy"
    if verdict == "busy" or not heartbeat(root, deps.pid, since, deps.clock()):
        by = _holder(root)
        return replace(st, busy_by=by), Report("busy", by=by)
    return _work(deps, cfg, root, replace(st, busy_by=None), since)


def release(deps: Deps) -> bool:
    try:
        cfg = config.load_config(deps.paths.config)
    except config.SttConfigError:
        return False
    root = _root(deps, cfg)
    return root.is_dir() and claim.release(root, deps.pid)


def once(deps: Deps) -> Report:
    st = state.load(deps.paths.state)
    try:
        st, report = run_pass(deps, st, iso(deps.clock()))
    finally:
        release(deps)
    state.save(deps.paths.state, st)
    return report


def watch(deps: Deps, *, interval: float = 2.0, stop: Callable[[], bool] | None = None,
          sleep: Callable[[float], None] = time.sleep,
          report: Callable[[Report], None] = lambda r: None) -> None:  # fmt: skip
    deps = replace(deps, stop=stop) if stop is not None else deps
    since, st = iso(deps.clock()), state.load(deps.paths.state)
    try:
        while not deps.stop():
            new, got = run_pass(deps, st, since)
            if got.state == "off":
                release(deps)
            if new != st:
                state.save(deps.paths.state, new)
            st = new
            report(got)
            sleep(interval)
    finally:
        release(deps)


def _own_failure(folder: Path) -> bool:
    doc = scan.read_json(folder / writer.RESULT, 64 * 1024)
    pipeline = doc.get("pipeline") if isinstance(doc, dict) else None
    return isinstance(pipeline, dict) and doc.get("state") == "failed" and pipeline.get("app") == APP


def retry(deps: Deps) -> int:
    """Unblock, forget retry counts and delete cpa-stt's own failed results (the contract's manual rerun)."""
    st = state.load(deps.paths.state)
    cleared = replace(st, blocked=None, selftest_ok=None, config_sig=None, failures=MappingProxyType({}))
    state.save(deps.paths.state, cleared)
    deps.paths.retry.touch(mode=0o600)
    root = _root(deps, config.load_config(deps.paths.config))
    mine = [path for _, path in scan.session_dirs(root) if _own_failure(path)]
    for path in mine:
        (path / writer.RESULT).unlink()
    return len(mine)
