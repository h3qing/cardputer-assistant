"""FakeFiles: the device side of file.* (docs/11 §4.7, §5.3, §5.4) for tests, without any transport.

handle(msg) answers one host request the way cpa-fw does: E_BUSY msg live while recording, E_NOT_READY
while the index rev is 0, paging by after/max, file.data with CRC-32 and base64, file.del idempotent
(a missing file is acked) and checked against size and sha256. Faults can be injected per test.
"""

from __future__ import annotations

import base64
import hashlib
import struct
import zlib
from dataclasses import dataclass, field


def wav_header(data_len: int) -> bytes:
    fmt = struct.pack("<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16)
    return b"RIFF" + struct.pack("<I", 36 + data_len) + b"WAVEfmt " + fmt + b"data" + struct.pack("<I", data_len)


def pcm(n_bytes: int, seed: int = 0) -> bytes:
    return bytes((seed * 7 + i * 13) % 256 for i in range(n_bytes))


@dataclass
class Segment:
    id: str
    rec: str
    seg: int
    last: bool
    mode: str
    data: bytes
    up_ms: int | None = 435000
    wall_ms: int | None = None
    project: str | None = "cpa"
    stars: tuple[int, ...] = ()
    lead_ms: int | None = None
    consent: str | None = None
    rcv: bool = False
    header: bytes | None = None
    sha: str | None = None  # a wrong device sha256, to test verification

    def blob(self) -> bytes:
        return (self.header if self.header is not None else wav_header(len(self.data))) + self.data

    def item(self) -> dict:
        at = None if self.up_ms is None else {"up_ms": self.up_ms, "wall_ms": self.wall_ms}
        out = {"id": self.id, "kind": "rec", "size": len(self.blob()), "rec": self.rec, "seg": self.seg,
               "last": self.last, "mode": self.mode, "dur_ms": len(self.data) // 32, "at": at,
               "project": self.project, "stars": list(self.stars)}  # fmt: skip
        if not self.rcv:
            out["sha256"] = self.sha or hashlib.sha256(self.data).hexdigest()
        extra = {"lead_ms": self.lead_ms, "consent": self.consent, "rcv": True if self.rcv else None}
        return {**out, **{k: v for k, v in extra.items() if v is not None}}


def memo(sid: str, n_bytes: int = 3200, **kw) -> Segment:
    return Segment(id=sid, rec=sid, seg=0, last=True, mode="memo", data=pcm(n_bytes, 1), lead_ms=150, **kw)


def meeting(ids: list[str], n_bytes: int = 4000, **kw) -> list[Segment]:
    return [Segment(id=sid, rec=ids[0], seg=i, last=i == len(ids) - 1, mode="meeting", data=pcm(n_bytes, i + 2),
                    up_ms=600000 + i * (n_bytes // 32), lead_ms=150 if i == 0 else None,
                    consent="ack" if i == 0 else None, stars=(100,) if i == 1 else (), **kw)
            for i, sid in enumerate(ids)]  # fmt: skip


@dataclass
class FakeFiles:
    segments: dict[str, Segment] = field(default_factory=dict)
    rev: int = 1
    live: bool = False
    bad_crc: dict[tuple[str, int], int] = field(default_factory=dict)  # (fid, off) -> times to corrupt
    busy_after_gets: int | None = None  # start "recording" after this many file.get
    gets: list[tuple[str, int]] = field(default_factory=list)
    dels: list[str] = field(default_factory=list)

    def add(self, *segments: Segment) -> None:
        for seg in segments:
            self.segments = {**self.segments, seg.id: seg}
            self.rev += 1

    def status(self) -> dict:
        return {"n": len(self.segments), "rev": self.rev, "live": self.live}

    def handle(self, msg: dict) -> dict:
        if self.live:
            return {"t": "error", "code": "E_BUSY", "msg": "live"}
        if self.rev == 0:
            return {"t": "error", "code": "E_NOT_READY"}
        return {"file.list": self._list, "file.get": self._get, "file.del": self._del}.get(msg["t"], self._ack)(msg)

    @staticmethod
    def _ack(_msg: dict) -> dict:
        return {"t": "ack"}

    def _list(self, msg: dict) -> dict:
        ids = sorted(i for i in self.segments if msg.get("after") is None or i > msg["after"])
        page = ids[: msg.get("max") or 4]
        return {"t": "file.items", "items": [self.segments[i].item() for i in page], "more": len(ids) > len(page)}

    def _get(self, msg: dict) -> dict:
        if self.busy_after_gets is not None and len(self.gets) >= self.busy_after_gets:
            self.live, self.busy_after_gets = True, None
            return {"t": "error", "code": "E_BUSY", "msg": "live"}
        seg = self.segments.get(msg["fid"])
        if seg is None:
            return {"t": "error", "code": "E_UNKNOWN_ITEM", "field": "fid"}
        self.gets.append((msg["fid"], msg["off"]))
        blob = seg.blob()
        chunk = blob[msg["off"] : msg["off"] + msg["len"]]
        crc = zlib.crc32(chunk)
        key = (msg["fid"], msg["off"])
        if self.bad_crc.get(key):
            self.bad_crc = {**self.bad_crc, key: self.bad_crc[key] - 1}
            crc ^= 1
        out = {"t": "file.data", "fid": seg.id, "off": msg["off"], "n": len(chunk), "size": len(blob), "crc": crc}
        return {**out, "b64": base64.b64encode(chunk).decode()} if chunk else out

    def _del(self, msg: dict) -> dict:
        seg = self.segments.get(msg["fid"])
        if seg is None:
            return {"t": "ack"}
        if msg["size"] != len(seg.blob()):
            return {"t": "error", "code": "E_BAD_FIELD", "field": "size"}
        if not seg.rcv and msg.get("sha256") not in (None, seg.item().get("sha256")):
            return {"t": "error", "code": "E_BAD_FIELD", "field": "sha256"}
        self.segments = {k: v for k, v in self.segments.items() if k != msg["fid"]}
        self.rev += 1
        self.dels.append(msg["fid"])
        return {"t": "ack"}
