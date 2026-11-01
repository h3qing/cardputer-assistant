"""Transcribing one recording folder (recording contract v1 transcriber role, docs/22 §4.6, §4.7). I/O.

Order: a manifest that cannot name its session and mode gets nothing, and so does a meeting while meetings
are off; another transcriber's folder is left alone; an invalid manifest or audio gets result.json failed
(E_VERSION / E_INVALID / E_AUDIO); a symlinked segment gets nothing. Then status.json (transcribing, by
cpa-stt) -> each run of contiguous segments through the silence gate and the command -> phantom lines
dropped -> transcript.json -> transcript.md -> result.json, each write stopping when a result.json has
appeared. Before each of the last three writes, and
all through each command run, the claim must still be ours (ctx.tick()); when it is not (or cpa-stt is
stopping) the run is killed and nothing more is written (Step "lost"). Before each of those writes
status.json must also still be ours; another program's means a second writer (Step "foreign").
"""

from __future__ import annotations

import hashlib
import os
import time
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from pathlib import Path

from cpa_stt import config, contract_out, gate, manifest, parse, runner, scan, writer
from cpa_stt.parse import Sentence
from cpad.protocol.validate import matches

MONOID = r"^[0-9A-HJKMNP-TV-Z]{7}-[0-9A-HJKMNP-TV-Z]{5}-[0-9A-HJKMNP-TV-Z]{4}$"
BLOCK = 1 << 20


@dataclass(frozen=True, slots=True)
class Ctx:
    cfg: config.SttConfig
    env: runner.Env
    now: Callable[[], str]
    tick: Callable[[], bool]
    clock: Callable[[], float] = time.time


@dataclass(frozen=True, slots=True)
class Step:
    """done | failed | gone (a result.json appeared) | foreign | skipped | command_failed | lost."""

    kind: str
    session: str | None = None
    dur_ms: int = 0
    took_ms: int = 0
    exit: int | None = None


@dataclass(frozen=True, slots=True)
class Heard:
    sentences: tuple[Sentence, ...]
    took_ms: int
    failed: runner.Outcome | None = None


def named(doc: object) -> tuple[str, str] | None:
    """(session, mode) when the manifest names them well enough to write a result for it."""
    if not isinstance(doc, dict) or not matches(MONOID, doc.get("session")) or doc.get("mode") not in manifest.MODES:
        return None
    return doc["session"], doc["mode"]


def _stt(ctx: Ctx) -> str:
    return contract_out.stt_label(ctx.cfg.argv[0] if ctx.cfg.argv else "")


def _fail(folder: scan.Folder, session: str, mode: str, code: str, ctx: Ctx, retryable: bool = False) -> Step:
    doc = contract_out.result_failed(session, mode, code, retryable, ctx.now(), _stt(ctx))
    return Step("failed" if writer.place_result(folder.path, contract_out.encode(doc)) else "gone", session)


def fail_retryable(folder: scan.Folder, ctx: Ctx, code: str = "E_STT") -> Step:
    session, mode = named(folder.doc) or ("", "memo")
    return _fail(folder, session, mode, code, ctx, retryable=True)


def process(folder: scan.Folder, ctx: Ctx) -> Step:
    found = named(folder.doc)
    if found is None:
        return Step("skipped")
    session, mode = found
    if mode == "meeting" and not ctx.cfg.meetings:
        return Step("skipped", session)
    if scan.foreign(folder.path, ctx.clock()):
        return Step("foreign", session)
    verdict = manifest.check(folder.doc)
    if not verdict.ok:
        return _fail(folder, session, mode, verdict.code, ctx)
    audio = scan.check_audio(folder.path, folder.doc)
    if audio == scan.SKIP:
        return Step("skipped", session)
    return _fail(folder, session, mode, audio, ctx) if audio else _transcribe(folder, session, mode, ctx)


def _pcm(folder: Path, run: manifest.Run) -> Iterator[bytes]:
    """The run's PCM after the skipped lead, in blocks."""
    skip = 44 + 2 * run.offset_ms * 16
    for k, seg in enumerate(run.segs):
        with os.fdopen(os.open(folder / seg.file, os.O_RDONLY | os.O_NOFOLLOW), "rb") as fh:
            fh.seek(skip if k == 0 else 44)
            while block := fh.read(BLOCK):
                yield block


def hear(folder: Path, run: manifest.Run, ctx: Ctx) -> Heard:
    if ctx.cfg.argv is None or not gate.loud_enough(_pcm(folder, run), run.samples - run.offset_ms * 16):
        return Heard((), 0)
    timeout, cap = runner.timeout_for(ctx.cfg.timeout_s, run.duration_s), runner.stdout_cap(run.duration_s)
    with runner.workdir() as wd:
        wav = folder / run.segs[0].file
        if len(run.segs) > 1:
            wav = wd / "run.wav"
            runner.write_wav(wav, [folder / s.file for s in run.segs])
        try:
            out = runner.run(ctx.cfg.argv, wav, run.offset_ms, timeout, ctx.env, wd, tick=ctx.tick, stdout_max=cap)
        except runner.SandboxMissing:
            out = runner.Outcome(None, False, b"", 0)
    if not out.ok:
        return Heard((), out.took_ms, out)
    return Heard(tuple(Sentence(run.t0_ms + s.t_ms, s.text) for s in parse.parse(out.stdout)), out.took_ms)


def _transcribe(folder: scan.Folder, session: str, mode: str, ctx: Ctx) -> Step:
    status = contract_out.status(session, "transcribing", ctx.now())
    if not writer.write_file(folder.path, "status.json", contract_out.encode(status)):
        return Step("gone", session)
    runs = manifest.runs(folder.doc)
    dur_ms = sum(r.samples for r in runs) // 16
    heard, took = (), 0
    for run in runs:
        got = hear(folder.path, run, ctx)
        took += got.took_ms
        if got.failed is not None:
            kind = "lost" if got.failed.lost else "command_failed"
            return Step(kind, session, dur_ms, took, got.failed.exit)
        heard = (*heard, *got.sentences)
    argv = ctx.cfg.argv or ()
    sentences = gate.keep(heard, (*config.prompts(argv), *ctx.cfg.drop_lines))
    kind = _write(folder, session, mode, sentences, ctx)
    return Step(kind, session, dur_ms, took, 0)


def _hold(folder: scan.Folder, ctx: Ctx) -> str | None:
    """None while the claim and status.json are still ours; else why nothing more may be written."""
    if not ctx.tick():
        return "lost"
    return None if scan.ours(folder.path) else "foreign"


def _write(folder: scan.Folder, session: str, mode: str, sentences: tuple[Sentence, ...], ctx: Ctx) -> str:
    stt = _stt(ctx)
    tj = contract_out.encode(contract_out.transcript(session, folder.doc["lead_ms"], stt, sentences))
    tm = contract_out.transcript_md(sentences).encode("utf-8")
    for name, data in (("transcript.json", tj), ("transcript.md", tm)):
        why = _hold(folder, ctx)
        if why:
            return why
        if not writer.write_file(folder.path, name, data):
            return "gone"
    outputs = {"transcript_json": hashlib.sha256(tj).hexdigest(), "transcript_md": hashlib.sha256(tm).hexdigest()}
    doc = contract_out.result_done(session, mode, outputs, stt, sentences, ctx.now())
    why = _hold(folder, ctx)
    if why:
        return why
    return "done" if writer.place_result(folder.path, contract_out.encode(doc)) else "gone"
