"""`cpa status` shows hello.store / hello.launcher (docs/09 §7): '-' for a v0.1 device or no device.

device_status() builds the status "device" block; the CLI is driven with a stubbed control client.
"""

from __future__ import annotations

import json

import pytest

from cpad.app import device_status
from cpad.cli import main as cli
from cpad.protocol import messages as m


def _hello(**extra) -> m.DeviceHello:
    return m.DeviceHello(id="d1", re="h1", fw="cpa-fw/0.2.0", dev="020000000001", boot="3QZK1M7-0000C", up_ms=1,
                         rst="software", time=False, pending=0, bat_mv=4150, revs=m.Revs(), **extra)  # fmt: skip


def test_device_status_for_a_v01_device_has_no_store():
    block = device_status("SER1", _hello(), 3)
    assert block == {"serial": "SER1", "dev": "020000000001", "fw": "cpa-fw/0.2.0", "boot": "3QZK1M7-0000C",
                     "pending": 3, "store": None, "launcher": None}  # fmt: skip


def test_device_status_reports_store_and_launcher():
    block = device_status("SER1", _hello(store="sd", launcher=True), 0)
    assert (block["store"], block["launcher"]) == ("sd", True)


def test_device_status_without_a_device():
    block = device_status(None, None, None)
    assert block == {"serial": None, "dev": None, "fw": None, "boot": None, "pending": None,
                     "store": None, "launcher": None}  # fmt: skip


def _running(device: dict) -> dict:
    return {"ok": True, "running": True, "version": "0.2.0", "link": "ready", "paused_s": 0.0,
            "device": {"serial": "SER1", "dev": "020000000001", "fw": "cpa-fw/0.2.0", "boot": "3QZK1M7-0000C",
                       "pending": 0, **device},
            "agent": {"status": "ok", "version": "2.1.272", "spawn": True, "reply": False},
            "projects": [], "projects_error": None, "items_stored": 0, "queue_open": 0}  # fmt: skip


@pytest.fixture
def status_doc(monkeypatch, tmp_path):
    monkeypatch.setenv("CPA_HOME", str(tmp_path / "home"))
    holder: dict = {}
    monkeypatch.setattr(cli.client, "request", lambda sock, req: holder["doc"])
    return holder


@pytest.mark.parametrize(
    ("device", "shown"),
    [
        ({}, "store=- launcher=-"),
        ({"store": None, "launcher": None}, "store=- launcher=-"),
        ({"store": "sd", "launcher": True}, "store=sd launcher=yes"),
        ({"store": "int", "launcher": False}, "store=int launcher=no"),
        ({"store": "none", "launcher": None}, "store=none launcher=-"),
    ],
)
def test_cli_prints_store_and_launcher(status_doc, capsys, device, shown):
    status_doc["doc"] = _running(device)
    assert cli.main(["status"]) == 0
    link_line = next(line for line in capsys.readouterr().out.splitlines() if line.startswith("link:"))
    assert link_line.endswith(shown)


def test_cli_json_puts_store_and_launcher_under_device(status_doc, capsys):
    status_doc["doc"] = _running({"store": "sd", "launcher": True})
    assert cli.main(["status", "--json"]) == 0
    device = json.loads(capsys.readouterr().out)["device"]
    assert (device["store"], device["launcher"]) == ("sd", True)
