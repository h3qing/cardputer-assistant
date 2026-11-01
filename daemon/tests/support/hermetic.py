"""Hermetic test runs (#71): no test reads or writes the real HOME, runs the real claude, whisper-cli, launchctl
or mdfind, or reaches the user's running cpad / cpa-stt.

At configure time, before any test module is imported, HOME moves to a short temp dir (the default cpad.sock
below it must fit AF_UNIX), CPA_HOME and the other variables in SCRUB are unset, and stub claude, whisper-cli
and launchctl that only log their arguments come first on PATH. Every test then gets its own fresh HOME.

PATH loses its folders under a home (the user's own ~/.local/bin and the like), every variable that points into a
home or belongs to a Claude Code session is unset too, and pwd lookups of this account answer with $HOME.

An audit hook (sys.addaudithook) refuses, and records, any open, directory change, socket or spawn that touches
the home pytest started with (or the account's home) or one of the GUARDED programs as found on the starting
PATH. A spawn is refused when its argv (shell strings included), program, cwd or environment names a home, or
when it hands the child an environment without HOME (the child would fall back to the real one). A test that
did fails at teardown; anything outside a test fails the session. Stat-only checks and what a child does on its
own are out of its reach: the temp HOME, the PATH and the stubs are what protects children, and
hermetic-check.sb (an OS sandbox around a whole run) is the backstop that checks it. A root left by a killed run
is swept at the next start.

A test that must run a real program (CPA_TEST_WHISPER=1) says so with @pytest.mark.real_programs("whisper-cli")
and finds it with real_program(); its own files still live in temp dirs.

Shared by daemon/tests/conftest.py and tools/hil/tests/conftest.py, so it imports nothing from cpad.
"""

from __future__ import annotations

import os
import pwd
import re
import shutil
import sys
import tempfile
import threading
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path

import pytest

STUBS = ("claude", "whisper-cli", "launchctl")
GUARDED = (*STUBS, "mdfind")
EXTRA_PATH = ("/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin")
SCRUB = ("CPA_HOME", "CPA_PROTOCOL_DIR", "CPA_LOG_CONTENT", "CLAUDE_CONFIG_DIR", "ZDOTDIR",
         "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME")  # fmt: skip
SESSION_VARS = ("CLAUDE", "ANTHROPIC")  # the Claude Code session tests often run in: its socket, token, binary
REPO = Path(__file__).resolve().parents[3]
OUTSIDE = "(outside a test)"
OWNER = "owner.pid"  # in each session root: the pytest that made it, so a later run can sweep a killed run's root
TOKENS = re.compile(r"[\s'\"=;|&()<>`]+")  # splits a shell string or --opt=value into words

# audit event -> positions of the path arguments it touches (os.symlink: only where the link goes)
PATH_EVENTS = {
    "open": (0,), "os.listdir": (0,), "os.scandir": (0,), "os.mkdir": (0,), "os.remove": (0,), "os.rmdir": (0,),
    "os.rename": (0, 1), "os.link": (0, 1), "os.symlink": (1,), "os.chmod": (0,), "os.chown": (0,),
    "os.utime": (0,), "os.truncate": (0,), "os.chdir": (0,), "shutil.rmtree": (0,), "shutil.copyfile": (0, 1),
    "shutil.move": (0, 1), "glob.glob": (0,), "glob.glob/2": (0, 2), "socket.connect": (1,), "socket.bind": (1,),
    "socket.sendto": (1,), "sqlite3.connect": (0,), "os.chflags": (0,), "shutil.copytree": (0, 1),
    "shutil.copystat": (0, 1), "shutil.copymode": (0, 1), "shutil.make_archive": (0, 2),
    "shutil.unpack_archive": (0, 1), "os.setxattr": (0,), "os.removexattr": (0,), "os.getxattr": (0,),
    "os.listxattr": (0,),
}  # fmt: skip
# audit event -> positions of (executable, argv, cwd, env); None: the event has no such argument
SPAWN_EVENTS = {"subprocess.Popen": (0, 1, 2, 3), "os.posix_spawn": (0, 1, None, 2), "os.exec": (0, 1, None, 2),
                "os.spawn": (1, 2, None, 3), "os.system": (None, 0, None, None)}  # fmt: skip

