"""Event records for events.jsonl (05 §7.3). Pure constructors; records are plain dicts, never mutated.

Every record has `ev` and `at` (daemon ISO time). Agent records never carry the user's text, nor does
dest.downgraded (v0.4.1: a note whose dest this cpad does not know, stored as a plain daily-note line).
"""

from __future__ import annotations

import hashlib
from collections.abc import Iterable, Mapping
from datetime import UTC, datetime
from typing import Any

from cpad.protocol.messages import OutboxItem, QueueBody
from cpad.queue.fold import QueueItem

Record = Mapping[str, Any]


def iso(ts: float) -> str:
    return datetime.fromtimestamp(ts, UTC).isoformat(timespec="milliseconds")


def _plain_body(item: OutboxItem) -> dict[str, Any]:
    body = item.body
    if isinstance(body, QueueBody):
        return {"text": body.text, "project": body.project, "intent": body.intent, "sid": body.sid,
                "uncertain": body.uncertain}  # fmt: skip
    dest = {"dest": body.dest} if body.dest is not None else {}  # v0.3 notes keep their exact record
    return {"text": body.text, "project": body.project, **dest}


def item_received(dev: str, item: OutboxItem, received_at: str, orphan: bool) -> dict[str, Any]:
    record = {
        "ev": "item.received",
        "at": received_at,
        "dev": dev,
        "id": item.id,
        "kind": item.kind,
        "body": _plain_body(item),
        "at_dev": {"up_ms": item.at.up_ms, "wall_ms": item.at.wall_ms},
        "received_at": received_at,
    }
    return {**record, "orphan": True} if orphan else record


def dest_downgraded(item: str, dest: str, at: str) -> dict[str, Any]:
    return {"ev": "dest.downgraded", "at": at, "item": item, "dest": dest}


def queue_added(
    *,
    qid: str | None,
    project: str,
    text: str,
    intent: str,
    origin: str,
    at: str,
    sid: str | None = None,
    from_item: str | None = None,
    uncertain: bool = False,
) -> dict[str, Any]:
    return {"ev": "queue.added", "at": at, "qid": qid, "project": project, "text": text, "intent": intent,
            "sid": sid, "origin": origin, "from_item": from_item, "uncertain": uncertain}  # fmt: skip


def queue_added_from_item(item: OutboxItem, at: str) -> dict[str, Any]:
    body = item.body
    assert isinstance(body, QueueBody)
    return queue_added(qid=None, project=body.project, text=body.text, intent=body.intent, origin="device", at=at,
                       sid=body.sid, from_item=item.id, uncertain=bool(body.uncertain))  # fmt: skip


def queue_handed(*, qid: str, project: str, text: str, intent: str, rec: str, at: str) -> dict[str, Any]:
    """docs/23 §3.4: a recording handed to AI by `cpa filing hand`; text is taken from the recording (queue_text),
    bytes and sha8 are its UTF-8 size and the first 8 hex digits of its sha256, as docs/22 §5.6 has them."""
    body = text.encode("utf-8")
    return {**queue_added(qid=qid, project=project, text=text, intent=intent, origin="inbox", at=at, from_item=rec),
            "voice": True, "bytes": len(body), "sha8": hashlib.sha256(body).hexdigest()[:8]}  # fmt: skip


def queue_done(qid: str, at: str, by: str | None = None) -> dict[str, Any]:
    """by "undo" (docs/23 §2.2, D17): a hand withdrawn by `cpa filing reopen`; older folds close the item alike."""
    return {"ev": "queue.done", "at": at, "qid": qid, **({"by": by} if by else {})}


def queue_reopened(item: QueueItem, at: str) -> dict[str, Any]:
    """`cpa queue reopen`: a closed QueueItem is open again (docs/20 U12), with its fields as queue.added had them,
    so the fold needs no memory of closed items."""
    return {"ev": "queue.reopened", "at": at, "qid": item.qid, "project": item.project, "text": item.text,
            "intent": item.intent, "sid": item.sid, "origin": item.origin, "uncertain": item.uncertain}  # fmt: skip


def queue_spawned(qid: str, at: str, name: str | None = None, short: str | None = None) -> dict[str, Any]:
    return {"ev": "queue.spawned", "at": at, "qid": qid, "name": name, "short": short}


def agent_spawned(project: str, name: str | None, short: str | None, at: str) -> dict[str, Any]:
    return {"ev": "agent.spawned", "at": at, "project": project, "name": name, "short": short}


def agent_spawn_failed(project: str, name: str | None, code: str, at: str, why: str | None = None) -> dict[str, Any]:
    record = {"ev": "agent.spawn_failed", "at": at, "project": project, "name": name, "code": code}
    return {**record, "why": why} if why else record  # why: untrusted | login (#25)


def agent_replied(sid: str, at: str) -> dict[str, Any]:
    return {"ev": "agent.replied", "at": at, "sid": sid}


def agent_reply_failed(sid: str, code: str, at: str) -> dict[str, Any]:
    return {"ev": "agent.reply_failed", "at": at, "sid": sid, "code": code}


def device_seen(dev: str, serial: str | None, fw: str, at: str) -> dict[str, Any]:
    return {"ev": "device.seen", "at": at, "dev": dev, "serial": serial, "fw": fw}


def dedupe_keys(events: Iterable[Record]) -> frozenset[tuple[str, str]]:
    return frozenset(
        (e["dev"], e["id"])
        for e in events
        if e.get("ev") == "item.received" and isinstance(e.get("dev"), str) and isinstance(e.get("id"), str)
    )
