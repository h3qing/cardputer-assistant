"""Tests for the host-side file.* invariants (docs/11-m3-design.md §5.4) enforced by flows.check_flow."""

from __future__ import annotations

import base64
from pathlib import Path

import flows
import pytest
import schemas

PROTOCOL = Path(__file__).resolve().parents[1]
FID = "3QZK1M7-00042-0A1C"
OTHER = "3QZK1M7-00042-0A1D"
SIZE = 1424  # 44 + 1380: one full chunk, then 44 bytes
REVS = {"projects": 0, "sessions": 0, "queue": 0}
READY = {"n": 1, "rev": 5, "live": False}


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def host(t: int, msg: dict, **extra: object) -> dict:
    return {"t": t, "from": "host", "msg": msg, **extra}


def dev(t: int, msg: dict, **extra: object) -> dict:
    return {"t": t, "from": "device", "msg": msg, **extra}


def pong(mid: str, re: str, files: dict | None = READY) -> dict:
    msg = {"v": 0, "t": "pong", "id": mid, "re": re, "up_ms": 1, "pending": 0, "revs": REVS}
    return {**msg, "files": files} if files is not None else msg


def ping(mid: str) -> dict:
    return {"v": 0, "t": "ping", "id": mid}


def get(mid: str, off: int, fid: str = FID, length: int = 1380) -> dict:
    return {"v": 0, "t": "file.get", "id": mid, "fid": fid, "off": off, "len": length}


def data(mid: str, re: str, off: int, n: int, fid: str = FID, size: int = SIZE) -> dict:
    b64 = base64.b64encode(b"\x00" * n).decode() if n else None
    return {
        "v": 0,
        "t": "file.data",
        "id": mid,
        "re": re,
        "fid": fid,
        "off": off,
        "n": n,
        "size": size,
        "crc": 0,
        "b64": b64,
    }


def delete(mid: str, size: int = SIZE) -> dict:
    return {"v": 0, "t": "file.del", "id": mid, "fid": FID, "size": size}


def file_list(mid: str) -> dict:
    return {"v": 0, "t": "file.list", "id": mid, "max": 4}


def ack(mid: str, re: str) -> dict:
    return {"v": 0, "t": "ack", "id": mid, "re": re}


def busy(mid: str, re: str, msg: str = "live") -> dict:
    return {"v": 0, "t": "error", "id": mid, "re": re, "code": "E_BUSY", "msg": msg}


def opened(files: dict | None = READY) -> tuple[dict, ...]:
    """The host learns the device state from a ping/pong (hello works the same way)."""
    return (host(0, ping("h0")), dev(1, pong("d0", "h0", files)))


def pulled() -> tuple[dict, ...]:
    return (
        *opened(),
        host(10, get("h1", 0)),
        dev(11, data("d1", "h1", 0, 1380)),
        host(20, get("h2", 1380)),
        dev(21, data("d2", "h2", 1380, 44)),
    )


def errors(contract: schemas.Contract, *steps: dict) -> tuple[str, ...]:
    flow = {"name": "t", "spec": "test", "description": "test", "initial": {}, "steps": list(steps)}
    return flows.check_flow(contract, flow)


def one_error(contract: schemas.Contract, *steps: dict) -> str:
    problems = errors(contract, *steps)
    assert len(problems) == 1, problems
    return problems[0]


# --- file.del only after every byte ---------------------------------------------


def test_full_pull_then_delete_is_clean(contract: schemas.Contract) -> None:
    assert errors(contract, *pulled(), host(30, delete("h3")), dev(31, ack("d3", "h3"))) == ()


def test_delete_before_the_last_chunk_is_reported(contract: schemas.Contract) -> None:
    problem = one_error(contract, *pulled()[:4], host(30, delete("h3")), dev(31, ack("d3", "h3")))
    assert "h3" in problem and "byte" in problem


def test_chunk_parked_in_device_tx_does_not_count_yet(contract: schemas.Contract) -> None:
    steps = (
        *pulled()[:4],
        host(20, get("h2", 1380)),
        dev(21, data("d2", "h2", 1380, 44), deliver_t=40),
        host(30, delete("h3")),
        dev(31, ack("d3", "h3")),
    )
    assert "h3" in one_error(contract, *steps)


