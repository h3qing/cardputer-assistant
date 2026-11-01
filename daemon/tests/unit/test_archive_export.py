"""cpa export (docs/15 §2.4): a plain copy of the archive plus what only events.jsonl knows, with SHA256SUMS.

Never follows a symlink out of the archive, never writes into the archive, $CPA_HOME or (by default) a cloud
folder, never exports config.json, raw events, serials or project roots.
"""

from __future__ import annotations

import hashlib
import json
import os
import stat
import zipfile
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

import pytest

from cpad.archive import export
from cpad.archive.export import ExportError, ExportOptions
from cpad.archive.fs import Root
from cpad.config import host
from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.events.log import EventLog
from cpad.filing.rows import device_id
from cpad.recordings import records as r
from tests.support import archive_tree as at

NOW = datetime(2026, 10, 21, 18, 0, tzinfo=timezone(timedelta(hours=-7)))
SECRET_ROOT = "/Users/someone/src/SECRET-ROOT"
SERIAL = "SECRET-SERIAL-020000000001"


@pytest.fixture
def home(tmp_path):
    paths = Paths.at(tmp_path / "home")
    paths.home.mkdir(mode=0o700)
    write_json(paths.config, {"notes_dir": str(tmp_path / "notes"), "device": {"serial": SERIAL}})
    write_json(paths.projects, {"version": 1, "projects": [{"key": "cpa", "name": "Cardputer", "slot": 1,
                                                             "root": SECRET_ROOT}]})  # fmt: skip
    received = "2026-10-21T22:00:00.000+00:00"
    note = {"ev": "item.received", "at": received, "dev": SERIAL, "id": "3QZK1M7-0000C-0005", "kind": "note",
            "body": {"text": "买牛奶", "project": "cpa"}, "at_dev": {"up_ms": 1, "wall_ms": 1792620000000},
            "received_at": received}  # fmt: skip
    queued = {"ev": "queue.added", "at": "x", "qid": "q1", "project": "cpa", "text": "fix it", "intent": "do",
              "sid": None, "origin": "cli", "from_item": None, "uncertain": False}  # fmt: skip
    EventLog(paths.events).append((note, queued, {"ev": "rec.hidden", "rec": at.MEMO}))
    return paths, Root.at(at.build(tmp_path / "notes"))


def _files(base: Path) -> dict[str, bytes]:
    return {p.relative_to(base).as_posix(): p.read_bytes() for p in sorted(base.rglob("*")) if p.is_file()}


def _run(home, target, **options):
    paths, root = home
    return export.export_dir(paths, root, target, ExportOptions(**options), clock=lambda: NOW)


def test_export_dir_layout(home, tmp_path):
    report = _run(home, tmp_path / "out")
    files = _files(tmp_path / "out")
    meeting = at.rec_dir("2026-10-21", "1402", at.MEETING)
    assert {"notes/2026-10-20.md", "notes/2026-10-21.md", f"{meeting}/summary.md", f"{meeting}/seg-000.wav",
            "data/captures.jsonl", "data/queue.jsonl", "data/projects.json", "CPA-EXPORT.json", "README.md",
            "SHA256SUMS"} <= set(files)  # fmt: skip
    assert files["notes/2026-10-21.md"] == at.NOTE_21.encode()
    assert not any(name.split("/")[-1].startswith(".") or name.endswith("ideas.md") for name in files)
    assert at.rec_dir("2026-10-21", "1405", at.MEMO) + "/summary.md" in files  # hidden is not deleted
    assert (report.notes, report.recordings, report.captures, report.queue, report.skipped) == (2, 6, 1, 1, 0)
    assert report.files == len(files) and report.target == (tmp_path / "out").resolve()


def test_sha256sums_cover_every_file(home, tmp_path):
    _run(home, tmp_path / "out")
    files = _files(tmp_path / "out")
    lines = files.pop("SHA256SUMS").decode().splitlines()
    assert lines == sorted(lines, key=lambda line: line.split("  ", 1)[1])
    assert {line.split("  ", 1)[1]: line.split("  ", 1)[0] for line in lines} == {
        name: hashlib.sha256(data).hexdigest() for name, data in files.items()}  # fmt: skip


def test_private_permissions(home, tmp_path):
    _run(home, tmp_path / "out")
    for path in [tmp_path / "out", *(tmp_path / "out").rglob("*")]:
        mode = stat.S_IMODE(path.stat().st_mode)
        assert mode == (0o700 if path.is_dir() else 0o600), path


