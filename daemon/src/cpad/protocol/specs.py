"""Per-message validators for protocol v0, written by hand from protocol/schema/v0 (05 §6.5, §6.7).

SPECS maps each message type to a Check over the whole message. The envelope fields v / t / id
and the format of re are checked first by the codec; here only `re` presence (request vs reply)
and type-specific rules remain. v0.4.1 (docs/13 §12.2): hello caps / host fields, dest, rec.*.
"""

from __future__ import annotations

import base64
import binascii
from collections.abc import Mapping
from typing import Any

from cpad.dest.specs import dest_specs
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import (
    NO_CONTROL,
    Check,
    Errors,
    Path,
    absent,
    all_of,
    any_object,
    array,
    boolean,
    integer,
    is_int,
    nullable,
    obj,
    one_of,
    string,
)

L = LIMITS
ID = string(L["id_pattern"])
MONOID = string(L["monoid_pattern"])
BOOT = string(L["boot_pattern"])
UINT32 = integer(0, 4294967295)
COUNT = integer(0, 65535)
SLOT = integer(L["slot_min"], L["slot_max"])
KEY = string(L["project_key_pattern"])
PROJECT_NAME = string(NO_CONTROL, min_len=1, max_bytes=L["project_name_max"])
SESSION_NAME = string(NO_CONTROL, min_len=1, max_bytes=L["session_name_max"])
QUEUE_TEXT = string(NO_CONTROL, min_len=1, max_bytes=L["queue_text_max"])
USER_TEXT = string(NO_CONTROL, min_len=L["text_min"], max_bytes=L["text_max"])
AGENT_TEXT = all_of(USER_TEXT, string(r"^(?!\s*-)"))
SID = string(L["sid_pattern"])
SHORT = string(r"^[!-~]+$", max_len=L["short_max"])
QID = string(L["qid_pattern"])
EPOCH_MS = integer(L["epoch_ms_min"], 9007199254740991)
TZ_MIN = integer(L["tz_min_min"], L["tz_min_max"])
REVS = obj({"projects": UINT32, "sessions": UINT32, "queue": UINT32})
SHA256 = string(L["sha256_pattern"])
REC_MODE = one_of("memo", "meeting")
SEG = integer(0, L["file_seg_max"])
FILES_N = integer(0, L["file_pending_max"])
SEG_BYTES_MAX = L["seg_ms_max"] * 32  # 16 kHz, 16 bit, mono
FILE_SIZE = integer(44, 44 + SEG_BYTES_MAX)
FILES = nullable(obj({"n": FILES_N, "rev": UINT32, "live": boolean()}))
LANG = one_of("zh", "en")  # v0.4.3 UI language (protocol README item 25)
# v0.4.0 debug.state.ui; overlay keeps 3 values. v0.4.3: the optional lang shown now.
UI = nullable(obj({"menu": boolean(), "locked": boolean()}, {"lang": LANG}))
# debug.state.diag (test firmware, protocol README item 21): heap / stack bytes, loop timing (uint32), key counts.
DIAG = nullable(
    obj(
        {
            **dict.fromkeys(("heap_free", "heap_min", "heap_block", "stack_left", "audio_stack_left"), UINT32),
            **dict.fromkeys(("loop_n", "loop_max_us", "loop_win_max_us", "gap_max_us", "gap_win_max_us"), UINT32),
            **dict.fromkeys(("loop_over_20", "loop_over_50", "loop_over_100", "loop_over_500"), UINT32),
            "ghost_n": COUNT,
            "kb_reinit_n": COUNT,
        }
    )
)
DEST = string(L["dest_pattern"], max_bytes=L["dest_max"])
HOST_ID = string(L["host_id_pattern"])
HOST_LABEL = string(NO_CONTROL, min_len=1, max_bytes=L["host_label_max"])
REC_CURSOR = string(L["rec_cursor_pattern"], max_len=L["rec_cursor_max"])
REC_TITLE = string(NO_CONTROL, min_len=1, max_bytes=L["rec_title_max"])
REC_FILTERS = ("all", "starred", "memo", "meeting")
REC_OPS = ("star", "unstar", "tag", "hide")
REC_STATES = ("on_device", "pulling", "archived", "processing", "blocked", "done", "transcript", "failed", "stale")
ERROR_CODES = (
    "E_PARSE", "E_TOO_LARGE", "E_VERSION", "E_UNKNOWN_TYPE", "E_BAD_FIELD",
    "E_NOT_READY", "E_STORAGE", "E_UNKNOWN_PROJECT", "E_UNKNOWN_SESSION", "E_UNKNOWN_ITEM",
    "E_NO_AGENT", "E_SPAWN", "E_NOT_REPLYABLE", "E_REPLY_FORKED", "E_BUSY", "E_INTERNAL",
)  # fmt: skip
ERROR_CODE = one_of(*ERROR_CODES)
SESSION_STATES = ("busy", "shell", "idle", "waiting", "unknown")
INTENTS = ("todo", "spawn", "feedback")
KEY_NAMES = (
    "`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "bksp",
    "tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\",
    "fn", "shift", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "enter",
    "ctrl", "opt", "alt", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "space",
    "g0",
)  # fmt: skip


