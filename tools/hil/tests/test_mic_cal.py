"""Pure analysis (harness.mic_cal) and decision (harness.mic_decide) behind the mic_cal scenario (mic gain brief §4,
§6, §7), on synthetic signals: WAV parse, Goertzel, onset, high-pass, noise percentiles, clipping at the M5 clamp,
lead_ms, block stats; then gain, validity, prediction, split choice, REG17 trim, lead and probe steps."""

import dataclasses
import math
import random
from array import array

import pytest

from harness import mic_cal as mc
from harness import mic_decide as md
from harness.rec_checks import wav_header

RATE = 16000
LAYOUT = (
    mc.Block("tone_a", 0, 1000, "tone"),
    mc.Block("tone_b", 1500, 1000, "tone"),
    mc.Block("en", 3000, 1200, "speech"),
    mc.Block("zh", 4700, 1200, "speech"),
)


def sine(dbfs, ms, freq=1000.0, phase=0.7):
    a = 10 ** (dbfs / 20) * 32768
    return [a * math.sin(2 * math.pi * freq * i / RATE + phase) for i in range(ms * 16)]


def ramped(dbfs, ms, ramp_ms=10):
    x, r = sine(dbfs, ms), ramp_ms * 16
    env = [0.5 - 0.5 * math.cos(math.pi * i / r) if i < r else 1.0 for i in range(len(x))]
    return [v * e * e2 for v, e, e2 in zip(x, env, reversed(env), strict=True)]


def gauss(rms, ms, seed=1):
    r = random.Random(seed)  # noqa: S311 - reproducible test noise
    return [r.gauss(0.0, rms) for _ in range(ms * 16)]


def put(x, at_ms, part):
    i = at_ms * 16
    return x[:i] + [a + b for a, b in zip(x[i : i + len(part)], part, strict=True)] + x[i + len(part) :]


def i16(x):
    return array("h", (max(-32752, min(32751, round(v))) for v in x))


def capture(t0_ms=4407, gain_db=0.0, noise_rms=6.0, burst_ms=120):
    """A meeting capture: a burst (the Mic.begin transient), room noise, then the reference layout."""
    x = gauss(noise_rms, t0_ms + 6400, seed=3)
    x = put(x, 0, [3000.0 * (1 if i % 2 else -1) for i in range(burst_ms * 16)])
    x = put(x, t0_ms, ramped(-36 + gain_db, 1000))
    x = put(x, t0_ms + 1500, ramped(-48 + gain_db, 1000))
    speech_rms = 10 ** ((-40 + gain_db) / 20) * 32768
    x = put(x, t0_ms + 3000, gauss(speech_rms, 1200, seed=4))
    x = put(x, t0_ms + 4700, gauss(speech_rms, 1200, seed=5))
    return i16(x)


# --- signal ------------------------------------------------------------------------------------


def test_pcm_reads_the_device_wav_and_refuses_any_other_header():
    data = array("h", [0, 1, -2, 32751, -32752])
    blob = wav_header(len(data) * 2) + data.tobytes()
    assert list(mc.pcm(blob)) == list(data)
    with pytest.raises(ValueError):
        mc.pcm(b"RIFX" + blob[4:])
    with pytest.raises(ValueError):
        mc.pcm(blob[:-1])


@pytest.mark.parametrize("ms", [800, 777])
def test_tone_db_is_the_1khz_level_re_a_full_scale_sine(ms):
    assert mc.tone_db(sine(-20.0, ms)) == pytest.approx(-20.0, abs=0.05)
    noisy = [a + b for a, b in zip(sine(-30.0, ms, phase=2.1), gauss(32768 * 10**-3, ms), strict=True)]
    assert mc.tone_db(noisy) == pytest.approx(-30.0, abs=0.05)


def test_tone_db_ignores_other_frequencies_and_silence():
    assert mc.tone_db(sine(-20.0, 800, freq=300.0)) < -80
    assert mc.tone_db([0] * 1600) == mc.FLOOR_DB


def test_onset_is_found_within_5_ms_and_none_without_a_tone():
    x = capture(t0_ms=4407)
    assert abs(mc.onset_ms(x) - 4407) <= 5
    assert mc.onset_ms(i16(gauss(6.0, 9000))) is None


def test_highpass_keeps_speech_band_and_removes_rumble():
    keep = mc.highpass(sine(-20.0, 500))[1600:]
    rumble = mc.highpass(sine(-20.0, 500, freq=50.0))[1600:]
    ref = mc.rms(sine(-20.0, 500)[1600:])
    assert 20 * math.log10(mc.rms(keep) / ref) == pytest.approx(0.0, abs=0.1)
    assert 20 * math.log10(mc.rms(rumble) / ref) < -15


