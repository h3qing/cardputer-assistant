"""--scenarios parsing (06 WP5, 09 §9.3, docs/12 M3-H, docs/13 V40-H): the ten automated scenarios of
05 §9.3, the v0.2 SD, migration and Launcher scenarios, the issue #15 back keys and the v0.4.0 nav, menu
and lock scenarios, the UI language (docs/21), soak, the v0.3 recording scenarios and the memo's G0 grace
and double tap (gh #51), the mic calibration and the manual smoke.

`all` is the ten short automated scenarios. Everything else runs only when named. Each migration
phase runs alone with --state (one firmware per phase, flashed by hand in between); launcher_boot
needs --expect-launcher; `real_claude_smoke` starts a real Claude Code session, so it runs alone
with --project-dir. rec_nosd needs the cardputer-adv-test-nosd firmware and the other rec_*
scenarios the card, so they never share a run. mic_cal (mic gain brief §6) and its verify pass mic_verify
(§6.7) play audio through the Mac's speakers and pause the real cpad first, so each runs alone; so does
rec_cold_start (issue #31), which plays a tone through them during its memos. menu and lock record (a
meeting, a memo) when the card is there and skip that part on the nosd build. TIMEOUT_S:
scenarios that need more than the runner's default 300 s (soak itself scales with --minutes in the runner).
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from pathlib import Path

AUTOMATED = (
    "handshake",
    "capture",
    "offline_replay",
    "dup_ack",
    "reboot",
    "board",
    "spawn",
    "offline_spawn",
    "latency",
    "cjk",
)
LAUNCHER = ("launcher_boot",)
STORAGE = ("sd_empty", "sd_store", "sd_bulk")
UI = ("back_keys", "nav", "menu", "lock", "lang")
EXPLICIT = ("soak",)
RECORDING = ("rec_memo", "rec_meeting", "rec_resume", "rec_live_hold", "rec_reboot", "rec_rotation", "rec_soak")
REC_G0 = ("rec_memo_grace", "rec_memo_handsfree")  # gh #51 (docs/20 U26): the card, as RECORDING
REC_NOSD = ("rec_nosd",)
CALIBRATION = ("mic_cal", "mic_verify")
COLD_START = ("rec_cold_start",)
SPEAKER = CALIBRATION + COLD_START  # play through the Mac's speakers: mic_cal's preflight, run alone
MIGRATE = ("migrate_seed", "migrate_check", "migrate_drained")
MANUAL_ONLY = ("real_claude_smoke",)
CANONICAL = (
    LAUNCHER + STORAGE + AUTOMATED + UI + EXPLICIT + RECORDING + REC_G0 + REC_NOSD + SPEAKER + MIGRATE + MANUAL_ONLY
)
ALONE = SPEAKER + MIGRATE + MANUAL_ONLY
TIMEOUT_S: Mapping[str, float] = {
    **dict.fromkeys(RECORDING + REC_G0 + REC_NOSD, 600.0),
    **dict.fromkeys(("menu", "lock"), 600.0),  # settle (the card emptied first), then a pull
    "rec_rotation": 1200.0,  # 30 x 10 s segments, then about 10 MB to pull
    "rec_soak": 1800.0,  # 10 min of recording, then about 19 MB to pull
    "mic_cal": 1200.0,  # 11 captures of about 30 s, a possible re-probe, 5 lead runs, a reboot
    "mic_verify": 600.0,  # the B1 probe (maybe twice), a reboot, one capture at kMicGain, a reboot
    "rec_cold_start": 600.0,  # three memos and their pulls, the drain on a failure
}


class SelectionError(ValueError):
    """The --scenarios / --project-dir / --state / --expect-launcher combination is not runnable."""


def parse_scenarios(spec: str) -> tuple[str, ...]:
    """Names in canonical order, duplicates collapsed."""
    names = {n.strip() for n in spec.split(",") if n.strip()}
    if not names:
        raise SelectionError("no scenario selected")
    if "all" in names:
        names = (names - {"all"}) | set(AUTOMATED)
    unknown = sorted(names - set(CANONICAL))
    if unknown:
        raise SelectionError(f"unknown scenario(s) {', '.join(unknown)}; choose from all, {', '.join(CANONICAL)}")
    for name in ALONE:
        if name in names and len(names) > 1:
            raise SelectionError(f"{name} must run alone")
    return tuple(n for n in CANONICAL if n in names)


def _validate_project_dir(names: Sequence[str], project_dir: Path | None) -> None:
    smoke = "real_claude_smoke" in names
    if smoke and project_dir is None:
        raise SelectionError("real_claude_smoke needs --project-dir (an empty temporary directory)")
    if smoke and not Path(project_dir).is_dir():
        raise SelectionError("--project-dir must be an existing directory")
    if not smoke and project_dir is not None:
        raise SelectionError("--project-dir is only used by real_claude_smoke")


def _validate_state(names: Sequence[str], state: Path | None) -> None:
    phases = [n for n in names if n in MIGRATE]
    if not phases and state is not None:
        raise SelectionError(f"--state is only used by {', '.join(MIGRATE)}")
    if phases and state is None:
        raise SelectionError(f"{phases[0]} needs --state FILE (the seed shared by the three migration phases)")
    if "migrate_seed" in phases and not Path(state).parent.is_dir():
        raise SelectionError("--state must be in an existing directory")
    if phases and phases[0] != "migrate_seed" and not Path(state).is_file():
        raise SelectionError(f"{phases[0]} needs the --state file migrate_seed wrote")


def validate(
    names: Sequence[str], project_dir: Path | None, state: Path | None = None, expect_launcher: bool = False
) -> None:
    _validate_project_dir(names, project_dir)
    _validate_state(names, state)
    if set(names) & set(REC_NOSD) and set(names) & set(RECORDING + REC_G0):
        raise SelectionError("rec_nosd needs the cardputer-adv-test-nosd firmware, the other rec_* scenarios the card")
    if "launcher_boot" in names and not expect_launcher:
        raise SelectionError("launcher_boot needs --expect-launcher (the image installed from the Launcher)")