def request(fields: Mapping[str, Check] | None = None, optional: Mapping[str, Check] | None = None, rules=()) -> Check:
    """Not a reply: re must be absent."""
    return obj(fields or {}, {"re": absent(), **(optional or {})}, rules)


def reply(fields: Mapping[str, Check] | None = None, optional: Mapping[str, Check] | None = None, rules=()) -> Check:
    return obj({"re": ID, **(fields or {})}, optional, rules)


def when(field: str, value: Any, then: Check) -> Check:
    """JSON-schema style if/then on one const field of the enclosing object."""

    def check(msg: Any, path: Path) -> Errors:
        return then(msg, path) if isinstance(msg, dict) and msg.get(field) == value else ()

    return check


def require_non_null(field: str, inner: Check) -> Check:
    return obj({field: inner})


def _hello() -> Check:
    caps = obj({"spawn": boolean(), "reply": boolean()}, {"recs": nullable(boolean()), "todo": nullable(boolean())})
    host = request(
        {"app": string(r"^[!-~]{1,32}$"), "agent": one_of("ok", "missing", "error"), "caps": caps},
        {"host_id": nullable(HOST_ID), "host_label": nullable(HOST_LABEL)},  # v0.4.1, optional
    )
    device = reply(
        {
            "fw": string(r"^cpa-fw/[!-~]{1,24}$"),
            "dev": string(r"^[0-9a-f]{12}$"),
            "boot": BOOT,
            "up_ms": UINT32,
            "rst": string(r"^[a-z0-9_]{1,16}$"),
            "time": boolean(),
            "pending": COUNT,
            "bat_mv": COUNT,
            "revs": REVS,
        },
        {
            "store": nullable(one_of("sd", "int", "none")),  # v0.2, optional (09 §6)
            "launcher": nullable(boolean()),
            "files": FILES,  # v0.3, optional (docs/11 §5.2)
        },
    )
    return obj({"role": one_of("host", "device")}, rules=(when("role", "host", host), when("role", "device", device)))


def _outbox_item() -> Check:
    note_body = obj({"text": USER_TEXT}, {"project": nullable(KEY), "dest": nullable(DEST)})
    queue_body = obj(
        {"text": USER_TEXT, "project": KEY, "intent": one_of(*INTENTS)},
        {"sid": nullable(SID), "uncertain": nullable(boolean())},
        rules=(when("intent", "feedback", require_non_null("sid", SID)),),
    )
    at = obj({"up_ms": UINT32}, {"wall_ms": nullable(EPOCH_MS)})
    return request(
        {"id": MONOID, "kind": one_of("note", "queue"), "at": at, "body": any_object()},
        rules=(
            when("kind", "note", obj({}, {"body": note_body})),
            when("kind", "queue", obj({}, {"body": queue_body})),
        ),
    )


