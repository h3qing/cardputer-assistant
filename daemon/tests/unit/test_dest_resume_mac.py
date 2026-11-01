"""cpad's recovery never retries a send from this Mac (docs/25 §7.4): it has no text in events.jsonl's dest.* records
(the text lives in rec.result), and only a person sends it again. Pure."""

from __future__ import annotations

from cpad.dest import catalog as c
from cpad.dest.model import Dest, FileOpts
from cpad.dest.pending import fold_pending
from cpad.dest.router import Step, resume

REC = "3QZK1M7-0000A-0001"
AT = "2026-10-22T09:14:05.000+00:00"
LATER = "2026-10-22T09:20:00.000+00:00"
SB = Dest("file.sb", "file", "sb", 7, leaves_mac=False, file=FileOpts("~/sb", "inbox.md"))
CAT = c.put(c.defaults(), SB)


def mac_send(*after: dict) -> list[dict]:
    decided = {"ev": "dest.decided", "at": AT, "item": REC, "src": "voice", "want": "file.sb", "dest": "file.sb",
               "kind": "file", "action": "deliver", "reason": "mac", "how": "pick", "origin": "mac", "prof": None,
               "attempt": 1}  # fmt: skip
    return [decided, *after]


def failed(retryable: bool) -> dict:
    return {"ev": "dest.failed", "at": AT, "item": REC, "dest": "file.sb", "kind": "file", "code": "E_DEST_IO",
            "retryable": retryable, "attempt": 1, "prof": None}  # fmt: skip


def test_a_failed_mac_send_is_never_retried_by_cpad():
    pend = fold_pending(mac_send(failed(True)))
    assert resume(pend, CAT, at=LATER, startup=False) == Step()
    assert resume(pend, CAT, at=LATER, startup=True) == Step()


def test_a_mac_send_running_at_start_up_is_interrupted_not_downgraded():
    step = resume(fold_pending(mac_send()), CAT, at=LATER, startup=True)
    ((rec,),) = (step.records,)
    assert (rec["ev"], rec["code"], rec["retryable"], rec["item"]) == ("dest.failed", "E_INTERRUPTED", False, REC)
    assert step.deliveries == ()
