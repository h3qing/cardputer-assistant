"""fakewoof: a deterministic WhisperWoof for the HIL (docs/12 M3-H; contract v1, docs/11 §7.2-§7.7).

It watches <notes_dir>/recordings/<date>/<session dir>/ and handles every session with a
manifest.json and no result.json, the way WhisperWoof's Cardputer inbox would, minus speech:
  - a manifest that is not contract v1 (protocol/recording/v1 reference checker; unknown fields are
    ignored) gets result.json failed / E_INVALID or E_VERSION and nothing else;
  - with verify_audio, every seg-NNN.wav must be a regular file matching its manifest entry (size,
    canonical header, whole-file SHA-256), else failed / E_INVALID;
  - otherwise status.json (queued, transcribing, summarizing), transcript.json, transcript.md,
    summary.md and, last, result.json (state done, level summary). The "transcript" is text_for(manifest):
    one sentence per segment; a memo whose text starts with 待办 ("to-do") is a todo, any other a note.
Every file is written as .<name>.tmp, fsynced and renamed, mode 0600. Session directories that
are symlinks are ignored, and a session whose id cannot be told is left alone.
"""

from __future__ import annotations

import asyncio
import functools
import hashlib
import json
import os
import re
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

from harness.rec_checks import segment_problems

REPO = Path(__file__).resolve().parents[3]
CONTRACT = REPO / "protocol" / "recording" / "v1"
sys.path.insert(0, str(REPO / "protocol" / "recording" / "tools"))
import check_recording  # noqa: E402  (the contract's reference checker)

MONOID = re.compile(r"^[0-9A-HJKMNP-TV-Z]{7}-[0-9A-HJKMNP-TV-Z]{5}-[0-9A-HJKMNP-TV-Z]{4}\Z")
DIR_SID = re.compile(r"^[0-9]{4}-([0-9a-hjkmnp-tv-z]{7}-[0-9a-hjkmnp-tv-z]{5}-[0-9a-hjkmnp-tv-z]{4})\Z")
PIPELINE = {"app": "fakewoof/1", "stt": "fakewoof-stt-1", "llm": "fakewoof-llm-1", "prompt": "fakewoof-v1"}
TITLE_MAX = 80
MEMO_MAX = 2000
DEFAULT_TEXT = {"memo": "HIL 语音便条", "meeting": "HIL 会议"}
OUTPUTS = (("transcript_json", "transcript.json"), ("transcript_md", "transcript.md"), ("summary", "summary.md"))


@functools.cache
def _contract() -> Any:
    return check_recording.load(CONTRACT)


def check_doc(doc: str, data: Any) -> tuple[str, tuple[str, ...]]:
    """The reference checker's verdict: ("ok", ()) or (code, fields)."""
    verdict = check_recording.validate(_contract(), doc, data)
    return verdict.code, verdict.fields


def vectors(kind: str) -> list[dict[str, Any]]:
    """protocol/recording/v1/vectors/<kind>/*.json (kind: valid | invalid)."""
    return [json.loads(p.read_text(encoding="utf-8")) for p in sorted((CONTRACT / "vectors" / kind).glob("*.json"))]


# --- pure builders ---------------------------------------------------------------------------


def intent_of(text: str) -> str:
    return "todo" if text.startswith("待办") else "note"


def default_text(manifest: Mapping[str, Any]) -> str:
    return DEFAULT_TEXT.get(manifest.get("mode"), "HIL")


def md_block_ids(times_ms: Sequence[int]) -> tuple[str, ...]:
    """^tHHMMSS per sentence; the second one in the same second gets -2, the third -3 (docs/11 §7.4)."""
    out, seen = [], {}
    for t in times_ms:
        s = t // 1000
        base = f"t{s // 3600:02d}{s // 60 % 60:02d}{s % 60:02d}"
        seen = {**seen, base: seen.get(base, 0) + 1}
        out.append(base if seen[base] == 1 else f"{base}-{seen[base]}")
    return tuple(out)


def _clock(t_ms: int) -> str:
    return f"{t_ms // 60000:02d}:{t_ms // 1000 % 60:02d}"


def sentences(manifest: Mapping[str, Any], text: str) -> tuple[dict[str, Any], ...]:
    out = []
    for seg in manifest["segments"]:
        lead = manifest["lead_ms"] if seg["i"] == 0 else 0
        out.append({"t_ms": seg["t0_ms"] + lead, "text": text if seg["i"] == 0 else f"{text}（第 {seg['i'] + 1} 段）"})
    return tuple(out)