def test_data_files_and_manifest(home, tmp_path):
    _run(home, tmp_path / "out", since=date(2026, 10, 21))
    files = _files(tmp_path / "out")
    captures = [json.loads(line) for line in files["data/captures.jsonl"].decode().splitlines()]
    assert captures[0]["text"] == "买牛奶" and set(captures[0]) == {"id", "kind", "at", "project", "dest", "text"}
    assert json.loads(files["data/queue.jsonl"])["qid"] == "q1"
    assert json.loads(files["data/projects.json"]) == {"version": 1, "projects": [
        {"key": "cpa", "name": "Cardputer", "slot": 1}]}  # fmt: skip
    meta = json.loads(files["CPA-EXPORT.json"])
    assert (meta["format"], meta["layout"], meta["since"], meta["until"], meta["audio"]) == (
        "cpa-export", 1, "2026-10-21", None, True)  # fmt: skip
    assert meta["created_at"] == "2026-10-21T18:00:00-07:00" and meta["counts"]["notes"] == 1
    assert not any(name.startswith(("notes/2026-10-20", "recordings/2026-10-20")) for name in files)


def _archived(rec: str, day: str, hhmm: str, mode: str, project: str | None) -> dict:
    return r.archived(dev=SERIAL, rec=rec, dir=at.rec_dir(day, hhmm, rec), mode=mode, project=project, segments=1,
                      dur_ms=60000, started_ms=at.MEETING_WALL, uncertain=False, gaps=[], consent=None,
                      at="x")  # fmt: skip


def test_filing_jsonl_has_one_row_per_archived_recording_hidden_ones_too(home, tmp_path):
    """docs/23 §2.3: the filing state, which lives only in events.jsonl, as rows of the filing list (§3.2)."""
    paths, _ = home
    key = host.id_key(paths.id_key)
    EventLog(paths.events).append((_archived(at.OLD, "2026-10-20", "0915", "meeting", None),
                                   _archived(at.MEETING, "2026-10-21", "1402", "meeting", "cpa"),
                                   _archived(at.MEMO, "2026-10-21", "1405", "memo", None)))  # fmt: skip
    _run(home, tmp_path / "out", since=date(2026, 10, 21))
    files = _files(tmp_path / "out")
    rows = [json.loads(line) for line in files["data/filing.jsonl"].decode().splitlines()]
    assert [(row["id"], row["filing"], row["project"]) for row in rows] == [(at.MEETING, "filed", "cpa"),
                                                                            (at.MEMO, "hidden", None)]  # fmt: skip
    assert rows[0]["at"] == {"wall_ms": at.MEETING_WALL, "tz_min": -420, "uncertain": False}
    assert rows[0]["device"] == device_id(key, SERIAL) and rows[0]["tx"] == "processing"
    assert b"SECRET" not in files["data/filing.jsonl"] and b"recordings/" not in files["data/filing.jsonl"]
    assert json.loads(files["CPA-EXPORT.json"])["counts"]["filing"] == 2
    assert "data/filing.jsonl" in files["README.md"].decode()


def test_filing_text_comes_from_the_transcript_when_there_is_no_memo_text(home, tmp_path):
    paths, _ = home
    old = _archived(at.OLD, "2026-10-20", "0915", "meeting", None)
    done = r.result(rec=at.OLD, state="done", level="transcript", title=None, memo_text=None, memo_intent=None,
                    actions_n=0, error_code=None, session=old, at="x")  # fmt: skip
    EventLog(paths.events).append((old, done))
    _run(home, tmp_path / "out")
    (row,) = [json.loads(line) for line in _files(tmp_path / "out")["data/filing.jsonl"].decode().splitlines()]
    assert (row["tx"], row["text"], row["device"]) == ("transcript", "旧会议只有转写。", None), "no id_key: no device"
    assert not paths.id_key.exists(), "the export never writes to $CPA_HOME"


