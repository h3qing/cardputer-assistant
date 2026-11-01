"""`cpa export` (docs/15 §2.4): the archive as a folder or a zip, plus what only events.jsonl knows. I/O shell.

  CPA-EXPORT.json   format, layout version, created_at, range, audio, counts, cpad version
  README.md         what each file is and how to verify it (fixed text, in the language of cpad.i18n)
  notes/<day>.md    daily notes
  recordings/<day>/<HHMM>-<sid>/…   copied as they are (no dot files; no WAV with audio=False)
  data/captures.jsonl, data/queue.jsonl, data/projects.json   (no local paths)
  data/filing.jsonl (docs/23 §2.3) one row of the filing list per archived recording, hidden ones too
  SHA256SUMS        `shasum -a 256 -c SHA256SUMS` inside the export
- the target must be new (or an empty directory), outside the archive and $CPA_HOME, and outside any folder
  that syncs online (cloud.py: iCloud Drive, ~/Library/CloudStorage, Desktop and Documents kept by iCloud,
  legacy sync folders, File Provider marks) unless allow_cloud;
- archive files are read with the confined reader (fs.py): a symlink anywhere is skipped, never followed;
  a file over its size cap is skipped and counted;
- the export is built in a hidden sibling and moved into place at the end; directories 0700, files 0600;
- never exported: config.json, the raw events.jsonl, logs, serials, id_key, project roots. cpad need not run, and
  nothing is written to $CPA_HOME.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import zipfile
from collections.abc import Callable, Iterable
from dataclasses import asdict, dataclass, replace
from datetime import date, datetime
from itertools import chain
from pathlib import Path
from types import MappingProxyType
from typing import Any, Protocol

from cpad import __version__
from cpad.archive import cloud, events_view, fs, notes, recordings
from cpad.archive.fs import ArchiveError, Root
from cpad.config.files import DIR_MODE, FILE_MODE
from cpad.config.settings import Paths
from cpad.events.log import EventLog
from cpad.filing import view
from cpad.i18n import T
from cpad.projects.registry import ProjectsError, load_registry
from cpad.recordings.land import private_dirs

FORMAT = "cpa-export"
LAYOUT = 1
AUDIO_MAX = 32 << 20  # a segment is at most 20 MB (recording contract v1)
OTHER_MAX = 64 << 20  # files a later contract version may add
DIRS_MAX = 1_000_000  # an export is complete: no 10,000-directory scan cap
CAPS = MappingProxyType({
    "manifest.json": recordings.MANIFEST_MAX, "status.json": recordings.STATUS_MAX,
    "result.json": recordings.RESULT_MAX, "summary.md": recordings.SUMMARY_MAX,
    "transcript.json": recordings.TRANSCRIPT_MAX, "transcript.md": recordings.TRANSCRIPT_MAX,
})  # fmt: skip
README = (
    """# Cardputer Assistant 导出

这是 `cpa export` 从本机归档（~/CardputerNotes，归档布局 v1）和 $CPA_HOME 导出的明文副本。
请把它当成和原来的笔记一样敏感的数据。

- notes/YYYY-MM-DD.md：每日笔记
- recordings/<日期>/<HHMM>-<会话>/：录音目录，按录音契约 v1 原样复制
  （manifest.json、转写、纪要、result.json；导出时没加 --no-audio 才有 WAV）
- data/captures.jsonl：每行一条采集（打字笔记、排队项、便条）：id、kind、at、project、dest、text
- data/queue.jsonl：导出时还没完成的排队项
- data/projects.json：项目的 key、name、slot（不含本机路径）
- data/filing.jsonl：每段录音一行的归档状态（新、已归档、已交给 AI、看过了、藏起来；藏起来的也在），
  形状同 `cpa filing ls --json` 的行：有效项目、转写状态、标题和最多 280 字；设备只给短 id，不给序列号或 MAC
- CPA-EXPORT.json：格式、布局版本、日期范围和计数
- SHA256SUMS：每个文件的 SHA-256

校验：在这个目录里执行 `shasum -a 256 -c SHA256SUMS`。
不包含：config.json、events.jsonl 原文、日志、设备序列号、项目的本机路径。
""",
    """# Cardputer Assistant export