def transcript_md(sents: Sequence[Mapping[str, Any]]) -> str:
    ids = md_block_ids([s["t_ms"] for s in sents])
    return "".join(f"- [{_clock(s['t_ms'])}] {s['text']} ^{i}\n" for s, i in zip(sents, ids, strict=True))


def _link(t_ms: int, sents: Sequence[Mapping[str, Any]]) -> str:
    ids = md_block_ids([s["t_ms"] for s in sents])
    k = max([i for i, s in enumerate(sents) if s["t_ms"] <= t_ms], default=0)
    return f"[@{_clock(t_ms)}](transcript.md#^{ids[k]})"


def actions(manifest: Mapping[str, Any]) -> list[dict[str, Any]]:
    if manifest["mode"] != "meeting":
        return []
    first = manifest["marks"][0]["t_ms"] if manifest["marks"] else None
    return [{"n": 1, "text": "跟进 HIL 测试", "owner": None, "due": None, "t_ms": first, "confident": False}]


def _started(manifest: Mapping[str, Any]) -> str:
    started = manifest["started"]
    tz = timezone(timedelta(minutes=started["tz_min"]))
    return datetime.fromtimestamp(started["wall_ms"] / 1000, tz).date().isoformat()


def summary_md(manifest: Mapping[str, Any], text: str, sents: Sequence[Mapping[str, Any]]) -> str:
    project = (manifest.get("project") or {}).get("key", "")
    audio = ", ".join(s["file"] for s in manifest["segments"])
    head = (f"---\ntitle: {text[:TITLE_MAX]}\ndate: {_started(manifest)}\nsource: cardputer\n"
            f"session: {manifest['session']}\nmode: {manifest['mode']}\nproject: {project}\n"
            f"duration: {manifest['duration_ms']}\naudio: [{audio}]\ntranscript: transcript.md\n"
            f"pipeline: {PIPELINE['app']}\n---\n\n")  # fmt: skip
    if manifest["mode"] == "memo":
        return head + f"{text}\n\n类型：{'待办' if intent_of(text) == 'todo' else '记录'}\n"
    stars = "".join(f"- ★ {_link(m['t_ms'], sents)}\n" for m in manifest["marks"] if m["k"] == "star")
    todo = "".join(f"- [ ] {a['text']} {_link(a['t_ms'] or 0, sents)} ^act-{a['n']}\n" for a in actions(manifest))
    return head + (f"## 摘要\n\n{text}\n\n## 重点（你的标记）\n\n{stars}\n## 讨论要点\n\n- {text}\n\n"
                   f"## 决定\n\n- （无）\n\n## 待办\n\n{todo}\n## 后续\n\n- （无）\n")  # fmt: skip


def status_doc(session: str, state: str, at: str) -> dict[str, Any]:
    return {"v": 1, "session": session, "attempt": 1, "state": state, "at": at}


def failed_doc(session: str, mode: str, code: str, at: str) -> dict[str, Any]:
    return {"v": 1, "session": session, "attempt": 1, "state": "failed", "level": None, "mode": mode,
            "outputs": {}, "error": {"code": code, "retryable": False}, "pipeline": PIPELINE,
            "finished_at": at}  # fmt: skip


def done_doc(manifest: Mapping[str, Any], text: str, digests: Mapping[str, str], at: str) -> dict[str, Any]:
    memo = {"text": text[:MEMO_MAX], "intent": intent_of(text)} if manifest["mode"] == "memo" else None
    outputs = {key: {"path": name, "sha256": digests[name]} for key, name in OUTPUTS}
    return {"v": 1, "session": manifest["session"], "attempt": 1, "state": "done", "level": "summary",
            "mode": manifest["mode"], "title": text[:TITLE_MAX], "memo": memo, "outputs": outputs,
            "actions": actions(manifest), "pipeline": PIPELINE, "error": None, "finished_at": at}  # fmt: skip


# --- I/O ------------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Outcome:
    session: str
    folder: Path
    state: str  # done | failed
    level: str | None
    error_code: str | None
    written: tuple[str, ...]  # file names in write order


