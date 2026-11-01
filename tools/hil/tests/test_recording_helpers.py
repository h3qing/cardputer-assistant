"""Pure checks behind the recording HIL scenarios (docs/12 M3-H): WAV and manifest accounting, the
device-clock timeline (G-A4), marks in session time, the E_BUSY window of rec_live_hold, transfer
rates and resume offsets from the tap, and the recording lines of the daily note."""

import hashlib
import struct

import pytest

from harness.rec_checks import (
    busy_window,
    contiguous_t0,
    file_rates,
    gets_by_connection,
    health_checks,
    mark_errors,
    meeting_line,
    parse_rec_line,
    rec_health,
    segment_problems,
    timeline,
    wav_header,
)
from harness.wire import make_rec

SID = "K3M9Q2T-00042-0A1C"


def tx(t, line):
    return make_rec(t, "tx", line.encode() + b"\n")


def rx(t, line):
    return make_rec(t, "rx", line.encode() + b"\n")


def opened(t):
    return make_rec(t, "open", b"/dev/cu.usbmodem1101")


# --- WAV and manifest ----------------------------------------------------------------------


def test_wav_header_is_the_canonical_16k_mono_pcm_header():
    h = wav_header(32000)
    assert len(h) == 44
    assert h[:4] == b"RIFF" and h[8:16] == b"WAVEfmt " and h[36:40] == b"data"
    assert struct.unpack("<I", h[4:8])[0] == 36 + 32000
    assert struct.unpack("<HHIIHH", h[20:36]) == (1, 1, 16000, 32000, 2, 16)
    assert struct.unpack("<I", h[40:44])[0] == 32000


def _seg(blob: bytes, **over):
    seg = {"i": 0, "file": "seg-000.wav", "bytes": len(blob), "samples": (len(blob) - 44) // 2,
           "sha256": hashlib.sha256(blob).hexdigest()}  # fmt: skip
    return {**seg, **over}


def test_a_landed_segment_matching_its_manifest_entry_has_no_problems():
    blob = wav_header(8) + bytes(range(8))
    assert segment_problems(blob, _seg(blob)) == ()


@pytest.mark.parametrize(
    ("mutate", "problem"),
    [
        (lambda b: b[:-2], "size"),
        (lambda b: b"RIFX" + b[4:], "header"),
        (lambda b: b[:-1] + b"\x55", "sha256"),
    ],
)
def test_size_header_and_whole_file_sha_are_each_checked(mutate, problem):
    blob = wav_header(8) + bytes(range(8))
    assert problem in segment_problems(mutate(blob), _seg(blob))


def test_samples_must_match_bytes():
    blob = wav_header(8) + bytes(8)
    assert "samples" in segment_problems(blob, _seg(blob, samples=5))


def test_t0_is_contiguous_when_each_segment_starts_where_the_previous_ended():
    segs = [{"t0_ms": 0, "samples": 480000}, {"t0_ms": 30000, "samples": 480000}, {"t0_ms": 60000, "samples": 1}]
    assert contiguous_t0({"segments": segs, "gaps": []})
    broken = [segs[0], {**segs[1], "t0_ms": 30128}]
    assert not contiguous_t0({"segments": broken, "gaps": []})


# --- device-clock timeline (G-A4) -----------------------------------------------------------


def test_timeline_compares_summed_samples_with_device_wall_time():
    # a silent memo start (v0.3.1): the 500 ms G0 hold, then Mic.begin; no cue in between
    t = timeline(start_key_ms=10_000, up0_ms=10_507, stop_key_ms=80_507, samples=70 * 16000 - 512)
    assert t.start_latency_ms == 507
    assert t.wall_ms == 70_000
    assert t.expected_samples == 1_120_000
    assert t.diff_samples == -512
    assert t.within(2048)
    assert not t.within(256)


# --- recording health counters (gh #21, G-A2 / G-A4 / G-A6) ---------------------------------

HEALTH = {"ring_high": 1, "mic_missing": 0, "rot_ms_max": 214, "heap_free": 171520, "heap_block": 155648}
IDLE = {"live": False, "mode": None, "seg": None, "files_n": 2, "files_rev": 19}


def test_rec_health_takes_the_five_counters_of_debug_state_rec():
    assert rec_health({**IDLE, **HEALTH}) == HEALTH
    assert rec_health(IDLE) == dict.fromkeys(HEALTH)  # a build (or a boot) that does not measure


