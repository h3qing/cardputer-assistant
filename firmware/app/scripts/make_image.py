#!/usr/bin/env python3
"""Release images (docs/09-m2-design.md §4.4), for CI releases and HIL test builds alike.

From a built env (pio run -e <env>) it writes to --out:
  cpa-<tag>.bin      merged image (bootloader 0x0, partitions 0x8000, boot_app0 0xe000,
                     app 0x10000): full flash at 0x0, or copied to the SD card for the
                     Launcher, which shows it as "cpa-<tag>" (at most 20 characters);
  cpa-<tag>-app.bin  the app alone, for 0x10000 (keeps the partition table);
  SHA256SUMS         `sha256sum -c` format.
The partition table is checked first (scripts/check_partitions.py): an image with a
spiffs label or a test app partition must never reach the Launcher.
merge_bin pads the gaps with 0xFF, so writing cpa-<tag>.bin at 0x0 erases the NVS at
0x9000 (a new epoch; safe by 05 §5.9) -- `pio run -t upload` does not.

Usage: python3 scripts/make_image.py --env cardputer-adv --tag v0.2.0 --out dist
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_partitions

TAG_RE = re.compile(r"^v\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$")
LAUNCHER_NAME_MAX = 20  # the Launcher shows a file name of at most 20 characters
APP_DIR = Path(__file__).resolve().parents[1]


def image_names(tag: str) -> tuple[str, str]:
    return f"cpa-{tag}.bin", f"cpa-{tag}-app.bin"


def check_tag(tag: str) -> str | None:
    """None when the tag is usable, else why not."""
    if not TAG_RE.match(tag):
        return f"tag {tag!r} is not v<major>.<minor>.<patch>[-suffix]"
    if len(f"cpa-{tag}") > LAUNCHER_NAME_MAX:
        return f"cpa-{tag} is longer than the Launcher's {LAUNCHER_NAME_MAX} characters"
    return None


def merge_command(pio: str, build: Path, boot_app0: Path, out: Path) -> list[str]:
    return [
        pio, "pkg", "exec", "--package", "tool-esptoolpy", "--",
        "esptool.py", "--chip", "esp32s3", "merge_bin", "-o", str(out),
        "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
        "0x0", str(build / "bootloader.bin"),
        "0x8000", str(build / "partitions.bin"),
        "0xe000", str(boot_app0),
        "0x10000", str(build / "firmware.bin"),
    ]  # fmt: skip


def sha256sums(files: list[Path]) -> str:
    rows = sorted((f.name, hashlib.sha256(f.read_bytes()).hexdigest()) for f in files)
    return "".join(f"{digest}  {name}\n" for name, digest in rows)


def default_boot_app0() -> Path:
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    return core / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"


def default_pio() -> str:
    return shutil.which("pio") or str(Path.home() / ".local" / "bin" / "pio")


def parse(argv: list[str]) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--env", required=True, help="PlatformIO env that was built, e.g. cardputer-adv")
    ap.add_argument("--tag", required=True, help="release tag, e.g. v0.2.0 or v0.2.0-dev")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--project", default=str(APP_DIR), help="firmware/app directory")
    ap.add_argument("--boot-app0", default=str(default_boot_app0()))
    ap.add_argument("--pio", default=default_pio())
    return ap.parse_args(argv)


def table_problems(partitions: Path) -> list[str]:
    try:
        return check_partitions.problems(check_partitions.decode(partitions.read_bytes()))
    except ValueError as e:
        return [str(e)]


def inputs_problem(build: Path, boot_app0: Path, env: str) -> str | None:
    names = ("bootloader.bin", "partitions.bin", "firmware.bin")
    missing = [str(build / n) for n in names if not (build / n).is_file()]
    missing += [] if boot_app0.is_file() else [str(boot_app0)]
    if missing:
        return f"missing {', '.join(missing)} (run: pio run -e {env})"
    bad = table_problems(build / "partitions.bin")
    return f"partition table: {'; '.join(bad)}" if bad else None


def main(argv: list[str], run=subprocess.run) -> int:
    args = parse(argv)
    build, boot_app0, out = Path(args.project) / ".pio" / "build" / args.env, Path(args.boot_app0), Path(args.out)
    problem = check_tag(args.tag) or inputs_problem(build, boot_app0, args.env)
    if problem:
        print(f"make_image: FAIL {problem}")
        return 1
    merged, app = (out / name for name in image_names(args.tag))
    out.mkdir(parents=True, exist_ok=True)
    result = run(merge_command(args.pio, build, boot_app0, merged), check=False, timeout=300)
    if result.returncode != 0 or not merged.is_file():
        print(f"make_image: FAIL esptool merge_bin exited {result.returncode}")
        return 1
    shutil.copyfile(build / "firmware.bin", app)
    (out / "SHA256SUMS").write_text(sha256sums([merged, app]))
    for f in (merged, app):
        print(f"make_image: {f} {f.stat().st_size:,} B")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