def test_noise_percentiles_broadband_and_after_the_150_hz_highpass():
    hum = sine(20 * math.log10(200 / 32768), 4500, freq=30.0)
    x = i16([a + b for a, b in zip(gauss(20.0, 4500), hum, strict=True)])
    n = mc.noise_stats(x)
    assert n.bb[0] <= n.bb[1] <= n.bb[2] and n.hp[0] <= n.hp[1] <= n.hp[2]
    assert n.bb[1] > 130
    assert 18 < n.hp[1] < 23
    assert n.hp_db == pytest.approx(20 * math.log10(n.hp[1] / 32768), abs=0.01)


def test_clipped_counts_samples_at_the_m5_clamp():
    assert mc.clipped([32751, -32752, 32750, -32751, 0, 32751]) == 3


def test_rms_bins_are_10_ms_truncated_like_hwtest():
    x = [3] * 160 + [-4] * 160 + [5] * 100
    assert mc.rms_bins(x) == [3, 4]


@pytest.mark.parametrize(
    ("bins", "lead"),
    [
        ([400, 300, 120, 30, 9, 8, 7] + [6] * 93, 40),
        ([6] * 50 + [500] + [6] * 49, 0),
        ([200, 100] + [6] * 28 + [300, 250] + [6] * 68, 20),
        ([200, 6, 6, 90, 6, 6, 6, 6, 6] + [6] * 91, 40),
        ([100] * 30, None),
    ],
)
def test_lead_ms_is_the_gate_cue_algorithm(bins, lead):
    assert mc.lead_ms(bins) == lead


def test_recommend_lead_rounds_the_worst_run_up_to_50_ms():
    assert mc.recommend_lead([40, 120, 90]) == 150
    assert mc.recommend_lead([0, 0]) == 50
    assert mc.recommend_lead([None, 60]) == 100
    assert mc.recommend_lead([]) is None


def test_step_is_the_gcd_of_the_samples():
    assert mc.step([8, -16, 24, 0]) == 8
    assert mc.step([8, 3]) == 1
    assert mc.step([0, 0]) == 0


def test_measure_reads_every_block_and_keeps_the_transient_out():
    cap = mc.measure(capture(), LAYOUT)
    assert abs(cap.onset_ms - 4407) <= 5
    assert cap.tone_a_db == pytest.approx(-36.0, abs=0.1)
    assert cap.tone_b_db == pytest.approx(-48.0, abs=0.1)
    assert cap.speech_db == pytest.approx(-40.0, abs=0.3)
    assert cap.tone_a_peak == pytest.approx(10 ** (-36 / 20) * 32768, rel=0.08)  # plus the room noise
    assert cap.transient_peak >= 3000 > cap.peak and cap.clipped == 0
    assert cap.lead_ms == 120
    assert cap.step == 1
    assert 5 < cap.noise.bb[1] < 7


def test_measure_without_a_tone_has_no_levels():
    cap = mc.measure(i16(gauss(6.0, 11000)), LAYOUT)
    assert cap.onset_ms is None and cap.tone_a_db is None and cap.speech_db is None


def test_lead_of_uses_the_first_two_seconds():
    x = i16([3000.0] * 80 * 16 + gauss(6.0, 1920) + [3000.0] * 16000)
    assert mc.lead_of(x) == 80


# --- settings ---------------------------------------------------------------------------------


def test_settings_nominal_gain_and_debug_mic_wire():
    by = {s.label: s for s in mc.SWEEP}
    assert by["B1"].nominal_db == pytest.approx(18.06, abs=0.01)
    assert by["U0"].nominal_db == 0.0
    assert {round(by[n].nominal_db, 6) for n in mc.SPLITS} == {30.0}
    assert (by["N27"].nominal_db, by["N33"].nominal_db, by["N36"].nominal_db) == (27.0, 33.0, 36.0)
    assert by["S10"].wire() == {"t": "debug.mic", "pga": 10, "vol": 0xBF, "mag": 2}


def test_sweep_is_the_brief_section_5_list():
    rows = [(s.label, s.pga, s.vol, s.mag) for s in mc.SWEEP]
    assert rows == [
        ("B1", 0, 0xBF, 16), ("U0", 0, 0xBF, 2), ("S6", 6, 0xD7, 2), ("S7", 7, 0xD1, 2), ("S8", 8, 0xCB, 2),
        ("S9", 9, 0xC5, 2), ("S10", 10, 0xBF, 2), ("N27", 9, 0xBF, 2), ("N33", 10, 0xC5, 2), ("N36", 10, 0xCB, 2),
        ("B2", 0, 0xBF, 16),
    ]  # fmt: skip


