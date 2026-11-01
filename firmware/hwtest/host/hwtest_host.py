"""Mac-side companion for the hwtest firmware.

Prints one short line per device event (so it can be watched live), echoes typed
lines back to the device, saves mic captures as WAV, and forwards commands that
are appended to a command file.

    uv run --with pyserial python hwtest_host.py [--cmd-file PATH] [--out DIR]

Command file lines: "!info", "!mic", or any text to show on the device screen.
With a subcommand it runs an M3 device gate instead (gate_cli.py):
    uv run --no-project --with pyserial==3.5 python hwtest_host.py xfer-sweep | cue-sweep | mic-capture ...
"""
import argparse
import base64
import json
import os
import struct
import time
from datetime import datetime

import serial
import serial.tools.list_ports as list_ports

VID_PID = (0x303A, 0x1001)


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == VID_PID:
            return p.device
    return None


def open_port(device):
    # ESP32-S3 USB-Serial/JTAG line states (TRM table, as RTS,DTR):
    #   0,0 clear download flag | 0,1 set download flag | 1,0 RESET chip | 1,1 no action.
    # macOS raises both lines on open, so lower RTS first (0,1: flag only), then DTR
    # (0,0: flag cleared) and never pass through 1,0. With both lines already low,
    # closing the port (HUPCL) changes nothing. Same order as esp-idf-monitor.
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = device, 115200, 0.05
    s.dsrdtr = True  # pyserial leaves DTR alone on open; RTS default (high) is a no-op
    s.open()
    s.rts = False
    time.sleep(0.02)
    s.dtr = False
    return s


def save_wav(path, pcm, rate=16000):
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(pcm))
    with open(path, "wb") as f:
        f.write(header + pcm)


def describe_keys(ev):
    held = ev.get("held", [])
    names = "+".join(k["k"] for k in held) or "(none)"
    matrix = " ".join(f"r{k['row']}c{k['col']}" for k in held)
    return f"KEYS n={ev.get('n')} {names}  [{matrix}]"


class Session:
    def __init__(self, ser, out_dir):
        self.ser, self.out_dir = ser, out_dir
        self.mic_chunks = []

    def send(self, text):
        self.ser.write((text + "\n").encode())

    def handle(self, ev):
        t = ev.get("t")
        if t == "hello":
            return "HELLO " + " ".join(f"{k}={v}" for k, v in ev.items() if k != "t")
        if t == "keys":
            return describe_keys(ev)
        if t == "line":
            self.send("echo: " + ev.get("text", ""))
            return f"LINE from device: {ev.get('text')!r} (echoed back)"
        if t == "rx":
            return f"ACK device received {ev.get('len')} chars"
        if t == "mic_start":
            self.mic_chunks = []
            return "MIC recording..."
        if t == "mic":
            self.mic_chunks.append(base64.b64decode(ev["b64"]))
            return None
        if t == "mic_end":
            return self.finish_mic(ev)
        return "EVENT " + json.dumps(ev, ensure_ascii=False)

    def finish_mic(self, ev):
        pcm = b"".join(self.mic_chunks)
        os.makedirs(self.out_dir, exist_ok=True)
        path = os.path.join(self.out_dir, f"mic-{datetime.now():%Y%m%d-%H%M%S}.wav")
        save_wav(path, pcm)
        stats = " ".join(f"{k}={ev[k]}" for k in ("n", "took_ms", "min", "max", "mean", "rms", "zeros"))
        return f"MIC done {stats} bytes={len(pcm)} saved={path}"


def poll_cmd_file(path, offset):
    if not path or not os.path.exists(path):
        return [], offset
    with open(path, "r", encoding="utf-8") as f:
        f.seek(offset)
        data = f.read()
        return [ln for ln in data.splitlines() if ln.strip()], f.tell()


def run(args):
    while True:
        device = find_port()
        if not device:
            time.sleep(1)
            continue
        print(f"CONNECTED {device}", flush=True)
        try:
            serve(open_port(device), args)
        except (serial.SerialException, OSError) as e:
            print(f"DISCONNECTED ({e.__class__.__name__})", flush=True)
            time.sleep(1)


def handshake(ser, session, tries=3):
    # Core 2.0.17 HWCDC can park the first reply after the host opens the port;
    # resend "!info" until a hello comes back so the TX path is known to be flowing.
    for _ in range(tries):
        session.send("!info")
        deadline, buf = time.time() + 2, b""
        while time.time() < deadline:
            buf += ser.read(4096)
            if b'"t":"hello"' in buf:
                return buf
    return buf


def serve(ser, args):
    session = Session(ser, args.out)
    buf = handshake(ser, session)
    offset = os.path.getsize(args.cmd_file) if args.cmd_file and os.path.exists(args.cmd_file) else 0
    while True:
        buf += ser.read(4096)
        *lines, buf = buf.split(b"\n")
        for raw in lines:
            raw = raw.strip()
            if not raw:
                continue
            try:
                msg = session.handle(json.loads(raw))
            except (ValueError, KeyError):
                msg = "RAW " + raw.decode("utf-8", "replace")[:200]
            if msg:
                print(msg, flush=True)
        cmds, offset = poll_cmd_file(args.cmd_file, offset)
        for c in cmds:
            session.send(c)
            print(f"SENT {c!r}", flush=True)


if __name__ == "__main__":
    import sys

    if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        import gate_cli  # M3 device gates: xfer-sweep, cue-sweep, cue-listen, mic-capture, ...

        sys.exit(gate_cli.main(sys.argv[1:]))
    ap = argparse.ArgumentParser()
    ap.add_argument("--cmd-file", default=None)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "captures"))
    run(ap.parse_args())
