"""The environment stamp end to end over FakeFiles (docs/23 §4.2, P1-D1): which Mac pulled a recording (its
user-given name) and whether the device was linked to it when the recording started ("here" only when provable).
Computed in memory when segment 0 lands, then copied into rec.seg.stored, manifest.json and rec.archived."""

from __future__ import annotations

import json
import logging

import pytest

from cpad.config.settings import Config
from cpad.recordings import stamp
from tests.support import fake_files as ff
from tests.support.rec_shell import Shell, until

MEMO = "K3M9Q2T-00042-0A1F"


class _Records(logging.Handler):
    def __init__(self) -> None:
        super().__init__(logging.DEBUG)
        self.records: list[logging.LogRecord] = []

    def emit(self, record: logging.LogRecord) -> None:
        self.records.append(record)


@pytest.fixture
def logged():
    """Every record of the cpad loggers, whatever an earlier test did to propagation."""
    handler, logger = _Records(), logging.getLogger("cpad")
    logger.addHandler(handler)
    try:
        yield handler.records
    finally:
        logger.removeHandler(handler)


def _shell(tmp_path, files: ff.FakeFiles, **config) -> Shell:
    return Shell(tmp_path, files, config=Config(notes_dir=str(tmp_path / "notes"), **config))


def _stamps(shell: Shell, sid: str) -> tuple:
    seg0 = next(e for e in shell.events("rec.seg.stored") if e["rec"] == sid and e["seg"] == 0)
    manifest = json.loads((shell.session(sid) / "manifest.json").read_text())
    archived = next(e for e in shell.archived() if e["rec"] == sid)
    return seg0.get("env", "absent"), manifest.get("env", "absent"), archived.get("env", "absent")


async def test_a_memo_recorded_while_linked_is_here_and_names_this_mac(tmp_path):
    files = ff.FakeFiles()
    shell = _shell(tmp_path, files, host_label="公司")
    async with shell.connected():  # hello at up_ms 900000
        files.add(ff.memo(MEMO, up_ms=905000))  # recorded at the desk, linked
        shell.sim.pong(up_ms=910000)
        await until(lambda: shell.archived())
    want = {"host": "公司", "link": "here"}
    assert _stamps(shell, MEMO) == (want, want, want)


async def test_a_pocket_memo_pulled_later_is_unknown_and_an_unnamed_mac_is_null(tmp_path):
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO, up_ms=435000))  # recorded before the hello: not linked to this Mac
    shell = _shell(tmp_path, files)
    async with shell.connected():
        await until(lambda: shell.archived())
    want = {"host": None, "link": "unknown"}
    assert _stamps(shell, MEMO) == (want, want, want)


async def test_a_new_hello_between_recording_and_pull_gives_unknown_never_a_false_here(tmp_path):
    files = ff.FakeFiles()
    shell = _shell(tmp_path, files, rec_pull=False)
    async with shell.connected():  # linked while recording, but nothing is pulled on this link
        files.add(ff.memo(MEMO, up_ms=905000))
        shell.sim.pong(up_ms=910000)
        await shell.settle()
    shell.config = Config(notes_dir=shell.config.notes_dir)
    shell.hello_up_ms = 950000  # the link dropped and came back: a new hello resets link_since
    async with shell.connected():
        await until(lambda: shell.archived())
    assert _stamps(shell, MEMO)[1] == {"host": None, "link": "unknown"}


async def test_a_failing_stamp_is_null_and_never_holds_up_the_landing(tmp_path, monkeypatch, logged):
    def broken(**_kw):
        raise RuntimeError("SECRET-HOST 公司")

    monkeypatch.setattr(stamp, "env", broken)
    files = ff.FakeFiles()
    files.add(ff.memo(MEMO))
    shell = _shell(tmp_path, files, host_label="公司")
    async with shell.connected():
        await until(lambda: shell.archived() and files.dels)
    assert _stamps(shell, MEMO) == (None, "absent", "absent")
    assert files.dels == [MEMO], "the segment still landed and was deleted from the device"
    failed = [vars(r) for r in logged if "stamp" in r.getMessage()]
    assert failed and failed[0]["fields"] == {"err": "RuntimeError"}
    assert all("SECRET-HOST" not in str(vars(r)) for r in logged)


async def test_only_segment_0_is_stamped(tmp_path):
    files = ff.FakeFiles()
    meeting = ff.meeting(["K3M9Q2T-00042-0A1C", "K3M9Q2T-00042-0A1D"])
    files.add(*meeting)
    shell = _shell(tmp_path, files, host_label="家")
    async with shell.connected():
        await until(lambda: shell.archived())
    stored = sorted(shell.events("rec.seg.stored"), key=lambda e: e["seg"])
    assert [("env" in e) for e in stored] == [True, False]
    assert _stamps(shell, meeting[0].rec)[1] == {"host": "家", "link": "unknown"}
