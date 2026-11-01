"""The I/O glue of the gate runners, driven by a fake serial port and a fake link."""

import struct

import gate_cue
import gate_mic
import gate_xfer
from gate_frames import FrameReader
from gate_link import GateLink


class FakeClock:
    def __init__(self, step=0.1):
        self.t, self.step = 0.0, step

    def __call__(self):
        self.t += self.step
        return self.t


class FakeSerial:
    def __init__(self, chunks):
        self.chunks, self.written = list(chunks), []

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, n):
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, data):
        self.written.append(data)


def test_gate_link_polls_stamps_and_collects():
    ser = FakeSerial([b'{"t":"pong","id":1}\n{"t":"chunk","n":3,"b64":"YWJj"}\n'])
    link = GateLink(ser, clock=FakeClock())
    seen = []
    got = link.collect("chunk", 1, 5.0, on_event=lambda ev, rx: seen.append(ev["t"]))
    assert got[0]["b64"] == "YWJj" and "_rx" in got[0]
    assert seen == ["pong", "chunk"]


def test_gate_link_nudges_when_silent_and_gives_up():
    ser = FakeSerial([])
    link = GateLink(ser, clock=FakeClock(step=0.5))
    assert link.wait_for("xfermk", 4.0) is None
    assert b"\n" in ser.written  # empty-line nudge (ADR-0001 §5.2)
    link.send_raw(b"!g\n")
    assert ser.written[-1] == b"!g\n"
    assert isinstance(link.reset_reader(), FrameReader)


class FakeLink:
    """Hands out scripted events: wait_for() by type, poll() in order."""

    def __init__(self, waits=None, polls=None):
        self.waits, self.polls, self.sent, self.raw = dict(waits or {}), list(polls or []), [], []
        self.reader = FrameReader()

    def reset_reader(self):
        self.reader = FrameReader()
        self.reader.wire_bytes = 4000
        return self.reader

    def send(self, text):
        self.sent.append(text)

    def send_raw(self, data):
        self.raw.append(data)

    def wait_for(self, kind, timeout):
        return self.waits.get(kind)

    def poll(self):
        return self.polls.pop(0) if self.polls else []


def test_run_xfer_keeps_credits_and_collects_everything(monkeypatch):
    t = {"now": 100.0}
    monkeypatch.setattr(gate_xfer.time, "perf_counter", lambda: t["now"])
    data = [{"t": "file.data", "_rx": 101.0 + i * 0.01, "_n": 1380, "_ok": True} for i in range(3)]
    polls = [data[:2], [{"t": "pong", "id": 1, "_rx": 101.6}], [data[2], {"t": "xs", "cpu1": 40, "_rx": 101.7}],
             [{"t": "xfer_end", "err": "", "frames": 3, "rearms": 1, "_rx": 102.0}]]
    link = FakeLink(waits={"xfer_start": {"t": "xfer_start", "_rx": 100.0}}, polls=polls)

    def tick():
        t["now"] += 0.6
        return link.polls.pop(0) if link.polls else []

    link.poll = tick
    run = gate_xfer.run_xfer(link, gate_xfer.XferConfig("b64", 1380, 2, 4, "ram", 1))
    assert link.sent[0] == "!xfer b64 1380 2 4 ram 1"
    assert link.raw[0] == b"!g\n" * 2  # initial window
    assert b"".join(link.raw[1:]) == b"!g\n" * 3  # one credit per frame
    assert len(run["rx"]) == 3 and run["xs"] == [{"t": "xs", "cpu1": 40}]
    assert run["end"]["frames"] == 3 and run["wire_bytes"] == 4000
    assert any(s.startswith("!ping") for s in link.sent)
    assert gate_xfer.summarize_run(run)["ok"] is True


def test_run_xfer_without_start_returns_empty():
    run = gate_xfer.run_xfer(FakeLink(), gate_xfer.XferConfig("bin", 4096, 0, 8, "sd", 1))
    assert run["start"] is None and run["rx"] == [] and run["end"] is None


def test_run_cue_strips_host_fields():
    link = FakeLink(waits={"cue": {"t": "cue", "start_us": 1, "_rx": 3.0}, "cue_bins": {"t": "cue_bins", "rms": [1]}})
    out = gate_cue.run_cue(link, gate_cue.CueRun("memo", 80, 64, "lockyield"))
    assert link.sent == ["!cue 80 64 memo lockyield 1000"]
    assert out == {"cue": {"t": "cue", "start_us": 1}, "bins": {"t": "cue_bins", "rms": [1]}}


def test_run_cue_skips_bins_after_an_error():
    link = FakeLink(waits={"cue": {"t": "cue", "error": "task"}, "cue_bins": {"t": "cue_bins"}})
    assert gate_cue.run_cue(link, gate_cue.CueRun("meet", 100, 64, "off"))["bins"] is None


def _wav(samples):
    pcm = struct.pack(f"<{len(samples)}h", *samples)
    head = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    head += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16)
    return head + b"data" + struct.pack("<I", len(pcm)) + pcm


def test_run_mic_pulls_and_measures(tmp_path):
    dest = tmp_path / "m.wav"

    def pull(link, path, out):
        assert path == "/sd/cpa_hwtest/mic.wav"
        with open(out, "wb") as f:
            f.write(_wav([100, -100] * 8000))
        return {"ok": True}

    link = FakeLink(waits={"sdrec": {"t": "sdrec", "result": "ok"}, "mic_done": {"t": "mic_done", "path": "/sd/cpa_hwtest/mic.wav"}})
    out = gate_mic.run_mic(link, gate_mic.MicRun(16, 6, 1), str(dest), pull)
    assert link.sent == ["!mic 16 6 1"]
    assert out["levels"]["rms_int16"] == 100.0 and out["sdrec"]["result"] == "ok"


def test_run_mic_without_reply_does_not_pull(tmp_path):
    out = gate_mic.run_mic(FakeLink(), gate_mic.MicRun(16, 0, 1), str(tmp_path / "x.wav"), pull=None)
    assert "pull" not in out and out["mic_done"] == {}