STUB = """#!/bin/sh
# {name} stand-in for tests (#71): logs its arguments and fails; the real program never runs.
printf '%s\\n' "{name} $*" >> '{log}'
echo '{name}: test stub, the real program never runs in tests' >&2
exit 127
"""


class RealHomeAccess(PermissionError):
    """The guard refused a test's access to the real home or a real program."""


def both_forms(paths) -> tuple[str, ...]:
    """Each path as given and resolved (/tmp vs /private/tmp), absolute; never the root."""
    kept = {p for p in paths if p and p != "/"}
    return tuple(sorted({os.path.abspath(p) for p in kept} | {os.path.realpath(p) for p in kept}))


def _under(path: str, roots: tuple[str, ...]) -> bool:
    return any(path == root or path.startswith(root + os.sep) for root in roots)


def _text(value) -> str | None:
    return (os.fsdecode(value) or None) if isinstance(value, str | bytes | os.PathLike) else None


def in_home(path: str, homes: tuple[str, ...], allowed: tuple[str, ...]) -> bool:
    full = os.path.abspath(path)
    return _under(full, homes) and not _under(full, allowed)


def _starts(text: str, root: str) -> bool:
    return text.startswith(root) and text[len(root) : len(root) + 1] in ("", os.sep, " ", "'", '"', ":", ";")


def mentions(text: str, homes: tuple[str, ...], allowed: tuple[str, ...]) -> str | None:
    """Where `text` (an argv word, a shell string, an env value) names something under a home, or None."""
    for root in homes:
        at = text.find(root)
        while at >= 0:
            rest = text[at:]
            if _starts(rest, root) and not any(_starts(rest, ok) for ok in allowed):
                return rest
            at = text.find(root, at + 1)
    return None


def _installs(name: str, search: str) -> list[str]:
    """Every `name` on `search` plus the usual install folders, first match first."""
    folders = dict.fromkeys(filter(None, (*search.split(os.pathsep), *EXTRA_PATH)))
    return [where for where in (shutil.which(name, path=folder) for folder in folders) if where]


def find_programs(search: str) -> dict[str, str]:
    """name -> path of each guarded program on `search` (plus the usual install folders)."""
    found = {name: _installs(name, search) for name in GUARDED}
    return {name: where[0] for name, where in found.items() if where}


def _spawn_args(event: str, args: tuple) -> tuple[list[str], str | None, Mapping | None]:
    """A spawn event's program and argv words (a shell string is one word), its cwd and its env (None: inherited)."""
    exe, argv, cwd, env = (args[at] if at is not None and at < len(args) else None for at in SPAWN_EVENTS[event])
    words = [argv] if isinstance(argv, str | bytes | os.PathLike) else list(argv or ())
    texts = [text for text in map(_text, [exe, *words]) if text]
    return texts, _text(cwd), env if isinstance(env, Mapping) else None


