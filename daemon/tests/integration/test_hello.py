"""v0.4.1 host side of the link (docs/13 §11.1, §12.2): caps and host identity on the host hello, old firmware,
and rec.list / rec.act handed to Recordings.on_device_request with the reply sent back as re = request id."""

from __future__ import annotations

import os
import stat

from cpad.protocol import messages as m
from cpad.protocol import rec_messages as r
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import matches
from tests.support.rig import note_item, until

SID = "3QZK1M7-00009-0001"
TITLE = "客户同步：定价和交付"


def _last_hello(rig) -> dict:
    return rig.fake.host_messages("hello")[-1]


def _logs(rig) -> str:
    return "".join(p.read_text(encoding="utf-8", errors="replace") for p in rig.paths.logs.glob("cpad.log*"))


class FakeRecordings:
    """Stands in for Recordings.on_device_request (V41-D2): records the requests, answers as told."""

    def __init__(self, reply=None, error: Exception | None = None):
        self.reply, self.error, self.seen = reply, error, []

    async def __call__(self, msg):
        self.seen.append(msg)
        if self.error is not None:
            raise self.error
        return self.reply


def _serve(rig, monkeypatch, fake: FakeRecordings) -> FakeRecordings:
    monkeypatch.setattr(rig.daemon.recordings, "on_device_request", fake, raising=False)
    return fake


# --- hello ----------------------------------------------------------------------------------


async def test_the_host_hello_carries_caps_and_a_host_id_that_survives_restarts(rigs):
    rig = rigs()
    await rig.start()
    await rig.synced()
    hello = _last_hello(rig)
    assert hello["caps"] == {"spawn": True, "reply": False, "recs": True, "todo": True}
    assert matches(LIMITS["host_id_pattern"], hello["host_id"]) and "host_label" not in hello
    assert rig.paths.host_id.read_text(encoding="ascii").strip() == hello["host_id"]
    assert stat.S_IMODE(os.stat(rig.paths.host_id).st_mode) == 0o600
    hellos = len(rig.fake.host_messages("hello"))
    await rig.restart()
    await rig.ready()
    await until(lambda: len(rig.fake.host_messages("hello")) > hellos)
    assert _last_hello(rig)["host_id"] == hello["host_id"]
    await rig.halt()
    assert "cpad started" in _logs(rig) and hello["host_id"] not in _logs(rig)


async def test_cpad_creates_the_id_key_once_and_never_logs_it(rigs):
    """docs/23 §3.2: the HMAC key of short device ids, created at start like host_id; exports only read it."""
    rig = rigs()
    await rig.start()
    await rig.ready()
    key = rig.paths.id_key.read_text(encoding="ascii").strip()
    assert len(key) == 64 and stat.S_IMODE(os.stat(rig.paths.id_key).st_mode) == 0o600
    await rig.restart()
    await rig.ready()
    assert rig.paths.id_key.read_text(encoding="ascii").strip() == key
    await rig.halt()
    assert key not in _logs(rig)


async def test_the_host_label_from_config_is_sent(rigs):
    rig = rigs(config={"host_label": "Studio Mac"})
    await rig.start()
    await rig.ready()
    assert _last_hello(rig)["host_label"] == "Studio Mac"


async def test_a_v040_device_ignores_the_new_fields_and_links_normally(rigs):
    rig = rigs(device={"fw": "cpa-fw/0.4.0"})
    await rig.start()
    await rig.synced()
    assert "host_id" in _last_hello(rig) and _last_hello(rig)["caps"]["recs"] is True
    ack = await rig.request(note_item("3QZK1M7-0000C-0003"))
    assert ack["status"] == "stored" and rig.daemon.status()["link"] == "ready"
    assert rig.fake.bad_lines == []


# --- rec.list / rec.act ---------------------------------------------------------------------


