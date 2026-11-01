"""The mic_cal reference (mic gain brief §6.2): tone synthesis, normalisation, layout, voice choice and the
read-only check of the Mac's audio output. Nothing here runs say, osascript or afplay."""

import json
import math
import wave

import pytest

from harness import mic_cal as mc
from harness import mic_ref as mr


def test_tone_has_its_level_and_raised_cosine_ramps():
    x = mr.tone(1000.0, -20.0, 1000)
    assert len(x) == 16000
    assert max(abs(v) for v in x) == round(10 ** (-20 / 20) * 32767)
    assert mc.tone_db(x[1600:14400]) == pytest.approx(-20.0, abs=0.05)
    assert x[0] == 0 and abs(x[-1]) <= 1
    assert max(abs(v) for v in x[:40]) < max(abs(v) for v in x[200:400]) / 2


def test_normalise_sets_the_peak_and_keeps_the_shape():
    out = mr.normalise([0, 100, -200, 40], -14.0)
    peak = round(10 ** (-14 / 20) * 32767)
    assert out == [0, round(peak / 2), -peak, round(peak / 5)]
    assert mr.normalise([0, 0], -14.0) == [0, 0]


def test_trim_silence_drops_the_quiet_head_and_tail():
    assert mr.trim_silence([0, 0, 1, 500, -300, 2, 0]) == [500, -300]
    assert mr.trim_silence([0, 0]) == []


def test_scaled_applies_a_gain_in_db_and_saturates():
    assert mr.scaled([100, -50], 20 * math.log10(2)) == [200, -100]
    assert mr.scaled([30000], 6.0) == [32767]


def test_assemble_lays_out_tones_and_speech_from_the_start_of_tone_a():
    a, b = mr.tone(1000.0, -20.0, 1000), mr.tone(1000.0, -32.0, 1000)
    en, zh = [1000] * 16 * 2000, [-1000] * 16 * 1500
    samples, blocks = mr.assemble(a, b, (("en", en), ("zh", zh)))
    assert blocks == (
        mc.Block("tone_a", 0, 1000, "tone"),
        mc.Block("tone_b", 1500, 1000, "tone"),
        mc.Block("en", 3000, 2000, "speech"),
        mc.Block("zh", 5500, 1500, "speech"),
    )
    start = mr.LEAD_IN_MS * 16
    assert samples[:start] == [0] * start and samples[start : start + len(a)] == a
    assert samples[start + 5500 * 16] == -1000
    assert len(samples) == (mr.LEAD_IN_MS + 7000 + mr.TAIL_MS) * 16


def test_assemble_without_a_chinese_voice_has_three_blocks():
    a = b = mr.tone(1000.0, -20.0, 1000)
    _, blocks = mr.assemble(a, b, (("en", [1] * 16000),))
    assert [blk.name for blk in blocks] == ["tone_a", "tone_b", "en"]


def test_max_gain_keeps_the_loudest_sample_1_db_under_full_scale():
    peak = round(10 ** (-14 / 20) * 32767)
    assert mr.max_gain_db([0, peak, -100]) == pytest.approx(13.0, abs=0.01)


SAY_LIST = """Albert              en_US    # Hello! My name is Albert.
Aman (English (India)) en_IN    # Hello! My name is Aman.
Eddy (Chinese (China mainland)) zh_CN    # 你好！我叫Eddy。
Samantha            en_US    # Hello! My name is Samantha.
Tingting            zh_CN    # 你好！我叫婷婷。
"""


def test_parse_voices_reads_names_with_spaces_and_locales():
    voices = mr.parse_voices(SAY_LIST)
    assert ("Aman (English (India))", "en_IN") in voices and ("Tingting", "zh_CN") in voices
    assert len(voices) == 5


def test_pick_voice_prefers_the_named_voice_then_any_of_the_language():
    voices = mr.parse_voices(SAY_LIST)
    assert mr.pick_voice(voices, mr.EN_VOICES, "en") == "Samantha"
    assert mr.pick_voice(voices, mr.ZH_VOICES, "zh") == "Tingting"
    rest = tuple(v for v in voices if v[0] not in ("Samantha", "Tingting"))
    assert mr.pick_voice(rest, mr.ZH_VOICES, "zh") == "Eddy (Chinese (China mainland))"
    assert mr.pick_voice((), mr.EN_VOICES, "en") is None


def _profiler(name="MacBook Pro Speakers", transport="coreaudio_device_type_builtin"):
    items = [
        {"_name": "MacBook Pro Microphone", "coreaudio_default_audio_input_device": "spaudio_yes"},
        {
            "_name": name,
            "coreaudio_default_audio_output_device": "spaudio_yes",
            "coreaudio_device_transport": transport,
        },
    ]
    return json.dumps({"SPAudioDataType": [{"_name": "coreaudio_device", "_items": items}]})


def test_output_check_passes_unmuted_built_in_speakers_and_reports_the_volume():
    problem, info = mr.output_check("false\n", "69\n", _profiler())
    assert problem is None and info == {"muted": False, "volume": 69, "device": "MacBook Pro Speakers"}


@pytest.mark.parametrize(
    ("muted", "volume", "profiler", "part"),
    [
        ("true", "69", _profiler(), "muted"),
        ("missing value", "missing value", _profiler(), "muted"),
        ("false", "0", _profiler(), "volume"),
        ("false", "50", _profiler("External Headphones"), "built-in speakers"),
        ("false", "50", _profiler("AirPods", "coreaudio_device_type_bluetooth"), "built-in speakers"),
        ("false", "50", json.dumps({"SPAudioDataType": []}), "no default output"),
        ("false", "50", "not json", "system_profiler"),
    ],
)
def test_output_check_refuses_anything_but_audible_built_in_speakers(muted, volume, profiler, part):
    problem, _ = mr.output_check(muted, volume, profiler)
    assert part in problem


def test_read_wav_downmixes_and_checks_the_rate(tmp_path):
    path = tmp_path / "s.wav"
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(b"".join(v.to_bytes(2, "little", signed=True) for v in (100, 300, -100, -300)))
    assert mr.read_wav(path) == [200, -200]
    mr.write_wav(path, [1, -2, 3])
    assert mr.read_wav(path) == [1, -2, 3]
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(22050)
        w.writeframes(b"\x00\x00")
    with pytest.raises(ValueError, match="16000"):
        mr.read_wav(path)