def test_filing_rows_survive_a_missing_transcript_and_a_malformed_directory(home, tmp_path):
    paths, _ = home
    failed = _archived(at.FAILED, "2026-10-20", "1100", "meeting", None)  # its folder has no transcript
    done = r.result(rec=at.FAILED, state="done", level="transcript", title=None, memo_text=None, memo_intent=None,
                    actions_n=0, error_code=None, session=failed, at="x")  # fmt: skip
    odd = {**_archived(at.BLOCKED, "2026-10-21", "1630", "meeting", None), "dir": "../elsewhere"}
    EventLog(paths.events).append((failed, done, odd))
    _run(home, tmp_path / "all")
    rows = [json.loads(line) for line in _files(tmp_path / "all")["data/filing.jsonl"].decode().splitlines()]
    assert [(row["id"], row["text"], row["at"]["tz_min"]) for row in rows] == [(at.FAILED, None, -420),
                                                                               (at.BLOCKED, None, None)]  # fmt: skip
    _run(home, tmp_path / "ranged", since=date(2026, 10, 20))
    ranged = _files(tmp_path / "ranged")["data/filing.jsonl"].decode().splitlines()
    assert [json.loads(line)["id"] for line in ranged] == [at.FAILED], "a directory that is not one has no day"


def test_nothing_secret_is_exported(home, tmp_path):
    _run(home, tmp_path / "out")
    blob = b"".join(_files(tmp_path / "out").values())
    assert b"SECRET" not in blob and b"notes_dir" not in blob and b"item.received" not in blob


def test_no_audio(home, tmp_path):
    report = _run(home, tmp_path / "out", audio=False)
    assert not any(name.endswith(".wav") for name in _files(tmp_path / "out"))
    assert json.loads(_files(tmp_path / "out")["CPA-EXPORT.json"])["audio"] is False and report.skipped == 0


def test_symlinks_out_of_the_archive_are_never_followed(home, tmp_path):
    _, root = home
    outside = tmp_path / "outside"
    outside.mkdir()
    (outside / "summary.md").write_text("SECRET OUTSIDE")
    meeting = root.path / at.rec_dir("2026-10-21", "1402", at.MEETING)
    (meeting / "summary.md").unlink()
    (meeting / "summary.md").symlink_to(outside / "summary.md")
    (root.path / "2026-10-19.md").symlink_to(outside / "summary.md")
    (root.path / "recordings" / "2026-10-18").symlink_to(outside, target_is_directory=True)
    (root.path / "recordings" / "2026-10-20" / f"0800-{at.PULLING.lower()}").symlink_to(outside)
    _run(home, tmp_path / "out")
    assert b"SECRET OUTSIDE" not in b"".join(_files(tmp_path / "out").values())


def test_oversized_files_are_skipped_and_counted(home, tmp_path):
    _, root = home
    folder = root.path / at.rec_dir("2026-10-21", "1630", at.BLOCKED)
    (folder / "status.json").write_text(json.dumps({"pad": "x" * 5000}))
    report = _run(home, tmp_path / "out")
    assert report.skipped == 1
    assert at.rec_dir("2026-10-21", "1630", at.BLOCKED) + "/status.json" not in _files(tmp_path / "out")


def test_an_empty_directory_is_a_valid_target(home, tmp_path):
    (tmp_path / "out").mkdir(mode=0o755)
    _run(home, tmp_path / "out")
    assert stat.S_IMODE((tmp_path / "out").stat().st_mode) == 0o700
    assert [p.name for p in tmp_path.iterdir() if p.name.startswith(".")] == []


def test_a_failed_export_leaves_nothing_behind(home, tmp_path, monkeypatch):
    def boom(*args, **kwargs):
        raise OSError("disk full")

    monkeypatch.setattr(export.events_view, "captures", boom)  # after the archive files were copied
    with pytest.raises(OSError):
        _run(home, tmp_path / "out")
    assert sorted(p.name for p in tmp_path.iterdir()) == ["home", "notes"]


def test_export_zip(home, tmp_path):
    paths, root = home
    target = tmp_path / "cpa-2026-10-21.zip"
    report = export.export_zip(paths, root, target, ExportOptions(audio=False), clock=lambda: NOW)
    assert stat.S_IMODE(target.stat().st_mode) == 0o600 and report.target == target.resolve()
    with zipfile.ZipFile(target) as zf:
        names = zf.namelist()
        assert all(name.startswith("cpa-2026-10-21/") for name in names)
        sums = zf.read("cpa-2026-10-21/SHA256SUMS").decode().splitlines()
        for line in sums:
            digest, name = line.split("  ", 1)
            assert hashlib.sha256(zf.read(f"cpa-2026-10-21/{name}")).hexdigest() == digest
        assert len(sums) == len(names) - 1 and not any(n.endswith(".wav") for n in names)
        assert all(stat.S_IMODE(info.external_attr >> 16) == 0o600 for info in zf.infolist())
    assert [p.name for p in tmp_path.iterdir() if p.name.startswith(".")] == []