# --- decision ---------------------------------------------------------------------------------

NOISE = mc.Noise(bb=(20.0, 21.0, 22.0), hp=(19.0, 20.6, 21.0))


def cap(tone_a, tone_b=None, speech=None, hp_db=-64.0, clipped=0):
    hp = 10 ** (hp_db / 20) * 32768
    return mc.Capture(
        onset_ms=4400, tone_a_db=tone_a, tone_b_db=tone_b if tone_b is not None else tone_a - 12,
        tone_a_peak=int(10 ** (tone_a / 20) * 32768), speech_db=speech if speech is not None else tone_a - 4,
        noise=dataclasses.replace(NOISE, hp=(hp, hp, hp)), clipped=clipped, peak=0, transient_peak=0, step=1,
        lead_ms=120,
    )  # fmt: skip


def ideal(**over):
    """Every setting at exactly its nominal gain change, tone A at -36 dBFS at B1."""
    base = {s.label: cap(-36.0 + s.nominal_db - mc.SWEEP[0].nominal_db) for s in mc.SWEEP}
    return {**base, **over}


def test_gain_is_tone_a_against_the_mean_of_both_baselines():
    caps = ideal(B1=cap(-36.4), B2=cap(-35.6))
    g = md.gain(caps, "S10")
    assert g.tone == pytest.approx(11.94, abs=0.01) and g.via == "A"
    assert g.speech == pytest.approx(11.94, abs=0.01)


def test_gain_falls_back_to_tone_b_when_tone_a_is_near_clipping():
    hot = dataclasses.replace(cap(-24.0), tone_a_peak=30000, tone_a_db=-1.0)
    g = md.gain(ideal(N36=hot), "N36")
    assert g.via == "B" and g.tone == pytest.approx(12.0, abs=0.01)


def test_a_consistent_sweep_is_valid():
    assert md.validity(ideal()) == ()


@pytest.mark.parametrize(
    ("over", "prefix"),
    [
        ({"B2": cap(-34.8)}, "drift"),
        ({"U0": cap(-53.26)}, "unity"),
        ({"S8": cap(-22.06)}, "applied: S8"),
        ({"S9": cap(-24.06, speech=-26.56)}, "linear: S9"),
        ({"N33": cap(-21.06, clipped=3)}, "clipped: N33"),
        ({"S7": dataclasses.replace(cap(-24.06), onset_ms=None, tone_a_db=None)}, "S7: no tone"),
    ],
)
def test_each_validity_rule_reports_its_failure(over, prefix):
    problems = md.validity(ideal(**over))
    assert any(p.startswith(prefix) for p in problems), problems


def test_a_missing_capture_is_a_validity_failure():
    caps = {k: v for k, v in ideal().items() if k != "S7"}
    assert "S7: missing" in md.validity(caps)


def test_clipping_at_36_db_is_not_a_validity_failure():
    assert md.validity(ideal(N36=cap(-18.06, clipped=5))) == ()


def test_predict_maps_the_users_level_through_the_gain():
    p = md.predict(11.94, noise_db=-64.0)
    assert (p.peak, p.rms, p.snr) == pytest.approx((-11.76, -35.06, 28.94))


def test_choose_split_prefers_pga_10_within_1_db_of_the_best():
    snr = {"S6": 28.0, "S7": 28.5, "S8": 29.0, "S9": 28.8, "S10": 28.1}
    assert md.choose_split(snr) == "S10"
    assert md.choose_split({**snr, "S10": 27.9}) == "S8"


@pytest.mark.parametrize(
    ("pga", "vol", "peak", "want"),
    [
        (10, 0xBF, -11.76, (10, 0xBF, -11.76)),
        (7, 0xD1, -13.0, (7, 0xD3, -12.0)),
        (7, 0xD1, -11.75, (7, 0xD1, -11.75)),  # -0.5 step rounds half up, to 0
        (10, 0xBF, -30.0, (10, 0xD7, -18.0)),  # clamped at +12 dB
    ],
)
def test_trim_sets_reg17_in_half_db_steps_within_bf_d7(pga, vol, peak, want):
    got = md.trim(pga, vol, peak)
    assert got[:2] == want[:2] and got[2] == pytest.approx(want[2])


