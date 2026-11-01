"""What `cpa setup check` looks at (docs/27 §2.3), read-only and fast: no Spotlight, no login shell, no network, no
serial port opened (the USB list only). Every outside call goes through SetupDeps, so tests run on a temp HOME.

The facts never hold a USB serial, host_id or dev: a board is its device code (docs/27 D6), the 4 upper-case hex
of sha256("cpa-dev:" + the serial without colons, lower case).
"""

from __future__ import annotations

import contextlib
import hashlib
import os
import plistlib
import shutil
import sys
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from cpad import __version__
from cpad.agents.claude_code import hook_settings as hset
from cpad.archive.cloud import in_cloud
from cpad.config.settings import Config, ConfigError, Paths, load_config
from cpad.control import client
from cpad.device import port_finder
from cpad.service import launchd

TCC_DIRS = ("Documents", "Desktop", "Downloads")  # a LaunchAgent cannot write there without a prompt
BREW_BINS = ("/opt/homebrew/bin", "/usr/local/bin")
GGML = "ggml-*.bin"


@dataclass(frozen=True, slots=True)
class ServiceFacts:
    plist: bool
    python: str | None  # ProgramArguments[0] of the LaunchAgent
    this_python: str
    running: bool
    version: str | None  # the running cpad's
    this_version: str


@dataclass(frozen=True, slots=True)
class NotesFacts:
    shown: str  # ~/… for a person
    exists: bool
    writable: bool  # the folder, or the nearest folder above it that exists
    cloud: bool
    tcc: str | None  # Documents, Desktop or Downloads when it lies under one


@dataclass(frozen=True, slots=True)
class DeviceFacts:
    bound_code: str | None
    codes: tuple[str, ...]  # the Cardputers on USB now


@dataclass(frozen=True, slots=True)
class SttFacts:
    whisper: bool
    models: tuple[str, ...]
    risky: str | None  # where a model link points when that place gets cleaned or synced
    brew: bool = True  # Homebrew is here (whisper-cpp comes from it)


@dataclass(frozen=True, slots=True)
class HooksFacts:
    readable: bool
    ours: tuple[str, ...]  # the cpa paths our hook handlers run
    this_cpa: str | None


@dataclass(frozen=True, slots=True)
class ClaudeFacts:
    configured: str | None  # config.json claude_bin when it still runs; cpad opens sessions with it
    found: str | None  # a claude on PATH, in ~/.local/bin, ~/.claude/local or Homebrew


@dataclass(frozen=True, slots=True)
class Facts:
    service: ServiceFacts
    notes: NotesFacts
    label: str | None
    device: DeviceFacts
    filevault: bool | None
    stt: SttFacts
    hooks: HooksFacts
    claude: ClaudeFacts


@dataclass(frozen=True)
class SetupDeps:
    home: Path = field(default_factory=Path.home)
    agents_dir: Path | None = None
    runner: Callable[..., launchd.Completed] = launchd.run
    which: Callable[..., str | None] = field(default=shutil.which)
    ports: Callable[[], tuple[port_finder.PortInfo, ...]] = port_finder.list_ports
    status: Callable[[Paths], Mapping[str, Any] | None] | None = None
    settings: Path | None = None
    executable: str = sys.executable
    argv0: str = sys.argv[0]
    brew_bins: tuple[str, ...] = BREW_BINS


def device_code(serial: str) -> str:
    norm = serial.replace(":", "").lower()
    return hashlib.sha256(f"cpa-dev:{norm}".encode()).hexdigest()[:4].upper()


def shown(path: Path, home: Path) -> str:
    with contextlib.suppress(ValueError):
        return "~/" + str(path.relative_to(home))
    return str(path)


def _live_status(paths: Paths) -> Mapping[str, Any] | None:
    try:
        return client.request(paths.sock, {"cmd": "status"}, timeout=2.0)
    except (client.DaemonNotRunning, OSError, ValueError):
        return None


def service_facts(paths: Paths, deps: SetupDeps) -> ServiceFacts:
    plist = launchd.plist_path(deps.agents_dir or deps.home / "Library" / "LaunchAgents")
    python = None
    with contextlib.suppress(OSError, ValueError, plistlib.InvalidFileException):
        program = plistlib.loads(plist.read_bytes()).get("ProgramArguments") or [None]
        python = program[0] if isinstance(program[0], str) else None
    doc = (deps.status or _live_status)(paths)
    version = doc.get("version") if doc and isinstance(doc.get("version"), str) else None
    return ServiceFacts(plist.exists(), python, deps.executable, doc is not None, version, __version__)


