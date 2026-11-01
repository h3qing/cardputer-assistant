"""dest.* records for events.jsonl (docs/14 §8.1). Pure constructors; records are plain dicts, never mutated.

No record carries the text: deliveries keep only its UTF-8 size and the first 8 hex digits of its
sha256, the same digest the log redaction prints (logging.redact). The text itself stays where it
already was, in item.received / rec.result. `by`: auto (sync or online), device (Enter / Opt+D on the
device), cli, app (the Mac app). Every record about a capture carries its docs/18 `prof` (None when unknown);
dest.changed carries the destination's fl, profiles and, for url / app, the pinned handler, never a template.
`code` stays on the Mac: E_DEST_IO (retryable), E_DEST_PATH, E_DEST_MISSING, E_DEST_TIMEOUT, E_INTERRUPTED.
v0.4.1's dest.downgraded{item, dest} is read as a downgrade.
"""

from __future__ import annotations

import hashlib
from typing import Any, Literal

from cpad.dest.handoff import draft_target
from cpad.dest.model import By, Capture, Decision, Delivery, Dest, Origin, flags, kind_of

DEST_EVENTS: frozenset[str] = frozenset(
    {
        "dest.decided",
        "dest.delivered",
        "dest.handed",
        "dest.failed",
        "dest.dropped",
        "dest.expired",
        "dest.changed",
        "dest.downgraded",
    }
)


def digest(text: str) -> tuple[int, str]:
    """(UTF-8 bytes, sha256[:8]) as logging.redact.redact_text computes them."""
    data = text.encode("utf-8", "surrogatepass")
    return len(data), hashlib.sha256(data).hexdigest()[:8]


def dest_decided(cap: Capture, decision: Decision, origin: Origin, at: str,
                 attempt: int | None = None) -> dict[str, Any]:  # fmt: skip
    """`attempt` only when the decision starts a delivery: fold_pending takes it as is, so folding twice is safe."""
    record = {"ev": "dest.decided", "at": at, "item": cap.ref, "src": cap.source, "want": cap.dest,
              "dest": decision.dest, "kind": kind_of(decision.dest), "action": decision.action,
              "reason": decision.reason, "how": cap.how, "origin": origin, "prof": cap.prof}  # fmt: skip
    record = {**record, "intent": decision.intent} if decision.intent else record
    return {**record, "attempt": attempt} if attempt is not None else record


def dest_delivered(d: Delivery, *, at: str, ref: str | None = None) -> dict[str, Any]:
    size, sha8 = digest(d.capture.text or "")
    return {"ev": "dest.delivered", "at": at, "item": d.capture.ref, "dest": d.dest.id, "kind": d.dest.kind,
            "by": d.by, "bytes": size, "sha8": sha8, "ref": ref, "leaves_mac": d.dest.leaves_mac,
            "attempt": d.attempt, "prof": d.capture.prof}  # fmt: skip


def dest_handed(d: Delivery, *, at: str, via: Literal["url", "clip"], text: str, ref: str | None) -> dict[str, Any]:
    """Handed to another app (docs/25 §5.6): a link opened or the clipboard written and the app brought forward;
    whether the app took it is unknown. Size and digest are of what the app received (header included). A filled
    draft carries `to`, the draft it occupies (handoff.draft_target), so the next process knows it is busy."""
    size, sha8 = digest(text)
    record = {"ev": "dest.handed", "at": at, "item": d.capture.ref, "dest": d.dest.id, "kind": d.dest.kind,
              "via": via, "by": d.by, "bytes": size, "sha8": sha8, "ref": ref, "leaves_mac": d.dest.leaves_mac,
              "attempt": d.attempt, "prof": d.capture.prof}  # fmt: skip
    filled = via == "url" and d.dest.url is not None
    return {**record, "to": draft_target(d.dest.url, d.capture.project)} if filled and d.dest.url else record


def dest_failed(d: Delivery, *, code: str, retryable: bool, at: str) -> dict[str, Any]:
    return {"ev": "dest.failed", "at": at, "item": d.capture.ref, "dest": d.dest.id, "kind": d.dest.kind,
            "code": code, "retryable": retryable, "attempt": d.attempt, "prof": d.capture.prof}  # fmt: skip


def dest_dropped(ref: str, dest: str, *, by: By, at: str, prof: str | None = None) -> dict[str, Any]:
    return {"ev": "dest.dropped", "at": at, "item": ref, "dest": dest, "by": by, "prof": prof}


def dest_expired(ref: str, dest: str, *, at: str, prof: str | None = None) -> dict[str, Any]:
    return {"ev": "dest.expired", "at": at, "item": ref, "dest": dest, "prof": prof}


def dest_changed(op: Literal["add", "rm", "bind", "verify", "map"], dest: Dest, *, by: By, at: str) -> dict[str, Any]:
    record = {"ev": "dest.changed", "at": at, "op": op, "id": dest.id, "by": by, "enabled": dest.enabled,
              "key": dest.key, "leaves_mac": dest.leaves_mac, "optin": dest.optin, "fl": flags(dest),
              "profiles": list(dest.profiles)}  # fmt: skip
    pinned = dest.url or dest.app
    if pinned is not None:
        record = {**record, "handler": {"bundle": pinned.bundle, "team": pinned.team}}
    return {**record, "verified": dest.url.verified} if dest.url is not None else record


def dest_usual(project: str, dest_id: str | None, *, by: By, at: str) -> dict[str, Any]:
    """A project's usual destination set (or forgotten: id null), docs/25 §3.4."""
    return {"ev": "dest.changed", "at": at, "op": "usual", "id": dest_id, "project": project, "by": by}
