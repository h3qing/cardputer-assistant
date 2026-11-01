"""RecPuller pure plan (docs/11 §6.2): resume, window, CRC strikes, E_BUSY gate, rev relist, ordering."""

from __future__ import annotations

import base64
import zlib

from cpad.protocol.file_messages import At, FileData, FileItem, Files
from cpad.recordings import pull
from tests.support import fake_files as ff
from tests.support.rec_shell import Shell, until

FID = "K3M9Q2T-00042-0A20"
SIZE = 200044


def item(fid: str, mode: str = "meeting", seg: int = 0, rec: str | None = None, size: int = SIZE) -> FileItem:
    return FileItem(id=fid, kind="rec", size=size, rec=rec or fid, seg=seg, last=True, mode=mode, dur_ms=6250,
                    at=At(1000))  # fmt: skip


def data(off: int, payload: bytes, *, fid: str = FID, size: int = SIZE, crc: int | None = None) -> FileData:
    b64 = base64.b64encode(payload).decode() if payload else None
    crc = zlib.crc32(payload) if crc is None else crc
    return FileData(id="d1", re="h1", fid=fid, off=off, n=len(payload), size=size, crc=crc, b64=b64)


def chunk(off: int, size: int = SIZE) -> bytes:
    return bytes((off + i) % 251 for i in range(min(pull.CHUNK, size - off)))


# --- resume and window --------------------------------------------------------------------


def test_resume_truncates_the_last_64_kib_of_the_part():
    assert pull.resume_offset(70380, SIZE) == 70380 - 65536 == 4844
    assert pull.resume_offset(1000, SIZE) == 0
    assert pull.resume_offset(10_000_000, SIZE) == SIZE - 65536


def test_window_keeps_at_most_four_gets_of_1380_bytes_in_flight():
    t, offsets = pull.issue(pull.start(FID, SIZE, 4844), paused=False)
    assert offsets == (4844, 6224, 7604, 8984)
    t, more = pull.issue(t, paused=False)
    assert more == ()
    t, written = pull.on_data(t, 4844, data(4844, chunk(4844)))
    assert written == chunk(4844) and t.done == 6224
    t, offsets = pull.issue(t, paused=False)
    assert offsets == (10364,)


def test_out_of_order_chunks_are_held_until_contiguous():
    t, _ = pull.issue(pull.start(FID, SIZE, 0), paused=False)
    t, early = pull.on_data(t, 1380, data(1380, chunk(1380)))
    assert early == b"" and t.done == 0
    t, both = pull.on_data(t, 0, data(0, chunk(0)))
    assert both == chunk(0) + chunk(1380) and t.done == 2760


def test_last_chunk_is_short_and_completes_the_file():
    size = 1424
    t, offsets = pull.issue(pull.start(FID, size, 0), paused=False)
    assert offsets == (0, 1380)
    t, _ = pull.on_data(t, 0, data(0, chunk(0, size), size=size))
    t, _ = pull.on_data(t, 1380, data(1380, chunk(1380, size), size=size))
    assert pull.complete(t) and t.done == size


# --- strikes ------------------------------------------------------------------------------


def test_bad_crc_retries_the_same_offset():
    t, _ = pull.issue(pull.start(FID, SIZE, 0), paused=False)
    t, written = pull.on_data(t, 0, data(0, chunk(0), crc=1))
    assert written == b"" and not t.failed
    t, offsets = pull.issue(t, paused=False)
    assert offsets == (0,)


def test_wrong_offset_fid_size_or_short_n_is_a_strike():
    for bad in (data(1380, chunk(0)), data(0, chunk(0), fid="K3M9Q2T-00042-0A21"), data(0, chunk(0)[:100])):
        t, _ = pull.issue(pull.start(FID, SIZE, 0), paused=False)
        t, written = pull.on_data(t, 0, bad)
        assert written == b"" and t.strikes == {0: 1}
    t, _ = pull.on_data(pull.issue(pull.start(FID, SIZE, 0), paused=False)[0], 0, data(0, chunk(0), size=SIZE + 2))
    assert t.failed


def test_three_strikes_on_one_chunk_give_up_the_file():
    t = pull.start(FID, SIZE, 0)
    for _ in range(3):
        t, offsets = pull.issue(t, paused=False)
        assert 0 in offsets
        t, _ = pull.on_data(t, 0, data(0, chunk(0), crc=7))
    assert t.failed
    assert pull.issue(t, paused=False)[1] == ()