def _snapshots() -> dict[str, Check]:
    project = obj(
        {"slot": SLOT, "key": KEY, "name": PROJECT_NAME, "run": COUNT, "need": COUNT, "queued": COUNT, "ok": boolean()}
    )
    session = obj(
        {
            "sid": SID,
            "slot": SLOT,
            "name": SESSION_NAME,
            "kind": one_of("tty", "bg"),
            "st": one_of(*SESSION_STATES),
            "need": boolean(),
            "age_s": UINT32,
        },
        {"short": nullable(SHORT)},
    )
    item = obj(
        {"qid": QID, "slot": SLOT, "text": QUEUE_TEXT, "intent": one_of(*INTENTS)},
        {"sid": nullable(SID), "uncertain": nullable(boolean())},  # uncertain: v0.4.1 (docs/20 U25)
    )
    per_project = (L["snapshot_queue_per_project_max"],)
    return {
        "projects.snapshot": request(
            {
                "rev": UINT32,
                "projects": array(project, max_items=L["snapshot_projects_max"], unique_by="slot"),
                "other": obj({"run": COUNT, "n": COUNT}),
            }
        ),
        "sessions.snapshot": request(
            {
                "rev": UINT32,
                "total": COUNT,
                "sessions": array(session, max_items=L["snapshot_sessions_max"], unique_by="sid"),
            }
        ),
        "queue.snapshot": request(
            {
                "rev": UINT32,
                "items": array(
                    item, max_items=L["snapshot_queue_max"], unique_by="qid", max_per_key=("slot", *per_project)
                ),
            }
        ),
    }


def _agent() -> dict[str, Check]:
    result = reply(
        {"ok": boolean()},
        {
            "code": nullable(ERROR_CODE),
            "name": nullable(SESSION_NAME),
            "short": nullable(SHORT),
            "why": nullable(string(r"^[a-z][a-z_]{0,15}$")),  # v0.4 (#25)
        },
        rules=(when("ok", False, require_non_null("code", ERROR_CODE)),),
    )
    return {
        "agent.spawn": request({"project": KEY, "text": AGENT_TEXT}, {"qid": nullable(QID)}),
        "agent.reply": request({"sid": SID, "text": AGENT_TEXT}),
        "agent.result": result,
        "queue.done": request({"qid": QID}),
        "inbox.dismiss": request({"sid": SID}),
    }


def _debug_rec() -> Check:
    """debug.state's rec{} (v0.3): the recorder's state and, optionally, its health counters."""
    return obj(
        {
            "live": boolean(),
            "mode": nullable(REC_MODE),
            "seg": nullable(SEG),
            "files_n": FILES_N,
            "files_rev": UINT32,
        },
        {
            "ring_high": nullable(COUNT),
            "mic_missing": nullable(UINT32),
            "rot_ms_max": nullable(UINT32),
            "heap_free": nullable(UINT32),
            "heap_block": nullable(UINT32),
        },
    )  # fmt: skip


def _debug() -> dict[str, Check]:
    summary = reply(
        {
            "up_ms": UINT32,
            "link": one_of("Detached", "Attached", "Ready"),
            "mod": string(r"^[a-z][a-z0-9_]{1,15}$"),
            "overlay": one_of("none", "compose", "confirm"),
            "outbox": obj({"pending": COUNT, "inflight": COUNT, "bad": COUNT}),
            "revs": REVS,
            "need": COUNT,
            "projects": array(
                obj({"slot": SLOT, "run": COUNT, "need": COUNT, "queued": COUNT}), max_items=9, unique_by="slot"
            ),
        },
        {
            "overlay_ms": nullable(UINT32),
            "key_ms": nullable(UINT32),
            "rec": nullable(_debug_rec()),
            "ui": UI,  # v0.4.0 (docs/13 §12.1)
            "diag": DIAG,  # gh #21
        },
    )

    def state(msg: Any, path: Path) -> Errors:
        return summary(msg, path) if isinstance(msg, dict) and "re" in msg else request()(msg, path)

    keys = array(one_of(*KEY_NAMES), min_items=1, max_items=3, unique=True)
    return {
        "debug.key": request({"keys": keys}, {"hold_ms": nullable(integer(0, 5000))}),
        "debug.state": state,
        "debug.reboot": request(),
        "debug.rec": request({"seg_ms": integer(L["seg_ms_min"], L["seg_ms_max"])}),
        "debug.mic": request({"pga": integer(0, 10), "vol": integer(0, 255), "mag": integer(1, 64)}),
        "debug.codec": request({"warm": boolean()}),
        "debug.lang": request({"lang": LANG}),
    }


def _b64_len(msg: Any, path: Path) -> Errors:
    """x-b64Len: b64 is present exactly when n > 0 and decodes to exactly n bytes (file.data, docs/11 §5.3)."""
    if not isinstance(msg, dict) or not is_int(msg.get("n")) or msg["n"] < 0:
        return ()
    b64 = msg.get("b64")
    if msg["n"] == 0:
        return () if b64 is None else ((*path, "b64"),)
    if not isinstance(b64, str):
        return ((*path, "b64"),)
    try:
        size = len(base64.b64decode(b64, validate=True))
    except (binascii.Error, ValueError):
        return ((*path, "b64"),)
    return () if size == msg["n"] else ((*path, "b64"),)


