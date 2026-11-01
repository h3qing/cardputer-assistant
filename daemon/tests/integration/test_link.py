"""Link-level integration over a pty FakeDevice (05 §6.3, §6.4, §7.2; US-04)."""

from __future__ import annotations

import asyncio
import os
import stat
import time
from dataclasses import replace

import pytest

from cpad.control import client
from cpad.device.safe_open import safe_open
from tests.support.rig import FAST, LineRecorder, PtySerial, until

RESET = (True, False)


@pytest.mark.parametrize(
    ("nudge_s", "hellos", "released_by"),
    [(0.5, 1, 1.9), (30.0, 2, 3.0)],
    ids=["blank-line nudge after 500 ms", "hello retry after 2 s"],
)
async def test_handshake_tx_parking(rigs, nudge_s, hellos, released_by):
    rig = rigs(device={"park_first_reply": True}, timing=replace(FAST, nudge_s=nudge_s))
    started = time.monotonic()
    await rig.start()
    await rig.ready(timeout=5.9)
    assert time.monotonic() - started < released_by
    assert len(rig.fake.host_messages("hello")) == hellos, "the parked reply is freed by the host's next write"
    await rig.synced()
    await asyncio.sleep(1.0)
    status = rig.daemon.status()
    assert (status["link"], status["reboots"]) == ("ready", 0)
    assert len(rig.fake.host_messages("time.set")) == 1, "a late duplicate hello reply must not trigger a resync"


async def test_silent_device_times_out_and_is_reopened(rigs):
    rig = rigs(timing=replace(FAST, timeout_s=1.0, ping_s=0.3))
    await rig.start()
    await rig.synced()
    rig.fake.stall()
    await until(lambda: rig.daemon.status()["last_release"] == "timeout", timeout=4)
    rig.fake.resume()
    await until(lambda: rig.recorder.count("open") >= 2, timeout=4)
    await rig.ready()


async def test_safe_open_never_rts1_dtr0(rigs):
    rig = rigs()
    await rig.start()
    await rig.ready()
    await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "pause", "seconds": 1})
    await until(lambda: rig.recorder.count("close") >= 1)
    await until(lambda: rig.recorder.count("open") >= 2, timeout=5)
    await rig.ready()
    assert rig.recorder.history, "control lines were recorded"
    assert RESET not in rig.recorder.history
    assert rig.recorder.history[-1] == (False, False)


def test_safe_open_on_a_real_pyserial_port(tmp_path):
    master, slave = os.openpty()
    try:
        recorder = LineRecorder()
        port = safe_open(os.ttyname(slave), factory=lambda: PtySerial(recorder))
        port.write(b"{}\n")
        assert os.read(master, 3) == b"{}\n"
        port.close()
        assert recorder.history == [(True, True), (True, True), (False, True), (False, False)]
    finally:
        os.close(master)
        os.close(slave)


async def test_credit_window_rx_overflow(rigs):
    timing = replace(FAST, ping_s=0.5, reclaim_s=1.0)
    rig = rigs(timing=timing)
    await rig.start()
    await rig.synced()
    link = rig.daemon.link

    # 1) ~12 KB of host-initiated lines while the device does not read: credit keeps it <= 4096 B.
    #    (debug.state is not coalesced; the unknown `pad` field is ignored by receivers, 05 §6.2)
    rig.fake.stall()
    for n in range(8):
        link.request_raw({"t": "debug.state", "n": n, "pad": "长" * 500})
    await asyncio.sleep(0.6)
    assert rig.fake.max_rx <= 4096 and rig.fake.dropped == 0
    assert link.credit_used() <= 4096
    rig.fake.resume()
    await until(lambda: len(rig.fake.host_messages("debug.state")) == 8, timeout=10)
    assert rig.fake.dropped == 0 and rig.fake.bad_lines == []

    # 2) The same volume bypassing credit overflows the device RX; rev reconciliation repairs the loss.
    rig.fake.stall()
    pad = "x" * 1000
    for i in range(12):
        os.write(rig.fake.slave, f'{{"v":0,"t":"ping","id":"z{i}","pad":"{pad}"}}\n'.encode())
    await until(lambda: rig.fake.dropped > 0)
    rig.claude.update(sessions=[{"pid": 1, "id": "aaaaaaaa", "cwd": rig.roots["cpa"], "kind": "background",
                                 "sessionId": "aaaaaaaa-0000-4000-8000-000000000001", "name": "cpa/x",
                                 "status": "busy", "state": "running"}])  # fmt: skip
    await until(lambda: rig.daemon.state.published.revs.sessions >= 2, timeout=5)
    await asyncio.sleep(0.5)
    rig.fake.resume()
    await until(lambda: rig.fake.revs["sessions"] == rig.daemon.state.published.revs.sessions, timeout=10)
    assert rig.fake.snapshots["sessions"]["sessions"][0]["sid"] == "aaaaaaaa"