def test_health_checks_pass_when_reported_and_no_capture_rearm_failed():
    checks = health_checks(HEALTH)
    assert [(c["name"], c["ok"]) for c in checks] == [("rec_health_reported", True), ("mic_missing_0", True)]
    assert checks[0]["rot_ms_max"] == 214 and checks[0]["heap_block"] == 155648  # recorded in the report


def test_health_checks_fail_when_a_counter_is_missing_or_a_block_was_at_risk():
    missing = health_checks({**HEALTH, "rot_ms_max": None})
    assert [c["ok"] for c in missing] == [False, True]
    assert missing[0]["missing"] == ["rot_ms_max"]
    assert [c["ok"] for c in health_checks({**HEALTH, "mic_missing": 3})] == [True, False]
    assert [c["ok"] for c in health_checks(rec_health(IDLE))] == [False, False]


# --- marks ----------------------------------------------------------------------------------


def test_mark_errors_are_session_time_minus_the_star_key_time():
    marks = [{"t_ms": 19_900, "k": "star"}, {"t_ms": 49_950, "k": "star"}]
    assert mark_errors(marks, star_key_ms=(30_000, 60_000), up0_ms=10_000) == (-100, -50)


def test_mark_errors_need_one_star_mark_per_press():
    assert mark_errors([{"t_ms": 1, "k": "star"}], star_key_ms=(1, 2), up0_ms=0) is None
    assert mark_errors([{"t_ms": 1, "k": "todo"}], star_key_ms=(1,), up0_ms=0) is None


# --- transfer rates and resume ----------------------------------------------------------------


def _get(t, mid, off, fid=SID):
    return tx(t, f'{{"v":0,"t":"file.get","id":"{mid}","fid":"{fid}","off":{off},"len":1380}}')


def _data(t, mid, off, n, fid=SID):
    return rx(t, f'{{"v":0,"t":"file.data","id":"d{mid}","re":"{mid}","fid":"{fid}","off":{off},"n":{n},'
                 f'"size":2804,"crc":0}}')  # fmt: skip


def test_file_rates_count_distinct_bytes_over_first_get_to_last_data():
    recs = (
        _get(0.0, "h1", 0), _get(0.0, "h2", 1380), _data(0.5, "h1", 0, 1380), _data(0.9, "h2", 1380, 1380),
        _get(1.0, "h3", 1380), _data(1.0, "h3", 1380, 1380), _get(1.0, "h4", 2760), _data(1.024, "h4", 2760, 44),
    )  # fmt: skip
    rate = file_rates(recs)[SID]
    assert (rate.bytes, rate.gets, rate.repeats, rate.first_off) == (2804, 4, 1, 0)
    assert rate.seconds == pytest.approx(1.024)
    assert rate.kb_s == pytest.approx(2804 / 1024 / 1.024)


def test_gets_are_split_by_port_open_for_resume_checks():
    recs = (opened(0), _get(0.1, "h1", 0), _get(0.1, "h2", 1380), _get(0.2, "h9", 0, fid="OTHER"), opened(5),
            _get(5.1, "h3", 1380))  # fmt: skip
    assert gets_by_connection(recs, SID) == ((0, 1380), (1380,))


# --- the E_BUSY window (rec_live_hold) --------------------------------------------------------


def _busy(t, mid, msg="live"):
    return rx(t, f'{{"v":0,"t":"error","id":"e{mid}","re":"{mid}","code":"E_BUSY","msg":"{msg}"}}')


def _pong(t, live):
    return rx(t, f'{{"v":0,"t":"pong","id":"p{t}","re":"x","up_ms":1,"files":{{"n":3,"rev":9,"live":{live}}}}}')


def test_busy_window_from_first_e_busy_to_the_next_live_false_status():
    recs = (
        _get(0.0, "h1", 0), _get(0.0, "h2", 1380), _data(0.1, "h1", 0, 1380), _busy(0.2, "h2"),
        _pong(1.0, "true"), _pong(5.0, "false"), _get(5.1, "h3", 1380), _data(5.2, "h3", 1380, 1380),
    )  # fmt: skip
    w = busy_window(recs)
    assert (w.start, w.end, w.live_seen) == (3, 5, True)
    assert (w.sent_inside, w.non_busy_inside, w.data_inside, w.busy_msgs) == ((), (), 0, ("live",))
    assert w.ok(max_late_data=1)


