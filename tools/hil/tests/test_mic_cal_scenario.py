"""The mic_cal / mic_verify I/O shell on a faked device (mic gain brief §3, §6.6, §6.7): any failure reboots
(drops debug.mic) and lets the HIL daemon empty the card into the rig's temp dir before the error leaves, so
the real cpad never pulls a calibration recording; the wait is bounded and its outcome is in the error.
mic_verify records its second capture at the firmware's own kMicGain, with no debug.mic after a reboot."""

import asyncio
import importlib
import itertools
from array import array
from types import SimpleNamespace

import pytest

from harness import mic_cal as mc
from harness import mic_ref
from harness.rec_checks import wav_header
from harness.report import check, error_result

sc = importlib.import_module("scenarios.mic_cal")  # the package's own `mic_cal` is the scenario function
NOISE = mc.Noise((6.0, 7.0, 8.0), (2.0, 2.0, 2.0))


def cap(tone_a):
    peak = 0 if tone_a is None else int(10 ** (tone_a / 20) * 32768)
    tone_b, speech = (None, None) if tone_a is None else (tone_a - 12, tone_a - 4)
    return mc.Capture(onset_ms=4400, tone_a_db=tone_a, tone_b_db=tone_b, tone_a_peak=peak, speech_db=speech,
                      noise=NOISE, clipped=0, peak=peak, transient_peak=0, step=1, lead_ms=120)  # fmt: skip


class FakeRig:
    def __init__(self, tmp):
        self.tmp = tmp
        self.link = SimpleNamespace(hello=SimpleNamespace(boot=1))

    async def ready(self, timeout=20.0):
        return self.link


class FakeDev:
    def __init__(self, rig, log):
        self.rig, self.log = rig, log

    async def reboot(self):
        self.log.append("reboot")
        self.rig.link = SimpleNamespace(hello=SimpleNamespace(boot=self.rig.link.hello.boot + 1))


@pytest.fixture
def run(monkeypatch, tmp_path):
    """A ctx on fakes and the log of what the scenario did to the device, in order."""
    log = []
    rig = FakeRig(tmp_path)

    def step(name):
        async def act(*_args):
            log.append(name)

        return act

    ref = SimpleNamespace(path=tmp_path / "ref.wav", ms=7000, blocks=(), gain_db=0.0, max_gain_db=12.0,
                          doc=lambda: {"sha256": "x"})  # fmt: skip
    monkeypatch.setattr(mic_ref, "output_state", lambda: (None, {"default_output": "MacBook Pro Speakers"}))
    monkeypatch.setattr(mic_ref, "build", lambda folder: ref)
    monkeypatch.setattr(mic_ref, "play", step("play"))
    monkeypatch.setattr(sc, "PRE_ROLL_S", 0.0)
    monkeypatch.setattr(sc, "TAIL_S", 0.0)
    monkeypatch.setattr(sc, "set_seg_ms", step("seg_ms"))
    monkeypatch.setattr(sc, "settle", step("settle"))
    monkeypatch.setattr(sc, "start_meeting", step("start"))
    monkeypatch.setattr(sc, "end_meeting", step("end"))
    monkeypatch.setattr(sc, "known", lambda rig: set())
    monkeypatch.setattr(sc, "set_mic", lambda dev, s: step(f"mic {s.label}")())
    return SimpleNamespace(rig=rig, dev=FakeDev(rig, log), opts=SimpleNamespace(report=None)), log


@pytest.fixture
def lands(run, monkeypatch, tmp_path):
    """Every meeting is archived as one segment of a few samples."""
    _, log = run
    x = array("h", [0, 9, -7] * 100)
    (tmp_path / "seg-000.wav").write_bytes(wav_header(len(x) * 2) + x.tobytes())
    n = itertools.count()

    async def archived(rig, before, modes, timeout):
        log.append("archived")
        return [{"rec": f"r{next(n)}", "dir": "s"}]

    def landed(rig, session):
        return SimpleNamespace(folder=tmp_path, manifest={"segments": [{"file": "seg-000.wav", "id": session["rec"]}]})

    monkeypatch.setattr(sc, "new_archives", archived)
    monkeypatch.setattr(sc, "landed", landed)
    monkeypatch.setattr(sc, "landed_checks", lambda rig, session: [])