@pytest.mark.slow
async def test_foreign_board_released_within_6s(rigs):
    rig = rigs(device={"foreign": "noise"})
    await rig.start()
    await until(lambda: rig.recorder.count("open") == 1)
    opened = rig.recorder.events[0][1]
    await until(lambda: rig.recorder.count("close") == 1, timeout=7)
    closed = next(t for e, t in rig.recorder.events if e == "close")
    assert closed - opened <= 6.2
    assert len(rig.fake.host_messages("hello")) <= 3
    writes_after_release = rig.fake.host_writes
    await asyncio.sleep(1.5)
    assert rig.recorder.count("open") == 1, "the port is left alone for 30 s"
    assert rig.fake.host_writes == writes_after_release
    status = rig.daemon.status()
    assert status["last_release"] == "no_reply" and status["device"]["serial"] is None
    assert status["backoff_s"] >= 28


async def test_foreign_hello_is_released_at_once(rigs):
    rig = rigs(device={"foreign": "hello"})
    await rig.start()
    await until(lambda: rig.recorder.count("close") == 1, timeout=3)
    assert rig.daemon.status()["last_release"] == "foreign"
    assert rig.daemon.status()["device"]["serial"] is None


async def test_no_poll_without_device(rigs):
    rig = rigs()
    attached = list(rig.ports)
    rig.ports.clear()
    await rig.start()
    await asyncio.sleep(1.5)
    assert rig.claude.polls() == 0, "agents --json must not run while no device is Ready"
    rig.ports.extend(attached)
    await rig.ready()
    await until(lambda: rig.claude.polls() >= 2)
    await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "pause", "seconds": 60})
    await until(lambda: rig.daemon.link is None)
    polls = rig.claude.polls()
    await asyncio.sleep(1.0)
    assert rig.claude.polls() == polls


async def test_pause_socket_perms(rigs):
    rig = rigs()
    await rig.start()
    await rig.ready()
    assert stat.S_IMODE(os.stat(rig.paths.sock).st_mode) == 0o600
    assert stat.S_IMODE(os.stat(rig.paths.home).st_mode) == 0o700
    reply = await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "pause", "seconds": 2})
    assert reply["ok"] is True and reply["paused_s"] == 2
    await until(lambda: rig.daemon.link is None)
    status = await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "status"})
    assert status["paused_s"] > 0 and status["link"] == "paused"
    await rig.ready(timeout=5)
    for path in (rig.paths.config, rig.paths.projects, rig.paths.events, rig.paths.lock):
        if path.exists():
            assert stat.S_IMODE(os.stat(path).st_mode) == 0o600, path


async def test_device_reboot_triggers_resync(rigs):
    rig = rigs(timing=replace(FAST, ping_s=0.3))
    await rig.start()
    await rig.synced()
    rig.fake.reboot()
    await until(lambda: rig.daemon.status()["reboots"] == 1, timeout=5)
    await until(lambda: rig.fake.revs["projects"] >= 1)
    assert len(rig.fake.host_messages("time.set")) == 2