def test_timeouts_count_as_strikes():
    t = pull.start(FID, SIZE, 0)
    for _ in range(3):
        t, _ = pull.issue(t, paused=False)
        t = pull.on_lost(t, 0)
    assert t.failed


# --- E_BUSY -------------------------------------------------------------------------------


def test_busy_requeues_the_refused_offsets_without_a_strike():
    t, _ = pull.issue(pull.start(FID, SIZE, 0), paused=False)
    t, _ = pull.on_data(t, 0, data(0, chunk(0)))
    t, _ = pull.on_data(t, 1380, data(1380, chunk(1380)))
    t = pull.on_busy(t, 2760)
    t = pull.on_busy(t, 4140)
    assert pull.issue(t, paused=True)[1] == ()
    t, offsets = pull.issue(t, paused=False)
    assert offsets[:2] == (2760, 4140) and t.strikes == {} and t.done == 2760


def test_gate_needs_files_live_false_and_rev_above_zero():
    gate = pull.Gate()
    assert not pull.gate_open(gate)
    assert not pull.gate_open(pull.observe(gate, Files(1, 0, False), 1))
    assert not pull.gate_open(pull.observe(gate, Files(1, 5, True), 1))
    assert not pull.gate_open(pull.observe(gate, None, 1))
    assert pull.gate_open(pull.observe(gate, Files(1, 5, False), 1))


def test_e_busy_closes_the_gate_until_a_later_status_shows_live_false():
    gate = pull.observe(pull.Gate(), Files(1, 14, False), 10)
    gate = pull.on_busy_gate(gate, 12)
    assert not pull.gate_open(gate)
    gate = pull.observe(gate, Files(1, 14, True), 13)
    assert not pull.gate_open(gate)
    assert pull.gate_open(pull.observe(gate, Files(2, 15, False), 14))


def test_e_busy_seen_after_a_newer_idle_status_does_not_close_the_gate():
    gate = pull.observe(pull.Gate(), Files(1, 14, False), 20)
    assert pull.gate_open(pull.on_busy_gate(gate, 12))


# --- rev and ordering ---------------------------------------------------------------------


def test_relist_only_when_a_status_shows_an_unexpected_rev():
    expect = pull.Expect()
    assert pull.needs_relist(expect)
    expect = pull.listed(expect, 5, seq=10)
    assert not pull.needs_relist(pull.observed(expect, 5, 11))
    assert not pull.needs_relist(pull.observed(expect, 9, 3))  # older than the listing
    expect = pull.deleted(expect, 12)  # own delete: device rev 6
    assert not pull.needs_relist(pull.observed(expect, 6, 13))
    assert not pull.needs_relist(pull.observed(expect, 5, 11))  # a status received before the ack
    assert pull.needs_relist(pull.observed(expect, 7, 13))  # a commit


def test_a_commit_seen_mid_file_is_not_hidden_by_a_later_own_delete():
    expect = pull.observed(pull.listed(pull.Expect(), 5, seq=1), 6, 4)  # memo committed mid-file
    expect = pull.deleted(expect, 6)  # then the current file is deleted: device rev 7
    assert pull.needs_relist(expect)
    relisted = pull.listed(expect, 6, seq=4)  # listing based on the rev-6 status; the ack came after it
    assert relisted.acks == (6,)
    assert not pull.needs_relist(pull.observed(relisted, 7, 8))


def test_a_status_received_while_paging_still_counts():
    paging = pull.observed(pull.Expect(), 6, 7)  # first listing based on the rev-5 status at seq 2
    assert pull.needs_relist(pull.listed(paging, 5, seq=2))


def test_memo_sessions_first_then_id_order_and_stored_segments_are_deleted():
    meeting = [item("K3M9Q2T-00042-0A1C", seg=0), item("K3M9Q2T-00042-0A1D", seg=1, rec="K3M9Q2T-00042-0A1C")]
    memo = item("K3M9Q2T-00042-0A1F", mode="memo")
    later = item("K3M9Q2T-00042-0A1G", mode="meeting")
    plan = pull.plan([later, meeting[1], memo, meeting[0]], stored=frozenset({"K3M9Q2T-00042-0A1C"}),
                     skip=frozenset({"K3M9Q2T-00042-0A1G"}))  # fmt: skip
    assert [(a, i.id) for a, i in plan] == [
        ("pull", "K3M9Q2T-00042-0A1F"),
        ("delete", "K3M9Q2T-00042-0A1C"),
        ("pull", "K3M9Q2T-00042-0A1D"),
    ]


