"""One row of the filing list (docs/23 §3.2), display_safe() (§3.3 rule 7) and the per-Mac id_key (§3.2 device)."""

from __future__ import annotations

import hashlib
import hmac
import json
import stat

from cpad.config import host
from cpad.config.settings import Paths, secure_home
from cpad.filing import fold as ff
from cpad.filing import rows, text
from cpad.projects.registry import Project
from cpad.recordings import index
from cpad.recordings import records as r

DEV = "020000000001"
AT = "2026-10-22T03:23:00.000+00:00"
MEMO = "3QZK1M7-0000A-0001"
KEY = bytes(range(32))
KNOWN = ff.Known.of((Project("cpa", "Cardputer", 1, "/r"),))
ROW_KEYS = {"id", "kind", "filing", "why", "asked", "at", "dur_ms", "device", "env", "prof", "project", "starred",
            "tx", "title", "text", "intent", "clips", "queue"}  # fmt: skip


def archived(project: str | None = None, **extra) -> dict:
    found = r.archived(dev=DEV, rec=MEMO, dir=f"recordings/2026-10-22/1123-{MEMO.lower()}", mode="memo",
                       project=project, segments=1, dur_ms=24000, started_ms=1792639380000, uncertain=True, gaps=[],
                       consent=None, at=AT)  # fmt: skip
    return {**found, **extra}


def result(**over) -> dict:
    fields = {"state": "done", "level": "summary", "title": "报价", "memo_text": "周五前发报价", "memo_intent": "todo",
              "actions_n": 0, "error_code": None, **over}  # fmt: skip
    return r.result(rec=MEMO, session=archived(), at=AT, **fields)


def row(events, statuses=None, files: rows.Files = rows.NO_FILES, key: bytes | None = KEY) -> dict:
    events = tuple(events)
    idx, log = index.fold(events), ff.fold(events)
    return rows.row(ff.filing_of(idx, log, MEMO, KNOWN), idx, KNOWN, statuses=statuses or {}, key=key, files=files)


def test_a_new_memo_without_text_yet():
    found = row([archived()], files=rows.Files(tz_min=480))
    assert set(found) == ROW_KEYS
    assert found == {"id": MEMO, "kind": "memo", "filing": "new", "why": ["unfiled"], "asked": None,
                     "at": {"wall_ms": 1792639380000, "tz_min": 480, "uncertain": True}, "dur_ms": 24000,
                     "device": hmac.new(KEY, b"dev|" + DEV.encode(), hashlib.sha256).hexdigest()[:12], "env": None,
                     "prof": None, "project": None, "starred": False, "tx": "waiting", "title": None, "text": None,
                     "intent": None, "clips": None, "queue": None}  # fmt: skip
    assert DEV not in str(found) and "recordings/" not in str(found), "no MAC address, no path"


def test_a_filed_memo_with_its_text_title_and_intent():
    found = row([archived(project="cpa"), result(), r.edited_star(rec=MEMO, starred=True, at=AT)])
    assert (found["filing"], found["why"], found["project"], found["starred"]) == ("filed", [], "cpa", True)
    assert (found["tx"], found["title"], found["text"], found["intent"]) == ("done", "报价", "周五前发报价", "todo")


def test_an_orphan_carries_the_key_it_asked_for_and_no_project():
    found = row([archived(project="acme")])
    assert (found["filing"], found["why"], found["asked"], found["project"]) == ("new", ["orphan"], "acme", None)
    done = row([archived(project="acme"), {"ev": "rec.edited", "at": AT, "rec": MEMO, "done": True}])
    assert (done["filing"], done["asked"], done["project"]) == ("done", None, None), "only this Mac's keys"


def test_a_hand_shows_its_queue_item():
    hand = {"ev": "queue.added", "at": AT, "qid": "q12", "project": "cpa", "text": "x", "intent": "spawn", "sid": None,
            "origin": "inbox", "from_item": MEMO, "uncertain": False}  # fmt: skip
    found = row([archived(), hand])
    assert (found["filing"], found["queue"], found["project"]) == ("handed", {"qid": "q12", "intent": "spawn",
                                                                             "open": True}, "cpa")  # fmt: skip


