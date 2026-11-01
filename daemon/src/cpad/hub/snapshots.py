"""Snapshot building (05 §7.6): bounded, sorted, UTF-8-safe, each line <= 2048 B.

rev moves only when content changes; age_s is not content (the device ages rows itself).
Revs start at 1 after the first publish, so a freshly booted device (revs 0) is always behind.
"""

from __future__ import annotations

from dataclasses import dataclass, replace

from cpad.hub.state import HubState, Published
from cpad.i18n import T
from cpad.inbox.need import Tracked, needs
from cpad.notes.render import clean
from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.protocol.limits import LIMITS
from cpad.queue.fold import QueueItem, open_items

KINDS = ("projects", "sessions", "queue")
VOICE = ("语音·", "Voice·")  # a spoken queue item's board summary starts with it (docs/22 §3.8, docs/23 §3.4)
RUNNING = frozenset({"busy", "shell"})
WORST_ID = "h" + "9" * (LIMITS["id_max"] - 1)


@dataclass(frozen=True, slots=True)
class Snapshots:
    projects: m.ProjectsSnapshot
    sessions: m.SessionsSnapshot
    queue: m.QueueSnapshot

    def get(self, kind: str):
        return getattr(self, kind)


def truncate_utf8(text: str, max_bytes: int) -> str:
    return text.encode("utf-8")[:max_bytes].decode("utf-8", "ignore")


def label(text: str, max_bytes: int, fallback: str) -> str:
    cut = truncate_utf8(clean(text).strip(), max_bytes).strip()
    return cut or truncate_utf8(fallback, max_bytes)


def _fit(msg, key: str) -> tuple:
    wire = m.to_wire(replace(msg, id=WORST_ID))
    _, kept = codec.fit_list(wire, key)
    return getattr(msg, key)[:kept]


def _slots(state: HubState) -> dict[str, int]:
    return {p.key: p.slot for p in state.projects}


def _need(state: HubState, t: Tracked, now: float) -> bool:
    return needs(t, now, state.idle_s, state.dismissed)


def _own_name(name: str, key: str) -> str:
    """The device writes <key>/<name>: a name that already starts with its own key/ loses it (docs/20 U14)."""
    rest = name.removeprefix(f"{key}/")
    return rest if rest.strip() else name


def _order(state: HubState, t: Tracked, now: float) -> tuple:
    group = 0 if _need(state, t, now) else 1 if t.info.status in RUNNING else 2
    return (group, t.since, t.info.sid)


def _first_per_sid(tracked: list[Tracked]) -> list[Tracked]:
    """The wire sid is 8 hex digits of the session id and the device refuses a snapshot that repeats one
    (E_BAD_FIELD sessions[i].sid), so one odd listing would cost it every row: keep the first of each."""
    first = {t.info.sid: n for n, t in reversed(list(enumerate(tracked)))}
    return [t for n, t in enumerate(tracked) if first[t.info.sid] == n]


def _shown(state: HubState, now: float) -> list[Tracked]:
    """Sessions of registered projects in display order, one per wire sid: what the sessions snapshot lists
    and what the board counts, so a project never shows a run or need that no row explains."""
    slots = _slots(state)
    return _first_per_sid(sorted((t for t in state.tracked if t.project in slots), key=lambda t: _order(state, t, now)))


def session_rows(state: HubState, now: float) -> tuple[tuple[m.SessionRow, ...], int]:
    slots = _slots(state)
    mine = _shown(state, now)
    rows = tuple(
        m.SessionRow(
            sid=t.info.sid,
            slot=slots[t.project],
            name=label(_own_name(t.info.name, t.project), LIMITS["session_name_max"], t.info.sid),
            kind=t.info.kind,
            st=t.info.status,
            need=_need(state, t, now),
            age_s=max(0, int(now - t.since)),
            short=label(t.info.short, LIMITS["short_max"], t.info.sid)
            if t.info.kind == "bg" and t.info.short
            else None,
        )
        for t in mine
    )
    return rows[: LIMITS["snapshot_sessions_max"]], len(rows)


def project_rows(state: HubState, now: float) -> tuple[tuple[m.ProjectRow, ...], m.Other]:
    shown = _shown(state, now)

    def row(p) -> m.ProjectRow:
        mine = [t for t in shown if t.project == p.key]
        return m.ProjectRow(
            slot=p.slot,
            key=p.key,
            name=label(p.name, LIMITS["project_name_max"], p.key),
            run=sum(t.info.status in RUNNING for t in mine),
            need=sum(_need(state, t, now) for t in mine),
            queued=len(open_items(state.queue, p.key)),
            ok=p.ok,
        )

    others = [t for t in state.tracked if t.project not in _slots(state)]
    other = m.Other(run=sum(t.info.status in RUNNING for t in others), n=len(others))
    return tuple(row(p) for p in sorted(state.projects, key=lambda p: p.slot)), other


def _summary(item: QueueItem) -> str:
    return label(T(*VOICE) + item.text if item.voice else item.text, LIMITS["queue_text_max"], item.qid)


def queue_rows(state: HubState) -> tuple[m.QueueRow, ...]:
    """Newest first, at most 4 per project and 12 in total; items of unregistered projects are left out."""
    slots, cap = _slots(state), LIMITS["snapshot_queue_per_project_max"]
    visible = [(item, slots[item.project]) for item in open_items(state.queue) if item.project in slots]
    kept = [(item, slot) for n, (item, slot) in enumerate(visible) if [s for _, s in visible[:n]].count(slot) < cap]
    rows = (m.QueueRow(i.qid, s, _summary(i), i.intent, i.sid, i.uncertain or None) for i, s in kept)
    return tuple(rows)[: LIMITS["snapshot_queue_max"]]


def _content(state: HubState, now: float) -> tuple[Snapshots, tuple]:
    projects, other = project_rows(state, now)
    sessions, total = session_rows(state, now)
    revs = state.published.revs
    snaps = Snapshots(
        m.ProjectsSnapshot(revs.projects, projects, other),
        m.SessionsSnapshot(revs.sessions, total, sessions),
        m.QueueSnapshot(revs.queue, queue_rows(state)),
    )
    snaps = Snapshots(
        replace(snaps.projects, projects=_fit(snaps.projects, "projects")),
        replace(snaps.sessions, sessions=_fit(snaps.sessions, "sessions")),
        replace(snaps.queue, items=_fit(snaps.queue, "items")),
    )
    ageless = tuple(replace(r, age_s=0) for r in snaps.sessions.sessions)
    keys = ((snaps.projects.projects, other), (total, ageless), snaps.queue.items)
    return snaps, keys


def publish(state: HubState, now: float) -> tuple[Published, Snapshots, tuple[str, ...]]:
    """Current snapshots; revs bumped for kinds whose content changed since the last publish."""
    snaps, keys = _content(state, now)
    old = state.published
    changed = tuple(k for k, new, prev in zip(KINDS, keys, old.keys, strict=True) if new != prev)
    revs = m.Revs(*(getattr(old.revs, k) + (k in changed) for k in KINDS))
    snaps = Snapshots(
        replace(snaps.projects, rev=revs.projects),
        replace(snaps.sessions, rev=revs.sessions),
        replace(snaps.queue, rev=revs.queue),
    )
    return Published(revs, keys), snaps, changed


def behind(host: m.Revs, device: m.Revs) -> tuple[str, ...]:
    return tuple(k for k in KINDS if getattr(host, k) != getattr(device, k))
