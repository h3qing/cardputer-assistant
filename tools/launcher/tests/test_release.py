"""Fetch Launcher 2.9.1 into a cache and verify size + SHA-256 before every use."""

import hashlib
import io
import os
from pathlib import Path

import pytest

from cpalauncher.release import (
    LAUNCHER,
    FetchError,
    Release,
    cache_path,
    check_blob,
    default_cache_dir,
    fetch,
    verify_file,
)

BLOB = os.urandom(5000)
SPEC = Release("9.9.9", "https://example.invalid/L.bin", "L.bin", len(BLOB), hashlib.sha256(BLOB).hexdigest())


class Net:
    """urlopen stand-in: serves `body` and counts calls."""

    def __init__(self, body: bytes = BLOB, error: Exception | None = None) -> None:
        self.body, self.error, self.urls = body, error, []

    def __call__(self, url: str, timeout: float):
        self.urls.append((url, timeout))
        if self.error:
            raise self.error
        return io.BytesIO(self.body)


def test_the_pinned_release_is_2_9_1() -> None:
    assert LAUNCHER.version == "2.9.1"
    assert (
        LAUNCHER.url == "https://github.com/bmorcelli/Launcher/releases/download/2.9.1/Launcher-m5stack-cardputer.bin"
    )
    assert LAUNCHER.size == 1_422_720
    assert LAUNCHER.sha256 == "2adff1ad67b5e08319dae682dc4edb01f1d857767c1738bdd0ccd097a8acb024"


def test_check_blob() -> None:
    assert check_blob(SPEC.size, SPEC.sha256, SPEC) is None
    assert "size" in check_blob(SPEC.size + 1, SPEC.sha256, SPEC)
    assert "SHA-256" in check_blob(SPEC.size, "0" * 64, SPEC)


def test_default_cache_dir_honours_xdg() -> None:
    assert default_cache_dir({"XDG_CACHE_HOME": "/c"}, Path("/h")) == Path("/c/cardputer-assistant/launcher")
    assert default_cache_dir({}, Path("/h")) == Path("/h/.cache/cardputer-assistant/launcher")


def test_fetch_downloads_verifies_and_caches(tmp_path: Path) -> None:
    net, said = Net(), []
    path = fetch(SPEC, tmp_path, urlopen=net, say=said.append)
    assert path == cache_path(tmp_path, SPEC) and path.read_bytes() == BLOB
    assert net.urls == [(SPEC.url, 60.0)]
    again = fetch(SPEC, tmp_path, urlopen=Net(error=AssertionError("no network on a cache hit")), say=said.append)
    assert again == path
    assert not [p for p in path.parent.iterdir() if p.name.startswith(".part")]


def test_a_corrupted_cache_is_downloaded_again(tmp_path: Path) -> None:
    path = cache_path(tmp_path, SPEC)
    path.parent.mkdir(parents=True)
    path.write_bytes(b"tampered")
    said: list[str] = []
    assert fetch(SPEC, tmp_path, urlopen=Net(), say=said.append).read_bytes() == BLOB
    assert any("does not match" in s for s in said)


@pytest.mark.parametrize("body", [BLOB[:-1], BLOB + b"!", b"\x00" * len(BLOB)])
def test_a_wrong_download_is_rejected_and_not_kept(tmp_path: Path, body: bytes) -> None:
    with pytest.raises(FetchError):
        fetch(SPEC, tmp_path, urlopen=Net(body), say=lambda _m: None)
    assert not cache_path(tmp_path, SPEC).exists()
    assert list(cache_path(tmp_path, SPEC).parent.iterdir()) == []


def test_a_network_error_is_a_fetch_error(tmp_path: Path) -> None:
    with pytest.raises(FetchError, match="download failed"):
        fetch(SPEC, tmp_path, urlopen=Net(error=OSError("unreachable")), say=lambda _m: None)


def test_only_https_is_fetched(tmp_path: Path) -> None:
    plain = Release("1", "http://example.invalid/L.bin", "L.bin", 1, "0" * 64)
    with pytest.raises(FetchError, match="https"):
        fetch(plain, tmp_path, urlopen=Net(), say=lambda _m: None)


def test_verify_file(tmp_path: Path) -> None:
    good = tmp_path / "good.bin"
    good.write_bytes(BLOB)
    assert verify_file(good, SPEC) is None
    assert "missing" in verify_file(tmp_path / "absent.bin", SPEC)
    good.write_bytes(BLOB[:10])
    assert "size" in verify_file(good, SPEC)
