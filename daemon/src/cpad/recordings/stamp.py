"""The Mac-side environment stamp of a recording (docs/23 §4.2, P1-D1). Pure.

env = {"host": ..., "link": ...}, computed from memory once, when segment 0 lands; it runs no command:
- host: this Mac's host_label (config.json, the user's own name for it, `cpa host name`); None while unset.
  Never the macOS computer name, which often carries the user's real name (D10).
- link: "here" only when cpad can prove the device was linked to it when segment 0 started: same boot as the
  current link and link_since.up_ms <= at.up_ms <= the latest hello / pong up_ms. A drop always ends in a new hello,
  which resets link_since, so a cpad restart or a sleeping Mac can only add "unknown", never a false "here".
  Extensible enum: the device-side stamp (docs/23 §4.4) may add "other"; readers show unknown values as unknown.
The stamp is an annotation: it never decides a profile, a route or a project.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from cpad.config.host import valid_label
from cpad.protocol.file_messages import At
from cpad.recordings.wall import Seen, boot_of

LINKS = ("here", "unknown")


def link(seg_id: str, at: At | None, since: Seen | None, seen: Seen | None) -> str:
    """since: the hello of the current link; seen: its latest hello / pong. None when there is no link."""
    if at is None or since is None or seen is None:
        return "unknown"
    same_boot = since.boot == seen.boot == boot_of(seg_id)
    return "here" if same_boot and since.up_ms <= at.up_ms <= seen.up_ms else "unknown"


def env(*, host: str | None, seg_id: str, at: At | None, since: Seen | None, seen: Seen | None) -> dict[str, Any]:
    return {"host": host, "link": link(seg_id, at, since, seen)}


def of_record(record: Mapping[str, Any]) -> dict[str, Any] | None:
    """The env of a rec.seg.stored record when well-formed, else None: only a known shape reaches a manifest."""
    found = record.get("env")
    if not isinstance(found, Mapping) or "host" not in found or found.get("link") not in LINKS:
        return None
    host = found["host"]
    return {"host": host, "link": found["link"]} if host is None or valid_label(host) else None
