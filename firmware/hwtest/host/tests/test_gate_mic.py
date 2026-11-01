import math
import struct

import pytest

from gate_mic import MicRun, levels, parse_wav, suggest_silence_rms


def wav(samples, rate=16000):
    pcm = struct.pack(f"<{len(samples)}h", *samples)
    head = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    head += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    return head + b"data" + struct.pack("<I", len(pcm)) + pcm


def tone(amp, secs, rate=16000):
    return [round(amp * math.sin(2 * math.pi * 440 * i / rate)) for i in range(int(secs * rate))]


def test_mic_run_validates_and_formats():
    assert MicRun(16, 0, 60).command() == "!mic 16 0 60"
    for bad in ((0, 0, 60), (16, 11, 60), (16, 0, 0), (16, 0, 301)):
        with pytest.raises(ValueError):
            MicRun(*bad)


def test_parse_wav_checks_the_header():
    rate, pcm = parse_wav(wav([1, -2, 3]))
    assert rate == 16000 and list(pcm) == [1, -2, 3]
    with pytest.raises(ValueError):
        parse_wav(b"RIFX" + wav([1])[4:])


def test_levels_of_a_tone_against_whisperwoof_gates():
    # amplitude 200 sine: RMS ~141 int16 = 0.0043 float, above both gates (0.001 / 0.003)
    lv = levels(tone(200, 2.0), 16000)
    assert lv["peak"] == 200
    assert lv["rms_int16"] == pytest.approx(141.4, abs=1)
    assert lv["rms_float"] == pytest.approx(141.4 / 32768, rel=0.01)
    assert lv["whole_file_pass"] is True
    assert lv["pieces"] == {"n": 1, "below_0_003": 0}
    assert lv["clipped"] == 0


def test_levels_quiet_file_fails_whole_file_gate():
    lv = levels([10, -10] * 16000, 16000)
    assert lv["whole_file_pass"] is False
    assert lv["pieces"]["below_0_003"] == 1


def test_levels_counts_clipping_and_20s_pieces():
    samples = tone(100, 45.0)
    samples[5] = 32767
    samples[6] = -32768
    lv = levels(samples, 16000)
    assert lv["clipped"] == 2
    assert lv["pieces"]["n"] == 3  # 20 s + 20 s + 5 s


def test_window_percentiles_split_noise_and_speech():
    samples = [5, -5] * 8000 + tone(1000, 1.0)  # 1 s noise, then 1 s "speech"
    lv = levels(samples, 16000)
    assert lv["win100_rms"]["p10"] == pytest.approx(5, abs=0.5)
    assert lv["win100_rms"]["p90"] > 600


def test_suggest_silence_rms_is_geometric_mean_in_float():
    assert suggest_silence_rms(10, 1000) == pytest.approx(100 / 32768)
    assert suggest_silence_rms(0, 1000) is None
