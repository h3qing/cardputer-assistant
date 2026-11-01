"""Incremental reader for the hwtest byte stream (pure, no I/O).

The device sends JSON lines; !xfer adds file.data frames, either one line carrying
"b64" or a header line carrying "bin":N followed by N raw bytes and a "\\n"
(audio-capture §7.3). feed() returns the parsed events; a file.data event has its
payload dropped and carries "_n" (payload bytes) and "_ok" (CRC-32 matched) instead.
A binary frame not followed by "\\n" counts as a framing error, and reading resumes at
the next line that parses (the resync rule of audio-capture §7.3).
"""

from __future__ import annotations

import base64
import binascii
import json
import zlib


class FrameReader:
    def __init__(self):
        self.buf = bytearray()
        self.pending = None  # header of a binary frame whose payload is still arriving
        self.frames = 0
        self.crc_bad = 0
        self.framing_errors = 0
        self.noise_lines = 0
        self.wire_bytes = 0

    def feed(self, data):
        self.wire_bytes += len(data)
        self.buf += data
        out = []
        while True:
            ev = self._next()
            if ev is None:
                return out
            if ev is not False:
                out.append(ev)

    def _next(self):
        """One event, False for a consumed non-event, None when more bytes are needed."""
        if self.pending is not None:
            return self._finish_bin()
        nl = self.buf.find(b"\n")
        if nl < 0:
            return None
        raw = bytes(self.buf[:nl]).strip()
        del self.buf[: nl + 1]
        if not raw:
            return False
        try:
            ev = json.loads(raw)
        except ValueError:
            self.noise_lines += 1
            return False
        if not isinstance(ev, dict):
            self.noise_lines += 1
            return False
        if ev.get("t") != "file.data":
            return ev
        if "bin" in ev:
            self.pending = ev
            return False
        return self._check(ev, self._b64(ev.pop("b64", "")))

    def _finish_bin(self):
        n = int(self.pending["bin"])
        if len(self.buf) < n + 1:
            return None
        payload, tail = bytes(self.buf[:n]), self.buf[n]
        ev, self.pending = self.pending, None
        if tail != 0x0A:
            self.framing_errors += 1
            del self.buf[:n]  # resume at the next line that parses
            return False
        del self.buf[: n + 1]
        return self._check(ev, payload)

    @staticmethod
    def _b64(text):
        try:
            return base64.b64decode(text, validate=True)
        except (binascii.Error, ValueError):
            return None

    def _check(self, ev, payload):
        self.frames += 1
        ok = payload is not None and len(payload) == ev.get("n") and zlib.crc32(payload) == ev.get("crc")
        if not ok:
            self.crc_bad += 1
        ev["_n"] = len(payload) if payload is not None else 0
        ev["_ok"] = ok
        return ev
