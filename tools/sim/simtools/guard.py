"""Keeps a cpad on the simulator away from the user's real data.

$CPA_HOME must be set, and be a directory inside the system's temporary directory (never the default
~/Library/Application Support/CardputerAssistant). config.json's notes_dir defaults to ~/CardputerNotes,
so it must be set and temporary too. prepare_home() writes such a config.json when there is none, with
claude_bin pointing at daemon/tests/fixtures/bin/fake-claude, whose state lives inside the home.
"""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

from cpad.config.settings import DEFAULT_HOME, Config, Paths, load_config, save_config, secure_home

REPO = Path(__file__).resolve().parents[3]
FAKE_CLAUDE = REPO / "daemon" / "tests" / "fixtures" / "bin" / "fake-claude"


class GuardError(RuntimeError):
    """The home or the notes directory is not a temporary directory."""


def temp_roots() -> tuple[Path, ...]:
    roots = {Path(tempfile.gettempdir()).resolve(), Path("/tmp").resolve()}  # noqa: S108
    return tuple(sorted(roots))


def is_temp(path: Path) -> bool:
    """Strictly inside a temporary root (the root itself is shared by everything)."""
    resolved = Path(path).expanduser().resolve()
    real_home = Path(DEFAULT_HOME).expanduser().resolve()
    inside = any(resolved != root and resolved.is_relative_to(root) for root in temp_roots())
    return inside and not resolved.is_relative_to(real_home)


def check_home(home: str | None) -> Path:
    if not home:
        raise GuardError("set CPA_HOME to a temporary directory, for example CPA_HOME=$(mktemp -d)")
    path = Path(home).expanduser().resolve()
    if not is_temp(path):
        roots = " or ".join(str(r) for r in temp_roots())
        raise GuardError(f"CPA_HOME={home} is not a temporary directory (it must be inside {roots})")
    return path


def check_notes(config: Config) -> None:
    notes = config.notes_path()
    if not is_temp(notes):
        raise GuardError(f"notes_dir {notes} is not a temporary directory: set it in config.json")


def prepare_home(home: Path, path_var: str) -> Paths:
    """The home's Paths; writes config.json (temporary notes, fake-claude) when there is none."""
    paths = Paths.at(Path(home))
    secure_home(paths)
    if paths.config.exists():
        return paths
    fake = paths.home / "fake-claude"
    fake.mkdir(mode=0o700, exist_ok=True)
    (fake / "state.json").write_text(json.dumps({"sessions": []}), encoding="utf-8")
    env = {"PATH": f"{FAKE_CLAUDE.parent}:{path_var}", "HOME": str(fake), "LANG": "en_US.UTF-8",
           "FAKE_CLAUDE_DIR": str(fake)}  # fmt: skip
    save_config(paths.config, Config(claude_bin=str(FAKE_CLAUDE), notes_dir=str(paths.home / "notes"), spawn_env=env))
    load_config(paths.config)  # what was written must load
    return paths