def test_one_more_listing_once_own_deletes_left_nothing_of_an_unfinished_session():
    s0 = item("K3M9Q2T-00042-0A1C", seg=0)
    s1 = item("K3M9Q2T-00042-0A1D", seg=1, rec=s0.rec)
    memo = item("K3M9Q2T-00042-0A1F", mode="memo")
    listing, cut = (s0, s1, memo), frozenset({s0.rec})
    assert pull.relist_to_finish(listing, deleted=frozenset({s0.id, s1.id}), unfinished=cut)
    assert not pull.relist_to_finish(listing, deleted=frozenset({s0.id}), unfinished=cut)  # s1 still on the device
    assert not pull.relist_to_finish(listing, deleted=frozenset({s0.id, s1.id}), unfinished=frozenset())  # archived
    assert not pull.relist_to_finish(listing, deleted=frozenset({memo.id}), unfinished=cut)
    assert not pull.relist_to_finish((), deleted=frozenset(), unfinished=cut)  # that listing already showed none


# --- the I/O shell (RecPuller + Recordings over SimLink / FakeFiles, tests/support/rec_shell.py) --

MEMO = "K3M9Q2T-00042-0A2F"
MEETING = ["K3M9Q2T-00042-0A2A", "K3M9Q2T-00042-0A2B", "K3M9Q2T-00042-0A2C"]


async def test_pulls_memo_first_lands_verifies_deletes_and_archives(tmp_path):
    shell = Shell(tmp_path, ff.FakeFiles())
    shell.files.add(*ff.meeting(MEETING), ff.memo(MEMO))
    async with shell.connected():
        await until(lambda: not shell.files.segments and len(shell.archived()) == 2)
    assert shell.files.gets[0][0] == MEMO
    assert [e["seg"] for e in shell.events("rec.seg.stored")] == [0, 0, 1, 2]
    assert len(shell.events("rec.seg.deleted")) == 4 and shell.events("queue.added") == []
    for sid in (MEMO, MEETING[0]):
        assert (shell.session(sid) / "manifest.json").exists()
    assert (shell.session(MEETING[0]) / "seg-002.wav").read_bytes()[44:] == ff.pcm(4000, 4)


async def test_a_session_cut_after_a_last_false_segment_is_archived_without_reconnecting(tmp_path):
    files = ff.FakeFiles()  # HIL rec_reboot (b): the power went at the segment change, segment 1 never existed
    files.add(ff.meeting(MEETING[:2])[0], ff.memo(MEMO))
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: not files.segments and len(shell.archived()) == 2)
        await shell.settle()
        shell.sim.pong()
        await shell.settle()
        assert len(shell.sim.host_sent("file.list")) == 2  # one more listing finishes the session, then quiet
    cut = next(e for e in shell.archived() if e["rec"] == MEETING[0])
    assert cut["gaps"] == [1] and cut["segments"] == 1
    assert (shell.session(MEETING[0]) / "manifest.json").exists()


async def test_a_session_missing_a_middle_segment_is_archived_once_its_last_segment_is_deleted(tmp_path):
    segs = ff.meeting(MEETING)
    files = ff.FakeFiles()
    files.add(segs[0], segs[2])
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: shell.archived())
    assert shell.archived()[0]["gaps"] == [1] and shell.archived()[0]["segments"] == 2


async def test_no_file_requests_while_live_or_before_the_index_is_ready(tmp_path):
    files = ff.FakeFiles(rev=0)
    files.add(ff.memo(MEMO))
    files.rev = 0
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await shell.settle()
        files.rev, files.live = 3, True
        shell.sim.pong()
        await shell.settle()
        assert shell.sim.host_sent("file.list") == []
        files.live = False
        shell.sim.pong()
        await until(lambda: not files.segments)


