"""cpa audit (docs/14 §8.2): what was decided, delivered, failed, dropped, expired or changed, oldest first.

Pure. Rows are built from a whitelist of dest.* fields, so no text can reach the output even if a
record carried some: deliveries show only the UTF-8 size and sha256[:8]. v0.4.1's dest.downgraded
reads as a downgrade. `--leaves-mac` keeps what left this Mac (deliveries and handovers to another app) and
what was allowed to (changes of destinations that leave it).
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Any

from cpad.dest.records import DEST_EVENTS

SINCE_RE = re.compile(r"([0-9]{1,6})([dhm])\Z")
UNITS = {"d": "days", "h": "hours", "m": "minutes"}


@dataclass(frozen=True, slots=True)
class AuditRow:
    at: str
    ev: str
    item: str | None
    dest: str | None
    detail: str
    by: str | None = None
    bytes: int | None = None
    sha8: str | None = None
    leaves_mac: bool = False


def parse_since(text: str, now: float) -> str:
    """A lower bound in UTC from 7d / 24h / 30m or an ISO time (local when it has no zone)."""
    match = SINCE_RE.match(text or "")
    if match:
        delta = timedelta(**{UNITS[match.group(2)]: int(match.group(1))})
        return (datetime.fromtimestamp(now, UTC) - delta).isoformat(timespec="milliseconds")
    try:
        when = datetime.fromisoformat(text)
    except (TypeError, ValueError) as exc:
        raise ValueError("--since takes 7d, 24h, 30m or an ISO time") from exc
    return when.astimezone(UTC).isoformat(timespec="milliseconds")


def _time(value: Any) -> datetime | None:
    try:
        when = datetime.fromisoformat(value)
    except (TypeError, ValueError):
        return None
    return when if when.tzinfo is not None else when.astimezone(UTC)


def _detail(e: Mapping[str, Any]) -> str:
    ev = e.get("ev")
    if ev == "dest.decided":
        wanted = f" (wanted {e.get('want')})" if e.get("want") not in (None, e.get("dest")) else ""
        return f"{e.get('action')}/{e.get('reason')} how={e.get('how')} origin={e.get('origin')}{wanted}"
    if ev == "dest.delivered":
        return f"ok attempt {e.get('attempt')}"
    if ev == "dest.handed":
        why = f" ({e.get('ref')})" if e.get("ref") else ""
        return f"handed via {e.get('via')}{why} attempt {e.get('attempt')}"
    if ev == "dest.failed":
        return f"{e.get('code')}{' retryable' if e.get('retryable') else ''} attempt {e.get('attempt')}"
    if ev == "dest.changed" and e.get("op") == "usual":
        return f"usual {e.get('project')} -> {e.get('id') or '-'}"
    if ev == "dest.changed":
        state = "on" if e.get("enabled") else "off"
        return f"{e.get('op')} key={e.get('key') or '-'} {state}{' opted-in' if e.get('optin') else ''}"
    return {"dest.dropped": "dropped", "dest.expired": "expired"}.get(str(ev), "downgrade (v0.4.1)")


def _row(e: Mapping[str, Any]) -> AuditRow:
    delivered = e.get("ev") in ("dest.delivered", "dest.handed")
    size = e.get("bytes") if delivered and isinstance(e.get("bytes"), int) else None
    sha8 = e.get("sha8") if delivered and isinstance(e.get("sha8"), str) else None
    leaves = bool(e.get("leaves_mac")) and e.get("ev") in ("dest.delivered", "dest.handed", "dest.changed")
    return AuditRow(at=str(e.get("at")), ev=str(e.get("ev")), item=e.get("item"), dest=e.get("dest") or e.get("id"),
                    detail=_detail(e), by=e.get("by"), bytes=size, sha8=sha8, leaves_mac=leaves)  # fmt: skip


def _wanted(e: Mapping[str, Any], since: datetime | None, leaves_mac: bool, dest: str | None) -> bool:
    when = _time(e.get("at"))
    if e.get("ev") not in DEST_EVENTS or when is None or (since is not None and when < since):
        return False
    if leaves_mac and not _row(e).leaves_mac:
        return False
    return dest is None or dest in (e.get("dest"), e.get("want"), e.get("id"))


def audit_rows(events: Iterable[Mapping[str, Any]], *, since: str | None = None, leaves_mac: bool = False,
               dest: str | None = None) -> tuple[AuditRow, ...]:  # fmt: skip
    """dest.* events (and v0.4.1 dest.downgraded) matching the filters, oldest first."""
    bound = _time(since) if since is not None else None
    chosen = [e for e in events if _wanted(e, bound, leaves_mac, dest)]
    return tuple(_row(e) for e in sorted(chosen, key=lambda e: _time(e["at"])))


def format_rows(rows: Iterable[AuditRow]) -> list[str]:
    def line(row: AuditRow) -> str:
        extra = [f"by={row.by}" if row.by else "", f"bytes={row.bytes} sha8={row.sha8}" if row.sha8 else "",
                 "leaves-mac" if row.leaves_mac else ""]  # fmt: skip
        head = f"{row.at}  {row.ev:<15} {row.dest or '-':<16} {row.item or '-':<18} {row.detail}"
        return "  ".join([head, *(x for x in extra if x)])

    return [line(row) for row in rows]
