"""HIL scenarios by --scenarios name: 05 §9.3 (1-11), the v0.2 SD, migration and Launcher scenarios
of 09 §9.3, the issue #15 back keys, the v0.4.0 nav / menu / lock (docs/13 V40-H), the UI language
(docs/21), the v0.3 recording scenarios of docs/12 M3-H and the memo's G0 grace and double tap (gh #51),
the mic gain calibration and its verify pass, the codec's cold start (issue #31), and the manual
real_claude_smoke. Order = harness.select.CANONICAL.
"""

from __future__ import annotations

from collections.abc import Awaitable, Callable
from typing import Any

from scenarios.agents import board, offline_spawn, spawn
from scenarios.back_keys import back_keys
from scenarios.capture import capture, dup_ack, offline_replay
from scenarios.cjk import cjk
from scenarios.common import Ctx
from scenarios.lang import lang
from scenarios.latency import latency
from scenarios.launcher import launcher_boot
from scenarios.link import handshake, reboot
from scenarios.lock import lock
from scenarios.menu import menu
from scenarios.mic_cal import mic_cal, mic_verify
from scenarios.nav import nav
from scenarios.rec_cold import rec_cold_start
from scenarios.rec_g0 import rec_memo_grace, rec_memo_handsfree
from scenarios.rec_long import rec_reboot, rec_rotation, rec_soak
from scenarios.recording import rec_live_hold, rec_meeting, rec_memo, rec_nosd, rec_resume
from scenarios.smoke import real_claude_smoke
from scenarios.soak import soak
from scenarios.storage import migrate_check, migrate_drained, migrate_seed, sd_bulk, sd_empty, sd_store

Scenario = Callable[[Ctx], Awaitable[dict[str, Any]]]

SCENARIOS: dict[str, Scenario] = {
    "launcher_boot": launcher_boot,
    "sd_empty": sd_empty,
    "sd_store": sd_store,
    "sd_bulk": sd_bulk,
    "handshake": handshake,
    "capture": capture,
    "offline_replay": offline_replay,
    "dup_ack": dup_ack,
    "reboot": reboot,
    "board": board,
    "spawn": spawn,
    "offline_spawn": offline_spawn,
    "latency": latency,
    "cjk": cjk,
    "back_keys": back_keys,
    "nav": nav,
    "menu": menu,
    "lock": lock,
    "lang": lang,
    "soak": soak,
    "rec_memo": rec_memo,
    "rec_meeting": rec_meeting,
    "rec_resume": rec_resume,
    "rec_live_hold": rec_live_hold,
    "rec_reboot": rec_reboot,
    "rec_rotation": rec_rotation,
    "rec_soak": rec_soak,
    "rec_memo_grace": rec_memo_grace,
    "rec_memo_handsfree": rec_memo_handsfree,
    "rec_nosd": rec_nosd,
    "mic_cal": mic_cal,
    "mic_verify": mic_verify,
    "rec_cold_start": rec_cold_start,
    "migrate_seed": migrate_seed,
    "migrate_check": migrate_check,
    "migrate_drained": migrate_drained,
    "real_claude_smoke": real_claude_smoke,
}
