"""Tap bookkeeping without a port: line splitting across reads, open markers, the lost-ack dropper."""

from harness.tap import Tap
from harness.wire import find, handshakes


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        self.t += 0.01
        return self.t


def test_rx_lines_split_across_reads_are_recorded_once_whole():
    tap = Tap(Clock())
    tap.on_open("/dev/cu.x")
    tap.on_rx(b'{"v":0,"t":"hello","id":"d1",')
    tap.on_rx(b'"re":"h1","role":"device"}\n{"v":0,"t":"ack","id":"d2","re":"h2"}\n')
    recs = tap.since()
    assert [r.dir for r in recs] == ["open", "rx", "rx"]
    assert recs[1].msg["re"] == "h1" and recs[2].msg["t"] == "ack"


def test_a_new_open_drops_a_partial_line_from_the_previous_port():
    tap = Tap(Clock())
    tap.on_open("/dev/cu.x")
    tap.on_rx(b'{"v":0,"t":"hel')
    tap.on_open("/dev/cu.x")
    tap.on_rx(b'{"v":0,"t":"ack","id":"d3","re":"h3"}\n')
    assert [r.msg["t"] for r in tap.since() if r.dir == "rx"] == ["ack"]


def test_tx_is_recorded_and_forwarded_including_blank_nudges():
    tap = Tap(Clock())
    assert tap.on_tx(b'{"v":0,"t":"ping","id":"h4"}\n') is True
    assert tap.on_tx(b"\n") is True
    assert [r.raw for r in tap.since()] == [b'{"v":0,"t":"ping","id":"h4"}\n', b"\n"]


def test_drop_next_swallows_one_ack_and_keeps_it_aside():
    tap = Tap(Clock())
    tap.drop_next("outbox.ack", 1)
    first = b'{"v":0,"t":"outbox.ack","id":"h5","re":"A","status":"stored"}\n'
    second = b'{"v":0,"t":"outbox.ack","id":"h6","re":"A","status":"duplicate"}\n'
    assert tap.on_tx(first) is False
    assert tap.on_tx(second) is True
    assert [r.msg["status"] for r in tap.dropped] == ["stored"]
    assert find(tap.since(), "tx", "outbox.ack") == 0 and tap.size() == 1


def test_drop_next_zero_disarms():
    tap = Tap(Clock())
    tap.drop_next("outbox.ack", 1)
    tap.drop_next("outbox.ack", 0)
    assert tap.on_tx(b'{"v":0,"t":"outbox.ack","id":"h7","re":"A","status":"stored"}\n') is True


def test_since_is_a_snapshot_and_feeds_handshake_timing():
    tap = Tap(Clock())
    tap.on_open("/dev/cu.x")
    tap.on_tx(b'{"v":0,"t":"hello","id":"h1","role":"host"}\n')
    snap = tap.since()
    tap.on_rx(b'{"v":0,"t":"hello","id":"d1","re":"h1","role":"device"}\n')
    assert len(snap) == 2 and tap.size() == 3
    (hs,) = handshakes(tap.since())
    assert hs.attempts == 1 and hs.ready_at is not None


# --- hold_rx: the daemon never hears the device's outbox items until the next device hello ----

ITEM = b'{"v":0,"t":"outbox.item","id":"069D183-00015-0001","kind":"note"}\n'
PONG = b'{"v":0,"t":"pong","id":"d4","re":"h4"}\n'
HELLO = b'{"v":0,"t":"hello","id":"d1","re":"h1","role":"device"}\n'


def test_on_rx_forwards_whole_lines_only():
    tap = Tap(Clock())
    assert tap.on_rx(b'{"v":0,"t":"po') == b""
    assert tap.on_rx(b'ng","id":"d4","re":"h4"}\r\n{"v"') == PONG
    assert tap.on_rx(b':0,"t":"ack","id":"d5","re":"h5"}\n') == b'{"v":0,"t":"ack","id":"d5","re":"h5"}\n'


def test_held_items_are_kept_aside_and_never_forwarded():
    tap = Tap(Clock())
    tap.hold_rx("outbox.item")
    assert tap.on_rx(ITEM + PONG) == PONG
    assert [r.msg["t"] for r in tap.since()] == ["pong"]
    assert [(r.dir, r.msg["t"]) for r in tap.dropped] == [("rx", "outbox.item")]
    assert tap.holding_rx()


def test_the_next_device_hello_ends_the_hold():
    tap = Tap(Clock())
    tap.hold_rx("outbox.item")
    assert tap.on_rx(ITEM + HELLO + ITEM) == HELLO + ITEM
    assert not tap.holding_rx()
    assert len(tap.dropped) == 1


def test_a_reopened_port_keeps_the_hold_until_the_hello():
    tap = Tap(Clock())
    tap.hold_rx("outbox.item")
    tap.on_open("/dev/cu.x")
    assert tap.on_rx(ITEM) == b""
    assert tap.on_rx(HELLO) == HELLO
    assert tap.on_rx(ITEM) == ITEM


def test_hold_rx_none_disarms():
    tap = Tap(Clock())
    tap.hold_rx("outbox.item")
    tap.hold_rx(None)
    assert tap.on_rx(ITEM) == ITEM
