"""The raw terminal of cpa filing triage (cli.term) on a pseudo-terminal: one key at a time, an escape sequence
kept whole and never taken for Escape, a lone Escape, UTF-8, the end of input; keys typed before it started are
dropped; the terminal's own settings come back, also after an error."""

from __future__ import annotations

import io
import os
import pty
import select
import termios

import pytest

from cpad.cli import term


@pytest.fixture
def tty_pair():
    master, slave = pty.openpty()
    yield master, slave
    for fd in (master, slave):
        try:
            os.close(fd)
        except OSError:
            pass


def _input(slave: int) -> io.RawIOBase:
    return open(slave, "rb", buffering=0, closefd=False)  # noqa: SIM115 - the fixture closes the descriptor


def _settings(fd: int) -> list:
    """The terminal's settings but PENDIN, a state bit the kernel itself sets and clears (macOS)."""
    found = termios.tcgetattr(fd)
    return [*found[:3], found[3] & ~termios.PENDIN, *found[4:]]


def _drain(master: int) -> None:
    """Read what the terminal echoed, as a terminal window would; a setting change waits for it."""
    while select.select([master], [], [], 0.1)[0]:
        os.read(master, 4096)


def test_keys_are_single_characters_and_whole_escape_sequences(tty_pair):
    master, slave = tty_pair
    out = io.StringIO()
    with term.raw(_input(slave), out) as t:
        os.write(master, "3x\x1b[A\x1b[1;5B\x1bOP\x1bq语\r\n\x04".encode())
        keys = [t.key() for _ in range(10)]
        os.write(master, b"\x1b")
        keys.append(t.key())
        t.say("一行")
    assert keys == ["3", "x", "\x1b[A", "\x1b[1;5B", "\x1bOP", "\x1bq", "语", "enter", "enter", "eof", "esc"]
    assert out.getvalue() == "一行\n"


def test_typeahead_is_dropped_and_the_settings_come_back(tty_pair):
    master, slave = tty_pair
    before = _settings(slave)
    os.write(master, b"123 typed before the screen")
    _drain(master)
    with term.raw(_input(slave), io.StringIO()) as t:
        lflag = termios.tcgetattr(slave)[3]
        assert not lflag & termios.ICANON and not lflag & termios.ECHO and lflag & termios.ISIG
        os.write(master, b"k")
        assert t.key() == "k"
    assert _settings(slave) == before


def test_the_settings_come_back_after_an_error(tty_pair):
    _, slave = tty_pair
    before = _settings(slave)
    with pytest.raises(KeyboardInterrupt), term.raw(_input(slave), io.StringIO()):
        raise KeyboardInterrupt
    assert _settings(slave) == before


def test_a_terminal_that_went_away_is_the_end_of_input(tty_pair):
    master, slave = tty_pair
    with term.raw(_input(slave), io.StringIO()) as t:
        os.close(master)
        assert t.key() == "eof"
        assert t.key() == "eof"


def test_the_way_out_of_a_terminal_that_went_away_does_not_fail(tty_pair):
    """Linux answers the settings restore on a hung-up terminal with EIO; a closed descriptor fails the same way."""
    _, slave = tty_pair
    gone = os.dup(slave)
    with term.raw(_input(gone), io.StringIO()) as t:
        os.close(gone)
        assert t.key() == "eof"


def test_a_cut_escape_sequence_does_not_wait_for_the_next_key(tty_pair):
    master, slave = tty_pair
    with term.raw(_input(slave), io.StringIO()) as t:
        os.write(master, b"\x1b[12")
        assert t.key() == "\x1b[12"
        os.write(master, b"\x1bO")
        assert t.key() == "\x1bO"
        os.write(master, b"x")
        assert t.key() == "x"


def test_a_read_error_is_the_end_of_input(tty_pair):
    _, slave = tty_pair
    gone = os.dup(slave)
    os.close(gone)  # Linux answers a hung-up terminal with EIO; a closed descriptor fails the same way
    assert term.RawTerm(gone, io.StringIO()).key() == "eof"


def test_full_width_keys_of_a_chinese_input_method_are_the_keys_they_show(tty_pair):
    """Pinyin on: ２ ｘ ？ and the ideographic space arrive full width; a committed character stays itself."""
    master, slave = tty_pair
    with term.raw(_input(slave), io.StringIO()) as t:
        os.write(master, "２ｘ？\u3000下".encode())
        assert [t.key() for _ in range(5)] == ["2", "x", "?", " ", "下"]


def test_drop_throws_away_keys_already_typed(tty_pair):
    master, slave = tty_pair
    with term.raw(_input(slave), io.StringIO()) as t:
        os.write(master, b"22")
        assert t.key() == "2"
        t.drop()
        os.write(master, b"x")
        assert t.key() == "x"
