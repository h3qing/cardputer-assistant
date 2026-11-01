"""cpa-stt's reading of manifest.json (untrusted input): the contract verdict, runs of contiguous segments
(a gap starts a new run, segment 0's lead_ms is skipped) and the order of work (docs/22 §4.6)."""

from __future__ import annotations

import pytest

from cpa_stt import manifest
from tests.support import stt_tree

SID = stt_tree.SID


def _doc(blobs=None, **kw):
    blobs = blobs or [(0, stt_tree.tone(1600), 0)]
    return stt_tree.manifest(SID, blobs, **kw)


def test_a_valid_memo_manifest_is_ok():
    assert manifest.check(_doc()).ok


@pytest.mark.parametrize(
    ("doc", "code", "fields"),
    [
        ([], "E_INVALID", ("(root)",)),
        ({"v": 2}, "E_VERSION", ("v",)),
        ({"v": "1"}, "E_VERSION", ("v",)),
    ],
)
def test_not_an_object_or_not_v1(doc, code, fields):
    assert manifest.check(doc) == manifest.Verdict(code, fields)


def test_field_faults_name_their_paths():
    doc = _doc()
    bad = {**doc, "mode": "clip", "segments": [{**doc["segments"][0], "file": "../seg-000.wav"}], "cap": "x"}
    assert manifest.check(bad) == manifest.Verdict("E_INVALID", ("cap", "mode", "segments[0].file"))


def test_index_rules_after_the_schema():
    doc = _doc([(0, stt_tree.tone(160), 0), (2, stt_tree.tone(160), 2000)])
    assert manifest.check(doc) == manifest.Verdict("E_INVALID", ("gaps",))
    assert manifest.check({**doc, "gaps": [1]}).ok
    wrong_bytes = {**doc, "gaps": [1], "segments": [{**doc["segments"][0], "bytes": 45}, doc["segments"][1]]}
    assert manifest.check(wrong_bytes) == manifest.Verdict("E_INVALID", ("segments[0].bytes",))


def test_one_segment_is_one_run_skipping_the_lead():
    (run,) = manifest.runs(_doc(lead_ms=200))
    assert (run.t0_ms, run.offset_ms, run.samples, [s.i for s in run.segs]) == (0, 200, 1600, [0])
    assert run.segs[0].file == "seg-000.wav" and run.duration_s == pytest.approx(0.1)


def test_a_gap_starts_a_new_run():
    blobs = [(0, stt_tree.tone(160), 0), (1, stt_tree.tone(320), 10), (3, stt_tree.tone(160), 90000)]
    first, second = manifest.runs(_doc(blobs, gaps=[2], lead_ms=150))
    assert ([s.i for s in first.segs], first.t0_ms, first.offset_ms, first.samples) == ([0, 1], 0, 150, 480)
    assert ([s.i for s in second.segs], second.t0_ms, second.offset_ms) == ([3], 90000, 0)


def test_without_segment_0_no_lead_is_skipped():
    (run,) = manifest.runs(_doc([(1, stt_tree.tone(160), 5000)], gaps=[0], lead_ms=300))
    assert (run.t0_ms, run.offset_ms) == (5000, 0)


def test_order_clips_first_then_memos_newest_first_then_meetings():
    clip = _doc(cap="3QZK1M7-00042-0A1E", wall_ms=1792620000000)
    old_memo = _doc(wall_ms=1792620000000)
    new_memo = _doc(wall_ms=1792620900000)
    meeting = _doc(mode="meeting", wall_ms=1792629999999)
    meeting_with_cap = _doc(mode="meeting", cap="3QZK1M7-00042-0A1E", wall_ms=1792620000001)
    ranked = sorted([meeting, old_memo, meeting_with_cap, new_memo, clip], key=manifest.order_key)
    assert ranked == [clip, new_memo, old_memo, meeting, meeting_with_cap]
    assert manifest.is_clip(clip) and not manifest.is_clip(meeting_with_cap)
