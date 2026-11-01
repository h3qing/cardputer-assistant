"""Which project does a session's cwd belong to? (05 §7.6)

1. realpath(cwd), longest matching project root (whole path components only);
2. otherwise `git -C <cwd> rev-parse --git-common-dir` (2 s timeout, cached 60 s), match its worktree;
3. otherwise None ("other").
"""

from __future__ import annotations

import os
import subprocess
import time
from collections.abc import Callable, Iterable

from cpad.projects.registry import Project

GIT_TIMEOUT_S = 2.0
CACHE_S = 60.0


def _within(path: str, root: str) -> bool:
    return path == root or path.startswith(root.rstrip(os.sep) + os.sep)


def match_root(projects: Iterable[Project], path: str) -> Project | None:
    hits = [p for p in projects if p.root is not None and _within(path, p.root)]
    return max(hits, key=lambda p: len(p.root), default=None)


def run_git(cwd: str) -> str | None:
    """Absolute git common dir for cwd, or None (not a repo, git missing, timeout)."""
    try:
        done = subprocess.run(
            ["git", "-C", cwd, "rev-parse", "--git-common-dir"],
            capture_output=True,
            text=True,
            timeout=GIT_TIMEOUT_S,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    out = done.stdout.strip()
    return os.path.realpath(os.path.join(cwd, out)) if done.returncode == 0 and out else None


class Resolver:
    """Stateful only for its cache; the decision itself is match_root()."""

    def __init__(self, git: Callable[[str], str | None] = run_git, clock: Callable[[], float] = time.monotonic):
        self._git = git
        self._clock = clock
        self._cache: dict[str, tuple[float, str | None]] = {}

    def _common_root(self, cwd: str) -> str | None:
        now = self._clock()
        hit = self._cache.get(cwd)
        if hit and now - hit[0] < CACHE_S:
            return hit[1]
        common = self._git(cwd)
        root = os.path.dirname(common) if common else None
        self._cache = {**self._cache, cwd: (now, root)}
        return root

    def project_for(self, projects: tuple[Project, ...], cwd: str | None) -> Project | None:
        if not cwd:
            return None
        real = os.path.realpath(cwd)
        direct = match_root(projects, real)
        if direct or not os.path.isdir(real):
            return direct
        root = self._common_root(real)
        return match_root(projects, root) if root else None