async def test_rec_list_reaches_recordings_and_the_page_goes_back_with_re(rigs, monkeypatch):
    rig = rigs()
    await rig.start()
    await rig.synced()
    row = r.RecRow(rec=SID, mode="meeting", st="done", starred=True, title=TITLE)
    fake = _serve(rig, monkeypatch, FakeRecordings(r.RecItems(re="", items=(row,), next="t1792440000000-a1")))
    rig.fake.send({"t": "rec.list", "id": "d23", "filter": "all", "max": 6, "cursor": "t1792450000000-a7"})
    page = await until(lambda: next(iter(rig.fake.host_messages("rec.items")), None))
    assert page["re"] == "d23" and page["next"] == "t1792440000000-a1"
    assert page["items"] == [{"rec": SID, "mode": "meeting", "st": "done", "starred": True, "title": TITLE}]
    assert fake.seen == [r.RecList(id="d23", filter="all", max=6, cursor="t1792450000000-a7")]


async def test_rec_act_replies_go_back_with_re(rigs, monkeypatch):
    rig = rigs()
    await rig.start()
    await rig.synced()
    fake = _serve(rig, monkeypatch, FakeRecordings(m.Ack(re="")))
    ack = await rig.request({"t": "rec.act", "id": "d27", "rec": SID, "op": "tag", "project": "cpa"})
    assert (ack["t"], ack["re"]) == ("ack", "d27")
    fake.reply = m.ErrorMsg(re="", code="E_UNKNOWN_ITEM")
    unknown = await rig.request({"t": "rec.act", "id": "d28", "rec": "3QZK1M7-00009-0ZZZ", "op": "hide"})
    assert (unknown["t"], unknown["re"], unknown["code"]) == ("error", "d28", "E_UNKNOWN_ITEM")
    assert fake.seen == [r.RecAct(id="d27", rec=SID, op="tag", project="cpa"),
                         r.RecAct(id="d28", rec="3QZK1M7-00009-0ZZZ", op="hide")]  # fmt: skip


async def test_a_failing_recordings_answers_e_internal_and_keeps_the_link(rigs, monkeypatch):
    rig = rigs()
    await rig.start()
    await rig.synced()
    _serve(rig, monkeypatch, FakeRecordings(error=RuntimeError(f"cannot read {TITLE}")))
    failed = await rig.request({"t": "rec.act", "id": "d29", "rec": SID, "op": "star"})
    assert (failed["t"], failed["re"], failed["code"]) == ("error", "d29", "E_INTERNAL")
    later = await rig.request(note_item("3QZK1M7-0000C-0003"), timeout=3)
    assert later["status"] == "stored" and rig.daemon.status()["link"] == "ready"
    assert TITLE not in _logs(rig), "a failure is logged by type, never with session content"


async def test_an_invalid_rec_request_is_refused_before_recordings(rigs, monkeypatch):
    rig = rigs()
    await rig.start()
    await rig.synced()
    fake = _serve(rig, monkeypatch, FakeRecordings(m.Ack(re="")))
    too_many = await rig.request({"t": "rec.list", "id": "d30", "filter": "all", "max": 7})
    delete = await rig.request({"t": "rec.act", "id": "d31", "rec": SID, "op": "delete"})
    assert (too_many["code"], too_many["field"]) == ("E_BAD_FIELD", "max")
    assert (delete["code"], delete["field"]) == ("E_BAD_FIELD", "op")
    assert fake.seen == []


async def test_the_real_recordings_answers_through_the_link(rigs):
    """The routing meets V41-D2's Recordings: an empty history is an empty page; an unknown session is refused."""
    rig = rigs()
    await rig.start()
    await rig.synced()
    rig.fake.send({"t": "rec.list", "id": "d40", "filter": "all", "max": 6})
    page = await until(lambda: next(iter(rig.fake.host_messages("rec.items")), None))
    assert (page["re"], page["items"]) == ("d40", []) and "next" not in page
    unknown = await rig.request({"t": "rec.act", "id": "d41", "rec": SID, "op": "hide"})
    assert (unknown["t"], unknown["re"], unknown["code"]) == ("error", "d41", "E_UNKNOWN_ITEM")
