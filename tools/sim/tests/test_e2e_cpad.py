"""End to end on the virtual device (gh #32 MVP 2): the real cpad on `cpa-sim serve`, no Cardputer.

`cpa-sim serve` runs the firmware on a pty. The HIL rig (tools/hil) runs the real cpad Daemon in this
process on a temporary $CPA_HOME, notes_dir and fake-claude, pointed at the pty only (simtools.hil_patch).
Then, as on the device:
  1. cpad's handshake reaches Ready and binds the sim's serial number;
  2. a note typed on the sim's keyboard (G0, text, Enter, from serve's stdin) travels as an outbox.item:
     cpad stores it (item.received in events.jsonl), writes it to the notes file and acks it, and the
     device's outbox empties;
  3. a session list from fake-claude reaches the board (debug.state), and the board is saved as a PNG
     and checked: the project row is drawn in the board's colours.
Needs the built simulator (`pio run -d firmware/sim`, or $CPA_SIM_BIN). Set CPA_SIM_SHOTS=DIR to keep the PNG.
"""

import asyncio
import os
import secrets
import shutil
from pathlib import Path

import pytest

from harness import keys as k
from harness.device import Device, slot_row
from harness.fakeclaude import bg_session, tty_session
from harness.rig import Rig, create_rig
from scenarios.common import SLOT, items_received, note_appears, token
from simtools.guard import is_temp
from simtools.hil_patch import sim_patches
from simtools.png_read import read_png
from simtools.serial_pty import SIM_SERIAL
from simtools.serve import SimServe

pytestmark = pytest.mark.usefixtures("built_sim")  # conftest.py: skipped without a build, failed if stale

# The palette index of a cpa-sim PNG is the sprite's byte: an entry of the theme's palette
# (docs/19 §2.2; tools/theme/tokens.py token_index): Fg, Ok, Warn and Select.
FG, OK, WARN, SELECT = 0x02, 0x05, 0x06, 0x08


async def handshake(rig: Rig) -> None:
    link = await rig.ready(timeout=30)
    assert link.hello.dev == "100000000051" and link.hello.fw.startswith("cpa-fw/")
    status = rig.daemon.status()
    assert status["link"] == "ready"
    assert status["device"]["serial"] == SIM_SERIAL  # bound to the sim, never a real board


async def capture(rig: Rig, dev: Device, sim: SimServe) -> None:
    text = f"sim capture {token(4)}"
    await dev.home()
    sim.send("press g0")
    await dev.wait_state(lambda s: s["overlay"] == "compose", 3, what="Compose open")
    sim.send(f"type {text}", "press enter")
    note, _ = await note_appears(rig, text, 10)
    assert items_received(rig, note.item_id) == 1
    synced = await dev.wait_state(lambda s: s["outbox"]["pending"] == 0, 10, what="outbox empty")
    assert synced.msg["outbox"]["inflight"] == 0


async def board(rig: Rig, dev: Device, sim: SimServe, keep: Path | None) -> None:
    root = str(rig.project_root)
    rig.fake.set_sessions([
        bg_session(secrets.token_hex(4), "cpa/看板一", root, "busy", "running"),
        tty_session(secrets.token_hex(4), "看板二", root, "waiting"),
    ])  # fmt: skip
    want = {"slot": SLOT, "run": 1, "need": 1}
    seen = await dev.wait_state(lambda s: {key: (slot_row(s, SLOT) or {}).get(key) for key in want} == want, 15,
                                what="slot 1 run=1 need=1 on the device")  # fmt: skip
    assert seen.msg["need"] == 1
    await dev.home()
    await dev.press(k.Press(("opt", "p")))  # v0.4.0: home is the root page, the board is Opt+P
    await dev.wait_state(lambda s: s["mod"] == "board", 3, what="the board")
    shot = sim.shot("board")
    if keep:
        keep.mkdir(parents=True, exist_ok=True)
        shutil.copy(shot.path, keep / shot.path.name)
    assert shot.module == "board" and shot.backlight == "on"
    img = read_png(shot.path)
    assert (img.width, img.height) == (480, 270)
    assert img.count_index(SELECT) > 2000  # the selected project row
    assert img.count_index(FG) > 500  # text
    assert img.count_index(OK) > 0 and img.count_index(WARN) > 0  # "online" and the "!1" that needs you


async def scenario(sim: SimServe, keep: Path | None) -> None:
    rig = create_rig()
    try:
        assert is_temp(rig.paths.home) and is_temp(rig.notes_dir)
        await rig.start()
        await handshake(rig)
        dev = Device(rig)
        await capture(rig, dev, sim)
        await board(rig, dev, sim, keep)
        await dev.press(k.Press(("opt", "h")))  # still answering after all of it
    finally:
        await rig.close(keep=False)


def test_cpad_on_the_virtual_device(tmp_path, monkeypatch):
    sim = SimServe.start(tmp_path / "shots")
    try:
        monkeypatch.setenv("CPA_HOME", str(tmp_path / "unused"))  # create_rig sets it; restored after
        for module, name, value in sim_patches(sim.path):
            monkeypatch.setattr(module, name, value)
        keep = os.environ.get("CPA_SIM_SHOTS")
        asyncio.run(asyncio.wait_for(scenario(sim, Path(keep) if keep else None), 120))
    finally:
        assert sim.stop() == 0
