"""Issue #4: a CPA_HOME whose cpad.sock path exceeds the AF_UNIX sun_path limit must fail clearly."""

from pathlib import Path

import pytest

from cpad.control import server


def test_short_path_is_accepted():
    server.check_socket_path(Path("/tmp/cpad.sock"), max_bytes=103)


def test_exact_limit_is_accepted_and_one_more_byte_is_rejected():
    ok = Path("/" + "a" * 92 + "/cpad.sock")  # 1 + 92 + 10 = 103 bytes
    assert len(str(ok).encode()) == 103
    server.check_socket_path(ok, max_bytes=103)
    with pytest.raises(server.SocketPathTooLong):
        server.check_socket_path(Path("/" + "a" * 93 + "/cpad.sock"), max_bytes=103)


def test_message_names_path_size_limit_and_fix():
    long_path = Path("/" + "b" * 120 + "/cpad.sock")
    with pytest.raises(server.SocketPathTooLong) as err:
        server.check_socket_path(long_path, max_bytes=103)
    message = str(err.value)
    assert str(long_path) in message
    assert "131 bytes" in message
    assert "103" in message
    assert "CPA_HOME" in message


def test_length_is_counted_in_bytes_not_characters():
    cjk = Path("/" + "录" * 30 + "/cpad.sock")  # 30 chars but 90 bytes of UTF-8
    with pytest.raises(server.SocketPathTooLong):
        server.check_socket_path(cjk, max_bytes=60)


def test_platform_limit_matches_sun_path_minus_nul():
    assert server.SOCKET_PATH_MAX in (103, 107)


async def test_start_refuses_long_path_before_touching_the_filesystem(tmp_path):
    long_home = tmp_path / ("c" * 120)
    srv = server.ControlServer(long_home / "cpad.sock", handler=None)
    with pytest.raises(server.SocketPathTooLong):
        await srv.start()


def test_cpa_daemon_reports_the_long_path_and_exits_nonzero(tmp_path, monkeypatch, capsys):
    from cpad.cli.main import main
    from cpad.config.files import write_json

    long_home = tmp_path / ("d" * 120)
    long_home.mkdir(mode=0o700)
    write_json(long_home / "config.json", {"claude_bin": "/nonexistent/claude", "notes_dir": str(tmp_path / "notes")})
    monkeypatch.setenv("CPA_HOME", str(long_home))
    assert main(["daemon"]) != 0
    err = capsys.readouterr().err
    assert "limit for unix sockets" in err
    assert "CPA_HOME" in err