@dataclass
class Guard:
    homes: tuple[str, ...]
    allowed: tuple[str, ...]  # inside a home but fine: this repository and the Python running the tests
    programs: Mapping[str, str]  # resolved path -> name of a real guarded program
    violations: list[str] = field(default_factory=list)
    test: str = OUTSIDE
    permitted: frozenset[str] = frozenset()
    active: bool = True
    _busy: threading.local = field(default_factory=threading.local)

    def __call__(self, event: str, args: tuple) -> None:
        if not self.active or (event not in PATH_EVENTS and event not in SPAWN_EVENTS):
            return
        if getattr(self._busy, "on", False):
            return
        self._busy.on = True
        try:
            why = self.refused(event, args)
        finally:
            self._busy.on = False
        if why:
            self.violations.append(f"{self.test}: {why}")
            raise RealHomeAccess(f"hermetic test guard (#71) refused: {why}")

    def refused(self, event: str, args: tuple) -> str | None:
        """Why this audit event must not happen in a test, or None."""
        if event in SPAWN_EVENTS:
            return self._spawn(event, args)
        for at in PATH_EVENTS.get(event, ()):
            path = _text(args[at]) if at < len(args) else None
            if path and self._in_home(path):
                return f"{event} {path}"
        return None

    @property
    def roots(self) -> tuple[tuple[str, ...], tuple[str, ...]]:
        return self.homes, self.allowed

    def _in_home(self, path: str) -> bool:
        return in_home(path, *self.roots)

    def _spawn(self, event: str, args: tuple) -> str | None:
        texts, cwd, env = _spawn_args(event, args)
        search = (os.environ if env is None else env).get("PATH", os.defpath)  # as exec would
        program = texts[0] if texts else ""
        where = shutil.which(program, path=search) if program and os.sep not in program else None
        for word in [*texts, *(token for text in texts for token in TOKENS.split(text) if token)]:
            why = self._program(event, word, search)
            if why:
                return why
        for text in filter(None, [*texts, where, cwd]):
            spot = mentions(text, self.homes, self.allowed)
            if spot:
                return f"{event} reaches {spot}"
        return self._child_env(event, env)

    def _program(self, event: str, word: str, search: str) -> str | None:
        name = os.path.basename(word)
        if name not in GUARDED or name in self.permitted:
            return None
        found = word if os.sep in word else shutil.which(word, path=search)
        if found and os.path.realpath(found) in self.programs:
            return f"{event} runs the real {name} ({found})"
        return None

    def _child_env(self, event: str, env: Mapping | None) -> str | None:
        """A child given its own environment needs a HOME (else it uses the real one) and nothing naming a home."""
        if env is None:
            return None
        if not (_text(env.get("HOME")) or _text(env.get(b"HOME"))):
            return f"{event} gives the child no HOME (it would fall back to the real one)"
        for value in filter(None, map(_text, env.values())):
            spot = mentions(value, self.homes, self.allowed)
            if spot:
                return f"{event} hands the child {spot}"
        return None


def new_guard(home: str | None, search: str) -> Guard:
    """The user's CPA_HOME / CLAUDE_CONFIG_DIR count as homes too: they may live outside $HOME."""
    own = (os.environ.get("CPA_HOME"), os.environ.get("CLAUDE_CONFIG_DIR"))
    homes = both_forms((home, pwd.getpwuid(os.getuid()).pw_dir, *own))
    allowed = both_forms((str(REPO), sys.prefix, sys.base_prefix, sys.exec_prefix, sys.base_exec_prefix))
    programs = {os.path.realpath(where): name for name in GUARDED for where in _installs(name, search)}
    return Guard(homes, allowed, programs)


@dataclass(frozen=True)
class Session:
    root: Path  # short temp dir: home/, bin/ (the stubs), calls.log, and each test's home
    home: str  # the HOME pytest started with
    real: Mapping[str, str]  # guarded program -> its path on the starting PATH
    saved: Mapping[str, str | None]  # the variables to put back at the end

    @property
    def bin(self) -> Path:
        return self.root / "bin"

    @property
    def log(self) -> Path:
        return self.root / "calls.log"


def _write_stubs(folder: Path, log: Path) -> None:
    folder.mkdir()
    for name in STUBS:
        stub = folder / name
        stub.write_text(STUB.format(name=name, log=log), encoding="utf-8")
        stub.chmod(0o755)


def _leaky(name: str, value: str, watch: Guard) -> bool:
    """A variable no test or child may inherit: the Claude Code session's, or one that points into a home."""
    return name not in ("HOME", "PATH") and (name.startswith(SESSION_VARS) or bool(mentions(value, *watch.roots)))


def sweep_stale(parent: Path) -> None:
    """Remove session roots whose pytest is gone (killed runs): their calls.log holds what the stubs were given."""
    for root in parent.glob("cpa*"):
        try:
            os.kill(int((root / OWNER).read_text(encoding="utf-8")), 0)
        except ProcessLookupError:
            shutil.rmtree(root, ignore_errors=True)
        except (OSError, ValueError):
            continue  # alive, not ours, or not a session root


def isolate(home: str, search: str, watch: Guard) -> Session:
    """Move this process (and its children) to a temp HOME with the stubs first on a PATH outside the homes."""
    parent = "/tmp" if os.path.isdir("/tmp") else None
    if parent:
        sweep_stale(Path(parent))
    root = Path(tempfile.mkdtemp(prefix="cpa", dir=parent))
    (root / OWNER).write_text(str(os.getpid()), encoding="utf-8")
    leaky = [name for name, value in os.environ.items() if _leaky(name, value, watch)]
    saved = {name: os.environ.get(name) for name in ("HOME", "PATH", *SCRUB, *leaky)}
    session = Session(root, home, find_programs(search), saved)
    _write_stubs(session.bin, session.log)
    (root / "home").mkdir()
    kept = [folder for folder in search.split(os.pathsep) if folder and not in_home(folder, *watch.roots)]
    os.environ.update(HOME=str(root / "home"), PATH=os.pathsep.join((str(session.bin), *kept)))
    for name in (*SCRUB, *leaky):
        os.environ.pop(name, None)
    return session


