"""The Mac-side environment stamp of a recording (docs/23 §4.2, P1-D1): env{host, link}, computed in memory once
segment 0 lands, copied into rec.seg.stored, manifest.json and rec.archived. link is "here" only when provable."""

from __future__ import annotations

from hypothesis import given
from hypothesis import strategies as st

from cpad.protocol.file_messages import At, FileItem
from cpad.recordings import manifest, stamp
from cpad.recordings import records as r
from cpad.recordings.wall import Seen, Wall

BOOT = "K3M9Q2T-00042"
SID = f"{BOOT}-0A1C"
DEV = "020000000001"
AT = "2026-10-21T06:45:10.000+00:00"
SINCE = Seen(BOOT, 900000, 1792620000000)  # the hello of the current link
SEEN = Seen(BOOT, 960000, 1792620060000)  # the latest pong of that link


def link(up_ms: int | None, *, seg_id: str = SID, since: Seen | None = SINCE, seen: Seen | None = SEEN) -> str:
    return stamp.link(seg_id, None if up_ms is None else At(up_ms), since, seen)


# --- link: "here" only when provable --------------------------------------------------------


def test_a_recording_started_inside_the_current_link_is_here():
    assert link(900000) == "here" and link(930000) == "here" and link(960000) == "here"


def test_a_recording_started_before_the_hello_is_unknown():
    """Recorded in the pocket, pulled when plugged in: the device was not linked to this Mac then."""
    assert link(899999) == "unknown" and link(435000) == "unknown"


def test_a_start_after_the_latest_pong_is_unknown():
    assert link(960001) == "unknown"


def test_another_boot_is_unknown():
    assert link(930000, seg_id="K3M9Q2T-00043-0A1C") == "unknown"
    assert link(930000, since=Seen("K3M9Q2T-00041", 0, 1)) == "unknown"


def test_no_device_time_or_no_current_link_is_unknown():
    assert link(None) == "unknown"
    assert link(930000, since=None) == "unknown" and link(930000, seen=None) == "unknown"


@given(since=st.integers(0, 2**32 - 1), up=st.integers(0, 2**32 - 1), seen=st.integers(0, 2**32 - 1),
       boot=st.sampled_from([BOOT, "K3M9Q2T-00043"]))  # fmt: skip
def test_here_is_never_claimed_without_proof(since, up, seen, boot):
    found = link(up, seg_id=f"{boot}-0A1C", since=Seen(BOOT, since, 1), seen=Seen(BOOT, seen, 2))
    assert found in stamp.LINKS
    assert (found == "here") == (boot == BOOT and since <= up <= seen)


# --- the stamp --------------------------------------------------------------------------------


def test_the_stamp_names_this_mac_only_by_its_user_given_label():
    assert stamp.env(host="公司", seg_id=SID, at=At(930000), since=SINCE, seen=SEEN) == {"host": "公司", "link": "here"}
    assert stamp.env(host=None, seg_id=SID, at=None, since=SINCE, seen=SEEN) == {"host": None, "link": "unknown"}


def test_a_stored_stamp_is_copied_only_when_well_formed():
    assert stamp.of_record({"env": {"host": "家", "link": "here"}}) == {"host": "家", "link": "here"}
    later = {"host": None, "link": "unknown", "net": {"id": "x"}}  # a later field is not copied by this cpad
    assert stamp.of_record({"env": later}) == {"host": None, "link": "unknown"}
    hosts = ("a\tb", "", "九个汉字的主机名称", 7)
    shapes = (None, "here", {"link": "here"}, {"host": None}, {"host": None, "link": "maybe"})
    for bad in (*shapes, *({"host": h, "link": "here"} for h in hosts)):
        assert stamp.of_record({"env": bad}) is None, bad
    assert stamp.of_record({}) is None


# --- records and manifest -------------------------------------------------------------------


def _stored(seg: int, env=None, *, last: bool = True) -> dict:
    item = FileItem(id=f"{BOOT}-0A{['1C', '1D'][seg]}", kind="rec", size=44 + 3200, rec=SID, seg=seg, last=last,
                    mode="memo", dur_ms=100, at=At(930000 + seg), sha256="a" * 64, project=None)  # fmt: skip
    return r.seg_stored(dev=DEV, item=item, file=f"recordings/2026-10-21/1402-x/seg-{seg:03d}.wav",
                        file_sha256="0" * 64, wall=Wall(1792620120000, False), repaired=False, fw="cpa-fw/0.3.0",
                        at=AT, env=env)  # fmt: skip


def _build(segs) -> dict:
    return manifest.build(tuple(segs), names={}, slots={}, tz_min=480, written_at=AT)


def test_segment_0_carries_its_stamp_or_null_and_later_segments_none():
    env = {"host": "公司", "link": "here"}
    assert _stored(0, env)["env"] == env
    assert _stored(0, None)["env"] is None, "a failed stamp is recorded as null"
    assert "env" not in _stored(1, env, last=False)


def test_the_manifest_and_rec_archived_copy_segment_0s_stamp():
    env = {"host": "公司", "link": "here"}
    doc = _build([_stored(0, env)])
    assert doc["env"] == env
    archived = manifest.archived_record(doc, dev=DEV, dir="recordings/2026-10-21/1402-x", at=AT)
    assert archived["env"] == env


def test_no_stamp_leaves_the_manifest_and_rec_archived_as_before():
    for segs in ([_stored(0, None)], [_stored(1, {"host": None, "link": "here"})],
                 [{k: v for k, v in _stored(0).items() if k != "env"}]):  # fmt: skip
        doc = _build(segs)
        assert "env" not in doc
        assert "env" not in manifest.archived_record(doc, dev=DEV, dir="d", at=AT)


def test_a_malformed_stamp_in_the_log_never_reaches_the_manifest():
    doc = _build([_stored(0, {"host": "公司", "link": "everywhere"})])
    assert "env" not in doc