async def broken(path, timeout):
    raise RuntimeError("afplay exited 1: no output device")


async def forever(*_args):
    await asyncio.Event().wait()


# --- failures: the card drained, debug.mic dropped, before the error leaves ----------------------


def test_a_play_failure_reboots_and_drains_the_card_before_the_error_leaves(run, monkeypatch):
    ctx, log = run
    monkeypatch.setattr(mic_ref, "play", broken)
    with pytest.raises(RuntimeError, match="afplay exited 1") as err:
        asyncio.run(sc.mic_cal(ctx))
    assert log == ["seg_ms", "settle", "mic B1", "start", "end", "reboot", "settle"]
    assert "card emptied" in error_result(err.value)["error"]


def test_the_runners_timeout_inside_a_capture_still_drains_the_card(run, monkeypatch):
    ctx, log = run
    monkeypatch.setattr(mic_ref, "play", forever)

    async def scenario_timeout():
        await asyncio.wait_for(sc.mic_cal(ctx), 0.1)  # harness.runner._scenario's wait_for

    with pytest.raises(TimeoutError) as err:
        asyncio.run(scenario_timeout())
    assert log[-3:] == ["end", "reboot", "settle"]
    assert "card emptied" in error_result(err.value)["error"]


def test_a_failure_after_the_archive_still_drains_its_delete(run, lands, monkeypatch):
    ctx, log = run
    monkeypatch.setattr(sc, "measure", lambda x, blocks: cap(None))  # B1 heard no tone
    with pytest.raises(sc.CalibrationError, match="no_tone"):
        asyncio.run(sc.mic_cal(ctx))
    assert log[-3:] == ["archived", "reboot", "settle"]


def test_a_drain_that_never_ends_is_bounded_and_the_error_says_so(run, monkeypatch):
    ctx, log = run
    monkeypatch.setattr(mic_ref, "play", broken)
    monkeypatch.setattr(sc, "DRAIN_S", 0.1)

    async def settle(dev):
        log.append("settle")
        if "reboot" in log:
            await forever()  # the card never empties

    monkeypatch.setattr(sc, "settle", settle)
    with pytest.raises(RuntimeError, match="afplay exited 1") as err:
        asyncio.run(sc.mic_cal(ctx))
    error = error_result(err.value)["error"]
    assert "cleanup FAILED" in error and "m3-device-gates.md" in error


# --- mic_verify: the firmware's own kMicGain -----------------------------------------------------


def test_mic_verify_records_at_the_firmwares_own_gain_after_a_reboot(run, lands, monkeypatch, tmp_path):
    ctx, log = run
    header = tmp_path / "mic_gain.h"
    header.write_text("constexpr MicGain kMicGain{10, kMicVolUnity, 2};\n", encoding="utf-8")
    monkeypatch.setattr(sc, "MIC_GAIN_H", header)
    caps = iter([cap(-36.0), cap(-36.0 + 30 - 18.06)])  # B1, then {10, 0xBF, 2}: +11.94 dB over B1
    monkeypatch.setattr(sc, "measure", lambda x, blocks: next(caps))

    async def cleaned(ctx, ids):
        log.append("card_cleaned")
        return [check("card_empty", True, ids=list(ids))]

    monkeypatch.setattr(sc, "card_cleaned", cleaned)
    res = asyncio.run(sc.mic_verify(ctx))
    meeting = ["settle", "start", "play", "end", "archived"]
    assert log == ["seg_ms", "settle", "mic B1", *meeting[1:], "settle", "reboot", *meeting, "settle", "reboot",
                   "card_cleaned"]  # fmt: skip
    assert res["ok"], res["checks"]
    assert res["verify"]["expected"] == {"pga": 10, "vol": 0xBF, "mag": 2}
    assert res["verify"]["peak_dbfs"] == pytest.approx(-11.76, abs=0.01)
    assert res["checks"][-1]["ids"] == ["r0", "r1"]
