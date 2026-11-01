"""Verify and land (docs/11 §6.2.8, §6.3): WAV header, SHA-256, staging .part, atomic landing, permissions."""

from __future__ import annotations

import errno
import hashlib
import os
import struct
from datetime import timedelta, timezone

import pytest

from cpad.protocol.file_messages import At, FileItem
from cpad.recordings import land, verify
from tests.support import fake_files as ff
from tests.support.rec_shell import Shell, until

FID = "K3M9Q2T-00042-0A1C"
TZ = timezone(timedelta(hours=8))


def pcm(n: int) -> bytes:
    return bytes(i % 256 for i in range(n))


def item(data: bytes, *, rcv: bool | None = None, sha: str | None = "auto") -> FileItem:
    digest = hashlib.sha256(data).hexdigest() if sha == "auto" else sha
    return FileItem(id=FID, kind="rec", size=44 + len(data), rec=FID, seg=0, last=True, mode="memo",
                    dur_ms=len(data) // 32, at=At(1000), sha256=digest, rcv=rcv)  # fmt: skip


def check(data: bytes, header: bytes, it: FileItem, size: int | None = None) -> verify.Verdict:
    return verify.check(it, size if size is not None else 44 + len(data), header, hashlib.sha256(data).hexdigest())


# --- verify -------------------------------------------------------------------------------


def test_wav_header_is_16k_mono_pcm16():
    header = verify.wav_header(32000)
    assert len(header) == 44 and header[:4] == b"RIFF" and header[8:16] == b"WAVEfmt "
    assert struct.unpack("<IHHIIHH", header[16:36]) == (16, 1, 1, 16000, 32000, 2, 16)
    assert struct.unpack("<I", header[4:8])[0] == 36 + 32000 and header[36:40] == b"data"
    assert struct.unpack("<I", header[40:44])[0] == 32000


def test_a_good_segment_verifies():
    data = pcm(3200)
    assert check(data, verify.wav_header(3200), item(data)) == verify.Verdict(True)


def test_size_header_and_sha_mismatches_fail():
    data = pcm(3200)
    assert check(data, verify.wav_header(3200), item(data), size=44 + 3202).reason == "size"
    assert check(data, verify.wav_header(3198), item(data)).reason == "header"
    assert check(data, verify.wav_header(3200), item(data, sha="0" * 64)).reason == "sha256"
    odd = pcm(3201)
    assert check(odd, verify.wav_header(3201), item(odd)).reason == "size"


def test_recovered_segment_gets_its_header_repaired_and_skips_sha():
    data = pcm(3200)
    verdict = check(data, verify.wav_header(0), item(data, rcv=True, sha=None))
    assert verdict == verify.Verdict(True, repaired=True)
    assert check(data, verify.wav_header(3200), item(data, rcv=True, sha=None)) == verify.Verdict(True)


def test_a_segment_without_a_device_sha_is_accepted_on_its_header():
    data = pcm(320)
    assert check(data, verify.wav_header(320), item(data, sha=None)).ok


# --- staging and landing ------------------------------------------------------------------


def test_part_files_are_private_and_truncated_on_resume(tmp_path):
    staging = land.Staging(tmp_path / "rec-staging")
    path = staging.path("020000000001", FID)
    with staging.open(path, keep=0) as part:
        part.append(b"x" * 100)
    assert path.stat().st_mode & 0o777 == 0o600 and path.parent.stat().st_mode & 0o777 == 0o700
    assert staging.size(path) == 100
    with staging.open(path, keep=40) as part:
        part.append(b"y" * 10)
    assert path.read_bytes() == b"x" * 40 + b"y" * 10
    assert staging.size(tmp_path / "missing.part") == 0


def test_session_dir_and_segment_names():
    assert land.session_dir(FID, 1792620120000, TZ) == "recordings/2026-10-22/0602-k3m9q2t-00042-0a1c"
    assert land.seg_name(7) == "seg-007.wav" and land.seg_name(1080) == "seg-1080.wav"


def _staged(tmp_path, data: bytes):
    staging = land.Staging(tmp_path / "rec-staging")
    path = staging.path("020000000001", FID)
    with staging.open(path, keep=0) as part:
        part.append(verify.wav_header(len(data)) + data)
    return staging, path


def test_verify_file_reads_header_and_hashes_the_data(tmp_path):
    data = pcm(6400)
    _, path = _staged(tmp_path, data)
    assert land.verify_file(path, item(data)) == verify.Verdict(True)
    assert land.verify_file(path, item(data, sha="1" * 64)).reason == "sha256"


def test_land_renames_into_the_notes_tree_with_private_modes(tmp_path):
    data = pcm(640)
    _, path = _staged(tmp_path, data)
    target = tmp_path / "notes" / "recordings" / "2026-10-21" / "0602-x" / "seg-000.wav"
    land.land(path, target, root=tmp_path / "notes")
    assert not path.exists() and target.read_bytes()[44:] == data
    assert target.stat().st_mode & 0o777 == 0o600
    for directory in (target.parent, target.parent.parent, target.parent.parent.parent):
        assert directory.stat().st_mode & 0o777 == 0o700
    assert land.file_sha256(target) == hashlib.sha256(verify.wav_header(640) + data).hexdigest()


def test_land_copies_through_a_dot_tmp_file_across_volumes(tmp_path):
    data = pcm(640)
    _, path = _staged(tmp_path, data)
    target = tmp_path / "notes" / "s" / "seg-001.wav"
    calls = []

    def cross_volume(src, dst):
        calls.append((os.path.basename(src), os.path.basename(dst)))
        if str(src).endswith(".part"):
            raise OSError(errno.EXDEV, "cross-device link")
        os.replace(src, dst)

    land.land(path, target, root=tmp_path / "notes", rename=cross_volume)
    assert calls == [("K3M9Q2T-00042-0A1C.part", "seg-001.wav"), (".seg-001.wav.tmp", "seg-001.wav")]
    assert target.read_bytes()[44:] == data and not path.exists()
    assert not (target.parent / ".seg-001.wav.tmp").exists()


def test_land_raises_other_rename_errors(tmp_path):
    _, path = _staged(tmp_path, pcm(64))

    def broken(src, dst):
        raise OSError(errno.ENOSPC, "full")

    with pytest.raises(OSError):
        land.land(path, tmp_path / "n" / "seg-000.wav", root=tmp_path / "n", rename=broken)
    assert path.exists()


def test_repair_header_rewrites_the_first_44_bytes(tmp_path):
    data = pcm(320)
    staging = land.Staging(tmp_path / "rec-staging")
    path = staging.path("020000000001", FID)
    with staging.open(path, keep=0) as part:
        part.append(verify.wav_header(0) + data)
    land.repair_header(path)
    assert path.read_bytes() == verify.wav_header(320) + data


# --- crash at every landing step (docs/11 §6.2.9): never a device delete without rec.seg.stored ---

CRASH_POINTS = {  # where the process "dies" -> (rec.seg.stored written, session archived) before it
    "verify": ("cpad.recordings.land.verify_file", 0, False),
    "rename": ("cpad.recordings.land.land", 0, False),
    "after rename": ("cpad.recordings.land.file_sha256", 0, False),
    "after stored": ("cpad.recordings.service.Recordings._maybe_archive", 1, False),
    "after ack": ("cpad.recordings.records.seg_deleted", 1, True),
}


def _crash(*_args, **_kwargs):
    raise RuntimeError("simulated crash")


async def _crash_then_restart(tmp_path, monkeypatch, target: str, stored_before: int, archived: bool) -> Shell:
    shell = Shell(tmp_path, ff.FakeFiles())
    shell.files.add(ff.memo(FID, n_bytes=4000))

    def stored_first(wire):
        if wire["t"] == "file.del":
            assert any(e["id"] == wire["fid"] for e in shell.events("rec.seg.stored")), "file.del before stored"

    with monkeypatch.context() as patch:
        patch.setattr(target, _crash)
        async with shell.connected(on_send=stored_first):
            await shell.settle(0.3)
    assert len(shell.events("rec.seg.stored")) == stored_before and bool(shell.archived()) == archived
    shell.restart()
    async with shell.connected(on_send=stored_first):
        await until(lambda: not shell.files.segments and shell.archived())
    return shell


@pytest.mark.parametrize("point", list(CRASH_POINTS))
async def test_a_crash_at_any_landing_step_never_deletes_unstored_audio(tmp_path, monkeypatch, point):
    shell = await _crash_then_restart(tmp_path, monkeypatch, *CRASH_POINTS[point])
    assert len(shell.events("rec.seg.stored")) == 1 and len(shell.events("rec.seg.deleted")) <= 1
    assert sorted(p.name for p in shell.session(FID).iterdir()) == ["manifest.json", "seg-000.wav"]
    assert not shell.part(FID).exists()


async def test_a_segment_failing_verification_stays_on_the_device_and_its_part_is_dropped(tmp_path):
    files = ff.FakeFiles()
    files.add(ff.memo(FID, sha="0" * 64), ff.memo("K3M9Q2T-00042-0A1D", header=b"X" * 44))
    shell = Shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: len(shell.service.puller.given_up) == 2)
    assert set(files.segments) == {FID, "K3M9Q2T-00042-0A1D"} and files.dels == []
    assert shell.events("rec.seg.stored") == [] and not shell.part(FID).exists()


async def test_an_unwritable_notes_dir_keeps_the_segment_on_the_device(tmp_path):
    files = ff.FakeFiles()
    files.add(ff.memo(FID))
    shell = Shell(tmp_path, files)
    shell.notes_dir.mkdir(mode=0o500)
    try:
        async with shell.connected():
            await until(lambda: shell.service.puller.given_up)
    finally:
        shell.notes_dir.chmod(0o700)
    assert FID in files.segments and shell.events("rec.seg.stored") == [] and shell.part(FID).exists()
    async with shell.connected():
        await until(lambda: not files.segments)
