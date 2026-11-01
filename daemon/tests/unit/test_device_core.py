"""Pure device-link rules: line splitting, credit window, handshake verdicts, reboot detection, port choice."""

from __future__ import annotations

from hypothesis import given
from hypothesis import strategies as st

from cpad.device import credit, framing, handshake, port_finder
from cpad.protocol import codec

# --- framing (05 §5.8 LineFramer rules, host side) -------------------------------------------


def _split_all(chunks):
    state, out = framing.Splitter(), []
    for chunk in chunks:
        state, lines = framing.split(state, chunk)
        out += lines
    return state, out


def test_split_joins_chunks_and_drops_carriage_returns():
    _, lines = _split_all([b'{"a":', b"1}\r\n{", b'"b":2}\n'])
    assert lines == [b'{"a":1}\n', b'{"b":2}\n']


def test_partial_line_waits_for_newline():
    state, lines = _split_all([b'{"a":1}'])
    assert lines == [] and state.buf == b'{"a":1}'


def test_oversize_line_is_reported_once_and_the_rest_discarded():
    big = b"{" + b"x" * 5000 + b"\n"
    state, lines = _split_all([big[:3000], big[3000:], b'{"ok":1}\n'])
    assert len(lines) == 2
    assert codec.decode_line(lines[0]).outcome == "E_TOO_LARGE"
    assert lines[1] == b'{"ok":1}\n' and state == framing.Splitter()


def test_line_of_exactly_line_max_bytes_passes():
    line = b"{" + b"x" * 2046 + b"\n"
    _, lines = _split_all([line])
    assert lines == [line]


@given(st.lists(st.binary(max_size=64), max_size=20))
def test_split_never_loses_complete_short_lines(chunks):
    data = b"".join(chunks).replace(b"\r", b"")
    _, lines = _split_all(chunks)
    assert b"".join(lines) == data[: data.rfind(b"\n") + 1]


# --- credit window (05 §6.3 rule 3; same semantics as protocol/tools/flows.py) -------------


def test_credit_counts_unanswered_bytes_until_reclaimed():
    c = credit.Credit().sent("h1", 1000, now=0.0).sent("h2", 1500, now=0.0)
    assert c.used(now=0.0) == 2500
    assert not c.can_send(2000, now=0.0)
    c = c.answered("h1")
    assert c.used(now=1.0) == 1500 and c.can_send(2000, now=1.0)
    assert c.used(now=4.999) == 1500
    assert c.used(now=5.0) == 0


def test_credit_prune_forgets_answered_and_expired_entries():
    c = credit.Credit().sent("h1", 10, now=0.0).sent("h2", 10, now=4.0).answered("h2")
    assert c.prune(now=5.0) == credit.Credit()


def test_credit_answer_for_unknown_id_changes_nothing():
    c = credit.Credit().sent("h1", 10, now=0.0)
    assert c.answered("zz") == c


def test_credit_limit_is_inclusive():
    c = credit.Credit().sent("h1", 4000, now=0.0)
    assert c.can_send(96, now=0.0) and not c.can_send(97, now=0.0)


# --- handshake verdicts and reboot detection (05 §6.4, US-04) -------------------------------

HELLO = (
    b'{"v":0,"t":"hello","id":"d1","re":"h1","role":"device","fw":"cpa-fw/0.1.0","dev":"020000000001",'
    b'"boot":"3QZK1M7-0000C","up_ms":53211,"rst":"poweron","time":false,"pending":3,"bat_mv":4150,'
    b'"revs":{"projects":0,"sessions":0,"queue":0}}\n'
)


def test_hello_reply_to_any_attempt_is_accepted():
    assert handshake.classify(codec.decode_line(HELLO), ("h0", "h1")) == "ok"
    assert handshake.classify(codec.decode_line(HELLO), ("h2",)) == "unrelated"


def test_foreign_firmware_prefix_is_foreign():
    line = HELLO.replace(b"cpa-fw/0.1.0", b"other-fw/1.0")
    assert handshake.classify(codec.decode_line(line), ("h1",)) == "foreign"


def test_version_mismatch_is_detected_from_error_and_from_v():
    error = b'{"v":0,"t":"error","id":"d1","re":"h1","code":"E_VERSION"}\n'
    assert handshake.classify(codec.decode_line(error), ("h1",)) == "version"
    assert handshake.classify(codec.decode_line(HELLO.replace(b'"v":0', b'"v":1')), ("h1",)) == "version"


def test_noise_and_other_replies_are_unrelated():
    assert handshake.classify(codec.decode_line(b"garbage\n"), ("h1",)) == "unrelated"
    ack = b'{"v":0,"t":"ack","id":"d1","re":"h1"}\n'
    assert handshake.classify(codec.decode_line(ack), ("h1",)) == "foreign"


def test_reboot_detection_rules():
    seen = handshake.Seen(boot="3QZK1M7-0000C", up_ms=53211)
    assert not handshake.is_reboot(None, "3QZK1M7-0000C", 1)
    assert handshake.is_reboot(seen, "3QZK1M7-0000D", 99999)
    assert handshake.is_reboot(seen, "3QZK1M7-0000C", 100)
    assert handshake.is_reboot(seen, None, 100)
    assert not handshake.is_reboot(seen, None, 60000)
    assert not handshake.is_reboot(handshake.Seen(None, 5), "3QZK1M7-0000C", 6)


# --- port choice (05 §7.2 PortFinder, US-04 AC1/AC2) ----------------------------------------


def _port(device, serial, vid=0x303A, pid=0x1001):
    return port_finder.PortInfo(device=device, serial=serial, vid=vid, pid=pid)


def test_only_espressif_usb_jtag_ports_are_candidates():
    ports = (_port("/dev/cu.a", "X", vid=0x1234), _port("/dev/cu.b", "Y", pid=0x0002), _port("/dev/cu.c", "Z"))
    assert port_finder.choose(ports, None, port_finder.Backoff(), now=0.0).device == "/dev/cu.c"


def test_bound_serial_ignores_every_other_board():
    ports = (_port("/dev/cu.a", "OTHER"), _port("/dev/cu.b", "MINE"))
    assert port_finder.choose(ports, "MINE", port_finder.Backoff(), now=0.0).device == "/dev/cu.b"
    assert port_finder.choose(ports[:1], "MINE", port_finder.Backoff(), now=0.0) is None


def test_backoff_skips_a_port_for_30_seconds():
    ports = (_port("/dev/cu.a", "A"), _port("/dev/cu.b", "B"))
    backoff = port_finder.Backoff().hold(ports[0], now=100.0, seconds=30.0)
    assert port_finder.choose(ports, None, backoff, now=110.0).device == "/dev/cu.b"
    assert port_finder.choose(ports[:1], None, backoff, now=129.9) is None
    assert port_finder.choose(ports[:1], None, backoff, now=130.0).device == "/dev/cu.a"


def test_backoff_key_falls_back_to_the_device_path():
    port = _port("/dev/cu.x", None)
    backoff = port_finder.Backoff().hold(port, now=0.0, seconds=30.0)
    assert backoff.blocked(port, now=1.0) and not backoff.blocked(port, now=31.0)


def test_list_ports_adapts_pyserial(monkeypatch):
    class Info:
        device, serial_number, vid, pid = "/dev/cu.usbmodem1101", "02:00:00:00:00:01", 0x303A, 0x1001

    monkeypatch.setattr(port_finder, "comports", lambda: [Info()])
    assert port_finder.list_ports() == (_port("/dev/cu.usbmodem1101", "02:00:00:00:00:01"),)
