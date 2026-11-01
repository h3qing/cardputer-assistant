"""hello.store / hello.launcher (v0.2, docs/09 §6): optional device hello fields; v stays 0.

A v0.1 device sends neither field and decodes to None; a v0.2 device reports its primary
store ("sd" / "int" / "none") and whether it runs under the Launcher.
"""

from __future__ import annotations

import pytest

from cpad.device import handshake
from cpad.protocol import codec
from cpad.protocol import messages as m

V01_HELLO = {
    "v": 0, "t": "hello", "id": "d1", "re": "h1", "role": "device", "fw": "cpa-fw/0.1.0", "dev": "020000000001",
    "boot": "3QZK1M7-0000C", "up_ms": 53211, "rst": "poweron", "time": False, "pending": 3, "bat_mv": 4150,
    "revs": {"projects": 0, "sessions": 0, "queue": 0},
}  # fmt: skip


def _decode(wire: dict) -> codec.Decoded:
    return codec.decode_line(codec.encode(wire))


def test_v01_device_hello_has_no_store_or_launcher():
    hello = m.from_wire(_decode(V01_HELLO).msg)
    assert isinstance(hello, m.DeviceHello)
    assert (hello.store, hello.launcher) == (None, None)


def test_dataclass_defaults_keep_v01_construction_working():
    hello = m.DeviceHello(id="d1", re="h1", fw="cpa-fw/0.1.0", dev="020000000001", boot="3QZK1M7-0000C", up_ms=1,
                          rst="poweron", time=False, pending=0, bat_mv=4150, revs=m.Revs())  # fmt: skip
    assert (hello.store, hello.launcher) == (None, None)


@pytest.mark.parametrize(
    ("extra", "expected"),
    [
        ({"store": "sd", "launcher": True}, ("sd", True)),
        ({"store": "int", "launcher": False}, ("int", False)),
        ({"store": "none", "launcher": False}, ("none", False)),
        ({"store": "sd"}, ("sd", None)),
        ({"launcher": True}, (None, True)),
        ({"store": None, "launcher": None}, (None, None)),
    ],
)
def test_v02_device_hello_carries_store_and_launcher(extra, expected):
    decoded = _decode({**V01_HELLO, **extra})
    assert decoded.ok, decoded
    hello = m.from_wire(decoded.msg)
    assert (hello.store, hello.launcher) == expected


@pytest.mark.parametrize(
    ("extra", "field"),
    [
        ({"store": "fat"}, "store"),
        ({"store": "SD"}, "store"),
        ({"store": ""}, "store"),
        ({"store": 1}, "store"),
        ({"launcher": "yes"}, "launcher"),
        ({"launcher": 0}, "launcher"),
    ],
)
def test_unknown_store_or_launcher_is_bad_field(extra, field):
    decoded = _decode({**V01_HELLO, **extra})
    assert (decoded.outcome, decoded.fields) == ("E_BAD_FIELD", (field,))


def test_handshake_accepts_a_v02_hello():
    decoded = _decode({**V01_HELLO, "store": "sd", "launcher": True})
    assert handshake.classify(decoded, ("h1",)) == "ok"


def test_host_hello_is_unchanged():
    hello = m.HostHello(app="cpad/0.2.0", agent="ok", caps=m.Caps(spawn=True, reply=False), id="h1")
    assert set(m.to_wire(hello)) == {"v", "t", "id", "role", "app", "agent", "caps"}
