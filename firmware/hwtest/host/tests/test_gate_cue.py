import pytest

from gate_cue import CUE_SWEEP, SILENT_SWEEP, CueRun, gate_verdict, lead_ms, recommend_lead, summarize_cue


def test_cue_run_command():
    assert CueRun("memo", 80, 64, "lock").command() == "!cue 80 64 memo lock 1000"
    with pytest.raises(ValueError):
        CueRun("chirp", 80, 64, "lock")
    with pytest.raises(ValueError):
        CueRun("memo", 80, 300, "lock")
    assert CueRun("meet", 100, 64, "lockyield").command() == "!cue 100 64 meet lockyield 1000"
    assert CueRun("silent", 80, 0, "lockyield").command() == "!cue 80 0 silent lockyield 1000"


def test_lead_ms_is_end_of_last_loud_bin_in_first_500ms():
    bins = [400, 300, 120, 30, 9, 8, 7] + [6] * 93  # floor ~6 from 500-1000 ms
    assert lead_ms(bins) == 40  # bins 0..3 above max(3*6, 6+8)=18 -> ends at 40 ms


def test_lead_ms_zero_when_quiet_and_ignores_after_window():
    bins = [6] * 50 + [500] + [6] * 49
    assert lead_ms(bins) == 0


def test_lead_ms_ignores_a_later_room_sound():
    # codec transient over by 20 ms; a sound at 300 ms is the room, not the codec
    bins = [200, 100] + [6] * 28 + [300, 250] + [6] * 68
    assert lead_ms(bins) == 20


def test_lead_ms_needs_a_quiet_run_of_50ms():
    bins = [200, 6, 6, 90, 6, 6, 6, 6, 6] + [6] * 91  # a blip at 30 ms still belongs to the transient
    assert lead_ms(bins) == 40


def test_lead_ms_needs_a_floor_window():
    assert lead_ms([100] * 30) is None


def test_recommend_lead_rounds_max_up_to_50ms():
    assert recommend_lead([40, 120, 90]) == 150
    assert recommend_lead([0, 0]) == 50  # never below one 50 ms step
    assert recommend_lead([]) is None
    assert recommend_lead([None, 60]) == 100


def mk(hammer, pattern="memo", start_us=300_000, loop_max_us=2_000, bad=0, rb_bad=0, lead_bins=None):
    return {
        "cue": {
            "done": 1,
            "pattern": pattern,
            "hammer": hammer,
            "spk_begin_us": 20_000,
            "tone_us": 90_000,
            "spk_end_us": 5_000,
            "mic_begin_us": 30_000,
            "first_buf_us": 40_000,
            "start_us": start_us,
            "lock_max_us": 30_000,
            "loop_max_us": loop_max_us,
            "spk_ok": 1,
            "mic_ok": 1,
            "spk_rb_bad": rb_bad,
            "mic_rb_bad": 0,
            "rb_err": 0,
            "i2c_err": 0,
            "bad_reads": 0,
            "bad_keys": bad,
        },
        "bins": {"rms": lead_bins or ([200, 100, 6] + [6] * 97)},
    }


def test_summarize_groups_by_pattern_and_hammer():
    runs = [mk("lock") for _ in range(3)] + [mk("nolock", bad=1)] + [mk("lock", pattern="meet", start_us=500_000)]
    s = summarize_cue(runs)
    lock = s["memo/lock"]
    assert lock["runs"] == 3 and lock["bad_keys"] == 0 and lock["codec_fail"] == 0
    assert lock["start_ms"]["max"] == 300.0
    assert lock["lead_ms"]["max"] == 20
    assert s["memo/nolock"]["bad_keys"] == 1
    assert s["meet/lock"]["start_ms"]["p50"] == 500.0


def test_codec_fail_counts_readback_and_begin_failures():
    r = mk("lock", rb_bad=2)
    r["cue"]["mic_ok"] = 0
    assert summarize_cue([r])["memo/lock"]["codec_fail"] == 1


def test_silent_run_never_begins_the_speaker_so_its_readback_is_not_a_failure():
    r = mk("lockyield", pattern="silent")
    r["cue"].update(spk_ok=0, spk_rb_bad=8)
    assert summarize_cue([r])["silent/lockyield"]["codec_fail"] == 0
    r["cue"]["mic_rb_bad"] = 1
    assert summarize_cue([r])["silent/lockyield"]["codec_fail"] == 1


def test_silent_sweep_is_20_locked_starts_without_a_cue():
    assert len(SILENT_SWEEP) >= 20
    assert {(c.pattern, c.hammer) for c in SILENT_SWEEP} == {("silent", "lockyield")}


def test_gate_verdict_passes_with_lock():
    runs = [mk("lock") for _ in range(20)] + [mk("lock", pattern="meet", start_us=600_000) for _ in range(20)]
    v = gate_verdict(summarize_cue(runs))
    assert v["pass"] is True
    assert v["memo_start_ms"] == pytest.approx(500 + 300.0)
    assert v["lead_ms"] == 50


def test_gate_verdict_fails_on_slow_loop_or_too_few_runs():
    runs = [mk("lock", loop_max_us=60_000) for _ in range(20)]
    runs += [mk("lock", pattern="meet", start_us=600_000) for _ in range(20)]
    v = gate_verdict(summarize_cue(runs))
    assert v["pass"] is False and "loop step" in " ".join(v["why"])
    v = gate_verdict(summarize_cue([mk("lock")]))
    assert v["pass"] is False


def test_gate_verdict_fails_slow_meeting_start():
    runs = [mk("lock") for _ in range(20)] + [mk("lock", pattern="meet", start_us=900_000) for _ in range(20)]
    assert gate_verdict(summarize_cue(runs))["pass"] is False


def test_verdict_for_another_lock_mode():
    runs = [mk("lockyield") for _ in range(20)] + [mk("lockyield", pattern="meet", start_us=600_000) for _ in range(20)]
    assert gate_verdict(summarize_cue(runs), lock="lockyield")["pass"] is True
    assert gate_verdict(summarize_cue(runs))["pass"] is False  # no plain-lock runs


def test_verdict_fails_on_lock_starvation():
    runs = [mk("lock") for _ in range(20)] + [mk("lock", pattern="meet", start_us=600_000) for _ in range(20)]
    runs[3]["cue"]["lock_wait_max_us"] = 3_600_000
    v = gate_verdict(summarize_cue(runs))
    assert v["pass"] is False and "lock wait" in " ".join(v["why"])


def test_default_sweep_has_20_locked_runs_per_pattern():
    for mode in ("lock", "lockyield"):
        locked = [c for c in CUE_SWEEP if c.hammer == mode]
        assert sum(1 for c in locked if c.pattern == "memo") >= 20
        assert sum(1 for c in locked if c.pattern == "meet") >= 20
    assert any(c.hammer == "nolock" for c in CUE_SWEEP)