def test_lost_chunk_does_not_count(contract: schemas.Contract) -> None:
    steps = (*pulled()[:5], dev(21, data("d2", "h2", 1380, 44), lost=True), host(30, delete("h3")))
    assert "h3" in one_error(contract, *steps, dev(31, ack("d3", "h3")))


def test_delete_size_must_match_the_pulled_file(contract: schemas.Contract) -> None:
    steps = (*pulled(), host(30, delete("h3", size=SIZE + 2)), dev(31, ack("d3", "h3")))
    assert "h3" in one_error(contract, *steps)


def test_pulled_bytes_survive_a_reconnect(contract: schemas.Contract) -> None:
    steps = (
        *pulled(),
        {"t": 25, "event": "detach"},
        host(30, ping("h9")),
        dev(31, pong("d9", "h9")),
        host(32, delete("h3")),
        dev(33, ack("d3", "h3")),
    )
    assert errors(contract, *steps) == ()


# --- the device must advertise files, idle and indexed ---------------------------


@pytest.mark.parametrize(
    ("files", "why"),
    [(None, "files"), ({"n": 1, "rev": 5, "live": True}, "live"), ({"n": 0, "rev": 0, "live": False}, "rev")],
)
def test_file_request_needs_an_idle_indexed_device(contract: schemas.Contract, files: dict | None, why: str) -> None:
    problem = one_error(contract, *opened(files), host(10, file_list("h1")), dev(11, busy("d1", "h1")))
    assert "h1" in problem and why in problem


def test_no_file_request_before_any_hello_or_pong(contract: schemas.Contract) -> None:
    assert "h1" in one_error(contract, host(10, file_list("h1")), dev(11, busy("d1", "h1")))


def test_reset_event_forgets_what_the_device_said(contract: schemas.Contract) -> None:
    steps = (*opened(), {"t": 5, "event": "device_reboot"}, host(10, get("h1", 0)), dev(11, data("d1", "h1", 0, 1380)))
    assert "h1" in one_error(contract, *steps)


def test_debug_rec_is_not_a_file_request(contract: schemas.Contract) -> None:
    rec = {"v": 0, "t": "debug.rec", "id": "h1", "seg_ms": 30000}
    assert errors(contract, *opened(None), host(10, rec), dev(11, ack("d1", "h1"))) == ()


MIC = {"v": 0, "t": "debug.mic", "id": "h1", "pga": 10, "vol": 191, "mag": 2}


def test_debug_mic_is_not_a_file_request(contract: schemas.Contract) -> None:
    assert errors(contract, *opened(None), host(10, MIC), dev(11, ack("d1", "h1"))) == ()


def test_debug_mic_answered_by_anything_but_ack_or_error_is_reported(contract: schemas.Contract) -> None:
    assert "h1" in one_error(contract, *opened(None), host(10, MIC), dev(11, data("d1", "h1", 0, 1380)))


CODEC = {"v": 0, "t": "debug.codec", "id": "h1", "warm": False}


def test_debug_codec_is_acked_or_refused_while_live(contract: schemas.Contract) -> None:
    assert errors(contract, *opened(None), host(10, CODEC), dev(11, ack("d1", "h1"))) == ()
    assert errors(contract, *opened(None), host(10, CODEC), dev(11, busy("d1", "h1"))) == ()


def test_debug_codec_answered_by_anything_but_ack_or_error_is_reported(contract: schemas.Contract) -> None:
    assert "h1" in one_error(contract, *opened(None), host(10, CODEC), dev(11, data("d1", "h1", 0, 1380)))


# --- E_BUSY stops the host until files.live:false --------------------------------


def busy_window() -> tuple[dict, ...]:
    return (
        *opened(),
        host(10, get("h1", 0)),
        host(10, get("h2", 1380)),
        dev(11, busy("d1", "h1")),
        dev(11, busy("d2", "h2")),
    )


def test_requests_already_in_flight_when_busy_arrives_are_fine(contract: schemas.Contract) -> None:
    assert errors(contract, *busy_window()) == ()