def _redirect_pwd(lookup):
    """pwd answers for this account with $HOME: ~user and a HOME-less Path.home() stay in the temp dir."""

    def redirected(key):
        entry = lookup(key)
        if entry.pw_uid != os.getuid():
            return entry
        home = os.environ.get("HOME") or str(session().root / "home")
        return pwd.struct_passwd((*entry[:5], home, entry.pw_shell))

    return redirected


_SESSION: Session | None = None
_GUARD: Guard | None = None
_PWD = (pwd.getpwuid, pwd.getpwnam)


def session() -> Session:
    assert _SESSION is not None, "the hermetic plugin is not configured"
    return _SESSION


def guard() -> Guard:
    assert _GUARD is not None, "the hermetic plugin is not configured"
    return _GUARD


def original_home() -> str:
    """The HOME pytest started with: for opt-in tests that need the user's own files (e.g. a whisper model)."""
    return session().home


def real_program(name: str) -> str | None:
    """A guarded program on the PATH pytest started with; running it needs @pytest.mark.real_programs(name)."""
    if name not in GUARDED:
        raise KeyError(name)
    return session().real.get(name)


def stub_dir() -> Path:
    return session().bin


def stub_calls() -> list[str]:
    """What the stubs were called with, one "name args" line per call, this session."""
    log = session().log
    return log.read_text(encoding="utf-8").splitlines() if log.exists() else []


# --- pytest hooks and fixtures (re-exported by the conftests) ------------------------------------------------


def pytest_configure(config: pytest.Config) -> None:
    global _SESSION, _GUARD
    config.addinivalue_line("markers", "real_programs(*names): an opt-in test may run these real programs")
    home = os.environ.get("HOME") or pwd.getpwuid(os.getuid()).pw_dir
    search = os.environ.get("PATH", "")
    _GUARD = new_guard(home, search)
    _SESSION = isolate(home, search, _GUARD)
    pwd.getpwuid, pwd.getpwnam = _redirect_pwd(_PWD[0]), _redirect_pwd(_PWD[1])
    sys.addaudithook(_GUARD)


def pytest_unconfigure(config: pytest.Config) -> None:
    pwd.getpwuid, pwd.getpwnam = _PWD
    if _GUARD is not None:
        _GUARD.active = False
    if _SESSION is not None:
        for name, value in _SESSION.saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
        shutil.rmtree(_SESSION.root, ignore_errors=True)


@pytest.fixture(autouse=True)
def hermetic_home(request: pytest.FixtureRequest, monkeypatch: pytest.MonkeyPatch):
    """A fresh temp HOME for each test; fails the test at teardown if it reached the real home or a real program."""
    home = Path(tempfile.mkdtemp(prefix="h", dir=session().root))
    monkeypatch.setenv("HOME", str(home))
    for name in SCRUB:
        monkeypatch.delenv(name, raising=False)
    marker = request.node.get_closest_marker("real_programs")
    watch = guard()
    seen = len(watch.violations)
    watch.test, watch.permitted = request.node.nodeid, frozenset(marker.args if marker else ())
    try:
        yield home
    finally:
        watch.test, watch.permitted = OUTSIDE, frozenset()
        shutil.rmtree(home, ignore_errors=True)
    leaked = watch.violations[seen:]
    if leaked:
        pytest.fail("reached the real home or a real program (#71):\n  " + "\n  ".join(leaked), pytrace=False)


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    if _GUARD is not None and _GUARD.violations and session.exitstatus == 0:
        session.exitstatus = pytest.ExitCode.TESTS_FAILED


def pytest_terminal_summary(terminalreporter) -> None:
    if _GUARD is not None and _GUARD.violations:
        terminalreporter.section("hermetic guard (#71): the real home or a real program was reached")
        for line in _GUARD.violations:
            terminalreporter.line(line)
