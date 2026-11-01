"""cpa-stt's own state, $CPA_HOME/stt/state.json (0600): what `cpa-stt status` shows and the retry clock.

- failures: per session, how often the command failed on it while the self-test passed; the next try is
  30 s, 2 min, then 8 min later, and the 4th failure is final (failed / E_STT, retryable) (docs/22 §4.6);
- blocked: model_missing or engine when the environment is broken (only status.json is written then);
- recent: the last 10 runs (session id, length, time taken, exit code), never any text;
- foreign / busy_by: folders another transcriber writes, and who held a fresh claim at the last pass.
A missing or corrupt file starts fresh.
"""

from __future__ import annotations

import json
from collections.abc import Mapping
from dataclasses import asdict, dataclass, field, replace
from pathlib import Path
from types import MappingProxyType
from typing import Any

from cpad.config.files import ensure_private_dir, write_json

BACKOFF_MS = (30_000, 120_000, 480_000)
RECENT_MAX = 10


@dataclass(frozen=True, slots=True)
class Failure:
    n: int
    next_ms: int


@dataclass(frozen=True, slots=True)
class Recent:
    session: str
    dur_ms: int
    took_ms: int
    exit: int | None


def _no_failures() -> Mapping[str, Failure]:
    return MappingProxyType({})


@dataclass(frozen=True, slots=True)
class SttState:
    blocked: str | None = None
    selftest_ok: bool | None = None
    selftest_at: str | None = None
    config_sig: str | None = None
    failures: Mapping[str, Failure] = field(default_factory=_no_failures)
    recent: tuple[Recent, ...] = ()
    foreign: tuple[str, ...] = ()
    busy_by: str | None = None


def failures(s: SttState, session: str) -> Failure | None:
    return s.failures.get(session)


def due(s: SttState, session: str, now_ms: int) -> bool:
    found = s.failures.get(session)
    return found is None or now_ms >= found.next_ms


def fail(s: SttState, session: str, now_ms: int) -> tuple[SttState, bool]:
    """Count one failure; (new state, True when retries are used up)."""
    n = (s.failures[session].n if session in s.failures else 0) + 1
    if n > len(BACKOFF_MS):
        return clear(s, session), True
    entry = Failure(n, now_ms + BACKOFF_MS[n - 1])
    return replace(s, failures=MappingProxyType({**s.failures, session: entry})), False


def clear(s: SttState, session: str) -> SttState:
    return replace(s, failures=MappingProxyType({k: v for k, v in s.failures.items() if k != session}))


def add_recent(s: SttState, entry: Recent) -> SttState:
    return replace(s, recent=(*s.recent, entry)[-RECENT_MAX:])


def _dump(s: SttState) -> dict[str, Any]:
    return {"blocked": s.blocked, "selftest_ok": s.selftest_ok, "selftest_at": s.selftest_at,
            "config_sig": s.config_sig, "failures": {k: asdict(v) for k, v in s.failures.items()},
            "recent": [asdict(r) for r in s.recent], "foreign": list(s.foreign), "busy_by": s.busy_by}  # fmt: skip


def _parse(doc: Any) -> SttState:
    failures_ = MappingProxyType({k: Failure(int(v["n"]), int(v["next_ms"])) for k, v in doc["failures"].items()})
    recent = tuple(Recent(str(r["session"]), int(r["dur_ms"]), int(r["took_ms"]), r["exit"]) for r in doc["recent"])
    return SttState(doc["blocked"], doc["selftest_ok"], doc["selftest_at"], doc["config_sig"], failures_, recent,
                    tuple(str(f) for f in doc["foreign"]), doc["busy_by"])  # fmt: skip


def load(path: Path) -> SttState:
    try:
        return _parse(json.loads(path.read_text(encoding="utf-8")))
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return SttState()


def save(path: Path, s: SttState) -> None:
    ensure_private_dir(path.parent)
    write_json(path, _dump(s))


__all__ = ["BACKOFF_MS", "Failure", "Recent", "SttState", "replace"]