def test_file_request_after_busy_is_reported(contract: schemas.Contract) -> None:
    problem = one_error(contract, *busy_window(), host(20, get("h3", 0)), dev(21, data("d3", "h3", 0, 1380)))
    assert "h3" in problem and "E_BUSY" in problem


def test_live_true_pong_does_not_clear_busy(contract: schemas.Contract) -> None:
    live = {"n": 1, "rev": 5, "live": True}
    steps = (*busy_window(), host(20, ping("h4")), dev(21, pong("d4", "h4", live)), host(30, get("h3", 0)))
    assert "h3" in one_error(contract, *steps, dev(31, data("d3", "h3", 0, 1380)))


def test_live_false_pong_clears_busy(contract: schemas.Contract) -> None:
    done = {"n": 2, "rev": 6, "live": False}
    steps = (*busy_window(), host(20, ping("h4")), dev(21, pong("d4", "h4", done)), host(30, get("h3", 0)))
    assert errors(contract, *steps, dev(31, data("d3", "h3", 0, 1380))) == ()


def test_busy_is_known_only_once_delivered(contract: schemas.Contract) -> None:
    steps = (*opened(), host(10, get("h1", 0)), dev(11, busy("d1", "h1"), deliver_t=50), host(20, get("h2", 0)))
    assert errors(contract, *steps, dev(21, data("d2", "h2", 0, 1380))) == ()


def test_busy_for_debug_rec_does_not_block_file_requests(contract: schemas.Contract) -> None:
    rec = {"v": 0, "t": "debug.rec", "id": "h1", "seg_ms": 30000}
    steps = (*opened(), host(10, rec), dev(11, busy("d1", "h1")), host(20, get("h2", 0)))
    assert errors(contract, *steps, dev(21, data("d2", "h2", 0, 1380))) == ()


# --- one reply of the right kind ---------------------------------------------------


@pytest.mark.parametrize(
    ("request_msg", "reply"),
    [
        (get("h7", 0), ack("d7", "h7")),
        (delete("h7"), data("d7", "h7", 0, 1380)),
        (file_list("h7"), ack("d7", "h7")),
    ],
    ids=["get-ack", "del-data", "list-ack"],
)
def test_wrong_reply_kind_is_reported(contract: schemas.Contract, request_msg: dict, reply: dict) -> None:
    steps = (*pulled(), host(30, request_msg), dev(31, reply))
    assert "h7" in one_error(contract, *steps)


@pytest.mark.parametrize(
    "reply",
    [data("d1", "h1", 1380, 44), data("d1", "h1", 0, 1380, fid=OTHER), data("d1", "h1", 0, 44)],
    ids=["offset", "fid", "n-over-len"],
)
def test_file_data_must_answer_the_request(contract: schemas.Contract, reply: dict) -> None:
    steps = (*opened(), host(10, get("h1", 0, length=40)), dev(11, reply))
    assert "h1" in one_error(contract, *steps)


def test_second_reply_to_a_file_request_is_reported(contract: schemas.Contract) -> None:
    steps = (*opened(), host(10, get("h1", 0)), dev(11, data("d1", "h1", 0, 1380)), dev(12, busy("d2", "h1")))
    assert any("h1" in p for p in errors(contract, *steps))


def test_fifth_file_request_in_flight_is_reported(contract: schemas.Contract) -> None:
    gets = tuple(host(10, get(f"h1{i}", 1380 * i)) for i in range(5))
    replies = tuple(dev(20, data(f"d1{i}", f"h1{i}", 1380 * i, 0, size=44)) for i in range(5))
    assert "h14" in one_error(contract, *opened(), *gets, *replies)


def test_answered_requests_free_their_slot(contract: schemas.Contract) -> None:
    first = tuple(host(10, get(f"h1{i}", 0)) for i in range(4))
    replies = tuple(dev(11, data(f"d1{i}", f"h1{i}", 0, 1380)) for i in range(4))
    assert (
        errors(contract, *opened(), *first, *replies, host(12, get("h9", 0)), dev(13, data("d9", "h9", 0, 1380))) == ()
    )