A plain-text copy that `cpa export` made of this Mac's archive (~/CardputerNotes, archive layout v1) and $CPA_HOME.
Treat it as data as sensitive as the notes themselves.

- notes/YYYY-MM-DD.md: the daily notes
- recordings/<date>/<HHMM>-<session>/: recording folders, copied as they are under the recording contract v1
  (manifest.json, transcripts, summaries, result.json; WAV only when the export was made without --no-audio)
- data/captures.jsonl: one capture per line (typed notes, queue items, memos): id, kind, at, project, dest, text
- data/queue.jsonl: the queue items still open at export time
- data/projects.json: each project's key, name and slot (no local paths)
- data/filing.jsonl: one line of filing state per recording (new, filed, handed to AI, done, hidden; hidden ones
  too), shaped like the rows of `cpa filing ls --json`: effective project, transcription state, title and at most
  280 characters; the device only by its short id, never its serial or MAC
- CPA-EXPORT.json: format, layout version, date range and counts
- SHA256SUMS: the SHA-256 of every file

To verify: run `shasum -a 256 -c SHA256SUMS` in this folder.
Not included: config.json, the raw events.jsonl, logs, the device serial, the projects' local paths.
""",
)  # (zh, en): written in the language of the moment of the export (cpad.i18n)
Clock = Callable[[], datetime]


class ExportError(ValueError):
    """The export was refused before anything was written; the message names the rule, not data."""


@dataclass(frozen=True, slots=True)
class ExportOptions:
    since: date | None = None
    until: date | None = None
    audio: bool = True
    allow_cloud: bool = False


@dataclass(frozen=True, slots=True)
class Entry:
    parts: tuple[str, ...]  # source, under the archive root
    dest: str  # path inside the export
    cap: int


@dataclass(frozen=True, slots=True)
class Counts:
    notes: int
    recordings: int
    files: int  # archive files copied
    bytes: int
    captures: int
    queue: int
    skipped: int
    filing: int = 0


@dataclass(frozen=True, slots=True)
class ExportReport:
    target: Path
    files: int  # every file written, SHA256SUMS included
    bytes: int
    skipped: int
    notes: int
    recordings: int
    captures: int
    queue: int


class Sink(Protocol):
    def write(self, rel: str, data: bytes) -> None: ...


# --- target ------------------------------------------------------------------------------------


def _inside(path: Path, base: Path) -> bool:
    return path == base or base in path.parents


def _real(path: Path) -> Path:
    return Path(os.path.realpath(path))


def _check_existing(path: Path, zip_file: bool) -> None:
    if not os.path.lexists(path):
        return
    if zip_file or path.is_symlink() or not path.is_dir() or any(path.iterdir()):
        raise ExportError("the target must not exist yet (a folder target may be an empty directory)")


def check_target(target: Path | str, *, notes_root: Path, cpa_home: Path, allow_cloud: bool, zip_file: bool = False,
                 user_home: Path | None = None, marks: cloud.Marks = cloud.xattr_names) -> Path:  # fmt: skip
    """The target with its parent's symlinks resolved, or ExportError."""
    given = Path(target).expanduser().absolute()
    if not given.parent.is_dir():
        raise ExportError("the target's parent directory does not exist")
    _check_existing(given, zip_file)
    resolved = _real(given.parent) / given.name
    if any(_inside(resolved, _real(base)) for base in (notes_root, cpa_home)):
        raise ExportError("the target must be outside the archive and $CPA_HOME")
    if not allow_cloud and cloud.in_cloud(resolved, user_home or Path.home(), marks):
        raise ExportError("the target syncs online (iCloud Drive, including Desktop and Documents when iCloud keeps "
                          "them, or a cloud storage folder); pass --allow-cloud to write there anyway")  # fmt: skip
    return resolved


# --- what to copy ------------------------------------------------------------------------------


def _cap(name: str) -> int:
    return CAPS.get(name, AUDIO_MAX if name.endswith(".wav") else OTHER_MAX)


