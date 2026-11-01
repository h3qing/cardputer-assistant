"""RecPuller's pure plan (docs/11 §6.2). The I/O shell is recordings/puller.py.

- Transfer: one segment pulled with at most file_inflight_max file.get of file_get_b64_max bytes in
  flight; chunks are checked (fid, off, size, n, CRC-32) and written in order; a bad or lost chunk is
  asked again at the same offset, and the third strike on one offset gives the file up for this
  connection; an E_BUSY reply puts its offset back without a strike.
- Gate: file.* only while the latest hello / pong shows files with live:false and rev > 0, and not
  between an E_BUSY reply and a later status with live:false (receive order, link.rx_seq).
- Expect: the rev the host expects (rev at listing time plus its own acknowledged deletes); a relist
  is due, between files only, once a status showed another rev than expected at that point.
- plan(): memo sessions first, then id order; segments already stored are only deleted.
- relist_to_finish(): one more listing once the host's own deletes took the last listed segment of a
  still unfinished (cut) session off the device; the rev then stays as expected, and only a full
  listing finishes such a session (docs/11 §6.4).
"""

from __future__ import annotations

import zlib
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field, replace
from types import MappingProxyType

from cpad.protocol.file_messages import FileData, FileItem, Files
from cpad.protocol.limits import LIMITS

CHUNK: int = LIMITS["file_get_b64_max"]
INFLIGHT: int = LIMITS["file_inflight_max"]
RESUME_BACK = 64 * 1024
STRIKES = 3


def _frozen(d: Mapping) -> Mapping:
    return MappingProxyType(dict(d))


@dataclass(frozen=True, slots=True)
class Transfer:
    fid: str
    size: int
    done: int  # bytes [0, done) are in the .part file
    next_off: int  # next offset never asked yet
    inflight: tuple[int, ...] = ()
    retry: tuple[int, ...] = ()
    early: Mapping[int, bytes] = field(default_factory=lambda: _frozen({}))
    strikes: Mapping[int, int] = field(default_factory=lambda: _frozen({}))
    failed: bool = False


def resume_offset(part_size: int, size: int) -> int:
    """Where a pull continues from a .part left by an earlier connection: its size minus 64 KiB."""
    return max(0, min(part_size, size) - RESUME_BACK)


def start(fid: str, size: int, off: int) -> Transfer:
    return Transfer(fid=fid, size=size, done=off, next_off=off)


def complete(t: Transfer) -> bool:
    return not t.failed and t.done >= t.size


def issue(t: Transfer, *, paused: bool) -> tuple[Transfer, tuple[int, ...]]:
    """Offsets to ask now: retries first, then new ones, up to INFLIGHT in flight."""
    if paused or t.failed:
        return t, ()
    room = INFLIGHT - len(t.inflight)
    retries = tuple(sorted(t.retry))[:room]
    fresh = tuple(range(t.next_off, t.size, CHUNK)[: room - len(retries)])
    next_off = fresh[-1] + CHUNK if fresh else t.next_off
    offsets = retries + fresh
    return replace(t, inflight=t.inflight + offsets, retry=tuple(sorted(t.retry))[len(retries) :],
                   next_off=next_off), offsets  # fmt: skip


def _without(t: Transfer, off: int) -> Transfer:
    return replace(t, inflight=tuple(o for o in t.inflight if o != off))


def _strike(t: Transfer, off: int) -> Transfer:
    count = t.strikes.get(off, 0) + 1
    strikes = _frozen({**t.strikes, off: count})
    if count >= STRIKES:
        return replace(t, strikes=strikes, failed=True)
    return replace(t, strikes=strikes, retry=(*t.retry, off))


def _chunk_ok(t: Transfer, off: int, reply: FileData, payload: bytes) -> bool:
    expected = min(CHUNK, t.size - off)
    same = (reply.fid, reply.off, reply.n, len(payload)) == (t.fid, off, expected, expected)
    return same and zlib.crc32(payload) == reply.crc


