"""Session completion, gap finalisation and manifest.json v1 (docs/11 §6.4, §7.3); the rec event fold."""

from __future__ import annotations

from cpad.protocol.file_messages import At, FileItem
from cpad.recordings import index, manifest
from cpad.recordings import records as r
from cpad.recordings.wall import Wall

DEV = "020000000001"
AT = "2026-10-21T06:45:10.000+00:00"
SID = "K3M9Q2T-00042-0A1C"
SEG_BYTES = 9600000  # 5 min


def seg_id(i: int) -> str:
    return f"K3M9Q2T-00042-0A{['1C', '1D', '1E', '1F', '1G', '1H'][i]}"


def stored(
    i: int,
    *,
    last: bool = False,
    data: int = SEG_BYTES,
    up_ms: int | None = None,
    stars=(),
    mode="meeting",
    consent=None,
    lead_ms=None,
    rcv=False,
    at=True,
    project="cpa",
    slot=None,
    wall=1792620120000,
) -> dict:
    up = 435000 + i * 300000 if up_ms is None else up_ms
    item = FileItem(id=seg_id(i), kind="rec", size=44 + data, rec=SID, seg=i, last=last, mode=mode,
                    dur_ms=data // 32, at=At(up) if at else None, sha256=None if rcv else "a" * 64, project=project,
                    slot=slot, stars=tuple(stars), lead_ms=lead_ms, consent=consent, rcv=rcv)  # fmt: skip
    return r.seg_stored(dev=DEV, item=item, file=f"recordings/2026-10-21/1402-{SID.lower()}/seg-{i:03d}.wav",
                        file_sha256=f"{i:064x}", wall=Wall(wall + i * 300000, False), repaired=rcv,
                        fw="cpa-fw/0.3.0", at=AT)  # fmt: skip


def build(segs, **kw) -> dict:
    defaults = {"names": {"cpa": "Cardputer 助手"}, "slots": {1: "cpa"}, "tz_min": 480, "written_at": AT}
    return manifest.build(tuple(segs), **{**defaults, **kw})


# --- completion and gaps ------------------------------------------------------------------


def test_a_session_is_complete_with_contiguous_segments_ending_last():
    assert manifest.is_complete((stored(0), stored(1), stored(2, last=True)))
    assert not manifest.is_complete((stored(0), stored(2, last=True)))
    assert not manifest.is_complete((stored(0), stored(1)))
    assert not manifest.is_complete((stored(1, last=True),))
    assert manifest.is_complete((stored(0, last=True, mode="memo"),))


def test_gaps_list_missing_indices_and_k_plus_1_after_a_trailing_last_false():
    assert manifest.gaps((stored(0), stored(2, last=True))) == (1,)
    assert manifest.gaps((stored(0), stored(1))) == (2,)
    assert manifest.gaps((stored(0), stored(2))) == (1, 3)
    assert manifest.gaps((stored(1, last=True),)) == (0,)
    assert manifest.gaps((stored(0), stored(1, last=True))) == ()


def test_finalisable_only_when_idle_and_the_listing_has_none_of_the_session():
    segs = (stored(0), stored(1))
    assert manifest.finalisable(segs, listed=frozenset())
    assert not manifest.finalisable(segs, listed=frozenset({SID}))
    assert not manifest.finalisable((), listed=frozenset())


# --- manifest -----------------------------------------------------------------------------


def test_t0_ms_from_cumulative_samples():
    doc = build([stored(0, lead_ms=200), stored(1), stored(2, last=True, data=64000)])
    assert [s["t0_ms"] for s in doc["segments"]] == [0, 300000, 600000]
    assert [s["samples"] for s in doc["segments"]] == [4800000, 4800000, 32000]
    assert doc["duration_ms"] == 602000 and doc["lead_ms"] == 200 and doc["gaps"] == []


def test_t0_ms_after_a_gap_comes_from_the_up_ms_difference():
    doc = build([stored(0, up_ms=435000), stored(2, up_ms=435000 + 900000, last=True, data=32000)])
    assert [(s["i"], s["t0_ms"]) for s in doc["segments"]] == [(0, 0), (2, 900000)]
    assert doc["gaps"] == [1] and doc["duration_ms"] == 901000


def test_trailing_last_false_finalises_with_gap_k_plus_1():
    doc = build([stored(0), stored(1, rcv=True)])
    assert doc["gaps"] == [2]
    assert doc["segments"][1]["recovered"] and doc["segments"][1]["repaired"]


def test_marks_are_converted_to_session_time_and_consent_is_carried():
    doc = build([stored(0, stars=(12800,), consent="ack"), stored(1, stars=(1000, 2000), last=True)])
    assert doc["marks"] == [{"t_ms": 12800, "k": "star"}, {"t_ms": 301000, "k": "star"}, {"t_ms": 302000, "k": "star"}]
    assert doc["consent"] == "ack"
    assert "consent" not in build([stored(0, mode="memo", last=True)])