def _rec_entries(root: Root, ref: recordings.RecRef, audio: bool) -> tuple[Entry, ...]:
    try:
        names = fs.list_names(root, ref.parts, kind="file")
    except ArchiveError:
        return ()
    kept = (name for name in names if audio or not name.endswith(".wav"))
    return tuple(Entry((*ref.parts, name), "/".join((*ref.parts, name)), _cap(name)) for name in kept)


def plan(root: Root, start: date | None = None, end: date | None = None, audio: bool = True) -> tuple[Entry, ...]:
    """Archive files to copy, oldest first: daily notes, then every recording (hidden ones too)."""
    days = sorted(notes.note_days(root, start, end))
    note_entries = tuple(Entry(notes.note_parts(day), f"notes/{day}.md", notes.NOTE_MAX) for day in days)
    refs = sorted(recordings.scan(root, start, end, max_dirs=DIRS_MAX).refs, key=lambda ref: ref.key)
    return note_entries + tuple(chain.from_iterable(_rec_entries(root, ref, audio) for ref in refs))


def _jsonl(rows: Iterable[Any]) -> bytes:
    return b"".join(json.dumps(row, ensure_ascii=False, separators=(",", ":")).encode() + b"\n" for row in rows)


def _json(doc: Any) -> bytes:
    return (json.dumps(doc, ensure_ascii=False, indent=2) + "\n").encode()


def _projects(paths: Paths) -> dict[str, Any]:
    try:
        projects = load_registry(paths.projects)
    except (ProjectsError, ValueError, OSError):
        projects = ()
    return {"version": 1, "projects": [{"key": p.key, "name": p.name, "slot": p.slot} for p in projects]}


def data_files(paths: Paths, root: Root, start: date | None,
               end: date | None) -> tuple[tuple[tuple[str, bytes], ...], int, int, int]:  # fmt: skip
    """(data/* files, number of captures, of open queue items, of filing rows) from events.jsonl, projects.json and
    the recordings' manifests and transcripts."""
    events = EventLog(paths.events).load()
    captures = events_view.captures(events, start, end, projects=view.Book.of(events, view.known(paths.projects))
                                    .projects())  # fmt: skip
    queue = events_view.queue_rows(events)
    filing = view.filing_rows(paths, root, start, end)
    files = (("data/captures.jsonl", _jsonl(asdict(c) for c in reversed(captures))),
             ("data/queue.jsonl", _jsonl(asdict(item) for item in queue)),
             ("data/projects.json", _json(_projects(paths))), ("data/filing.jsonl", _jsonl(filing)))  # fmt: skip
    return files, len(captures), len(queue), len(filing)


def sha256sums(entries: Iterable[tuple[str, str]]) -> str:
    """`shasum -a 256` lines for (path, hex digest) pairs, sorted by path."""
    return "".join(f"{digest}  {path}\n" for path, digest in sorted(entries))


# --- writing -----------------------------------------------------------------------------------


class _DirSink:
    def __init__(self, base: Path) -> None:
        self.base = base

    def write(self, rel: str, data: bytes) -> None:
        path = self.base / rel
        private_dirs(path.parent, self.base)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, FILE_MODE)
        with os.fdopen(fd, "wb") as fh:
            os.fchmod(fh.fileno(), FILE_MODE)
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())


class _ZipSink:
    def __init__(self, zf: zipfile.ZipFile, top: str, when: datetime) -> None:
        self.zf, self.top, self.when = zf, top, when

    def write(self, rel: str, data: bytes) -> None:
        info = zipfile.ZipInfo(f"{self.top}/{rel}", date_time=self.when.timetuple()[:6])
        info.external_attr = (stat.S_IFREG | FILE_MODE) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        self.zf.writestr(info, data)


Written = tuple[str, str, int]  # (path in the export, sha256, bytes)


def _put(sink: Sink, rel: str, data: bytes) -> Written:
    sink.write(rel, data)
    return rel, hashlib.sha256(data).hexdigest(), len(data)


def _copy(sink: Sink, root: Root, entry: Entry) -> Written | None:
    try:
        data = fs.read_bytes(root, entry.parts, entry.cap)
    except ArchiveError:
        return None
    return _put(sink, entry.dest, data)


