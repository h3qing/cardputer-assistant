"""Status for `get_status` (docs/15 §3.2, §3.4). STATUS_REQUEST is the only message ever sent to cpad.sock.

- cpad running: its status reply, reduced to an allow-list of scalar fields (no serial, dev, boot, paths,
  warnings or error text);
- cpad not running, or its reply is unusable: the same shape from files (projects.json, events.jsonl and the
  recordings' status.json), as `cpa status` does offline.
Project roots are never returned. Nothing is written.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping
from pathlib import Path
from types import MappingProxyType
from typing import Any

from cpad import __version__
from cpad.archive.fs import Root
from cpad.config.settings import Paths
from cpad.control import client
from cpad.events.log import EventLog
from cpad.projects.registry import ProjectsError, load_registry
from cpad.queue.fold import fold_queue
from cpad.recordings import index, sessions
from cpad.recordings.results import offline_statuses

STATUS_REQUEST: Mapping[str, str] = MappingProxyType({"cmd": "status"})
DEVICE = ("fw", "pending", "store", "launcher")
AGENT = ("status", "spawn", "reply")
RECORDINGS = ("on_device", "pulling", "waiting", "processing", "blocked", "failed", "last_result_at")
SCALARS = (str, int, float, bool, type(None))
Request = Callable[[Path, dict[str, Any]], Any]


def _scalar(value: Any) -> Any:
    return value if isinstance(value, SCALARS) else None


def _pick(doc: Any, keys: tuple[str, ...]) -> dict[str, Any]:
    source = doc if isinstance(doc, Mapping) else {}
    return {key: _scalar(source.get(key)) for key in keys}


def _registry(paths: Paths) -> tuple[Any, ...]:
    try:
        return load_registry(paths.projects)
    except (ProjectsError, ValueError, OSError):
        return ()


def _running_projects(raw: Any, paths: Paths) -> list[dict[str, Any]]:
    names = {p.key: p.name for p in _registry(paths)}
    rows = [p for p in raw if isinstance(p, Mapping)] if isinstance(raw, list) else []
    return [{"key": _scalar(p.get("key")), "name": names.get(p.get("key")), "slot": _scalar(p.get("slot")),
             "ok": _scalar(p.get("ok"))} for p in rows]  # fmt: skip


def _running(doc: Mapping[str, Any], paths: Paths) -> dict[str, Any]:
    return {"daemon": "running", "version": _scalar(doc.get("version")), "link": _scalar(doc.get("link")),
            "device": _pick(doc.get("device"), DEVICE), "agent": _pick(doc.get("agent"), AGENT),
            "projects": _running_projects(doc.get("projects"), paths), "queue_open": _scalar(doc.get("queue_open")),
            "recordings": _pick(doc.get("recordings"), RECORDINGS)}  # fmt: skip


def _offline(paths: Paths, root: Root) -> dict[str, Any]:
    events = EventLog(paths.events).load()
    idx = index.fold(events)
    rows = sessions.rows(idx, statuses=offline_statuses(root.path, idx))
    recs = sessions.summary(rows, idx=idx, on_device=None, pulling=False)
    projects = [{"key": p.key, "name": p.name, "slot": p.slot, "ok": p.ok} for p in _registry(paths)]
    return {"daemon": "not running", "version": __version__, "link": None, "device": None, "agent": None,
            "projects": projects, "queue_open": len(fold_queue(events).items),
            "recordings": _pick(recs, RECORDINGS)}  # fmt: skip


def status(paths: Paths, root: Root, request: Request = client.request) -> dict[str, Any]:
    try:
        doc = request(paths.sock, dict(STATUS_REQUEST))
    except (client.DaemonNotRunning, OSError, ValueError):
        return _offline(paths, root)
    return _running(doc, paths) if isinstance(doc, Mapping) else _offline(paths, root)
