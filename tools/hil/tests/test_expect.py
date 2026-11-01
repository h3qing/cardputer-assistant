"""Expected values (pure): v0.2 hello fields, the --expect-launcher judgement, exactly-once delivery."""

from dataclasses import dataclass

from harness.expect import delivery, launcher_mismatch, launcher_of, store_of
from harness.notes import NoteLine


@dataclass(frozen=True)
class Hello:  # the daemon's DeviceHello shape
    store: str | None = None
    launcher: bool | None = None


def note(text, item_id):
    return NoteLine("10:00", "cpa", text, item_id)


def test_hello_fields_from_a_wire_dict_or_the_daemon_object():
    assert store_of({"t": "hello", "store": "sd"}) == "sd"
    assert launcher_of({"t": "hello", "launcher": True}) is True
    assert store_of(Hello(store="int")) == "int"
    assert launcher_of(Hello(launcher=False)) is False


def test_a_v01_hello_has_neither_field():
    assert store_of({"t": "hello"}) is None
    assert launcher_of(Hello()) is None
    assert store_of(None) is None


def test_launcher_expectation_only_when_asked():
    assert launcher_mismatch({"launcher": False}, expect=False) is None
    assert launcher_mismatch({"launcher": True}, expect=False) is None
    assert launcher_mismatch({"launcher": True}, expect=True) is None


def test_launcher_expected_but_not_reported():
    assert "launcher=False" in launcher_mismatch({"launcher": False}, expect=True)
    assert "launcher=None" in launcher_mismatch({}, expect=True)


def test_delivery_exactly_once():
    notes = (note("a1", "B-0001"), note("a2", "B-0002"), note("other", "C-0001"))
    d = delivery(("a1", "a2"), notes, ("B-0001", "B-0002"))
    assert d.ok
    assert d.ids == ("B-0001", "B-0002")
    assert d.counts == {"a1": 1, "a2": 1}
    assert (d.missing, d.doubled, d.extra_ids, d.received_twice) == ((), (), (), ())


def test_delivery_reports_missing_and_doubled_texts():
    notes = (note("a1", "B-0001"), note("a1", "B-0009"))
    d = delivery(("a1", "a2"), notes, ("B-0001", "B-0009"))
    assert not d.ok
    assert d.missing == ("a2",)
    assert d.doubled == ("a1",)
    assert d.counts == {"a1": 2, "a2": 0}


def test_delivery_reports_ids_that_are_not_the_expected_notes():
    notes = (note("a1", "B-0001"),)
    d = delivery(("a1",), notes, ("B-0001", "Z-0007"))
    assert not d.ok
    assert d.extra_ids == ("Z-0007",)


def test_delivery_reports_an_id_received_twice():
    notes = (note("a1", "B-0001"),)
    d = delivery(("a1",), notes, ("B-0001", "B-0001"))
    assert not d.ok
    assert d.received_twice == ("B-0001",)
