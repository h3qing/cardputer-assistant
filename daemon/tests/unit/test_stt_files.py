"""cpa-stt file I/O: atomic writes that never touch a folder with result.json (docs/22 §4.7), and finding
finished recordings as untrusted input (lstat, O_NOFOLLOW, inside the root, sizes, WAV header, sha256)."""

from __future__ import annotations

import json
import os
import stat
import time

import pytest

from cpa_stt import scan, writer
from tests.support import stt_tree

SID = stt_tree.SID

# --- writer -------------------------------------------------------------------------------------


def _leftovers(folder):
    return sorted(p.name for p in folder.iterdir() if p.name.startswith("."))


def test_write_file_is_private_and_leaves_no_temp(tmp_path):
    assert writer.write_file(tmp_path, "status.json", b"{}\n")
    assert (tmp_path / "status.json").read_bytes() == b"{}\n"
    assert stat.S_IMODE((tmp_path / "status.json").stat().st_mode) == 0o600
    assert _leftovers(tmp_path) == []
    assert writer.write_file(tmp_path, "status.json", b"{ }\n")
    assert (tmp_path / "status.json").read_bytes() == b"{ }\n"


def test_nothing_is_written_once_result_json_exists(tmp_path):
    (tmp_path / "result.json").write_bytes(b"theirs")
    assert not writer.write_file(tmp_path, "transcript.json", b"mine")
    assert not (tmp_path / "transcript.json").exists() and _leftovers(tmp_path) == []


def test_a_symlink_named_like_our_file_is_replaced_not_followed(tmp_path):
    outside = tmp_path / "outside.txt"
    outside.write_bytes(b"keep")
    folder = tmp_path / "s"
    folder.mkdir()
    (folder / "transcript.md").symlink_to(outside)
    assert writer.write_file(folder, "transcript.md", b"new")
    assert outside.read_bytes() == b"keep"
    assert not (folder / "transcript.md").is_symlink() and (folder / "transcript.md").read_bytes() == b"new"


def test_a_stale_temp_file_does_not_block_the_write(tmp_path):
    (tmp_path / f".status.json.{os.getpid()}.tmp").symlink_to(tmp_path / "elsewhere")
    assert writer.write_file(tmp_path, "status.json", b"x")
    assert not (tmp_path / "elsewhere").exists() and _leftovers(tmp_path) == []


def test_result_is_linked_into_place_and_never_overwrites(tmp_path):
    assert writer.place_result(tmp_path, b"mine")
    assert (tmp_path / "result.json").read_bytes() == b"mine"
    assert stat.S_IMODE((tmp_path / "result.json").stat().st_mode) == 0o600
    assert not writer.place_result(tmp_path, b"again")
    assert (tmp_path / "result.json").read_bytes() == b"mine" and _leftovers(tmp_path) == []


def test_has_result_counts_even_a_dangling_symlink(tmp_path):
    assert not writer.has_result(tmp_path)
    (tmp_path / "result.json").symlink_to(tmp_path / "nowhere")
    assert writer.has_result(tmp_path)


# --- finding sessions ---------------------------------------------------------------------------


def _names(found):
    return [f.rel for f in found]


def test_finds_finished_sessions_and_skips_the_rest(tmp_path):
    root = tmp_path / "recordings"
    ok = stt_tree.make_session(root)
    done = stt_tree.make_session(root, "3QZK1M7-00042-0A20")
    (done / "result.json").write_text("{}")
    unfinished = root / "2026-10-21" / "1500-3qzk1m7-00042-0a21"
    unfinished.mkdir(parents=True)
    (unfinished / "seg-000.wav").write_bytes(stt_tree.tone(160))
    stt_tree.make_session(root, "3QZK1M7-00042-0A22", date="not-a-date")
    (root / "2026-10-21" / ".hidden").mkdir()
    (root / ".transcriber").write_text("{}")
    found = scan.folders(root)
    assert _names(found) == ["2026-10-21/" + ok.name]
    assert found[0].doc["session"] == SID and found[0].path == ok.resolve()


