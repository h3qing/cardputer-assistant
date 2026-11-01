"""Install targets: an app-only ESP32-S3 image and the name the Launcher will show for it.

`flash firmware` writes the bytes as one app (no data partitions), so only an app image will do:
first byte 0xE9, 1..16 segments, chip id 9 (ESP32-S3), no partition table at 0x8000 (a merged
`cpa-<tag>.bin` must go through the Launcher's SD install instead; use `cpa-<tag>-app.bin` here).
Known images are local backups kept out of git (backups/README.md) and checked against their SHA-256.
"""

from __future__ import annotations

import hashlib
import struct
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

from cpalauncher.console import check_name

APP_MAGIC = 0xE9
ESP32S3_CHIP_ID = 9
HEADER_LEN = 24
MAX_SEGMENTS = 16
LAUNCHER_FREE = 0x800000 - 0x170000  # 6,881,280 B: the whole dynamic area of an empty 8 MB Launcher table


class ImageError(Exception):
    """The target cannot be installed as given."""


@dataclass(frozen=True, slots=True)
class Known:
    key: str
    rel_path: str  # from the repository root
    name: str  # the Launcher's display name
    size: int
    sha256: str


@dataclass(frozen=True, slots=True)
class AppImage:
    name: str
    path: Path
    data: bytes
    sha256: str


FACTORY = Known(
    key="factory",
    rel_path="backups/factory-app-cardputer-adv-v0.9.bin",
    name="factory-v0.9",
    size=2_600_704,
    sha256="5eccc4d9c9f711cfae2a239578c30c8689f8e05b7f12146e7c9f657ad036bce4",
)
KNOWN: Mapping[str, Known] = {FACTORY.key: FACTORY}


def check_app(data: bytes) -> str | None:
    """None when `data` looks like an ESP32-S3 app image the console can install, else why not."""
    if not data:
        return "empty file"
    if len(data) < HEADER_LEN:
        return "too short for an ESP image header"
    if data[0] != APP_MAGIC:
        return "not an ESP app image (the first byte must be 0xE9)"
    if data[0x8000:0x8002] == b"\xaa\x50":
        return "a merged image (partition table at 0x8000): install the matching -app.bin instead"
    if not 1 <= data[1] <= MAX_SEGMENTS:
        return f"bad segment count {data[1]}"
    chip = struct.unpack_from("<H", data, 12)[0]
    if chip != ESP32S3_CHIP_ID:
        return f"built for chip id {chip}, not the ESP32-S3 ({ESP32S3_CHIP_ID})"
    if len(data) > LAUNCHER_FREE:
        return f"{len(data)} B is larger than the Launcher's {LAUNCHER_FREE} B of free flash"
    return None


def name_for(path: Path) -> str:
    """cpa-v0.2.0-app.bin -> cpa-v0.2.0 (the release convention, docs/09 §4.4); x.bin -> x."""
    stem = path.name[:-4] if path.name.lower().endswith(".bin") else path.name
    return stem[:-4] if stem.endswith("-app") else stem


def resolve(target: str, repo_root: Path, name: str | None = None, known: Mapping[str, Known] = KNOWN) -> AppImage:
    """A known key (`factory`) or a path to an app image, checked and ready to send."""
    path, data, default_name = _known(known[target], repo_root) if target in known else _file(target)
    chosen = name or default_name
    problem = check_name(chosen)
    if problem and name:
        raise ImageError(f"bad --name {name!r}: {problem}")
    if problem:
        raise ImageError(f"{chosen!r} cannot be the Launcher name ({problem}); pass --name")
    problem = check_app(data)
    if problem:
        raise ImageError(f"{path.name}: {problem}")
    return AppImage(chosen, path, data, hashlib.sha256(data).hexdigest())


def _known(spec: Known, repo_root: Path) -> tuple[Path, bytes, str]:
    path = repo_root / spec.rel_path
    if not path.is_file():
        raise ImageError(f"{spec.rel_path} not found: the {spec.key} image is a local backup (backups/README.md)")
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if (len(data), digest) != (spec.size, spec.sha256):
        raise ImageError(f"{spec.rel_path}: size or SHA-256 differs from backups/README.md; refusing to install")
    return path, data, spec.name


def _file(target: str) -> tuple[Path, bytes, str]:
    path = Path(target).expanduser()
    if not path.is_file():
        raise ImageError(f"{target} not found")
    return path, path.read_bytes(), name_for(path)