def test_trim_drops_a_pga_step_when_reg17_would_go_below_0_db():
    assert md.trim(10, 0xBF, -10.5) == (9, 0xC2, pytest.approx(-12.0))
    assert md.trim(10, 0xBF, -4.0) == (7, 0xC1, pytest.approx(-12.0))


def test_decide_picks_s10_and_suggests_the_whisperwoof_threshold():
    d = md.decide(ideal())
    assert d["ok"] and d["problems"] == []
    assert (d["split"], d["pga"], d["vol"], d["vol_hex"], d["mag"]) == ("S10", 10, 0xBF, "0xBF", 2)
    assert d["peak_dbfs"] == pytest.approx(-11.76, abs=0.01)
    assert d["silence_rms"] == pytest.approx(10 ** ((-64.0 - 35.06 - 14) / 40), rel=1e-3)
    assert d["headroom"] == {"N33": 0, "N36": 0, "ok": True}


def test_decide_falls_back_a_pga_step_for_a_louder_voice():
    d = md.decide(ideal(), user_peak=-22.0)
    assert (d["pga"], d["vol"]) == (9, 0xC1)
    assert d["peak_dbfs"] == pytest.approx(-12.06, abs=0.01)


def test_decide_keeps_the_constants_when_the_sweep_is_invalid():
    d = md.decide(ideal(U0=cap(-53.26)))
    assert not d["ok"] and d["pga"] is None and any(p.startswith("unity") for p in d["problems"])


def test_decide_refuses_a_final_peak_outside_the_window():
    d = md.decide(ideal(), user_peak=-40.0)
    assert not d["ok"] and d["pga"] is None
    assert any("window" in p for p in d["problems"])


def test_headroom_reports_clipping_at_n33_and_n36():
    d = md.decide(ideal(N36=cap(-18.06, clipped=5)))
    assert d["ok"] and d["headroom"] == {"N33": 0, "N36": 5, "ok": False}


def test_lead_verdict_keeps_150_only_when_the_worst_run_fits():
    assert md.lead_verdict([120, 130, 90, 140, 110]) == {"leads": [120, 130, 90, 140, 110], "recommended": 150,
                                                         "keep_150": True}  # fmt: skip
    assert md.lead_verdict([120, 170])["recommended"] == 200
    assert md.lead_verdict([None, None]) == {"leads": [None, None], "recommended": None, "keep_150": False}


@pytest.mark.parametrize(
    ("tone_a", "gain", "want"),
    [
        (-36.0, 0.0, ("ok", 0.0)),
        (-20.0, 0.0, ("rescale", -16.0)),
        (-55.0, 0.0, ("rescale", 13.0)),
        (-65.0, 0.0, ("too_quiet", None)),
        (-50.0, -16.0, ("rescale", -2.0)),
    ],
)
def test_probe_step_aims_tone_a_at_minus_36_within_the_reference_headroom(tone_a, gain, want):
    assert md.probe_step(tone_a, gain, max_gain_db=13.0) == want


def test_rows_and_markdown_carry_the_numbers_only():
    caps = ideal()
    rows = md.rows(caps)
    assert [r["label"] for r in rows] == [s.label for s in mc.SWEEP]
    s10 = next(r for r in rows if r["label"] == "S10")
    assert s10["vol"] == "0xBF" and s10["gain_db"] == pytest.approx(11.94, abs=0.01)
    text = md.markdown(rows, md.decide(caps), md.lead_verdict([120]))
    assert "| S10 | 10 | 0xBF | 2 |" in text and "PGA 10, REG17 0xBF, mag 2" in text


def test_rows_before_both_baselines_have_no_gain():
    rows = md.rows({"B1": cap(-36.0), "U0": cap(-54.06)})
    assert [r["label"] for r in rows] == ["B1", "U0"] and rows[1]["gain_db"] is None


@pytest.mark.parametrize(
    ("doc", "part"),
    [
        ({"running": False, "recordings": {"on_device": None, "pulling": 0}}, "not running"),
        ({"running": True, "link": "paused", "recordings": {"on_device": None, "pulling": 0}}, "cannot see"),
        ({"running": True, "link": "ready", "recordings": {"on_device": 2, "pulling": 0}}, "2 recording"),
        ({"running": True, "link": "ready", "recordings": {"on_device": 0, "pulling": 1}}, "pulling"),
    ],
)
def test_status_problem_refuses_unless_the_card_is_known_empty(doc, part):
    assert part in md.status_problem(doc)


def test_status_problem_passes_an_empty_card():
    assert md.status_problem({"running": True, "link": "ready", "recordings": {"on_device": 0, "pulling": 0}}) is None