def on_data(t: Transfer, off: int, reply: FileData) -> tuple[Transfer, bytes]:
    """Apply a file.data reply to the get at `off`; returns the bytes to append to the .part file."""
    t = _without(t, off)
    if reply.size != t.size:
        return replace(t, failed=True), b""
    payload = reply.payload()
    if not _chunk_ok(t, off, reply, payload):
        return _strike(t, off), b""
    early = {**t.early, off: payload}
    done, out = t.done, b""
    while done in early:
        piece = early.pop(done)
        done, out = done + len(piece), out + piece
    return replace(t, done=done, early=_frozen(early)), out


def on_lost(t: Transfer, off: int) -> Transfer:
    """No reply within reply_s: ask the same offset again (a strike)."""
    return _strike(_without(t, off), off)


def on_busy(t: Transfer, off: int) -> Transfer:
    """E_BUSY: ask the same offset again once the gate reopens (not a strike)."""
    return replace(_without(t, off), retry=(*t.retry, off))


def give_up(t: Transfer) -> Transfer:
    return replace(t, failed=True)


# --- gate ---------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Gate:
    files: Files | None = None
    seq: int = 0
    busy_seq: int | None = None


def observe(g: Gate, files: Files | None, seq: int) -> Gate:
    """A hello / pong received at link.rx_seq `seq`; a later live:false status clears an E_BUSY."""
    cleared = files is not None and not files.live and g.busy_seq is not None and seq > g.busy_seq
    return Gate(files=files, seq=seq, busy_seq=None if cleared else g.busy_seq)


def on_busy_gate(g: Gate, seq: int) -> Gate:
    """An E_BUSY reply received at `seq`, unless a newer status already showed live:false."""
    if g.files is not None and not g.files.live and g.seq > seq:
        return g
    return replace(g, busy_seq=max(g.busy_seq or 0, seq))


def gate_open(g: Gate) -> bool:
    return g.files is not None and not g.files.live and g.files.rev > 0 and g.busy_seq is None


# --- rev ----------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Expect:
    """What the host expects files.rev to be: the rev its listing was based on, plus one per own
    acknowledged file.del. Statuses and acks are compared in receive order (link.rx_seq)."""

    rev: int | None = None  # rev of the status the listing was based on; None = never listed
    seq: int = 0  # receive order of that status
    acks: tuple[int, ...] = ()  # receive order of own file.del acks after it
    seen: tuple[tuple[int, int], ...] = ()  # (seq, rev) of statuses received after it


def listed(e: Expect, rev: int, seq: int) -> Expect:
    """A full listing based on the status (rev, seq); later acks and statuses still count."""
    return Expect(
        rev=rev, seq=seq, acks=tuple(a for a in e.acks if a > seq), seen=tuple(x for x in e.seen if x[0] > seq)
    )


def deleted(e: Expect, seq: int) -> Expect:
    return replace(e, acks=(*e.acks, seq))


def observed(e: Expect, rev: int, seq: int) -> Expect:
    return replace(e, seen=(*e.seen, (seq, rev))) if seq > e.seq else e


def needs_relist(e: Expect) -> bool:
    """A status showed a rev other than expected at its receive order (a commit, or a reboot)."""
    if e.rev is None:
        return True
    return any(rev != e.rev + sum(a < seq for a in e.acks) for seq, rev in e.seen)


# --- ordering -----------------------------------------------------------------------------


def plan(items: Iterable[FileItem], *, stored: frozenset[str], skip: frozenset[str] = frozenset()):
    """(action, item) in pull order: memo sessions first, then id; 'delete' when already stored."""
    ordered = sorted((i for i in items if i.id not in skip), key=lambda i: (i.mode != "memo", i.id))
    return tuple(("delete" if i.id in stored else "pull", i) for i in ordered)


def relist_to_finish(listing: Iterable[FileItem], *, deleted: frozenset[str], unfinished: frozenset[str]) -> bool:
    """True when own acknowledged deletes removed every listed segment of a session that is still unfinished."""
    items = tuple(listing)
    left = {i.rec for i in items if i.id not in deleted}
    gone = {i.rec for i in items if i.id in deleted}
    return bool((gone - left) & unfinished)