def _counts(copied: tuple[Written | None, ...], captures: int, queue: int, filing: int) -> Counts:
    done = tuple(c for c in copied if c is not None)
    recs = {"/".join(rel.split("/")[:3]) for rel, _, _ in done if rel.startswith("recordings/")}
    return Counts(notes=sum(1 for rel, _, _ in done if rel.startswith("notes/")), recordings=len(recs),
                  files=len(done), bytes=sum(size for _, _, size in done), captures=captures, queue=queue,
                  skipped=len(copied) - len(done), filing=filing)  # fmt: skip


def _manifest(options: ExportOptions, counts: Counts, now: datetime) -> dict[str, Any]:
    return {"format": FORMAT, "layout": LAYOUT, "created_at": now.isoformat(timespec="seconds"),
            "since": options.since.isoformat() if options.since else None,
            "until": options.until.isoformat() if options.until else None, "audio": options.audio,
            "counts": asdict(counts), "cpad_version": __version__}  # fmt: skip


def _fill(sink: Sink, paths: Paths, root: Root, options: ExportOptions, now: datetime) -> ExportReport:
    copied = tuple(_copy(sink, root, e) for e in plan(root, options.since, options.until, options.audio))
    data, captures, queue, filing = data_files(paths, root, options.since, options.until)
    counts = _counts(copied, captures, queue, filing)
    extras = (*data, ("CPA-EXPORT.json", _json(_manifest(options, counts, now))), ("README.md", T(*README).encode()))
    written = tuple(c for c in copied if c is not None) + tuple(_put(sink, rel, blob) for rel, blob in extras)
    sums = _put(sink, "SHA256SUMS", sha256sums((rel, digest) for rel, digest, _ in written).encode())
    return ExportReport(target=Path(), files=len(written) + 1, bytes=sum(w[2] for w in (*written, sums)),
                        skipped=counts.skipped, notes=counts.notes, recordings=counts.recordings,
                        captures=captures, queue=queue)  # fmt: skip


def _now() -> datetime:
    return datetime.now().astimezone()


def _start(paths: Paths, root: Root, target: Path | str, options: ExportOptions, zip_file: bool) -> tuple[Path, Path]:
    """(final target, hidden sibling to build in)."""
    if options.since and options.until and options.since > options.until:
        raise ExportError("--since must not be after --until")
    final = check_target(target, notes_root=root.path, cpa_home=paths.home, allow_cloud=options.allow_cloud,
                         zip_file=zip_file)  # fmt: skip
    return final, final.parent / f".{final.name}.partial-{os.getpid()}"


def export_dir(paths: Paths, root: Root, target: Path | str, options: ExportOptions,
               clock: Clock = _now) -> ExportReport:  # fmt: skip
    final, tmp = _start(paths, root, target, options, zip_file=False)
    os.mkdir(tmp, DIR_MODE)
    try:
        os.chmod(tmp, DIR_MODE)
        report = _fill(_DirSink(tmp), paths, root, options, clock())
        os.rename(tmp, final)  # replaces an empty directory, never a full one
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    return replace(report, target=final)


def _top(final: Path) -> str:
    name = final.name[:-4] if final.name.lower().endswith(".zip") else final.name
    return name or FORMAT


def export_zip(paths: Paths, root: Root, target: Path | str, options: ExportOptions,
               clock: Clock = _now) -> ExportReport:  # fmt: skip
    final, tmp = _start(paths, root, target, options, zip_file=True)
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, FILE_MODE)
    try:
        with os.fdopen(fd, "wb") as fh:
            os.fchmod(fh.fileno(), FILE_MODE)
            now = clock()
            with zipfile.ZipFile(fh, "w", zipfile.ZIP_DEFLATED) as zf:
                report = _fill(_ZipSink(zf, _top(final), now), paths, root, options, now)
            fh.flush()
            os.fsync(fh.fileno())
        if os.path.lexists(final):
            raise ExportError("the target must not exist yet (a folder target may be an empty directory)")
        os.rename(tmp, final)
    finally:
        tmp.unlink(missing_ok=True)
    return replace(report, target=final)
