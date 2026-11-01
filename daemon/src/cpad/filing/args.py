"""Arguments of `cpa filing` and its errors (docs/23 §3.3 rules 1 and 4). Pure.

argv carries only ids, project keys and enum words, each validated before anything is read or written; text
never does (`--text -` reads the search words from stdin). The error codes are fixed (protocol/filing/v1
error.schema.json); an error names the argument at fault (`field`) and never echoes its value. Exit code 2 for
a bad argument (E_BAD_ARG, E_UNKNOWN_PROJECT), 1 when the recording's state refuses the action.
"""

from __future__ import annotations

from typing import Any

from cpad.archive import ids
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import matches

E_BAD_ARG = "E_BAD_ARG"
E_UNKNOWN_ID = "E_UNKNOWN_ID"
E_UNKNOWN_PROJECT = "E_UNKNOWN_PROJECT"
E_NO_TEXT = "E_NO_TEXT"
E_ALREADY = "E_ALREADY"
E_UNAVAILABLE = "E_UNAVAILABLE"
USAGE = (E_BAD_ARG, E_UNKNOWN_PROJECT)
VERSION = 1


class FilingError(ValueError):
    """A refused `cpa filing` call: a fixed code and the argument at fault; nothing was written."""

    def __init__(self, code: str, field: str | None = None) -> None:
        super().__init__(f"{code} ({field})" if field else code)
        self.code = code
        self.field = field

    @property
    def exit_code(self) -> int:
        return 2 if self.code in USAGE else 1

    def doc(self) -> dict[str, Any]:
        return {"ok": False, "v": VERSION, "error": self.code, **({"field": self.field} if self.field else {})}


def parse_id(text: Any) -> str:
    """A recording session id (MonoId) in any case, returned upper case as events.jsonl has it."""
    try:
        return ids.parse_sid(text).upper()
    except ids.BadId:
        raise FilingError(E_BAD_ARG, "id") from None


def parse_key(text: Any, field: str = "project") -> str:
    if not matches(LIMITS["project_key_pattern"], text):
        raise FilingError(E_BAD_ARG, field)
    return text


def parse_choice(text: Any, choices: tuple[str, ...], field: str) -> str:
    if text not in choices:
        raise FilingError(E_BAD_ARG, field)
    return text
