import base64
import math
import struct

from gate_warm import goertzel_db, pcm_from_lines, summarize, tone_track

RATE = 16000


def sine(n, dbfs, hz=1000.0, start=0):
    """Phase-shifted so that no sample is exactly 0 (a codec's zeros are the thing under test)."""
    amp = 10 ** (dbfs / 20) * 32768
    return [round(amp * math.sin(2 * math.pi * hz * (start + i) / RATE + 0.25)) for i in range(n)]


def lines_of(x, per=512):
    out = []
    for seq, off in enumerate(range(0, len(x), per)):
        blob = struct.pack(f"<{len(x[off:off + per])}h", *x[off:off + per])
        out.append({"t": "warm_pcm", "seq": seq, "b64": base64.b64encode(blob).decode()})
    return [*out, {"t": "warm_end", "lines": len(out)}]


def test_pcm_round_trips_and_checks_every_line():
    x = [0, 1, -1, 32767, -32768] * 300
    got, ok = pcm_from_lines(lines_of(x))
    assert ok and got == x
    lines = lines_of(x)
    _, ok = pcm_from_lines(lines[:1] + lines[2:])  # a line lost on the way
    assert not ok
    _, ok = pcm_from_lines(lines[:-1])  # no warm_end
    assert not ok


def test_goertzel_reads_a_tone_level_and_ignores_silence():
    assert abs(goertzel_db(sine(160, -30.0)) - -30.0) < 0.2
    assert goertzel_db([0] * 160) == -150.0
    assert goertzel_db(sine(160, -30.0, hz=250.0)) < -45.0  # another frequency


def test_tone_track_is_one_level_per_10_ms():
    track = tone_track([0] * 320 + sine(480, -24.0))
    assert len(track) == 5
    assert track[:2] == [-150.0, -150.0]
    assert all(abs(v - -24.0) < 0.2 for v in track[2:])


def test_summarize_cold_start_with_a_transient():
    x = [0] * 12000 + [9000, -9000] * 40 + sine(28000, -30.0, start=12080)  # 750 ms of zeros, a thump
    s = summarize(x)
    assert s["lead0_ms"] == 750.0
    assert s["zeros_after"] == 0
    assert abs(s["steady_db"] - -30.0) < 0.3
    assert s["reach_ms"] == 760  # the first 10 ms window past the zeros and the thump
    assert s["onset_peak"] == 9000
    assert s["steady_peak"] == max(abs(v) for v in sine(160, -30.0))


def test_summarize_warm_start():
    s = summarize(sine(32000, -30.0))
    assert s["lead0_ms"] == 0.0 and s["reach_ms"] == 0
    assert s["onset_peak"] == s["steady_peak"]


def test_quiet_lead_covers_the_dc_servo_tail_before_the_zeros():
    # M5Unified's offset servo carries over from the last capture: a few small values come first
    x = [19, 12, 7, 3, 1] + [0] * 795 + sine(8000, -30.0, start=800)
    s = summarize(x)
    assert s["lead0_ms"] == 0.0
    assert s["quiet_ms"] == 50.0
    assert s["onset_peak"] == max(abs(v) for v in sine(160, -30.0))  # measured after the quiet lead


def test_mute_ends_at_the_first_sample_over_100():
    # a servo tail above QUIET, the zeros, then the tone: the first |x| > 100 ends the mute
    x = [40, 39, 38, 37] + [0] * 15996 + [60, -150] + sine(4000, -30.0)
    s = summarize(x)
    assert s["quiet_ms"] == 0.0
    assert s["mute_ms"] == 1000.0625
    assert summarize(sine(1600, -30.0))["mute_ms"] == 0.0
    assert summarize([0] * 1600)["mute_ms"] is None


def test_summarize_counts_zeros_after_the_lead_and_handles_all_zero():
    x = [0] * 160 + [5, 0, 0, -5] * 1000
    assert summarize(x)["zeros_after"] == 2000
    s = summarize([0] * 8000)
    assert s["lead0_ms"] == 500.0 and s["reach_ms"] is None and s["steady_db"] == -150.0
    assert s["quiet_ms"] == 500.0
