"""Validators for the destination messages (docs/14 §7.2), written by hand from protocol/schema/v0.

dests.snapshot   H->D  rev, dests[<=12] {id, slot, label, kind, key?, fl?}

protocol/specs.py registers these in SPECS, so this module imports only protocol.validate and
protocol.limits: reaching protocol.codec from here would be an import cycle.
"""

from __future__ import annotations

from cpad.protocol.validate import Check, absent, array, integer, nullable, obj, string

SLOT_MAX = 12
DESTS_MAX = 12
LABEL_MAX = 24  # UTF-8 bytes
DEST_ID_PATTERN = r"^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,11})?$"
KIND_PATTERN = r"^[a-z]{3,7}$"  # a pattern, not an enum: old firmware hides a new kind instead of refusing the list
KEY_PATTERN = r"^[a-z]$"
FLAGS_PATTERN = r"^[a-z]{0,8}$"  # letters this firmware does not know are ignored (docs/25 §4.6)
LABEL_PATTERN = r'^[^\u0000-\u001f\u007f"\\]*$'  # no escapes needed on the wire (§7.6)
UINT32 = integer(0, 4294967295)


def _request(fields: dict[str, Check]) -> Check:
    """Not a reply: re must be absent (as protocol.specs.request)."""
    return obj(fields, {"re": absent()})


def _dests_snapshot() -> Check:
    row = obj(
        {
            "id": string(DEST_ID_PATTERN),
            "slot": integer(1, SLOT_MAX),
            "label": string(LABEL_PATTERN, min_len=1, max_bytes=LABEL_MAX),
            "kind": string(KIND_PATTERN),
        },
        {
            "key": nullable(string(KEY_PATTERN)),
            "fl": nullable(string(FLAGS_PATTERN)),
        },
    )
    return _request({"rev": UINT32, "dests": array(row, max_items=DESTS_MAX, unique_by="slot")})


def dest_specs() -> dict[str, Check]:
    return {"dests.snapshot": _dests_snapshot()}
