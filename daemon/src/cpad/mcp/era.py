"""Protocol eras (docs/15 §3.3). Pure.

Modern (2026-07-28): no session; every request carries the version and client capabilities in
params._meta. Legacy (2025-11-25, 2025-06-18): one `initialize`, then the whole process keeps the
negotiated version. The client's self-reported name is recorded in the audit and never trusted.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any

MODERN = ("2026-07-28",)
LEGACY = ("2025-11-25", "2025-06-18")
META_VERSION = "io.modelcontextprotocol/protocolVersion"
META_CAPS = "io.modelcontextprotocol/clientCapabilities"
META_CLIENT = "io.modelcontextprotocol/clientInfo"
CLIENT_FIELD_MAX = 64
_CONTROL = re.compile(r"[\u0000-\u001f\u007f-\u009f  ]")

NO_VERSION = (
    "No protocol version. Either send initialize (protocolVersion 2025-11-25 or 2025-06-18) first, or put "
    "io.modelcontextprotocol/protocolVersion 2026-07-28 and io.modelcontextprotocol/clientCapabilities in params._meta"
)


@dataclass(frozen=True, slots=True)
class ClientInfo:
    name: str
    version: str


UNKNOWN = ClientInfo("unknown", "")


@dataclass(frozen=True, slots=True)
class Session:
    legacy: str | None = None
    client: ClientInfo | None = None


@dataclass(frozen=True, slots=True)
class Era:
    name: str  # "legacy" | "modern"
    version: str
    client: ClientInfo


@dataclass(frozen=True, slots=True)
class Fault:
    code: int
    message: str
    data: dict[str, Any] | None = None


def _bounded(value: Any) -> str:
    return _CONTROL.sub(" ", value)[:CLIENT_FIELD_MAX] if isinstance(value, str) else ""


def client_info(raw: Any) -> ClientInfo:
    if not isinstance(raw, dict):
        return UNKNOWN
    return ClientInfo(_bounded(raw.get("name")) or UNKNOWN.name, _bounded(raw.get("version")))


def negotiate(requested: str) -> str:
    return requested if requested in LEGACY else LEGACY[0]


def initialize(session: Session, params: dict[str, Any]) -> tuple[Session, str] | Fault:
    if session.legacy is not None:
        return Fault(-32600, "Already initialized")
    requested = params.get("protocolVersion")
    if not isinstance(requested, str):
        return Fault(-32602, "protocolVersion must be a string")
    version = negotiate(requested)
    return Session(version, client_info(params.get("clientInfo"))), version


def has_modern_version(params: dict[str, Any]) -> bool:
    meta = params.get("_meta")
    return isinstance(meta, dict) and META_VERSION in meta


def _modern(meta: dict[str, Any]) -> Era | Fault:
    version, caps = meta.get(META_VERSION), meta.get(META_CAPS)
    if not isinstance(version, str) or not isinstance(caps, dict):
        return Fault(-32602, "params._meta needs a protocolVersion string and a clientCapabilities object")
    if version not in MODERN:
        return Fault(-32022, "Unsupported protocol version", {"supported": list(MODERN),
                                                               "requested": version[:CLIENT_FIELD_MAX]})  # fmt: skip
    return Era("modern", version, client_info(meta.get(META_CLIENT)))


def era_of(session: Session, params: dict[str, Any]) -> Era | Fault:
    """Which era serves this request, or the error to send."""
    if session.legacy is not None:
        return Era("legacy", session.legacy, session.client or UNKNOWN)
    meta = params.get("_meta")
    if not isinstance(meta, dict) or (META_VERSION not in meta and META_CAPS not in meta):
        return Fault(-32602, NO_VERSION, {"supported": [*MODERN, *LEGACY]})
    return _modern(meta)
