"""The verify pass after kMicGain is committed (mic gain brief §6.7): kMicGain read from mic_gain.h, and a capture
at the firmware's own gain checked against B1 of the same sitting."""

import dataclasses

import pytest

from harness import mic_cal as mc
from harness import mic_decide as md
from scenarios.mic_cal import MIC_GAIN_H

NOISE = mc.Noise((6.0, 7.0, 8.0), (2.0, 2.0, 2.0))
WANT = mc.Setting("kMicGain", 10, 0xBF, 2)  # +11.94 dB over B1


def cap(tone_a, speech=None, clipped=0, step=1):
    return mc.Capture(
        onset_ms=4400, tone_a_db=tone_a, tone_b_db=None if tone_a is None else tone_a - 12,
        tone_a_peak=0 if tone_a is None else int(10 ** (tone_a / 20) * 32768),
        speech_db=speech if speech is not None or tone_a is None else tone_a - 4, noise=NOISE, clipped=clipped,
        peak=0, transient_peak=0, step=step, lead_ms=120,
    )  # fmt: skip


B1 = cap(-36.0)


# --- kMicGain from the header ------------------------------------------------------------------


def test_shipped_gain_reads_kmicgain_from_the_header():
    text = "// was kMicGain{0, 0xBF, 16}\nconstexpr MicGain kMicGain{10, 0xC5, 2};\nstatic_assert(ok(kMicGain));"
    assert mc.shipped_gain(text) == mc.Setting("kMicGain", 10, 0xC5, 2)


def test_shipped_gain_knows_the_unity_volume_name():
    assert mc.shipped_gain("constexpr MicGain kMicGain{0, kMicVolUnity, 16};") == mc.Setting("kMicGain", 0, 0xBF, 16)


@pytest.mark.parametrize(
    "text",
    [
        "",
        "constexpr MicGain kMicGain{10, 0xC5};",
        "constexpr MicGain kMicGain{10, kOther, 2};",
        "constexpr MicGain kMicGain{11, 0xBF, 2};",
        "constexpr MicGain kMicGain{0, 0xBF, 0};",
        "constexpr MicGain kMicGain{0, 0x100, 2};",
    ],
)
def test_shipped_gain_refuses_what_it_cannot_read_or_the_firmware_would_refuse(text):
    with pytest.raises(ValueError):
        mc.shipped_gain(text)


def test_the_checked_in_header_parses():
    s = mc.shipped_gain(MIC_GAIN_H.read_text(encoding="utf-8"))
    assert 0 <= s.pga <= 10 and 0 <= s.vol <= 0xFF and 1 <= s.mag <= 64


# --- the verdict -------------------------------------------------------------------------------


def test_verify_passes_the_calibrated_gain():
    v = md.verify(B1, cap(-36.0 + 11.94), WANT)
    assert v["ok"] and v["problems"] == []
    assert v["expected"] == {"pga": 10, "vol": 0xBF, "mag": 2}
    assert v["nominal_db"] == pytest.approx(11.94, abs=0.01) and v["gain_db"] == pytest.approx(11.94, abs=0.01)
    assert v["peak_dbfs"] == pytest.approx(-11.76, abs=0.01) and v["via"] == "A"


def test_verify_catches_a_build_still_at_the_placeholder():
    v = md.verify(B1, cap(-36.0), WANT)  # recorded at B1's gain: kMicGain was not what got flashed
    assert not v["ok"]
    assert [p.split(":")[0] for p in v["problems"]] == ["applied", "peak"]


def test_verify_wants_no_clipping_and_full_resolution_at_unity():
    v = md.verify(B1, cap(-24.06, clipped=3, step=8), WANT)
    assert [p.split(":")[0] for p in v["problems"]] == ["clipped", "resolution"]


def test_verify_wants_tone_and_speech_to_agree():
    v = md.verify(B1, cap(-24.06, speech=-26.0), WANT)  # speech dG +14.0 vs tone dG +11.94
    assert [p.split(":")[0] for p in v["problems"]] == ["linear"]


def test_verify_measures_from_tone_b_when_tone_a_is_near_clipping():
    hot = dataclasses.replace(cap(-24.06), tone_a_peak=30000, tone_a_db=-1.0)
    v = md.verify(B1, hot, WANT)
    assert v["ok"] and v["via"] == "B"


def test_verify_without_a_level_has_no_verdict():
    v = md.verify(B1, cap(None), WANT)
    assert not v["ok"] and v["expected"] == {"pga": 10, "vol": 0xBF, "mag": 2}
    assert v["problems"] == ["no tone or speech level in B1 or the kMicGain capture"]
