import base64
import json
import zlib

from gate_frames import FrameReader


def b64_frame(seq, payload, crc=None):
    head = {
        "v": 0,
        "t": "file.data",
        "id": f"d{seq}",
        "re": f"h{seq}",
        "off": 0,
        "n": len(payload),
        "size": 9600044,
        "crc": zlib.crc32(payload) if crc is None else crc,
        "b64": base64.b64encode(payload).decode(),
    }
    return json.dumps(head, separators=(",", ":")).encode() + b"\n"


def bin_frame(seq, payload, crc=None, tail=b"\n"):
    head = {
        "v": 0,
        "t": "file.data",
        "id": f"d{seq}",
        "n": len(payload),
        "crc": zlib.crc32(payload) if crc is None else crc,
        "bin": len(payload),
    }
    return json.dumps(head, separators=(",", ":")).encode() + b"\n" + payload + tail


def test_plain_json_lines_come_out_as_events():
    r = FrameReader()
    out = r.feed(b'{"t":"pong","id":3}\n{"t":"xs","s":0}\n')
    assert [e["t"] for e in out] == ["pong", "xs"]


def test_b64_frame_is_decoded_and_crc_checked():
    r = FrameReader()
    good = r.feed(b64_frame(1, b"hello world"))[0]
    bad = r.feed(b64_frame(2, b"hello world", crc=5))[0]
    assert good["t"] == "file.data" and good["_n"] == 11 and good["_ok"] is True
    assert "b64" not in good  # payload is not kept
    assert bad["_ok"] is False
    assert r.crc_bad == 1 and r.frames == 2


def test_bin_frame_split_across_feeds():
    payload = bytes(range(256)) * 4
    blob = bin_frame(7, payload) + b'{"t":"pong","id":1}\n'
    r = FrameReader()
    events = []
    for i in range(0, len(blob), 37):
        events += r.feed(blob[i : i + 37])
    assert [e["t"] for e in events] == ["file.data", "pong"]
    assert events[0]["_ok"] is True and events[0]["_n"] == 1024
    assert r.wire_bytes == len(blob)


def test_bin_payload_containing_newlines_is_not_split():
    payload = b"\n" * 100
    r = FrameReader()
    out = r.feed(bin_frame(1, payload))
    assert len(out) == 1 and out[0]["_ok"] is True


def test_bin_frame_without_trailing_newline_resyncs():
    r = FrameReader()
    out = r.feed(bin_frame(1, b"abcd", tail=b"X") + b'garbage\n{"t":"pong","id":9}\n')
    kinds = [e["t"] for e in out]
    assert "pong" in kinds
    assert r.framing_errors == 1


def test_noise_lines_are_counted_not_raised():
    r = FrameReader()
    out = r.feed(b"\x00\xffnot json\n\n" + b'{"t":"pong"}\n')
    assert [e["t"] for e in out] == ["pong"]
    assert r.noise_lines == 1


def test_non_object_json_is_noise():
    r = FrameReader()
    assert r.feed(b"[1,2]\n42\n") == []
    assert r.noise_lines == 2


def test_b64_frame_with_bad_base64_counts_as_crc_bad():
    r = FrameReader()
    line = b'{"t":"file.data","n":3,"crc":0,"b64":"%%%"}\n'
    out = r.feed(line)
    assert out[0]["_ok"] is False and r.crc_bad == 1
