"""What can be installed through the console: app-only ESP32-S3 images, by name."""

import hashlib
import struct
from pathlib import Path

import pytest

from cpalauncher.images import (
    FACTORY,
    LAUNCHER_FREE,
    Known,
    check_app,
    name_for,
    resolve,
)
from cpalauncher.images import (
    ImageError as BadImage,
)


def app_image(size: int = 4096, chip: int = 9, segments: int = 3) -> bytes:
    header = bytes([0xE9, segments, 2, 0x4F]) + struct.pack("<I", 0x40380000) + bytes(4)
    header += struct.pack("<H", chip) + bytes(9) + b"\x01"
    return header + bytes(size - len(header))


def merged_image() -> bytes:
    body = bytearray(app_image(0x20000))
    body[0x8000:0x8003] = b"\xaa\x50\x01"
    return bytes(body)


def test_factory_is_the_backup_from_issue_12() -> None:
    assert FACTORY.key == "factory" and FACTORY.name == "factory-v0.9"
    assert FACTORY.rel_path == "backups/factory-app-cardputer-adv-v0.9.bin"
    assert (FACTORY.size, FACTORY.sha256[:12]) == (2_600_704, "5eccc4d9c9f7")


def test_a_good_app_image_passes() -> None:
    assert check_app(app_image()) is None


@pytest.mark.parametrize(
    ("data", "why"),
    [
        (b"", "empty"),
        (b"\xe9\x03", "header"),
        (b"\x00" + app_image()[1:], "0xE9"),
        (app_image(segments=0), "segment"),
        (app_image(segments=17), "segment"),
        (app_image(chip=5), "ESP32-S3"),
        (merged_image(), "merged"),
    ],
)
def test_bad_images_are_refused(data: bytes, why: str) -> None:
    assert why in check_app(data)


def test_an_image_bigger_than_the_launcher_space_is_refused() -> None:
    assert "larger" in check_app(app_image(LAUNCHER_FREE + 16))


@pytest.mark.parametrize(
    ("file", "name"),
    [
        ("cpa-v0.2.0-app.bin", "cpa-v0.2.0"),
        ("cpa-v0.2.0-rc1-app.bin", "cpa-v0.2.0-rc1"),
        ("bruce.bin", "bruce"),
        ("firmware.BIN", "firmware"),
    ],
)
def test_name_for(file: str, name: str) -> None:
    assert name_for(Path("/x") / file) == name


def write(tmp_path: Path, name: str, data: bytes) -> Path:
    path = tmp_path / name
    path.write_bytes(data)
    return path


def test_resolve_a_path(tmp_path: Path) -> None:
    data = app_image()
    image = resolve(str(write(tmp_path, "cpa-v0.3.0-app.bin", data)), tmp_path)
    assert (image.name, image.data, image.sha256) == ("cpa-v0.3.0", data, hashlib.sha256(data).hexdigest())


def test_resolve_with_an_explicit_name(tmp_path: Path) -> None:
    path = write(tmp_path, "cpa-v0.2.0-rc1-app.bin", app_image())
    assert resolve(str(path), tmp_path, name="cpa-v0.2.0").name == "cpa-v0.2.0"


def test_resolve_refuses_a_bad_name(tmp_path: Path) -> None:
    path = write(tmp_path, "a.bin", app_image())
    with pytest.raises(BadImage, match="name"):
        resolve(str(path), tmp_path, name="two words")


def test_resolve_refuses_a_file_name_that_makes_a_bad_name(tmp_path: Path) -> None:
    path = write(tmp_path, "a-very-long-firmware-name-v1.bin", app_image())
    with pytest.raises(BadImage, match="--name"):
        resolve(str(path), tmp_path)


def test_resolve_refuses_a_merged_image(tmp_path: Path) -> None:
    path = write(tmp_path, "cpa-v0.2.0.bin", merged_image())
    with pytest.raises(BadImage, match="-app.bin"):
        resolve(str(path), tmp_path)


def test_resolve_a_missing_file(tmp_path: Path) -> None:
    with pytest.raises(BadImage, match="not found"):
        resolve(str(tmp_path / "nope.bin"), tmp_path)


def known(tmp_path: Path, data: bytes, sha: str | None = None) -> Known:
    write(tmp_path, "factory.bin", data)
    return Known("factory", "factory.bin", "factory-v0.9", len(data), sha or hashlib.sha256(data).hexdigest())


def test_resolve_a_known_image_by_key(tmp_path: Path) -> None:
    data = app_image()
    image = resolve("factory", tmp_path, known={"factory": known(tmp_path, data)})
    assert (image.name, image.path, image.data) == ("factory-v0.9", tmp_path / "factory.bin", data)


def test_a_known_image_must_match_its_checksum(tmp_path: Path) -> None:
    spec = known(tmp_path, app_image(), sha="0" * 64)
    with pytest.raises(BadImage, match="SHA-256"):
        resolve("factory", tmp_path, known={"factory": spec})


def test_a_known_image_must_be_present(tmp_path: Path) -> None:
    spec = Known("factory", "missing.bin", "factory-v0.9", 1, "0" * 64)
    with pytest.raises(BadImage, match="backups"):
        resolve("factory", tmp_path, known={"factory": spec})