async def test_e_busy_pauses_until_live_false_then_resumes_by_offset(tmp_path):
    files = ff.FakeFiles(busy_after_gets=3)
    files.add(ff.memo(MEMO, n_bytes=20000))
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: files.live)
        await shell.settle()
        sent = len(shell.sim.trace)
        shell.sim.pong()  # still live
        await shell.settle()
        assert [msg["t"] for _, side, msg in shell.sim.trace[sent:] if side == "host"] == ["ping"]
        files.live = False
        shell.sim.pong()
        await until(lambda: not files.segments)
    offsets = [off for _, off in files.gets]
    assert offsets[:3] == [0, 1380, 2760] and offsets[3] == 4140 and 0 not in offsets[3:]
    assert len(shell.events("rec.seg.stored")) == 1


async def test_bad_crc_is_asked_again_and_three_strikes_give_up_until_the_next_connection(tmp_path):
    files = ff.FakeFiles(bad_crc={(MEMO, 1380): 1})
    files.add(ff.memo(MEMO, n_bytes=6000))
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: not files.segments)
    assert files.gets.count((MEMO, 1380)) == 2
    other = "K3M9Q2T-00042-0A30"
    files.add(ff.memo(other, n_bytes=6000))
    files.bad_crc = {(other, 2760): 3}
    async with shell.connected():
        await until(lambda: files.gets.count((other, 2760)) == 3)
        await shell.settle()
        assert other in files.segments and shell.staged(other) > 0
    async with shell.connected():
        await until(lambda: not files.segments)
    assert len(shell.events("rec.seg.stored")) == 2


async def test_relist_waits_for_the_current_file_and_only_on_a_new_rev(tmp_path):
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO, n_bytes=30000))
    shell = Shell(tmp_path, files)
    later = "K3M9Q2T-00042-0A31"
    async with shell.connected():
        await until(lambda: len(files.gets) >= 4)
        files.add(ff.memo(later))
        shell.sim.pong()
        await until(lambda: not files.segments)
        await shell.settle()
    lists = [k for k, (_, side, msg) in enumerate(shell.sim.trace) if side == "host" and msg["t"] == "file.list"]
    dels = [k for k, (_, side, msg) in enumerate(shell.sim.trace) if side == "host" and msg["t"] == "file.del"]
    assert len(lists) == 2 and dels[0] < lists[1]
    shell.sim.pong()
    await shell.settle()
    assert len(shell.sim.host_sent("file.list")) == 2


async def test_an_already_stored_segment_is_only_deleted(tmp_path):
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO))
    shell = Shell(tmp_path, files)
    async with shell.connected(drop=lambda w: w["t"] == "file.del"):
        await until(lambda: shell.events("rec.seg.stored"))
        await shell.settle(0.3)
    assert MEMO in files.segments
    async with shell.connected():
        await until(lambda: not files.segments)
    assert len(shell.events("rec.seg.stored")) == 1 and len(shell.events("rec.seg.deleted")) == 1
    assert len(shell.sim.host_sent("file.get")) == 0


async def test_resume_from_a_part_left_by_an_earlier_run(tmp_path):
    seg = ff.memo(MEMO, n_bytes=200000)
    files = ff.FakeFiles()
    files.add(seg)
    shell = Shell(tmp_path, files)
    shell.stage(MEMO, seg.blob()[:70380])
    async with shell.connected():
        await until(lambda: not files.segments)
    assert files.gets[0] == (MEMO, 4844)


async def test_a_lost_reply_is_asked_again(tmp_path):
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO))
    shell = Shell(tmp_path, files)
    dropped = []

    def drop_once(wire):
        first = wire["t"] == "file.get" and wire["off"] == 0 and not dropped
        dropped.extend([1] if first else [])
        return first

    async with shell.connected(drop=drop_once):
        await until(lambda: not files.segments)
    assert files.gets.count((MEMO, 0)) == 1 and dropped


async def test_a_closed_link_keeps_the_part_for_the_next_connection(tmp_path):
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO, n_bytes=300000))
    shell = Shell(tmp_path, files)
    async with shell.connected(drop=lambda w: w["t"] == "file.get" and w["off"] >= 100 * pull.CHUNK):
        await until(lambda: len(files.gets) == 100)
        await shell.settle(0.05)
    assert shell.staged(MEMO) == 100 * pull.CHUNK and MEMO in files.segments
    resumed = len(files.gets)
    async with shell.connected():
        await until(lambda: not files.segments)
    assert files.gets[resumed] == (MEMO, 100 * pull.CHUNK - 65536)
    assert len(shell.events("rec.seg.stored")) == 1
