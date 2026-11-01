"""`cpa status` over the control socket shows what the device's hello said about its store (docs/09 §7)."""

from __future__ import annotations

import asyncio

from cpad.cli.main import main as cpa
from cpad.control import client


async def test_status_reports_store_and_launcher_of_a_v02_device(rigs, monkeypatch, capsys):
    rig = rigs(device={"hello_extra": {"store": "sd", "launcher": True}})
    await rig.start()
    await rig.ready()
    doc = await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "status"})
    assert (doc["device"]["store"], doc["device"]["launcher"]) == ("sd", True)
    monkeypatch.setenv("CPA_HOME", str(rig.paths.home))
    assert await asyncio.to_thread(cpa, ["status"]) == 0
    assert "store=sd launcher=yes" in capsys.readouterr().out


async def test_status_of_a_v01_device_shows_dashes(rigs, monkeypatch, capsys):
    rig = rigs()
    await rig.start()
    await rig.ready()
    monkeypatch.setenv("CPA_HOME", str(rig.paths.home))
    assert await asyncio.to_thread(cpa, ["status"]) == 0
    out = capsys.readouterr().out
    assert "link: ready" in out and "store=- launcher=-" in out