def test_symlinked_date_session_or_manifest_are_ignored(tmp_path):
    root = tmp_path / "recordings"
    elsewhere = stt_tree.make_session(tmp_path / "elsewhere")
    root.mkdir()
    (root / "2026-10-22").symlink_to(elsewhere.parent)
    day = root / "2026-10-23"
    day.mkdir()
    (day / "1402-x").symlink_to(elsewhere)
    linked = stt_tree.make_session(root, "3QZK1M7-00042-0A30", date="2026-10-24")
    (linked / "manifest.json").unlink()
    (linked / "manifest.json").symlink_to(elsewhere / "manifest.json")
    assert scan.folders(root) == ()


def test_an_oversized_or_broken_manifest_is_unreadable(tmp_path):
    root = tmp_path / "recordings"
    big = stt_tree.make_session(root)
    (big / "manifest.json").write_bytes(b" " * (scan.MANIFEST_MAX + 1))
    broken = stt_tree.make_session(root, "3QZK1M7-00042-0A31")
    (broken / "manifest.json").write_text("{", encoding="utf-8")
    docs = [f.doc for f in scan.folders(root)]
    assert docs == [scan.Unreadable("(size)"), scan.Unreadable("(root)")]


def test_missing_root_is_empty(tmp_path):
    assert scan.folders(tmp_path / "nope") == ()


def _doc(folder):
    return json.loads((folder / "manifest.json").read_text())


def test_audio_checks_pass_for_a_good_session(tmp_path):
    folder = stt_tree.make_session(tmp_path, blobs=[stt_tree.tone(160), stt_tree.tone(320)])
    assert scan.check_audio(folder, _doc(folder)) is None


@pytest.mark.parametrize(
    ("damage", "code"),
    [
        (lambda f: (f / "seg-000.wav").write_bytes(stt_tree.tone(161)), "E_INVALID"),
        (lambda f: (f / "seg-000.wav").unlink(), "E_INVALID"),
        (lambda f: (f / "seg-000.wav").write_bytes(b"RIFX" + stt_tree.tone(160)[4:]), "E_AUDIO"),
        (lambda f: (f / "seg-000.wav").write_bytes(stt_tree.tone(160)[:-2] + b"\x01\x02"), "E_INVALID"),
    ],
    ids=["size", "missing", "header", "sha256"],
)
def test_audio_faults(tmp_path, damage, code):
    folder = stt_tree.make_session(tmp_path, blobs=[stt_tree.tone(160)])
    damage(folder)
    assert scan.check_audio(folder, _doc(folder)) == code


def test_a_symlinked_segment_means_skip(tmp_path):
    folder = stt_tree.make_session(tmp_path, blobs=[stt_tree.tone(160)])
    real = tmp_path / "real.wav"
    (folder / "seg-000.wav").rename(real)
    (folder / "seg-000.wav").symlink_to(real)
    assert scan.check_audio(folder, _doc(folder)) == scan.SKIP


def test_foreign_writers_are_recognised(tmp_path):
    folder = stt_tree.make_session(tmp_path)
    now = time.time()
    assert not scan.foreign(folder, now)
    (folder / "transcript.md").write_text("theirs")
    assert scan.foreign(folder, now)
    (folder / "status.json").write_text(json.dumps({"state": "transcribing", "by": "cpa-stt"}))
    assert not scan.foreign(folder, now)
    (folder / "status.json").write_text(json.dumps({"state": "transcribing"}))
    assert scan.foreign(folder, now)
    (folder / "status.json").write_text("{")
    assert scan.foreign(folder, now)


def test_another_programs_files_older_than_five_minutes_are_leftovers(tmp_path):
    folder = stt_tree.make_session(tmp_path)
    now = time.time()
    for name in ("status.json", "transcript.md"):
        (folder / name).write_text("theirs")
        os.utime(folder / name, (now - 301, now - 301))
    assert not scan.foreign(folder, now)
    os.utime(folder / "transcript.md", (now - 299, now - 299))
    assert scan.foreign(folder, now)
    os.utime(folder / "transcript.md", (now + 3600, now + 3600))
    assert scan.foreign(folder, now)


def test_more_than_four_hours_is_invalid(tmp_path):
    folder = stt_tree.make_session(tmp_path, blobs=[stt_tree.tone(160)])
    doc = _doc(folder)
    huge = {**doc, "segments": [{**doc["segments"][0], "samples": 9_600_000, "i": k} for k in range(25)]}
    assert scan.check_audio(folder, huge) == "E_INVALID"
