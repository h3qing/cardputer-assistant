"""Recordings end to end over a pty FakeDevice (docs/11 §3.3, §6): pull, verify, land, manifest, result, notes.

v0.4.1 (docs/13 §8, §9.3): a memo sent to todo becomes a checkbox; the recordings app lists, stars, hides
and `cpa rec unhide` brings a session back, all without touching its files.
"""

from __future__ import annotations

import asyncio
import dataclasses
import hashlib
import json

import pytest

from cpad.cli.main import main as cpa
from cpad.control import client
from cpad.control.server import API
from tests.support import fake_files as ff
from tests.support.rig import note_item, until

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

MEMO = "K3M9Q2T-00042-0A2F"
MEETING = ["K3M9Q2T-00042-0A2A", "K3M9Q2T-00042-0A2B", "K3M9Q2T-00042-0A2C"]
SHA = "a" * 64
FILE_TYPES = ("file.list", "file.get", "file.del")


def _rig(rigs, files: ff.FakeFiles | None, housekeeping_s: float = 0.2):
    return rigs(device={"fw": "cpa-fw/0.3.0", "hello_extra": {"store": "sd"}, "files": files},
                housekeeping_s=housekeeping_s)  # fmt: skip


def _events(rig, ev: str) -> list[dict]:
    return [e for e in rig.events() if e["ev"] == ev]


def _session(rig, sid: str):
    found = next(e for e in _events(rig, "rec.archived") if e["rec"] == sid)
    return rig.notes_dir / found["dir"]


def fakewoof(folder, sid: str, mode: str, intent: str = "todo") -> None:
    """The WhisperWoof side of contract v1, reduced to what cpad reads: result.json written last."""
    manifest = json.loads((folder / "manifest.json").read_text())
    assert manifest["session"] == sid
    for name in ("transcript.json", "transcript.md", "summary.md"):
        (folder / name).write_text("{}" if name.endswith(".json") else "x")
    memo = {"text": "待办：周五发报价", "intent": intent} if mode == "memo" else None
    outputs = {
        k: {"path": p, "sha256": SHA}
        for k, p in (
            ("transcript_json", "transcript.json"),
            ("transcript_md", "transcript.md"),
            ("summary", "summary.md"),
        )
    }
    result = {"v": 1, "session": sid, "attempt": 1, "state": "done", "level": "summary", "mode": mode,
              "title": "客户同步" if mode == "meeting" else "报价", "memo": memo, "outputs": outputs,
              "actions": [{"n": 1, "text": "发报价", "confident": True}], "error": None,
              "finished_at": "2026-10-21T14:49:02.000+08:00", "x_future_field": {"ok": True}}  # fmt: skip
    tmp = folder / ".result.json.tmp"
    tmp.write_text(json.dumps(result, ensure_ascii=False))
    tmp.rename(folder / "result.json")


def _daily_lines(rig) -> list[str]:
    return [line for p in rig.notes_dir.glob("*.md") for line in p.read_text().splitlines()]


async def test_memo_and_meeting_are_pulled_landed_archived_and_noted(rigs):
    files = ff.FakeFiles()
    files.add(*ff.meeting(MEETING, n_bytes=40000), ff.memo(MEMO, n_bytes=20000))
    expected = {seg.id: seg.blob() for seg in files.segments.values()}
    rig = _rig(rigs, files)
    await rig.start()
    await until(lambda: not files.segments and len(_events(rig, "rec.archived")) == 2, timeout=15)
    for sid, mode in ((MEMO, "memo"), (MEETING[0], "meeting")):
        folder = _session(rig, sid)
        manifest = json.loads((folder / "manifest.json").read_text())
        for seg in manifest["segments"]:
            data = (folder / seg["file"]).read_bytes()
            assert data == expected[seg["id"]] and hashlib.sha256(data).hexdigest() == seg["sha256"]
        assert manifest["consent"] == "ack" if mode == "meeting" else "consent" not in manifest
        fakewoof(folder, sid, mode)
    lines = await until(lambda: len(_daily_lines(rig)) == 2 and _daily_lines(rig), timeout=10)
    assert any(line.startswith("- [ ] ") and "待办：周五发报价" in line for line in lines)
    assert any("会议：[客户同步](recordings/" in line and "待办 1" in line for line in lines)
    assert _events(rig, "queue.added") == [] and len(_events(rig, "rec.result")) == 2
    rows = await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "rec"})
    assert sorted(r["state"] for r in rows["sessions"]) == ["done", "done"] and rows["api"] == API