def _writable(path: Path) -> bool:
    probe = next((p for p in (path, *path.parents) if p.exists()), None)
    return probe is not None and probe.is_dir() and os.access(probe, os.W_OK | os.X_OK)


def notes_facts(cfg: Config, home: Path) -> NotesFacts:
    path = cfg.notes_path()
    resolved = Path(os.path.realpath(path))
    tcc = next((d for d in TCC_DIRS if (home / d) == resolved or (home / d) in resolved.parents), None)
    cloud = False
    with contextlib.suppress(OSError):
        cloud = in_cloud(resolved, home)
    exists = path.is_dir()
    writable = _writable(path) and (not path.exists() or exists)
    return NotesFacts(shown(path, home), exists, writable, cloud, tcc)


def device_facts(cfg: Config, deps: SetupDeps) -> DeviceFacts:
    try:
        ports = deps.ports()
    except OSError:
        ports = ()
    boards = sorted((p for p in ports if (p.vid, p.pid) == (port_finder.ESPRESSIF_VID, port_finder.USB_JTAG_PID)),
                    key=lambda p: p.device)  # fmt: skip
    codes = tuple(device_code(p.serial) if p.serial else "????" for p in boards)
    return DeviceFacts(device_code(cfg.device_serial) if cfg.device_serial else None, codes)


def filevault(deps: SetupDeps) -> bool | None:
    try:
        out = deps.runner(["/usr/bin/fdesetup", "status"]).stdout
    except (OSError, ValueError):
        return None
    return True if "FileVault is On" in out else False if "FileVault is Off" in out else None


def _risky(link: Path, home: Path) -> str | None:
    if not link.is_symlink():
        return None
    target = Path(os.path.realpath(link))
    near = next((d for d in TCC_DIRS if (home / d) in target.parents), None)
    with contextlib.suppress(OSError):
        if near is None and in_cloud(target, home):
            return "iCloud"
    return near


def stt_facts(paths: Paths, deps: SetupDeps) -> SttFacts:
    found = deps.which("whisper-cli") or next(
        (b for b in deps.brew_bins if os.access(Path(b) / "whisper-cli", os.X_OK)), None)  # fmt: skip
    models_dir = paths.home / "models"
    links = sorted(models_dir.glob(GGML)) if models_dir.is_dir() else []
    present = [p for p in links if p.exists()]
    risky = next((r for r in (_risky(p, deps.home) for p in present) if r), None)
    return SttFacts(found is not None, tuple(p.name for p in present), risky, _brew(deps))


def _executable(path: Path) -> bool:
    return path.is_file() and os.access(path, os.X_OK)


def _brew(deps: SetupDeps) -> bool:
    return deps.which("brew") is not None or any(_executable(Path(b) / "brew") for b in deps.brew_bins)


def claude_facts(cfg: Config, deps: SetupDeps) -> ClaudeFacts:
    configured = cfg.claude_bin if cfg.claude_bin and _executable(Path(cfg.claude_bin)) else None
    places = (deps.home / ".local" / "bin", deps.home / ".claude" / "local", *(Path(b) for b in deps.brew_bins))
    found = deps.which("claude") or next((str(p / "claude") for p in places if _executable(p / "claude")), None)
    return ClaudeFacts(configured, found)


def hooks_facts(deps: SetupDeps) -> HooksFacts:
    this_cpa = None
    with contextlib.suppress(ValueError):
        this_cpa = hset.find_cpa(deps.argv0, deps.which)
    try:
        doc = hset.load(deps.settings or hset.default_path()).doc
    except (OSError, ValueError):
        return HooksFacts(False, (), this_cpa)
    groups = doc.get("hooks") if isinstance(doc.get("hooks"), Mapping) else {}
    entries = (e for gs in groups.values() if isinstance(gs, list) for g in gs if isinstance(g, Mapping)
               for e in (g.get("hooks") if isinstance(g.get("hooks"), list) else ()))  # fmt: skip
    ours = tuple(dict.fromkeys(e["command"] for e in entries if hset.is_ours(e)))
    return HooksFacts(True, ours, this_cpa)


def gather(paths: Paths, deps: SetupDeps) -> Facts:
    try:
        cfg = load_config(paths.config)
    except (ConfigError, OSError):
        cfg = Config()
    return Facts(service_facts(paths, deps), notes_facts(cfg, deps.home), cfg.host_label, device_facts(cfg, deps),
                 filevault(deps), stt_facts(paths, deps), hooks_facts(deps), claude_facts(cfg, deps))  # fmt: skip
