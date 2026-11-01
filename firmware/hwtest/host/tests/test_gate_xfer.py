import pytest

from gate_xfer import (
    SD_SWEEP,
    SWEEP,
    XferConfig,
    choose_ring,
    decide_encoding,
    per_second_kbs,
    summarize_run,
)


def test_config_command_and_label():
    c = XferConfig("b64", 1380, 4, 8, "sd", 10)
    assert c.command() == "!xfer b64 1380 4 8 sd 10"
    assert c.label() == "b64-1380-d4-r8-sd"


def test_config_rejects_bad_values():
    with pytest.raises(ValueError):
        XferConfig("hex", 1380, 4, 8, "sd", 10)
    with pytest.raises(ValueError):
        XferConfig("b64", 1380, 4, 6, "sd", 10)
    with pytest.raises(ValueError):
        XferConfig("b64", 5000, 4, 8, "sd", 10)


def test_keep_open_source_and_sd_sweep():
    assert XferConfig("b64", 1380, 4, 4, "sdkeep").command() == "!xfer b64 1380 4 4 sdkeep 10"
    srcs = {c.src for c in SD_SWEEP}
    assert srcs == {"sd", "sdkeep"} and all(c.depth == 4 for c in SD_SWEEP)


def test_sweep_covers_the_gate_axes():
    encs = {c.enc for c in SWEEP}
    rings = {c.ring_kb for c in SWEEP}
    depths = {c.depth for c in SWEEP}
    srcs = {c.src for c in SWEEP}
    assert encs == {"b64", "bin"} and {4, 8} <= rings and {0, 1, 4} <= depths and srcs == {"ram", "sd"}
    # the app's own configuration is in it
    assert any(c.enc == "b64" and c.length == 1380 and c.depth == 4 and c.ring_kb == 4 and c.src == "sd" for c in SWEEP)


def test_per_second_kbs_buckets_by_receive_time():
    rx = [(0.0, 1024), (0.5, 1024), (1.2, 2048), (2.9, 1024), (3.0, 512)]
    # whole seconds after the first frame only; the partial last second is dropped
    assert per_second_kbs(rx) == [2.0, 2.0, 1.0]
    assert per_second_kbs([]) == []


def test_summarize_run_numbers():
    rx = [(t * 0.01, 1380) for t in range(201)]  # 2 s of 1380 B every 10 ms
    run = {
        "rx": rx,
        "wire_bytes": 201 * 2030,
        "rtt_ms": [3.0, 4.0, 50.0],
        "xs": [{"cpu0": 1, "cpu1": 40}, {"cpu0": 3, "cpu1": 60}],
        "crc_bad": 0,
        "framing_errors": 0,
        "end": {"frames": 201, "rearms": 2, "err": ""},
    }
    s = summarize_run(run)
    assert s["kbs"] == pytest.approx(1380 * 200 / 2.0 / 1024, rel=1e-3)
    assert s["wire_kbs"] > s["kbs"]
    assert s["rtt"]["p95"] == 50.0
    assert s["cpu1"] == {"mean": 50.0, "max": 60}
    assert s["min_per_hour"] == pytest.approx(115_200_000 / (s["kbs"] * 1024) / 60, rel=1e-3)
    assert s["ok"] is True


def test_summarize_run_flags_errors():
    run = {"rx": [], "wire_bytes": 0, "rtt_ms": [], "xs": [], "crc_bad": 1, "framing_errors": 0, "end": None}
    s = summarize_run(run)
    assert s["ok"] is False and s["kbs"] is None


def row(enc, kbs, ring=8, depth=4, src="sd", length=1380):
    return {"config": XferConfig(enc, length, depth, ring, src, 10).as_dict(), "summary": {"kbs": kbs, "ok": True, "rtt": {"p95": 10}}}


def test_decide_encoding_follows_audio_capture_7_3():
    assert decide_encoding([row("b64", 330), row("bin", 470, length=4096)])["choice"] == "b64"
    assert decide_encoding([row("b64", 150), row("bin", 200, length=4096)])["choice"] == "bin"
    assert decide_encoding([row("b64", 200), row("bin", 220, length=4096)])["choice"] == "b64"
    assert decide_encoding([row("b64", 120), row("bin", 150, length=4096)])["choice"] == "adpcm?"


def test_decide_encoding_ignores_failed_rows():
    bad = row("bin", 900, length=4096)
    bad["summary"]["ok"] = False
    assert decide_encoding([row("b64", 330), bad])["bin_kbs"] is None


def test_choose_ring_prefers_small_unless_clearly_faster():
    rows = [row("b64", 130, ring=4), row("b64", 135, ring=8)]
    assert choose_ring(rows)["ring_kb"] == 4
    rows = [row("b64", 100, ring=4), row("b64", 130, ring=8)]
    assert choose_ring(rows)["ring_kb"] == 8
    assert choose_ring([])["ring_kb"] is None