def _file_item() -> Check:
    stars = array(integer(0, L["seg_ms_max"] - 1), max_items=L["file_marks_max"])
    at = obj({"up_ms": UINT32}, {"wall_ms": nullable(EPOCH_MS)})
    return obj(
        {
            "id": MONOID,
            "kind": one_of("rec"),
            "size": FILE_SIZE,
            "rec": MONOID,
            "seg": SEG,
            "last": boolean(),
            "mode": REC_MODE,
            "dur_ms": integer(0, L["seg_ms_max"]),
            "at": nullable(at),
        },
        {
            "sha256": nullable(SHA256),
            "project": nullable(KEY),
            "slot": nullable(SLOT),
            "stars": nullable(stars),
            "lead_ms": nullable(integer(0, L["lead_ms_max"])),
            "consent": nullable(one_of("ack")),
            "rcv": nullable(boolean()),
            "dest": nullable(DEST),  # v0.4.1, memo segment 0 only
        },
    )


def _files() -> dict[str, Check]:
    b64 = string(r"^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$", min_len=4, max_len=1840)
    data = reply(
        {"fid": MONOID, "off": UINT32, "n": integer(0, L["file_get_b64_max"]), "size": FILE_SIZE, "crc": UINT32},
        {"b64": nullable(b64)},
        rules=(_b64_len,),
    )
    return {
        "file.list": request(optional={"after": nullable(MONOID), "max": nullable(integer(1, L["file_list_max"]))}),
        "file.items": reply({"items": array(_file_item(), max_items=L["file_list_max"]), "more": boolean()}),
        "file.get": request({"fid": MONOID, "off": UINT32, "len": integer(1, L["file_get_b64_max"])}),
        "file.data": data,
        "file.del": request({"fid": MONOID, "size": FILE_SIZE}, {"sha256": nullable(SHA256)}),
    }


def _project_only_with_tag(msg: Any, path: Path) -> Errors:
    """rec.act: project is required with tag (when() below) and absent or null with any other op."""
    stray = isinstance(msg, dict) and msg.get("op") != "tag" and msg.get("project") is not None
    return ((*path, "project"),) if stray else ()


def _recs() -> dict[str, Check]:
    row = obj(
        {"rec": MONOID, "mode": REC_MODE, "st": one_of(*REC_STATES)},
        {
            "wall_ms": nullable(EPOCH_MS),
            "dur_ms": nullable(UINT32),
            "project": nullable(KEY),
            "starred": nullable(boolean()),
            "marks": nullable(COUNT),
            "title": nullable(REC_TITLE),
            "todo_n": nullable(COUNT),
        },
    )
    tagged = when("op", "tag", obj({"project": nullable(KEY)}))
    return {
        "rec.list": request(
            {"filter": one_of(*REC_FILTERS), "max": integer(1, L["rec_list_max"])}, {"cursor": nullable(REC_CURSOR)}
        ),
        "rec.items": reply(
            {"items": array(row, max_items=L["rec_list_max"], unique_by="rec")}, {"next": nullable(REC_CURSOR)}
        ),
        "rec.act": request({"rec": MONOID, "op": one_of(*REC_OPS)}, rules=(tagged, _project_only_with_tag)),
    }


SPECS: Mapping[str, Check] = {
    "hello": _hello(),
    "ping": request(),
    "pong": reply({"up_ms": UINT32, "pending": COUNT, "revs": REVS}, {"files": FILES}),
    "ack": reply(),
    "error": reply(
        {"code": ERROR_CODE},
        {"field": nullable(string(r"^[!-~]{1,48}$")), "msg": nullable(string(r"^[ -~]{1,80}$"))},
    ),
    "time.set": request({"epoch_ms": EPOCH_MS, "tz_min": TZ_MIN}),
    "outbox.item": _outbox_item(),
    "outbox.ack": reply(
        {"re": MONOID, "status": one_of("stored", "duplicate", "rejected")},
        {"code": nullable(ERROR_CODE)},
        rules=(when("status", "rejected", require_non_null("code", ERROR_CODE)),),
    ),
    **_snapshots(),
    **_agent(),
    **_debug(),
    **_files(),
    **_recs(),
    **dest_specs(),
}
