"""E14: a bug in a link handler or in one loop iteration never stops the daemon's background tasks.

Without a guard the exception ends the asyncio task for good while the process stays alive, so launchd
never restarts it: the device is never served again and `cpa status` shows a stale link.
"""

from __future__ import annotations

from dataclasses import replace

from tests.support.rig import FAST, note_item, until

ID1 = "3QZK1M7-0000C-0003"


def _fail_first(real, error: Exception):
    """A stand-in that raises `error` on its first call and delegates afterwards; `.calls` counts calls."""

    def wrapper(*args):
        wrapper.calls += 1
        if wrapper.calls == 1:
            raise error
        return real(*args)

    wrapper.calls = 0
    return wrapper


async def test_link_handler_crash_releases_the_port_and_reconnects(rigs, monkeypatch):
    rig = rigs(timing=replace(FAST, backoff_s=1.0))
    await rig.start()
    await rig.synced()
    daemon = rig.daemon
    monkeypatch.setattr(daemon, "on_request", _fail_first(daemon.on_request, RuntimeError("handler bug")))
    rig.fake.send(note_item(ID1))
    await until(lambda: daemon.status()["last_release"] == "internal_error", timeout=3)
    status = daemon.status()
    assert daemon.link is None and status["link"] == "offline"
    assert status["backoff_s"] > 0, "the port is held off so a repeating crash cannot spin"
    assert rig.recorder.count("close") == 1
    await rig.ready(timeout=5)
    ack = await rig.request(note_item(ID1), timeout=3)  # the device's outbox retries the unacked item
    assert ack["status"] == "stored" and daemon.status()["link"] == "ready"


async def test_supervisor_survives_a_port_listing_failure(rigs, monkeypatch):
    rig = rigs(timing=replace(FAST, backoff_s=0.3))
    await rig.start()
    lister = _fail_first(rig.port_lister, OSError("IOKit busy"))
    monkeypatch.setattr(rig.daemon, "_lister", lister)
    await rig.ready(timeout=5)
    assert lister.calls >= 2


async def test_poller_survives_a_failed_poll(rigs, monkeypatch):
    rig = rigs()
    await rig.start()
    await rig.ready()
    daemon = rig.daemon
    resolve = _fail_first(daemon._resolve, ValueError("embedded null byte"))
    monkeypatch.setattr(daemon, "_resolve", resolve)
    await until(lambda: resolve.calls >= 3, timeout=3)
    polls = rig.claude.polls()
    await until(lambda: rig.claude.polls() > polls, timeout=3)


async def test_housekeeping_survives_a_failed_reload(rigs, monkeypatch):
    rig = rigs(with_device=False, housekeeping_s=0.1)
    await rig.start()
    daemon = rig.daemon
    reload_config = _fail_first(lambda: None, PermissionError(13, "denied"))
    monkeypatch.setattr(daemon, "_reload_config", reload_config)
    await until(lambda: reload_config.calls >= 3, timeout=3)
    assert not rig.task.done()
