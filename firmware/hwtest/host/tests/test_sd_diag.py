"""sd_diag.py: command -> reply type, and collecting a command's output over a fake port."""

import sd_diag
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


def test_reply_type_is_the_command_word():
    assert sd_diag.reply_type("!sdf3 128 131072") == "sdf3"
    assert sd_diag.reply_type("!sddiag reset") == "sddiag"
    assert sd_diag.reply_type("  !xfermk 9375") == "xfermk"


def test_run_command_keeps_progress_lines_and_the_reply():
    ser = FakeSerial([
        b'{"t":"sdfile_rep","rep":0,"ok":1}\n',
        b'{"t":"sdfile_rep","rep":1,"ok":0}\n{"t":"sdfile","good":1}\n',
    ])
    link = GateLink(ser, clock=FakeClock())
    out = sd_diag.run_command(link, "!sdfile 9375 2", timeout=60)
    assert ser.written[0] == b"!sdfile 9375 2\n"
    assert out["reply"] == {"t": "sdfile", "good": 1}
    assert [e["rep"] for e in out["events"] if e["t"] == "sdfile_rep"] == [0, 1]
    assert all("_rx" not in e for e in out["events"])


def test_run_command_without_reply_times_out():
    link = GateLink(FakeSerial([]), clock=FakeClock(step=1.0))
    out = sd_diag.run_command(link, "!sdid", timeout=5)
    assert out["reply"] is None and out["cmd"] == "!sdid"


def test_summary_line_names_the_failure():
    reply = {"t": "xfermk", "ok": 0, "wrote": 8372224, "errno": 5}
    diag = {"t": "sddiag", "diag": {"events": 2, "wfail": 1, "busy_max_ms": 512,
                                   "last": {"stage": "busy", "cmd": 25, "sector": 139808, "busy_ms": 500}}}
    assert sd_diag.summary(reply) == "xfermk ok=0 wrote=8372224 errno=5"
    line = sd_diag.summary(diag)
    assert "last=busy cmd25 sector=139808 busy_ms=500" in line and "busy_max_ms=512" in line