def _json(doc: Mapping[str, Any]) -> bytes:
    return (json.dumps(doc, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


def write_atomic(folder: Path, name: str, data: bytes) -> str:
    """.<name>.tmp, fsync, rename (docs/11 §7.2); 0600. Returns the hex SHA-256 of data."""
    tmp = folder / f".{name}.tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    try:
        os.write(fd, data)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.chmod(tmp, 0o600)
    os.replace(tmp, folder / name)
    return hashlib.sha256(data).hexdigest()


def audio_problems(folder: Path, manifest: Mapping[str, Any]) -> tuple[str, ...]:
    out = []
    for seg in manifest["segments"]:
        path = folder / seg["file"]
        if path.is_symlink() or not path.is_file():
            out.append(f"{seg['file']}: missing")
            continue
        out += [f"{seg['file']}: {p}" for p in segment_problems(path.read_bytes(), seg)]
    return tuple(out)


def _session_of(folder: Path, data: Any) -> str | None:
    sid = data.get("session") if isinstance(data, dict) else None
    if isinstance(sid, str) and MONOID.match(sid):
        return sid
    m = DIR_SID.match(folder.name)
    return m.group(1).upper() if m else None


def _read_manifest(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, UnicodeDecodeError):
        return None


def local_now() -> datetime:
    return datetime.now().astimezone()


@dataclass
class FakeWoof:
    root: Path  # <notes_dir>/recordings
    text_for: Callable[[Mapping[str, Any]], str] = default_text
    verify_audio: bool = True
    now: Callable[[], datetime] = local_now
    outcomes: tuple[Outcome, ...] = ()

    def pending(self) -> tuple[Path, ...]:
        if not self.root.is_dir():
            return ()
        folders = (f for day in sorted(self.root.iterdir()) if day.is_dir() and not day.is_symlink()
                   for f in sorted(day.iterdir()))  # fmt: skip
        return tuple(f for f in folders if f.is_dir() and not f.is_symlink() and (f / "manifest.json").is_file()
                     and not (f / "manifest.json").is_symlink() and not (f / "result.json").exists())  # fmt: skip

    def scan(self) -> tuple[Outcome, ...]:
        found = tuple(o for o in map(self.process, self.pending()) if o is not None)
        self.outcomes = self.outcomes + found
        return found

    async def watch(self, stop: asyncio.Event, interval: float = 0.5) -> None:
        while not stop.is_set():
            await asyncio.to_thread(self.scan)
            try:
                await asyncio.wait_for(stop.wait(), interval)
            except TimeoutError:
                pass

    def _at(self) -> str:
        return self.now().isoformat(timespec="milliseconds")

    def process(self, folder: Path) -> Outcome | None:
        data = _read_manifest(folder / "manifest.json")
        session = _session_of(folder, data)
        if session is None:
            return None
        code, _ = check_doc("manifest", data) if isinstance(data, dict) else ("E_INVALID", ("(root)",))
        if code == "ok" and self.verify_audio and audio_problems(folder, data):
            code = "E_INVALID"
        if code != "ok":
            mode = data.get("mode") if isinstance(data, dict) and data.get("mode") in DEFAULT_TEXT else "memo"
            write_atomic(folder, "result.json", _json(failed_doc(session, mode, code, self._at())))
            return Outcome(session, folder, "failed", None, code, ("result.json",))
        return self._done(folder, data)

    def _done(self, folder: Path, manifest: Mapping[str, Any]) -> Outcome:
        session, text = manifest["session"], self.text_for(manifest)
        sents = sentences(manifest, text)
        written: list[str] = []
        for state in ("queued", "transcribing"):
            write_atomic(folder, "status.json", _json(status_doc(session, state, self._at())))
            written.append("status.json")
        transcript = {"v": 1, "session": session, "attempt": 1, "lead_ms": manifest["lead_ms"],
                      "engine": {"stt": PIPELINE["stt"]}, "sentences": list(sents)}  # fmt: skip
        digests = {"transcript.json": write_atomic(folder, "transcript.json", _json(transcript)),
                   "transcript.md": write_atomic(folder, "transcript.md", transcript_md(sents).encode())}  # fmt: skip
        write_atomic(folder, "status.json", _json(status_doc(session, "summarizing", self._at())))
        summary = summary_md(manifest, text, sents).encode()
        digests = {**digests, "summary.md": write_atomic(folder, "summary.md", summary)}
        write_atomic(folder, "result.json", _json(done_doc(manifest, text, digests, self._at())))
        written += ["transcript.json", "transcript.md", "status.json", "summary.md", "result.json"]
        return Outcome(session, folder, "done", "summary", None, tuple(written))
