"""The pinned Launcher release: fetch it once into a cache, verify size and SHA-256 before every use.

Evidence: docs/gates/launcher.md §4 and docs/07 G-L1 (2026-10-20: 1,422,720 B, SHA-256 2adff1ad...,
flashed at 0x0 with esptool). Nothing unverified ever reaches the cache path: the download goes to a
temporary file next to it and is renamed only after both checks pass.
"""

from __future__ import annotations

import hashlib
import os
import tempfile
import urllib.request
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

READ_CHUNK = 64 * 1024
TIMEOUT_S = 60.0


@dataclass(frozen=True, slots=True)
class Release:
    version: str
    url: str
    file_name: str
    size: int
    sha256: str


LAUNCHER = Release(
    version="2.9.1",
    url="https://github.com/bmorcelli/Launcher/releases/download/2.9.1/Launcher-m5stack-cardputer.bin",
    file_name="Launcher-m5stack-cardputer.bin",
    size=1_422_720,
    sha256="2adff1ad67b5e08319dae682dc4edb01f1d857767c1738bdd0ccd097a8acb024",
)


class FetchError(Exception):
    """The release could not be downloaded, or the bytes are not the pinned release."""


def default_cache_dir(env: Mapping[str, str], home: Path) -> Path:
    base = Path(env["XDG_CACHE_HOME"]) if env.get("XDG_CACHE_HOME") else home / ".cache"
    return base / "cardputer-assistant" / "launcher"


def cache_path(cache_dir: Path, spec: Release) -> Path:
    return cache_dir / spec.version / spec.file_name


def check_blob(size: int, sha256: str, spec: Release) -> str | None:
    """None when (size, sha256) are the pinned release's, else what differs."""
    if size != spec.size:
        return f"size {size} B, expected {spec.size} B"
    if sha256 != spec.sha256:
        return f"SHA-256 {sha256[:16]}..., expected {spec.sha256[:16]}..."
    return None


def verify_file(path: Path, spec: Release) -> str | None:
    if not path.is_file():
        return f"{path} is missing"
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for block in iter(lambda: fh.read(READ_CHUNK), b""):
            digest.update(block)
    return check_blob(path.stat().st_size, digest.hexdigest(), spec)


def fetch(spec: Release, cache_dir: Path, urlopen: Callable[..., Any] = urllib.request.urlopen,
          say: Callable[[str], None] = print) -> Path:  # fmt: skip
    """The verified release file in the cache, downloading it when absent or not matching."""
    if not spec.url.startswith("https://"):
        raise FetchError(f"refusing to fetch over anything but https: {spec.url}")
    path = cache_path(cache_dir, spec)
    if path.exists():
        problem = verify_file(path, spec)
        if problem is None:
            say(f"cached Launcher {spec.version}: {path} (size and SHA-256 verified)")
            return path
        say(f"cached Launcher {spec.version} does not match ({problem}); downloading again")
        path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    say(f"downloading Launcher {spec.version} from {spec.url}")
    _download(spec, path, urlopen)
    say(f"Launcher {spec.version}: {path} (size and SHA-256 verified)")
    return path


def _download(spec: Release, path: Path, urlopen: Callable[..., Any]) -> None:
    fd, tmp_name = tempfile.mkstemp(prefix=".part-", dir=path.parent)
    tmp = Path(tmp_name)
    try:
        with os.fdopen(fd, "wb") as out:
            size, sha256 = _copy(spec, urlopen, out)
        problem = check_blob(size, sha256, spec)
        if problem:
            raise FetchError(f"downloaded file is not Launcher {spec.version}: {problem}")
        os.replace(tmp, path)
    except OSError as exc:
        raise FetchError(f"download failed: {exc.__class__.__name__}: {exc}") from exc
    finally:
        tmp.unlink(missing_ok=True)


def _copy(spec: Release, urlopen: Callable[..., Any], out: Any) -> tuple[int, str]:
    """Stream at most size + 1 bytes (enough to notice a longer file) while hashing."""
    digest, size = hashlib.sha256(), 0
    with urlopen(spec.url, timeout=TIMEOUT_S) as resp:  # noqa: S310 -- https only, checked in fetch()
        while size <= spec.size:
            block = resp.read(min(READ_CHUNK, spec.size + 1 - size))
            if not block:
                break
            digest.update(block)
            out.write(block)
            size += len(block)
    return size, digest.hexdigest()
