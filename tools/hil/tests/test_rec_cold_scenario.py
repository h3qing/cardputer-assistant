"""The rec_cold_start I/O shell on a faked device (issue #31): the three takes in order (debug.codec warm false,
nothing, warm true, each sent while the tone already plays and before G0), the pulled memos measured by
harness.cold_start, the tone file, and debug.codec's error on a build without it."""

import asyncio
import importlib
import math
import wave
from array import array
from types import SimpleNamespace

import pytest

from harness import mic_ref
from harness.device import DeviceError
from harness.mic_cal import RATE
from harness.rec_checks import wav_header
from harness.report import check

sc = importlib.import_module("scenarios.rec_cold")


def segment(muted: int, n: int = RATE * 3) -> bytes:
    """A landed segment: `muted` zeros, then a -36 dBFS 1 kHz cosine."""
    amp = 10 ** (-36 / 20) * 32767
    x = array("h", [0] * muted + [round(amp * math.cos(2 * math.pi * 1000 * i / RATE)) for i in range(n - muted)])
    body = x.tobytes()
    return wav_header(len(body)) + body


@pytest.fixture
def run(monkeypatch, tmp_path):
    """A ctx on fakes; the log of what the scenario did, in order; per take the muted samples and up_ms."""
    log, takes = [], iter([(0, 2134), (0, 4100), (0, 6250)])  # (muted samples, up_ms); G0 down at 1000/3000/5000
    keys = iter([1000, 3000, 5000])

    def step(name):
        async def act(*_args, **_kw):
            log.append(name)

        return act

    async def hold(dev, ms):
        log.append(f"g0 {ms}")
        return next(keys)

    async def codec(dev, warm):
        log.append(f"codec {warm}")

    async def wait_state(pred, timeout, **_kw):
        log.append("stopped")

    async def archives(rig, before, modes, timeout):
        muted, up = next(takes)
        rec = f"s{len(log)}"
        folder = tmp_path / rec
        folder.mkdir()
        (folder / "seg-000.wav").write_bytes(segment(muted))
        state[rec] = up
        return [{"rec": rec, "dir": rec, "mode": "memo"}]

    state = {}
    monkeypatch.setattr(mic_ref, "play", step("play"))
    monkeypatch.setattr(sc, "PRE_ROLL_S", 0.0)
    monkeypatch.setattr(sc, "settle", step("settle"))
    monkeypatch.setattr(sc, "known", lambda rig: set())
    monkeypatch.setattr(sc, "hold_memo", hold)
    monkeypatch.setattr(sc, "set_codec", codec)
    monkeypatch.setattr(sc, "new_archives", archives)
    monkeypatch.setattr(sc, "landed_checks", lambda rig, s: [check("manifest_contract_v1", True)])
    manifest = {"segments": [{"file": "seg-000.wav", "id": "id0"}]}
    monkeypatch.setattr(sc, "landed", lambda rig, s: SimpleNamespace(folder=tmp_path / s["dir"], manifest=manifest))
    monkeypatch.setattr(sc, "up0", lambda rig, sid: state[sid])

    async def cleaned(ctx, ids):
        log.append(f"cleaned {len(ids)}")
        return [check("card_empty", True)]

    monkeypatch.setattr(sc, "card_cleaned", cleaned)
    dev = SimpleNamespace(wait_state=wait_state)
    return SimpleNamespace(rig=SimpleNamespace(tmp=tmp_path), dev=dev), log


def test_three_takes_in_order_each_with_the_tone_first(run, tmp_path):
    ctx, log = run
    res = asyncio.run(sc._run(ctx, tmp_path / "tone.wav"))
    take = ["settle", "play", "g0 4500", "stopped"]
    assert log == [
        *take[:2], "codec False", *take[2:],
        *take,
        *take[:2], "codec True", *take[2:],
        "settle", "cleaned 3",
    ]  # fmt: skip
    assert [t["label"] for t in res["takes"]] == ["cold", "warm", "power_on"]
    assert [t["start_to_started_ms"] for t in res["takes"]] == [762, 728, 878]
    names = {c["name"]: c["ok"] for c in res["checks"]}
    assert names["cold_was_cold"] and names["warm_no_leading_mute"] and names["captures_intact"]
    assert not names["power_on_warm_up_worked"]  # 878 ms: the warm-up would not have worked
    assert not res["ok"]


def test_the_tone_file_is_nine_seconds_of_16_khz_mono(tmp_path):
    path = sc.tone_file(tmp_path / "rec_cold")
    with wave.open(str(path), "rb") as w:
        assert (w.getnchannels(), w.getframerate(), w.getnframes()) == (1, RATE, RATE * sc.TONE_MS // 1000)


def test_a_build_without_debug_codec_says_which_one_to_flash():
    class Dev:
        async def request(self, msg):
            return None, SimpleNamespace(msg={"t": "error", "code": "E_UNKNOWN_TYPE"})

    with pytest.raises(DeviceError, match="flash cardputer-adv-test with debug.codec"):
        asyncio.run(sc.set_codec(Dev(), False))