def test_busy_window_flags_requests_answered_with_data_inside_the_window():
    recs = (_get(0.0, "h1", 0), _busy(0.1, "h1"), _pong(0.5, "true"), _get(0.6, "h2", 0), _data(0.7, "h2", 0, 1380),
            _busy(0.8, "h3", "full"), _pong(5.0, "false"))  # fmt: skip
    w = busy_window(recs)
    assert w.sent_inside == ("h2",)
    assert w.non_busy_inside == ("h2",)
    assert w.data_inside == 1
    assert w.busy_msgs == ("live", "full")
    assert not w.ok(max_late_data=1)


def test_busy_window_without_any_e_busy_is_not_ok():
    w = busy_window((_get(0.0, "h1", 0), _data(0.1, "h1", 0, 1380)))
    assert w.start is None and not w.ok(max_late_data=1)


def test_busy_window_never_cleared_is_not_ok():
    w = busy_window((_get(0.0, "h1", 0), _busy(0.1, "h1"), _pong(0.5, "true")))
    assert w.end is None and not w.ok(max_late_data=1)


def test_a_short_recording_between_two_pongs_needs_no_live_true_status():
    w = busy_window((_get(0.0, "h1", 0), _busy(0.1, "h1"), _pong(5.0, "false")))
    assert not w.live_seen and w.ok(max_late_data=1)


# --- daily-note lines -----------------------------------------------------------------------


def test_parse_rec_line_reads_box_time_tag_body_and_block():
    folder = "recordings/2026-10-21/1405-k3m9q2t-00042-0a1c"
    line = f"- [ ] 14:05 #cpa 待办 HIL 便条 [音频]({folder}/seg-000.wav) ^cpa-k3m9q2t-00042-0a1c"
    got = parse_rec_line(line)
    assert (got.todo, got.time, got.tag, got.sid, got.unprocessed) == (True, "14:05", "cpa", SID, False)
    assert got.body.startswith("待办 HIL 便条 [音频](recordings/")


def test_parse_rec_line_reads_meeting_and_unprocessed_forms():
    meeting = parse_rec_line(
        "- ~14:02 会议：[HIL 会议](recordings/x/summary.md) 1 分钟 · 待办 1 ^cpa-k3m9q2t-00042-0a1c"
    )
    assert (meeting.todo, meeting.time, meeting.tag, meeting.unprocessed) == (False, "~14:02", None, False)
    failed = parse_rec_line("- 14:02 #cpa 会议（未转写）[音频](recordings/x/) ^cpa-k3m9q2t-00042-0a1c-u")
    assert failed.unprocessed and failed.sid == SID


@pytest.mark.parametrize(
    "body",
    [
        "会议：[HIL 会议](recordings/x/summary.md) 1 分钟 · 待办 1",
        "Meeting: [HIL meeting](recordings/x/summary.md) 1 min · to-dos: 1",
    ],
)
def test_a_meeting_line_is_read_in_either_language(body):
    """cpad writes the line in the Mac's language at that moment (cpad.i18n): zh or en."""
    assert meeting_line(body, todos=1)
    assert not meeting_line(body, todos=2)
    assert not meeting_line("语音：x [音频](recordings/x/seg-000.wav)", todos=1)
    assert not meeting_line("Voice: x [audio](recordings/x/seg-000.wav)", todos=1)


def test_parse_rec_line_ignores_other_lines():
    assert parse_rec_line("# 2026-10-21") is None
    assert parse_rec_line("- 14:02 #cpa hello ^cpa-nothex") is None


def test_a_request_crossing_the_first_e_busy_on_the_wire_is_not_a_host_violation():
    recs = (_get(0.0, "h1", 0), _busy(1.0, "h1"), _get(1.0001, "h2", 1380), _busy(1.002, "h2"),
            _get(1.5, "h3", 2760), _busy(1.51, "h3"), _pong(5.0, "false"))  # fmt: skip
    w = busy_window(recs)
    assert w.sent_after_ms == pytest.approx((0.1, 500.0))
    assert w.host_late(grace_ms=20) == ("h3",)
    assert busy_window(recs[:4] + recs[6:]).host_late(grace_ms=20) == ()
