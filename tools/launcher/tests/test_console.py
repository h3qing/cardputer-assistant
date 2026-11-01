"""Launcher 2.9.1 console replies and the partition table printout (pure)."""

import pytest

from cpalauncher.console import (
    BANNER,
    Kind,
    check_name,
    classify,
    label_base,
    parse_partitions,
    read_only_command,
)
from fakes import render_table, standard_table


def test_ready_carries_the_size() -> None:
    reply = classify("READY 2600704")
    assert (reply.kind, reply.n) == (Kind.READY, 2600704)


def test_ack_carries_written_and_total() -> None:
    reply = classify("ACK 2048/2600704\r")
    assert (reply.kind, reply.n, reply.total) == (Kind.ACK, 2048, 2600704)


@pytest.mark.parametrize("line", ["ACK", "ACK 12", "ACK x/y", "ACK 1/2/3", "READY", "READY -1", "READY 12k"])
def test_a_garbled_ready_or_ack_is_malformed(line: str) -> None:
    assert classify(line).kind is Kind.MALFORMED


def test_ok_and_err_keep_their_text() -> None:
    assert classify("OK flashed, rebooting").kind is Kind.OK
    err = classify("  ERR no space for app: too big ")
    assert (err.kind, err.text) == (Kind.ERR, "ERR no space for app: too big")


def test_banner_and_version() -> None:
    assert classify(BANNER).kind is Kind.BANNER
    version = classify("Launcher 2.9.1")
    assert (version.kind, version.text) == (Kind.VERSION, "2.9.1")


@pytest.mark.parametrize("line", ["", "Launcher", "Launcher SD", "OKAY", "ERROR", "nvs  type=data", "�\x00"])
def test_everything_else_is_other(line: str) -> None:
    assert classify(line).kind is Kind.OTHER


def test_parse_partitions_reads_the_launcher_printout() -> None:
    table = parse_partitions(render_table(standard_table(boot="cpav02")))
    assert table.complete
    labels = [p.label for p in table.entries]
    assert labels == ["nvs", "otadata", "app0", "coredump", "factor", "cpav02"]
    cpa = table.find("cpav02")
    assert (cpa.type, cpa.subtype, cpa.offset, cpa.size, cpa.boot) == ("app", "ota_1", 0x3F0000, 0xE0000, True)
    assert not table.find("factor").boot
    assert [a.label for a in table.ota_apps()] == ["factor", "cpav02"]
    assert table.free == ((0x4D0000, 0x330000),)
    assert table.boot_app().label == "cpav02"


def test_parse_partitions_ignores_noise_and_marks_a_cut_table() -> None:
    lines = ("Press the button to enter the Launcher!", "== Partition table ==",
             "nvs              type=data subtype=0x02      offset=0x009000 size=0x005000 (20 KB)")  # fmt: skip
    table = parse_partitions(lines)
    assert not table.complete
    assert [p.label for p in table.entries] == ["nvs"]
    assert table.boot_app() is None
    assert table.find("nope") is None


def test_parse_partitions_without_a_header_is_empty() -> None:
    table = parse_partitions(("ERR partition table read failed",))
    assert (table.entries, table.free, table.complete) == ((), (), False)


@pytest.mark.parametrize(
    ("name", "base"),
    [("cpa-v0.2.0", "cpav02"), ("factory-v0.9", "factor"), ("AB", "ab0000"), ("---", "app000"), ("x_y", "xy0000")],
)
def test_label_base_matches_the_launcher(name: str, base: str) -> None:
    assert label_base(name) == base


@pytest.mark.parametrize("name", ["cpa-v0.2.0", "factory-v0.9", "a", "A.b_c-9", "x" * 20])
def test_good_names(name: str) -> None:
    assert check_name(name) is None


@pytest.mark.parametrize("name", ["", "has space", "x" * 21, "-lead", ".lead", "semi;colon", "new\nline", "é"])
def test_bad_names(name: str) -> None:
    assert check_name(name)


@pytest.mark.parametrize("line", ["version", "partitions", "whoami", "help", "  version  "])
def test_read_only_commands(line: str) -> None:
    assert read_only_command(line)


@pytest.mark.parametrize(
    "line", ["reboot", "partition deleteall", "wifi list", "settings get", "flash firmware a 1", ""]
)
def test_commands_that_change_or_reveal_state_are_refused(line: str) -> None:
    assert not read_only_command(line)