def test_failed_and_stale_have_no_text_and_say_why():
    failed = row([archived(), result(state="failed", level=None, title=None, memo_text=None, error_code="E_STT")])
    assert (failed["tx"], failed["why"]) == ("failed", ["unfiled", "no_text"])
    stale = row([archived(project="cpa"), r.stale(rec=MEMO, session=archived(), at=AT)])
    assert (stale["tx"], stale["why"]) == ("stale", ["no_text"])
    assert row([archived()], statuses={MEMO: ("blocked", "model_missing")})["tx"] == "blocked"


def test_transcript_text_is_used_when_there_is_no_memo_text_and_everything_is_display_safe():
    found = row([archived(), result(level="transcript", title="a‮b\x9bc", memo_text=None)],
                files=rows.Files(transcript="第一句。 第二句。" + "长" * 400))  # fmt: skip
    assert found["title"] == "ab c" and found["text"].startswith("第一句。 第二句。")
    assert len(found["text"]) == rows.TEXT_MAX


def test_env_is_read_from_the_archived_record_without_any_network_stamp():
    env = {"host": "公司", "link": "here", "net": {"id": "x"}}
    assert row([archived(env=env)])["env"] == {"host": "公司", "link": "here"}
    assert row([archived()], key=None)["device"] is None


def test_the_mac_name_in_the_stamp_is_display_safe_too():
    env = {"host": "\u202e家\u2066" + "x" * 30, "link": "here"}
    assert row([archived(env=env)])["env"] == {"host": "家" + "x" * 23, "link": "here"}
    assert row([archived(env={"host": "\u202e", "link": None})])["env"] == {"host": None, "link": None}


def test_a_link_that_is_not_an_enum_word_is_null_and_a_word_this_cpad_does_not_know_passes():
    for bad in ("HERE", "here\x1b[31m", "x" * 17, 5, ["here"]):
        assert row([archived(env={"host": None, "link": bad})])["env"] == {"host": None, "link": None}, bad
    assert row([archived(env={"host": None, "link": "other"})])["env"]["link"] == "other", "an extensible enum"


# --- display_safe ------------------------------------------------------------------------------


def test_display_safe_drops_controls_c1_bidi_and_line_separators():
    hostile = "a\x00b\x1bc\x7fd\x85e\x9bf‪g‮h⁦i⁩j k l\nm"
    assert text.display_safe(hostile) == "a b c d e fghij k l m"
    assert text.display_safe("普通的字 and émoji 🙂") == "普通的字 and émoji 🙂"
    assert text.excerpt("  x  " + "y" * 300, 10) == "x  yyyyyyy" and text.excerpt(None, 10) is None
    assert text.excerpt("‮\x00 ", 10) is None


def test_display_safe_turns_a_lone_surrogate_into_a_space_and_keeps_real_emoji():
    """Half an emoji a transcriber cut (valid JSON "\\ud83d") would crash every UTF-8 output: ls, show, export."""
    half = json.loads('"a\\ud83db"')
    assert text.excerpt(half, 80) == "a b" and text.display_safe(half).encode("utf-8") == b"a b"
    assert text.display_safe(json.loads('"\\ud83d\\ude42"')) == "🙂", "a pair in JSON is one character"
    assert text.queue_text(half) == "a b"


# --- id_key ------------------------------------------------------------------------------------


def test_id_key_is_created_once_private_and_kept(tmp_path):
    path = tmp_path / "id_key"
    assert host.stored_id_key(path) is None and not path.exists(), "readers never create it"
    first = host.id_key(path)
    assert host.stored_id_key(path) == first
    assert len(first) == 32 and host.id_key(path) == first
    assert stat.S_IMODE(path.stat().st_mode) == 0o600
    path.write_text("not hex\n")
    assert host.stored_id_key(path) is None
    replaced = host.id_key(path)
    assert len(replaced) == 32 and replaced != first and host.id_key(path) == replaced


def test_secure_home_tightens_the_id_key(tmp_path):
    paths = Paths.at(tmp_path / "home")
    paths.home.mkdir(mode=0o700)
    host.id_key(paths.id_key)
    paths.id_key.chmod(0o644)
    assert str(paths.id_key) in secure_home(paths)
