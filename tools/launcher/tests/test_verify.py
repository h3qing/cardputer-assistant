"""Was the install real? Partition-table diff (next Launcher session) or the daemon's handshake (pure)."""

import pytest

from cpalauncher.console import parse_partitions
from cpalauncher.verify import daemon_verdict, expected_fw, verify_partitions
from fakes import Part, render_table, standard_table

BEFORE = parse_partitions(render_table(standard_table(boot="cpav02")))
NEW = Part("cpav03", "app", "ota_2", 0x4D0000, 0xE0000, True)


def after(*extra: Part, boot: str | None = None):
    """The table after the install; BOOT goes to `boot`, else to the new part flagged boot."""
    chosen = boot if boot is not None else next((p.label for p in extra if p.boot), None)
    return parse_partitions(render_table(standard_table(boot=chosen, extra=extra)))


def test_one_new_app_set_to_boot_is_verified() -> None:
    verdict = verify_partitions(BEFORE, after(NEW), "cpa-v0.3.0", 880_240)
    assert verdict.ok
    assert "cpav03" in verdict.detail and "0x4D0000" in verdict.detail


def test_a_reused_label_digit_is_still_verified() -> None:
    second = Part("cpav01", "app", "ota_2", 0x4D0000, 0xE0000, True)
    assert verify_partitions(BEFORE, after(second), "cpa-v0.2.0", 880_240).ok


def test_an_unexpected_label_is_verified_with_a_note() -> None:
    odd = Part("app1", "app", "ota_2", 0x4D0000, 0xE0000, True)
    verdict = verify_partitions(BEFORE, after(odd), "cpa-v0.3.0", 880_240)
    assert verdict.ok and "expected" in verdict.detail


def test_no_new_app_is_not_verified() -> None:
    verdict = verify_partitions(BEFORE, after(boot="cpav02"), "cpa-v0.3.0", 880_240)
    assert not verdict.ok and "found 0" in verdict.detail


def test_a_new_app_not_set_to_boot_is_not_verified() -> None:
    idle = Part("cpav03", "app", "ota_2", 0x4D0000, 0xE0000, False)
    verdict = verify_partitions(BEFORE, after(idle, boot="cpav02"), "cpa-v0.3.0", 880_240)
    assert not verdict.ok and "boot" in verdict.detail


def test_a_partition_smaller_than_the_image_is_not_verified() -> None:
    verdict = verify_partitions(BEFORE, after(NEW), "cpa-v0.3.0", 0xE0000 + 1)
    assert not verdict.ok and "smaller" in verdict.detail


def test_an_incomplete_table_is_not_verified() -> None:
    cut = parse_partitions(render_table(standard_table(extra=(NEW,)))[:-1])
    assert not verify_partitions(BEFORE, cut, "cpa-v0.3.0", 10).ok


@pytest.mark.parametrize(
    ("name", "fw"),
    [("cpa-v0.2.0", "cpa-fw/0.2.0"), ("cpa-v0.2.0-rc1", "cpa-fw/0.2.0"), ("factory-v0.9", None), ("x", None)],
)
def test_expected_fw(name: str, fw: str | None) -> None:
    assert expected_fw(name) == fw


def doc(link="ready", fw="cpa-fw/0.2.0", launcher=True, running=True, store="sd"):
    return {"running": running, "link": link, "device": {"fw": fw, "launcher": launcher, "store": store}}


def test_daemon_ready_on_launcher_with_the_right_fw() -> None:
    ok, detail = daemon_verdict(doc(), "cpa-fw/0.2.0")
    assert ok and detail == "link ready fw=cpa-fw/0.2.0 launcher=yes store=sd"


def test_daemon_fw_with_a_suffix_matches() -> None:
    assert daemon_verdict(doc(fw="cpa-fw/0.2.0-test"), "cpa-fw/0.2.0")[0]
    assert not daemon_verdict(doc(fw="cpa-fw/0.2.01"), "cpa-fw/0.2.0")[0]


def test_daemon_without_an_expected_fw_only_needs_launcher() -> None:
    assert daemon_verdict(doc(fw="cpa-fw/9.9.9"), None)[0]


@pytest.mark.parametrize(
    ("status", "why"),
    [
        (None, "not running"),
        (doc(running=False), "not running"),
        (doc(link="paused"), "link paused"),
        (doc(link="handshaking"), "link handshaking"),
        (doc(launcher=False), "launcher=no"),
        (doc(launcher=None), "launcher=-"),
        (doc(fw="cpa-fw/0.1.0"), "expected cpa-fw/0.2.0"),
    ],
)
def test_daemon_not_yet(status, why: str) -> None:
    ok, detail = daemon_verdict(status, "cpa-fw/0.2.0")
    assert not ok and why in detail