async def test_recording_started_mid_pull_pauses_until_live_false_then_resumes_by_offset(rigs):
    files = ff.FakeFiles(busy_after_gets=10)
    files.add(ff.memo(MEMO, n_bytes=60000))
    rig = _rig(rigs, files)
    await rig.start()
    await until(lambda: files.live, timeout=10)
    await asyncio.sleep(1.5)  # at least one pong with live:true
    sent = len(rig.fake.received)
    await asyncio.sleep(0.5)
    assert [m["t"] for m in rig.fake.received[sent:] if m["t"] in FILE_TYPES] == []
    with rig.fake.lock:
        files.live = False
    await until(lambda: not files.segments, timeout=10)
    offsets = [off for _, off in files.gets]
    assert offsets[10] >= 10 * 1380 - 4 * 1380 and offsets.count(0) == 1
    assert len(_events(rig, "rec.seg.stored")) == 1


async def test_cpa_pause_mid_transfer_then_resume_without_duplicate_events(rigs):
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO, n_bytes=400000))
    rig = _rig(rigs, files)
    await rig.start()
    await until(lambda: len(files.gets) >= 60, timeout=10)
    await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "pause", "seconds": 1})
    await until(lambda: rig.daemon.link is None, timeout=5)
    paused_at = len(files.gets)
    await until(lambda: not files.segments, timeout=20)
    assert files.gets[paused_at][1] > 0  # resumed from the .part, not from 0
    # The device drops the segment when it acks file.del; cpad records the delete just after (#40).
    await until(lambda: _events(rig, "rec.seg.deleted") and _events(rig, "rec.archived"), timeout=10)
    await asyncio.sleep(0.3)  # room for a duplicate to show up
    assert len(_events(rig, "rec.seg.stored")) == 1 and len(_events(rig, "rec.seg.deleted")) == 1
    assert len(_events(rig, "rec.archived")) == 1


async def test_a_segment_listed_again_is_deleted_unpulled_and_archived_once(rigs):
    """docs/28 S1a: the device keeps a handed-over segment and may list it again (a DELIV lost to a power
    cut, or "deliver again" with this Mac's host_id unknown): cpad already stored it, so it deletes it without
    pulling a byte, and the session is archived once."""
    files = ff.FakeFiles()
    memo = ff.memo(MEMO, n_bytes=20000)
    files.add(memo)
    rig = _rig(rigs, files)
    await rig.start()
    await until(lambda: not files.segments and _events(rig, "rec.archived"), timeout=15)
    gets = len(files.gets)
    with rig.fake.lock:
        files.add(memo)  # listed again: files.rev moves
    await until(lambda: not files.segments and files.dels.count(MEMO) == 2, timeout=15)
    await asyncio.sleep(0.3)  # room for a duplicate to show up
    assert len(files.gets) == gets
    assert len(_events(rig, "rec.seg.stored")) == 1 and len(_events(rig, "rec.archived")) == 1
    assert len([p for p in (rig.notes_dir / "recordings").rglob("manifest.json")]) == 1


async def test_a_meeting_cut_at_a_segment_change_is_archived_with_gaps_on_the_same_connection(rigs):
    files = ff.FakeFiles()  # HIL rec_reboot (b): segment 0 is last:false and segment 1 never got committed
    files.add(ff.meeting(MEETING[:2], n_bytes=40000)[0])
    rig = _rig(rigs, files)
    await rig.start()
    archived = await until(lambda: _events(rig, "rec.archived"), timeout=10)
    assert archived[0]["rec"] == MEETING[0] and archived[0]["gaps"] == [1]
    assert (_session(rig, MEETING[0]) / "manifest.json").exists() and not files.segments
    assert len(rig.fake.host_messages("hello")) == 1  # no reconnect needed


async def test_a_v02_device_without_files_is_never_sent_file_requests(rigs):
    rig = _rig(rigs, None)
    await rig.start()
    await rig.ready()
    await asyncio.sleep(2.5)
    assert [m for m in rig.fake.received if m.get("t") in FILE_TYPES] == []
    status = await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "status"})
    assert status["recordings"]["on_device"] is None


# --- v0.4.1 ---------------------------------------------------------------------------------------------


@dataclasses.dataclass
class TodoMemo(ff.Segment):
    """A memo recorded with G0 held and T pressed: segment 0 carries dest "todo" (docs/13 §9.3)."""

    def item(self) -> dict:
        return {**super().item(), "dest": "todo"}


def _digests(folder) -> dict[str, str]:
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.iterdir())}


async def _ls(rig, monkeypatch, capsys, *args: str) -> list[dict] | str:
    monkeypatch.setenv("CPA_HOME", str(rig.paths.home))
    capsys.readouterr()
    assert await asyncio.to_thread(cpa, ["rec", *args]) == 0
    out = capsys.readouterr().out
    assert "报价" not in out  # never a title or any text
    return json.loads(out) if "--json" in args else out


def _reply(rig, msg_id: str) -> dict | None:
    return next(
        (x for t in ("rec.items", "ack", "error") for x in rig.fake.host_messages(t) if x["re"] == msg_id), None
    )


