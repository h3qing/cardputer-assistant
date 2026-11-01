"""This Mac's identity on the host hello (docs/13 §11.1, #19): host_id in $CPA_HOME, host_label from config."""

from __future__ import annotations

import logging
import os
import stat

import pytest

from cpad.config import host, settings
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import matches


def _mode(path) -> int:
    return stat.S_IMODE(os.stat(path).st_mode)


class _Records(logging.Handler):
    def __init__(self) -> None:
        super().__init__(logging.DEBUG)
        self.records: list[logging.LogRecord] = []

    def emit(self, record: logging.LogRecord) -> None:
        self.records.append(record)


@pytest.fixture
def logged():
    """Every record of the cpad loggers, whatever an earlier test did to propagation."""
    handler, logger = _Records(), logging.getLogger("cpad")
    level = logger.level
    logger.addHandler(handler)
    logger.setLevel(logging.DEBUG)
    try:
        yield handler.records
    finally:
        logger.removeHandler(handler)
        logger.setLevel(level)


# --- host_id --------------------------------------------------------------------------------


def test_host_id_is_created_once_private_and_well_formed(tmp_path):
    path = tmp_path / "host_id"
    first = host.host_id(path)
    assert matches(LIMITS["host_id_pattern"], first)
    assert _mode(path) == 0o600
    assert path.read_text(encoding="ascii").strip() == first


def test_host_id_persists_across_restarts(tmp_path):
    path = tmp_path / "host_id"
    first = host.host_id(path)
    before = path.stat().st_mtime_ns
    assert host.host_id(path) == first and host.host_id(path) == first
    assert path.stat().st_mtime_ns == before, "an existing id is read, never rewritten"


def test_each_install_gets_its_own_random_id(tmp_path):
    assert host.host_id(tmp_path / "a") != host.host_id(tmp_path / "b")


@pytest.mark.parametrize("junk", [b"", b"7F3A9C1E5B2D4A60\n", b"abc\n", b"\xff\xfe\n", b"a" * 33])
def test_a_malformed_host_id_file_is_replaced_once(tmp_path, junk):
    path = tmp_path / "host_id"
    path.write_bytes(junk)
    path.chmod(0o644)
    fresh = host.host_id(path)
    assert matches(LIMITS["host_id_pattern"], fresh)
    assert host.host_id(path) == fresh and _mode(path) == 0o600


def test_host_id_never_appears_in_a_log_record(tmp_path, logged):
    path = tmp_path / "host_id"
    path.write_bytes(b"NOT AN ID\n")
    value = host.host_id(path)
    host.host_id(path)
    assert logged, "replacing a malformed id is logged"
    assert all(value not in record.getMessage() and value not in str(vars(record)) for record in logged)


def test_secure_home_tightens_a_loose_host_id(tmp_path):
    paths = settings.Paths.at(tmp_path / "home")
    paths.home.mkdir(mode=0o700)
    host.host_id(paths.host_id)
    paths.host_id.chmod(0o644)
    assert str(paths.host_id) in settings.secure_home(paths)
    assert _mode(paths.host_id) == 0o600


def test_host_id_lives_in_cpa_home(tmp_path):
    assert settings.Paths.at(tmp_path).host_id == tmp_path / "host_id"


# --- host_label -----------------------------------------------------------------------------


def test_host_label_comes_from_config_when_set():
    assert settings.parse_config({}).host_label is None
    cfg = settings.parse_config({"host_label": "八个汉字的主机名"})
    assert cfg.host_label == "八个汉字的主机名"
    assert settings.parse_config(settings.dump_config(cfg)) == cfg


def test_an_unset_host_label_leaves_config_json_as_before():
    assert "host_label" not in settings.dump_config(settings.Config())


@pytest.mark.parametrize("bad", ["", "九个汉字的主机名称", "a" * 25, "Studio\tMac", "a\u007fb", 7, True, "Mac \ud800",
                                 "\udfff"])  # fmt: skip
def test_an_invalid_host_label_is_a_config_error(bad):
    """A lone surrogate (json.loads accepts "\\ud800") cannot be sent: the hello would fail to encode."""
    with pytest.raises(settings.ConfigError, match="host_label"):
        settings.parse_config({"host_label": bad})
