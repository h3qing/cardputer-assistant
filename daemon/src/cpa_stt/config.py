"""cpa-stt's own configuration, $CPA_HOME/stt/config.json (docs/22 §4.3, §4.4). Pure, plus load / save.

{"provider": "off" | "command", "recordings_dir": null (= cpad's notes_dir/recordings), "argv": [...] | null,
 "sandbox_mach": [...], "meetings": true, "timeout_s": 120, "drop_lines": []}

The command is an argv array, never a shell line: argv[0] is an absolute path; "{wav}" is exactly one whole
element and "{offset_ms}" at most one. This is cpa-stt's own config format, not an outside contract
(docs/15 §1). Unknown keys are refused, so a typo never silently falls back to a default.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cpad.config.files import ensure_private_dir, write_json
from cpad.protocol.validate import matches

PROVIDERS = ("off", "command")
WAV, OFFSET = "{wav}", "{offset_ms}"
DEFAULT_MACH = ("com.apple.MTLCompilerService", "com.apple.system.opendirectoryd.membership")
MACH_NAME = r"^[A-Za-z0-9._-]{1,128}$"
ARGV_MAX, ARG_MAX, DROP_MAX, DROP_LEN = 64, 4096, 200, 500
KEYS = frozenset({"provider", "recordings_dir", "argv", "sandbox_mach", "meetings", "timeout_s", "drop_lines"})


class SttConfigError(ValueError):
    """The configuration is invalid; nothing is saved."""


@dataclass(frozen=True, slots=True)
class SttConfig:
    provider: str = "off"
    recordings_dir: str | None = None
    argv: tuple[str, ...] | None = None
    sandbox_mach: tuple[str, ...] = DEFAULT_MACH
    meetings: bool = True
    timeout_s: float = 120.0
    drop_lines: tuple[str, ...] = ()

    @property
    def on(self) -> bool:
        return self.provider == "command"


@dataclass(frozen=True, slots=True)
class SttPaths:
    home: Path
    dir: Path
    config: Path
    state: Path
    log: Path
    models: Path
    retry: Path  # `cpa-stt retry` leaves it; the next pass of any running cpa-stt consumes it

    @classmethod
    def at(cls, home: Path) -> SttPaths:
        stt = home / "stt"
        log = stt / "log" / "cpa-stt.log"
        return cls(home, stt, stt / "config.json", stt / "state.json", log, home / "models", stt / "retry")


def _strings(value: Any, key: str, *, max_items: int, max_len: int) -> tuple[str, ...]:
    ok = isinstance(value, list) and len(value) <= max_items
    if not ok or not all(isinstance(v, str) and 0 < len(v) <= max_len and "\x00" not in v for v in value):
        raise SttConfigError(f"{key} must be a list of at most {max_items} non-empty strings")
    return tuple(value)


def check_argv(value: Any) -> tuple[str, ...]:
    argv = _strings(value, "argv", max_items=ARGV_MAX, max_len=ARG_MAX)
    if not argv or not os.path.isabs(argv[0]):
        raise SttConfigError("argv[0] must be an absolute path to the program (no shell)")
    if argv.count(WAV) != 1 or argv.count(OFFSET) > 1:
        raise SttConfigError('argv needs "{wav}" exactly once and "{offset_ms}" at most once')
    if any(p in a and a != p for a in argv for p in (WAV, OFFSET)):
        raise SttConfigError("a placeholder must be a whole argv element")
    return argv


def _number(doc: dict, key: str, default: float) -> float:
    value = doc.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int | float) or not 0 < value <= 86400:
        raise SttConfigError(f"{key} must be a number of seconds above 0")
    return float(value)


def _mach(value: Any) -> tuple[str, ...]:
    names = _strings(value, "sandbox_mach", max_items=32, max_len=128)
    if not all(matches(MACH_NAME, n) for n in names):
        raise SttConfigError("sandbox_mach holds service names like com.apple.MTLCompilerService")
    return names


def _optional_dir(value: Any) -> str | None:
    if value is not None and not isinstance(value, str):
        raise SttConfigError("recordings_dir must be a path or null")
    return value


def parse_config(doc: Any) -> SttConfig:
    if not isinstance(doc, dict):
        raise SttConfigError("the config must be a JSON object")
    unknown = sorted(set(doc) - KEYS)
    if unknown:
        raise SttConfigError(f"unknown keys: {', '.join(unknown)}")
    provider = doc.get("provider", "off")
    if provider not in PROVIDERS:
        raise SttConfigError("provider must be off or command")
    argv = doc.get("argv")
    if provider == "command" or argv is not None:
        argv = check_argv(argv)
    if not isinstance(doc.get("meetings", True), bool):
        raise SttConfigError("meetings must be true or false")
    return SttConfig(
        provider=provider,
        recordings_dir=_optional_dir(doc.get("recordings_dir")),
        argv=argv,
        sandbox_mach=_mach(doc.get("sandbox_mach", list(DEFAULT_MACH))),
        meetings=doc.get("meetings", True),
        timeout_s=_number(doc, "timeout_s", 120.0),
        drop_lines=_strings(doc.get("drop_lines", []), "drop_lines", max_items=DROP_MAX, max_len=DROP_LEN),
    )


def dump_config(cfg: SttConfig) -> dict[str, Any]:
    return {"provider": cfg.provider, "recordings_dir": cfg.recordings_dir,
            "argv": list(cfg.argv) if cfg.argv is not None else None, "sandbox_mach": list(cfg.sandbox_mach),
            "meetings": cfg.meetings, "timeout_s": cfg.timeout_s, "drop_lines": list(cfg.drop_lines)}  # fmt: skip


def load_config(path: Path) -> SttConfig:
    if not path.exists():
        return SttConfig()
    try:
        return parse_config(json.loads(path.read_text(encoding="utf-8")))
    except (OSError, ValueError) as exc:
        raise SttConfigError(f"{path}: {exc}") from exc


def save_config(path: Path, cfg: SttConfig) -> None:
    ensure_private_dir(path.parent)
    write_json(path, dump_config(cfg))


def missing_paths(argv: tuple[str, ...]) -> tuple[str, ...]:
    """argv[0] and every absolute-path argument whose target (after symlinks) does not exist."""
    paths = (a for k, a in enumerate(argv) if k == 0 or os.path.isabs(a))
    return tuple(p for p in paths if not os.path.exists(os.path.realpath(p)))


def prompts(argv: tuple[str, ...]) -> tuple[str, ...]:
    """Prompt texts given to the engine (--prompt X or --prompt=X): echoed on silence, so they are filtered."""
    after = (argv[k + 1] for k, a in enumerate(argv[:-1]) if a == "--prompt")
    inline = (a.split("=", 1)[1] for a in argv if a.startswith("--prompt="))
    return tuple(p for p in (*after, *inline) if p)


def recordings_root(cfg: SttConfig, notes_dir: Path) -> Path:
    return Path(cfg.recordings_dir).expanduser() if cfg.recordings_dir else notes_dir / "recordings"
