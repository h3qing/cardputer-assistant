"""Serial link for the M3 gate runners: JSON lines plus !xfer binary frames.

Compatible with sd_run.Link (send / events / collect), so sd_run.pull and
sd_run.handshake work over it; adds poll(), wait_for() and send_raw().
"""

from __future__ import annotations

import time

from gate_frames import FrameReader


class GateLink:
    def __init__(self, ser, clock=time.perf_counter):
        self.ser, self.clock = ser, clock
        self.reader = FrameReader()

    def reset_reader(self):
        self.reader = FrameReader()
        return self.reader

    def send(self, text):
        self.ser.write((text + "\n").encode())

    def send_raw(self, data):
        self.ser.write(data)

    def poll(self):
        """Whatever arrived (waits up to the port timeout), stamped with "_rx"."""
        data = self.ser.read(max(1, self.ser.in_waiting))
        if not data:
            return []
        now = self.clock()
        evs = self.reader.feed(data)
        for ev in evs:
            ev["_rx"] = now
        return evs

    def events(self, secs, until=None):
        out, deadline = [], self.clock() + secs
        while self.clock() < deadline:
            out += self.poll()
            if until and any(until(e) for e in out):
                break
        return out

    def wait_for(self, kind, timeout):
        """First event of one type; nudges with an empty line after 2 s of silence."""
        got = self.collect(kind, 1, timeout)
        return got[0] if got else None

    def collect(self, kind, count, timeout, on_event=None):
        got, deadline = [], self.clock() + timeout
        while len(got) < count and self.clock() < deadline:
            evs = self.events(2.0, until=lambda e: e.get("t") == kind)
            if not evs:
                self.send("")
            for ev in evs:
                if on_event:
                    on_event(ev, ev["_rx"])
                if ev.get("t") == kind:
                    got.append(ev)
        return got
