"""One row of the filing list (docs/23 §3.2; the contract protocol/filing/v1 pins its shape). Pure.

- ids, times, states, the effective project, at most TITLE_MAX / TEXT_MAX characters of display_safe text;
- never a path (docs/15 principle 6) and never the device's `dev` (its factory MAC address): `device` is
  HMAC-SHA256(id_key, "dev|" + dev), first 6 bytes in hex, so the ids of two Macs cannot be joined;
- `project` is one of this Mac's keys or null; a new session whose key this Mac does not know has it in `asked`;
- `env` is the recording's environment stamp when cpad wrote one (P1-D1): host (display_safe) and link only,
  never a network; a link that is not an extensible-enum word (WORD) is null, which readers show as unknown;
- `prof` (v0.8) and `clips` (voice captures, #52) stay null until those land.
"""

from __future__ import annotations

import hashlib
import hmac
import re
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from cpad.filing.fold import Filing, Known
from cpad.filing.text import excerpt
from cpad.recordings import index, sessions

TITLE_MAX = 80
TEXT_MAX = 280
HOST_MAX = 24  # host_label is 1-24 UTF-8 bytes where it is set
WORD = re.compile(r"[a-z][a-z0-9_]{0,15}\Z")  # an extensible enum value (row.schema.json env.link)
TX = {"archived": "waiting"}  # sessions.state_of's word -> the docs/15 §3.4 word
NO_TEXT = ("failed", "stale")
Status = tuple[str, str | None]


@dataclass(frozen=True, slots=True)
class Files:
    """What only the session's files say, read by the I/O shell (filing.view)."""

    tz_min: int | None = None  # manifest.json started.tz_min
    transcript: str | None = None  # the first sentences, read only when there is no memo text


NO_FILES = Files()


def device_id(key: bytes, dev: str) -> str:
    return hmac.new(key, b"dev|" + dev.encode("utf-8"), hashlib.sha256).hexdigest()[:12]


def _str(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def _why(f: Filing, tx: str) -> list[str]:
    head = ["orphan"] if f.orphan else ["unfiled"] if f.filing == "new" else []
    return head + (["no_text"] if tx in NO_TEXT else [])


def env_of(archived: Mapping[str, Any]) -> dict[str, Any] | None:
    """The Mac-side stamp of a rec.archived (P1-D1): host and link only, never a network; None without one."""
    env = archived.get("env")
    if not isinstance(env, Mapping):
        return None
    link = _str(env.get("link"))
    return {"host": excerpt(_str(env.get("host")), HOST_MAX), "link": link if link and WORD.match(link) else None}


def done_result(idx: index.RecIndex, rec: str) -> Mapping[str, Any]:
    return next((e for e in reversed(idx.results.get(rec, ())) if e.get("state") == "done"), {})


def tx_of(idx: index.RecIndex, rec: str, statuses: Mapping[str, Status]) -> str:
    """The transcription state, in the words of docs/15 §3.4 plus stale."""
    state, _ = sessions.state_of(idx, rec, statuses)
    return TX.get(state, state)


def queue_of(f: Filing) -> dict[str, Any] | None:
    return {"qid": f.hand.qid, "intent": f.hand.intent, "open": f.hand.open} if f.hand else None


def row(f: Filing, idx: index.RecIndex, known: Known, *, statuses: Mapping[str, Status], key: bytes | None,
        files: Files = NO_FILES) -> dict[str, Any]:  # fmt: skip
    archived, done = idx.archived[f.rec], done_result(idx, f.rec)
    tx, dev = tx_of(idx, f.rec, statuses), _str(archived.get("dev"))
    text = _str(done.get("memo_text")) or files.transcript
    return {
        "id": f.rec, "kind": archived.get("mode"), "filing": f.filing, "why": _why(f, tx), "asked": f.asked,
        "at": {"wall_ms": archived.get("started_ms"), "tz_min": files.tz_min,
               "uncertain": bool(archived.get("uncertain"))},
        "dur_ms": archived.get("dur_ms"), "device": device_id(key, dev) if key and dev else None,
        "env": env_of(archived), "prof": None, "project": f.project if f.project in known.keys else None,
        "starred": index.edit_of(idx, f.rec).starred, "tx": tx, "title": excerpt(_str(done.get("title")), TITLE_MAX),
        "text": excerpt(text, TEXT_MAX), "intent": _str(done.get("memo_intent")), "clips": None, "queue": queue_of(f),
    }  # fmt: skip