def test_manifest_fields_and_whole_file_sha():
    doc = build([stored(0, last=True, mode="memo", data=64000)])
    assert (doc["v"], doc["kind"], doc["session"], doc["attempt"], doc["mode"]) == (1, "cardputer.recording", SID, 1,
                                                                                     "memo")  # fmt: skip
    assert doc["project"] == {"key": "cpa", "name": "Cardputer 助手"}
    assert doc["started"] == {"wall_ms": 1792620120000, "tz_min": 480, "uncertain": False}
    assert doc["audio"] == {"sample_rate": 16000, "channels": 1, "bits": 16}
    seg = doc["segments"][0]
    assert (seg["file"], seg["id"], seg["bytes"], seg["sha256"]) == ("seg-000.wav", seg_id(0), 64044, "0" * 64)
    assert doc["device"] == {"dev": DEV, "fw": "cpa-fw/0.3.0"} and doc["written_at"] == AT


def test_project_from_slot_or_none():
    assert build([stored(0, project=None, slot=1, last=True)])["project"] == {"key": "cpa", "name": "Cardputer 助手"}
    assert build([stored(0, project=None, slot=5, last=True)])["project"] is None
    assert build([stored(0, project="zz", last=True)])["project"] == {"key": "zz", "name": None}


def tagged(doc: dict) -> str | None:
    rec = manifest.archived_record(doc, dev=DEV, dir="d", at=AT)
    assert rec["project"] == (doc["project"] or {}).get("key")
    return rec["project"]


def test_a_tag_set_after_segment_0_wins_in_the_manifest_and_the_archive():  # #46: 1-9 in segment 1
    doc = build([stored(0, project=None), stored(1), stored(2, last=True)])
    assert doc["project"] == {"key": "cpa", "name": "Cardputer 助手"} and tagged(doc) == "cpa"
    by_slot = build([stored(0, project=None), stored(1, project=None, slot=1, last=True)])
    assert by_slot["project"] == {"key": "cpa", "name": "Cardputer 助手"} and tagged(by_slot) == "cpa"


def test_a_tag_cleared_after_segment_0_wins_in_the_manifest_and_the_archive():  # #46: 1, then 0 in segment 1
    doc = build([stored(0), stored(1, project=None), stored(2, project=None, last=True)])
    assert doc["project"] is None and tagged(doc) is None


def test_an_unchanged_tag_and_a_trailing_gap_keep_the_last_stored_segments_tag():
    assert tagged(build([stored(0), stored(1), stored(2, last=True)])) == "cpa"
    assert tagged(build([stored(0, project=None), stored(1, project=None, last=True)])) is None
    assert tagged(build([stored(0, project=None), stored(1)])) == "cpa"  # last:false, finalised with gap 2


def test_archived_record_summarises_the_manifest():
    doc = build([stored(0, consent="ack"), stored(2, last=True, data=32000)])
    rec = manifest.archived_record(doc, dev=DEV, dir=f"recordings/2026-10-21/1402-{SID.lower()}", at=AT)
    assert rec["ev"] == "rec.archived" and rec["rec"] == SID and rec["segments"] == 2 and rec["gaps"] == [1]
    assert (rec["mode"], rec["project"], rec["consent"], rec["started_ms"]) == ("meeting", "cpa", "ack", 1792620120000)


# --- the event fold -----------------------------------------------------------------------


def test_fold_tracks_stored_deleted_archived_results_and_stale():
    session = {"dir": "d", "mode": "memo", "project": None, "started_ms": 1, "uncertain": False, "dur_ms": 1000}
    events = [
        stored(0),
        stored(1),
        stored(1),
        r.seg_deleted(DEV, seg_id(0), AT),
        {"ev": "rec.seg.stored"},
        {"ev": "item.received", "id": "x"},
        r.archived(
            dev=DEV,
            rec=SID,
            dir="d",
            mode="memo",
            project=None,
            segments=2,
            dur_ms=1,
            started_ms=1,
            uncertain=False,
            gaps=[],
            consent=None,
            at=AT,
        ),  # fmt: skip
        r.result(
            rec=SID,
            state="failed",
            level=None,
            title=None,
            memo_text=None,
            memo_intent=None,
            actions_n=0,
            error_code="E_STT",
            session=session,
            at=AT,
        ),  # fmt: skip
        r.stale(rec=SID, session=session, at=AT),
    ]
    idx = index.fold(events[:4])
    assert index.stored_ids(idx, DEV) == frozenset({seg_id(0), seg_id(1)})
    assert [s["seg"] for s in index.segments(idx, SID)] == [0, 1]
    assert index.open_sessions(idx) == (SID,) and idx.deleted == frozenset({(DEV, seg_id(0))})
    idx = index.fold(events[4:], idx)
    assert index.open_sessions(idx) == () and SID in idx.archived
    assert index.result_states(idx, SID) == frozenset({"failed"}) and SID in idx.stale
    assert index.latest_result(idx, SID)["error_code"] == "E_STT"
    assert index.stored_ids(idx, "other") == frozenset()
