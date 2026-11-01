"""Queries over the serial tap's record of both directions (pure)."""

from harness.wire import (
    Dropper,
    Handshake,
    Rec,
    RxHold,
    decode,
    exchanges,
    find,
    handshakes,
    latencies,
    make_rec,
    not_ready,
    nudges,
    reply_to,
    stuck,
)


def tx(t, line):
    return make_rec(t, "tx", line.encode() + b"\n")


def rx(t, line):
    return make_rec(t, "rx", line.encode() + b"\n")


RECS = (
    tx(0.00, '{"v":0,"t":"hello","id":"h1","role":"host"}'),
    rx(0.06, '{"v":0,"t":"hello","id":"d1","re":"h1","role":"device","boot":"B1"}'),
    tx(0.10, '{"v":0,"t":"debug.state","id":"h2"}'),
    tx(0.20, ""),
    rx(0.70, '{"v":0,"t":"debug.state","id":"d2","re":"h2","up_ms":5}'),
    rx(0.80, '{"v":0,"t":"outbox.item","id":"ABC","kind":"note"}'),
    tx(0.81, '{"v":0,"t":"outbox.ack","id":"h3","re":"ABC","status":"stored"}'),
    tx(0.90, '{"v":0,"t":"ping","id":"h4"}'),
)


def test_decode_reads_json_objects_and_nothing_else():
    assert decode(b'{"a":1}\n') == {"a": 1}
    assert decode(b"\n") is None
    assert decode(b"boot log\n") is None
    assert decode(b"[1]\n") is None
    assert decode(b'{"a":\xff}\n') is None


def test_make_rec_keeps_raw_bytes_and_the_decoded_message():
    r = rx(1.5, '{"t":"ack"}')
    assert r == Rec(1.5, "rx", b'{"t":"ack"}\n', {"t": "ack"})


def test_find_by_direction_type_and_predicate():
    assert find(RECS, "tx", "debug.state") == 2
    assert find(RECS, "rx", "hello") == 1
    assert find(RECS, "tx", "hello", start=1) is None
    assert find(RECS, "rx", "hello", where=lambda m: m.get("boot") == "B2") is None


def test_reply_to_matches_re():
    assert reply_to(RECS, "h2").msg["up_ms"] == 5
    assert reply_to(RECS, "h4") is None
    assert reply_to(RECS, "h2", start=5) is None


def test_exchanges_pair_host_requests_with_device_replies_only():
    ex = exchanges(RECS)
    assert [(e.id, e.t, e.sent_at, e.reply_at) for e in ex] == [
        ("h1", "hello", 0.0, 0.06),
        ("h2", "debug.state", 0.1, 0.7),
        ("h4", "ping", 0.9, None),
    ]


def test_exchanges_window_by_index():
    assert [e.id for e in exchanges(RECS, start=2, end=5)] == ["h2"]


def test_latencies_skip_unanswered():
    assert latencies(exchanges(RECS)) == [0.06, 0.6]


def test_stuck_counts_late_and_missing_replies():
    ex = exchanges(RECS)
    assert [e.id for e in stuck(ex, now=1.0, limit_s=0.5)] == ["h2"]
    assert [e.id for e in stuck(ex, now=2.0, limit_s=0.5)] == ["h2", "h4"]
    assert stuck(ex, now=2.0, limit_s=5.0) == ()


def test_nudges_are_blank_host_lines():
    assert nudges(RECS) == 1
    assert nudges(RECS, start=4) == 0


def test_dropper_drops_only_the_first_n_of_a_type():
    d = Dropper("outbox.ack", 1)
    d, first = d.decide({"t": "outbox.ack"})
    d, second = d.decide({"t": "outbox.ack"})
    _, other = Dropper("outbox.ack", 1).decide({"t": "ping"})
    assert (first, second, other) == (True, False, False)
    assert d.remaining == 0


def test_dropper_ignores_lines_that_are_not_messages():
    d, dropped = Dropper("outbox.ack", 1).decide(None)
    assert dropped is False and d.remaining == 1


def test_handshakes_time_each_port_open_to_the_device_hello():
    recs = (
        make_rec(0.0, "open", b"/dev/cu.usbmodem1101"),
        tx(0.01, '{"v":0,"t":"hello","id":"h1","role":"host"}'),
        tx(2.01, '{"v":0,"t":"hello","id":"h2","role":"host"}'),
        rx(2.07, '{"v":0,"t":"hello","id":"d1","re":"h2","role":"device","boot":"B1","up_ms":900}'),
        tx(2.10, '{"v":0,"t":"time.set","id":"h3"}'),
        make_rec(10.0, "close", b"/dev/cu.usbmodem1101"),
        make_rec(11.0, "open", b"/dev/cu.usbmodem1101"),
        tx(11.01, '{"v":0,"t":"hello","id":"h4","role":"host"}'),
        make_rec(17.0, "open", b"/dev/cu.usbmodem1101"),
        tx(17.01, '{"v":0,"t":"hello","id":"h5","role":"host"}'),
        rx(17.05, '{"v":0,"t":"hello","id":"d1","re":"h5","role":"device","boot":"B1","up_ms":15900}'),
    )
    hs = handshakes(recs)
    assert hs[0] == Handshake(0.0, 2.07, 2, {"v": 0, "t": "hello", "id": "d1", "re": "h2", "role": "device",
                                              "boot": "B1", "up_ms": 900})  # fmt: skip
    assert hs[1] == Handshake(11.0, None, 1, None)
    assert (hs[2].ready_at, hs[2].attempts, hs[2].hello["up_ms"]) == (17.05, 1, 15900)
    assert round(hs[0].seconds, 3) == 2.07 and hs[1].seconds is None


def test_not_ready_counts_device_e_not_ready_replies_only():
    recs = (
        rx(1.0, '{"v":0,"t":"error","id":"d9","re":"h9","code":"E_NOT_READY"}'),
        rx(1.1, '{"v":0,"t":"error","id":"d10","re":"h10","code":"E_BAD_FIELD"}'),
        tx(1.2, '{"v":0,"t":"error","id":"h11","re":"d3","code":"E_NOT_READY"}'),
        rx(1.3, '{"v":0,"t":"error","id":"d11","re":"h12","code":"E_NOT_READY"}'),
    )
    assert not_ready(recs) == 2
    assert not_ready(recs, start=1) == 1
    assert not_ready(RECS) == 0


def test_rx_hold_drops_its_type_until_a_device_hello():
    hold = RxHold("outbox.item")
    assert hold.decide({"t": "outbox.item", "id": "A"}) == (hold, True)
    assert hold.decide({"t": "pong", "re": "h1"}) == (hold, False)
    assert hold.decide(None) == (hold, False)
    assert hold.decide({"t": "hello", "re": "h9"}) == (None, False)


def test_rx_hold_ignores_a_host_hello_without_re():
    hold = RxHold("outbox.item")
    assert hold.decide({"t": "hello", "role": "host"}) == (hold, False)