async def _ask(rig, msg_id: str, t: str, **fields) -> dict:
    """One request of the device's recordings app over the link, and cpad's reply to it."""
    rig.fake.send({"t": t, "id": msg_id, **fields})
    return await until(lambda: _reply(rig, msg_id), timeout=5)


async def _listed(rig, msg_id: str, **kw) -> list[dict]:
    reply = await _ask(rig, msg_id, "rec.list", **{"filter": "all", "max": 6, **kw})
    assert reply["t"] == "rec.items" and "next" not in reply
    return reply["items"]


async def test_a_todo_memo_is_listed_starred_hidden_and_unhidden(rigs, monkeypatch, capsys):
    files = ff.FakeFiles()
    files.add(TodoMemo(id=MEMO, rec=MEMO, seg=0, last=True, mode="memo", data=ff.pcm(20000, 1), lead_ms=150))
    rig = _rig(rigs, files)
    await rig.start()
    await until(lambda: _events(rig, "rec.archived"), timeout=15)
    assert _events(rig, "rec.archived")[0]["dest"] == "todo"
    folder = _session(rig, MEMO)
    fakewoof(folder, MEMO, "memo", intent="note")  # the user's T wins over WhisperWoof's intent
    lines = await until(lambda: _daily_lines(rig), timeout=10)
    assert lines[0].startswith("- [ ] ") and "待办：周五发报价" in lines[0] and "语音：" not in lines[0]
    before = _digests(folder)

    [row] = await _listed(rig, "d2")
    assert (row["rec"], row["st"], row["title"], row["todo_n"], row["project"]) == (MEMO, "done", "报价", 1, "cpa")
    assert (await _ask(rig, "d3", "rec.act", rec=MEMO, op="star"))["t"] == "ack"
    assert [r["rec"] for r in await _listed(rig, "d4", filter="starred")] == [MEMO]
    assert [(r["sid"], r["starred"]) for r in await _ls(rig, monkeypatch, capsys, "ls", "--json")] == [(MEMO, True)]

    assert (await _ask(rig, "d5", "rec.act", rec=MEMO, op="hide"))["t"] == "ack"
    assert await _listed(rig, "d6") == [] and await _ls(rig, monkeypatch, capsys, "ls", "--json") == []
    hidden = (await _ls(rig, monkeypatch, capsys, "ls", "--hidden")).splitlines()[1]
    assert hidden.startswith(MEMO) and "★" in hidden and hidden.endswith("done (hidden)")
    unknown = await _ask(rig, "d7", "rec.act", rec="K3M9Q2T-00042-0ZZZ", op="hide")
    assert (unknown["t"], unknown["code"]) == ("error", "E_UNKNOWN_ITEM")

    await _ls(rig, monkeypatch, capsys, "unhide", MEMO)
    assert [r["sid"] for r in await _ls(rig, monkeypatch, capsys, "ls", "--json")] == [MEMO]
    await until(lambda: rig.daemon.recordings.index.edits[MEMO].hidden is False, timeout=5)
    assert [r["rec"] for r in await _listed(rig, "d8")] == [MEMO]
    assert _digests(folder) == before and len(_daily_lines(rig)) == 1


async def _unhidden_by_the_cli(rigs, monkeypatch, capsys):
    """A pulled memo the device hid, then `cpa rec unhide` from another process; housekeeping never polls."""
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO, n_bytes=20000))
    rig = _rig(rigs, files, housekeeping_s=600.0)
    await rig.start()
    await until(lambda: _events(rig, "rec.archived"), timeout=15)
    await rig.synced()
    assert (await _ask(rig, "d1", "rec.act", rec=MEMO, op="hide"))["t"] == "ack"
    assert await _listed(rig, "d2") == []
    await _ls(rig, monkeypatch, capsys, "unhide", MEMO)
    return rig


async def test_an_unhide_read_back_by_a_hub_append_reaches_the_device_list(rigs, monkeypatch, capsys):
    """docs/13 §8.4: a typed note's append reads the CLI's rec.unhidden first; Recordings must get it too."""
    rig = await _unhidden_by_the_cli(rigs, monkeypatch, capsys)
    assert (await rig.request(note_item("3QZK1M7-0000C-0003")))["status"] == "stored"
    assert [r["rec"] for r in await _listed(rig, "d3")] == [MEMO]
    assert rig.daemon.recordings.index.edits[MEMO].hidden is False


async def test_an_unhide_read_back_by_the_reconnect_append_reaches_the_device_list(rigs, monkeypatch, capsys):
    """The device.seen of a reconnect (Daemon._bind) reads the CLI's rec.unhidden first."""
    rig = await _unhidden_by_the_cli(rigs, monkeypatch, capsys)
    await asyncio.to_thread(client.request, rig.paths.sock, {"cmd": "pause", "seconds": 1})
    await until(lambda: len(_events(rig, "device.seen")) == 2, timeout=8)
    await rig.ready()
    assert [r["rec"] for r in await _listed(rig, "d3")] == [MEMO]