def test_sha256sums_format():
    assert export.sha256sums((("b.md", "0" * 64), ("a.md", "f" * 64))) == f"{'f' * 64}  a.md\n{'0' * 64}  b.md\n"


# --- check_target -------------------------------------------------------------------------------


def _check(target, home, *, zip_=False, allow_cloud=False, user_home=None):
    paths, root = home
    return export.check_target(target, notes_root=root.path, cpa_home=paths.home, allow_cloud=allow_cloud,
                               zip_file=zip_, user_home=user_home)  # fmt: skip


def test_check_target_accepts_a_new_path(home, tmp_path):
    assert _check(tmp_path / "new", home) == (tmp_path / "new").resolve()


@pytest.mark.parametrize("make", ["file", "full_dir", "symlink", "no_parent"])
def test_check_target_refuses_existing_things(home, tmp_path, make):
    target = tmp_path / "out"
    if make == "file":
        target.write_text("x")
    elif make == "full_dir":
        target.mkdir()
        (target / "x").write_text("x")
    elif make == "symlink":
        (tmp_path / "elsewhere").mkdir()
        target.symlink_to(tmp_path / "elsewhere")
    else:
        target = tmp_path / "missing" / "out"
    with pytest.raises(ExportError):
        _check(target, home)


def test_check_target_refuses_an_existing_zip_or_an_empty_dir_for_zip(home, tmp_path):
    (tmp_path / "out").mkdir()
    assert _check(tmp_path / "out", home) == (tmp_path / "out").resolve()
    with pytest.raises(ExportError):
        _check(tmp_path / "out", home, zip_=True)


def test_check_target_refuses_the_archive_and_cpa_home(home, tmp_path):
    paths, root = home
    for target in (root.path / "backup", root.path / "recordings" / "x", paths.home / "export"):
        with pytest.raises(ExportError):
            _check(target, home)
    link = tmp_path / "into-notes"
    link.symlink_to(root.path)
    with pytest.raises(ExportError):
        _check(link / "backup", home)


@pytest.mark.parametrize("cloud", ["Library/Mobile Documents/com~apple~CloudDocs", "Library/CloudStorage/Dropbox"])
def test_check_target_refuses_cloud_folders_unless_allowed(home, tmp_path, cloud):
    user_home = tmp_path / "user"
    folder = user_home / cloud
    folder.mkdir(parents=True)
    with pytest.raises(ExportError) as exc:
        _check(folder / "backup", home, user_home=user_home)
    assert "--allow-cloud" in str(exc.value)
    assert _check(folder / "backup", home, user_home=user_home, allow_cloud=True) == (folder / "backup").resolve()


def test_check_target_refuses_documents_kept_by_icloud(home, tmp_path):
    user_home = tmp_path / "user"
    (user_home / "Documents").mkdir(parents=True)
    (user_home / "Library/Mobile Documents/com~apple~CloudDocs/Documents").mkdir(parents=True)
    with pytest.raises(ExportError) as exc:
        _check(user_home / "Documents" / "cardputer-backup", home, user_home=user_home)
    assert "Desktop" in str(exc.value) and "--allow-cloud" in str(exc.value)
    target = user_home / "Documents" / "cardputer-backup"
    assert _check(target, home, user_home=user_home, allow_cloud=True) == target.resolve()


def test_check_target_refuses_a_folder_a_sync_provider_marked(home, tmp_path):
    marked = tmp_path / "user" / "Work"
    marked.mkdir(parents=True)
    paths, root = home
    with pytest.raises(ExportError):
        export.check_target(
            marked / "backup",
            notes_root=root.path,
            cpa_home=paths.home,
            allow_cloud=False,
            user_home=tmp_path / "user",
            marks=lambda p: frozenset({"com.apple.file-provider-domain-id"} if p == marked else ()),
        )


def test_since_after_until_is_refused(home, tmp_path):
    with pytest.raises(ExportError):
        _run(home, tmp_path / "out", since=date(2026, 10, 22), until=date(2026, 10, 21))
    assert not os.path.lexists(tmp_path / "out")
