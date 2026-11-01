"""$CPA_HOME layout and config.json (05 §7.1).

config.json: claude_bin (absolute), notes_dir, device.serial, poll_s=3, inbox_idle_s=60,
spawn_env (login-shell whitelist), experimental_bg_reply=false; v0.3 recordings: transcribe_wait_h=24
(hours before a session without result.json gets a 'not transcribed' line), rec_pull=true; v0.4.1: host_label
(optional name of this Mac on the device, written only when set); lang: zh or en (`cpa lang`, written only when
set; none: CPA_LANG and the locale decide, see cpad.i18n). Directory 0700, files 0600.
"""

from __future__ import annotations

import os
from collections.abc import Mapping
from dataclasses import dataclass, field, replace
from pathlib import Path
from types import MappingProxyType
from typing import Any

from cpad.config.files import DIR_MODE, FILE_MODE, ensure_private_dir, read_json, write_json
from cpad.config.host import valid_label

DEFAULT_HOME = "~/Library/Application Support/CardputerAssistant"
DEFAULT_NOTES = "~/CardputerNotes"


class ConfigError(ValueError):
    """config.json is invalid."""


def cpa_home() -> Path:
    return Path(os.environ.get("CPA_HOME") or DEFAULT_HOME).expanduser()


@dataclass(frozen=True, slots=True)
class Paths:
    home: Path
    config: Path
    projects: Path
    events: Path
    sock: Path
    lock: Path
    logs: Path
    host_id: Path
    id_key: Path  # docs/23 §3.2: the HMAC key of short device ids

    @classmethod
    def at(cls, home: Path) -> Paths:
        return cls(home, home / "config.json", home / "projects.json", home / "events.jsonl", home / "cpad.sock",
                   home / "cpad.lock", home / "logs", home / "host_id", home / "id_key")  # fmt: skip


def _env_map() -> Mapping[str, str]:
    return MappingProxyType({})


@dataclass(frozen=True, slots=True)
class Config:
    claude_bin: str | None = None
    notes_dir: str = DEFAULT_NOTES
    device_serial: str | None = None
    poll_s: float = 3.0
    inbox_idle_s: float = 60.0
    spawn_env: Mapping[str, str] = field(default_factory=_env_map)
    experimental_bg_reply: bool = False
    transcribe_wait_h: float = 24.0
    rec_pull: bool = True
    host_label: str | None = None
    lang: str | None = None

    def notes_path(self) -> Path:
        return Path(self.notes_dir).expanduser()


def _number(doc: Mapping, key: str, default: float, minimum: float, strict: bool) -> float:
    value = doc.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ConfigError(f"{key} must be a number")
    if value < minimum or (strict and value == minimum):
        raise ConfigError(f"{key} is out of range")
    return float(value)


def _optional_str(value: Any, key: str) -> str | None:
    if value is not None and not isinstance(value, str):
        raise ConfigError(f"{key} must be a string")
    return value


def _spawn_env(value: Any) -> Mapping[str, str]:
    if not isinstance(value, Mapping) or not all(isinstance(k, str) and isinstance(v, str) for k, v in value.items()):
        raise ConfigError("spawn_env must map strings to strings")
    return MappingProxyType(dict(value))


def _host_label(value: Any) -> str | None:
    if value is not None and not valid_label(value):
        raise ConfigError("host_label must be 1-24 bytes of text without control characters")
    return value


def _lang(value: Any) -> str | None:
    if value is not None and value not in ("zh", "en"):
        raise ConfigError("lang must be zh or en")
    return value


def parse_config(doc: Any) -> Config:
    if not isinstance(doc, Mapping):
        raise ConfigError("config.json must be an object")
    claude_bin = _optional_str(doc.get("claude_bin"), "claude_bin")
    if claude_bin is not None and not os.path.isabs(claude_bin):
        raise ConfigError("claude_bin must be an absolute path")
    device = doc.get("device") or {}
    reply, rec_pull = doc.get("experimental_bg_reply", False), doc.get("rec_pull", True)
    if not isinstance(device, Mapping) or not isinstance(reply, bool) or not isinstance(rec_pull, bool):
        raise ConfigError("device must be an object, experimental_bg_reply and rec_pull booleans")
    notes = _optional_str(doc.get("notes_dir", DEFAULT_NOTES), "notes_dir") or DEFAULT_NOTES
    return Config(
        claude_bin=claude_bin,
        notes_dir=notes,
        device_serial=_optional_str(device.get("serial"), "device.serial"),
        poll_s=_number(doc, "poll_s", 3.0, 0.0, strict=True),
        inbox_idle_s=_number(doc, "inbox_idle_s", 60.0, 0.0, strict=False),
        spawn_env=_spawn_env(doc.get("spawn_env", {})),
        experimental_bg_reply=reply,
        transcribe_wait_h=_number(doc, "transcribe_wait_h", 24.0, 0.0, strict=True),
        rec_pull=rec_pull,
        host_label=_host_label(doc.get("host_label")),
        lang=_lang(doc.get("lang")),
    )


def load_config(path: Path) -> Config:
    if not path.exists():
        return Config()
    try:
        return parse_config(read_json(path))
    except ValueError as exc:
        raise ConfigError(str(exc)) from exc


def dump_config(cfg: Config) -> dict[str, Any]:
    return {
        "claude_bin": cfg.claude_bin,
        "notes_dir": cfg.notes_dir,
        "device": {"serial": cfg.device_serial},
        "poll_s": cfg.poll_s,
        "inbox_idle_s": cfg.inbox_idle_s,
        "spawn_env": dict(cfg.spawn_env),
        "experimental_bg_reply": cfg.experimental_bg_reply,
        "transcribe_wait_h": cfg.transcribe_wait_h,
        "rec_pull": cfg.rec_pull,
        **({"host_label": cfg.host_label} if cfg.host_label is not None else {}),
        **({"lang": cfg.lang} if cfg.lang is not None else {}),
    }


def save_config(path: Path, cfg: Config) -> None:
    write_json(path, dump_config(cfg))


def merge_config(path: Path, **values: Any) -> Config:
    """Set these top-level keys and leave every other key as the user wrote it (no defaults filled in): what
    `cpa service install` does, so re-running the installer never rewrites someone's config (docs/27 §2.2).
    Refuses (ConfigError) when the file, or the result, is not a valid config."""
    try:
        doc = read_json(path) if path.exists() else {}
        if not isinstance(doc, dict):
            raise ValueError("config.json must hold a JSON object")
        merged = {**doc, **values}
        cfg = parse_config(merged)
    except ValueError as exc:
        raise ConfigError(str(exc)) from exc
    write_json(path, merged)
    return cfg


def replace_config(cfg: Config, **changes: Any) -> Config:
    if "spawn_env" in changes:
        changes = {**changes, "spawn_env": MappingProxyType(dict(changes["spawn_env"]))}
    return replace(cfg, **changes)


def secure_home(paths: Paths) -> tuple[str, ...]:
    """Create $CPA_HOME (0700) and logs/; tighten anything looser than 0700 / 0600. Returns what was fixed."""
    loose_home = paths.home.exists() and paths.home.stat().st_mode & 0o777 != DIR_MODE
    ensure_private_dir(paths.home)
    ensure_private_dir(paths.logs)
    files = (paths.config, paths.projects, paths.events, paths.lock, paths.host_id, paths.id_key)
    loose = tuple(p for p in files if p.exists() and p.stat().st_mode & 0o777 != FILE_MODE)
    for path in loose:
        path.chmod(FILE_MODE)
    return ((str(paths.home),) if loose_home else ()) + tuple(str(p) for p in loose)
