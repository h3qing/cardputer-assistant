"""Scenario selection for run_e2e.py --scenarios (06 WP5)."""

import pytest

from harness.select import (
    AUTOMATED,
    CALIBRATION,
    CANONICAL,
    COLD_START,
    LAUNCHER,
    MANUAL_ONLY,
    MIGRATE,
    REC_G0,
    REC_NOSD,
    RECORDING,
    SPEAKER,
    STORAGE,
    TIMEOUT_S,
    UI,
    SelectionError,
    parse_scenarios,
    validate,
)


def test_automated_list_is_05_section_9_3_items_1_to_10():
    assert AUTOMATED == (
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


def test_acceptance_list_parses_in_canonical_order():
    spec = "handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk"
    assert parse_scenarios(spec) == AUTOMATED


def test_order_is_canonical_not_as_typed_and_duplicates_collapse():
    assert parse_scenarios("cjk, capture,handshake,capture") == ("handshake", "capture", "cjk")


def test_all_means_the_automated_ten_without_soak_or_real_claude():
    names = parse_scenarios("all")
    assert names == AUTOMATED
    assert "soak" not in names
    assert "real_claude_smoke" not in names


def test_soak_is_selected_only_by_name():
    assert parse_scenarios("soak") == ("soak",)


def test_unknown_names_are_reported():
    with pytest.raises(SelectionError, match="bogus"):
        parse_scenarios("handshake,bogus")


@pytest.mark.parametrize("spec", ["", " , ", ","])
def test_empty_selection_is_refused(spec):
    with pytest.raises(SelectionError):
        parse_scenarios(spec)


def test_real_claude_smoke_is_manual_only_and_must_run_alone():
    assert MANUAL_ONLY == ("real_claude_smoke",)
    with pytest.raises(SelectionError, match="alone"):
        parse_scenarios("real_claude_smoke,handshake")


def test_real_claude_smoke_needs_a_project_dir(tmp_path):
    with pytest.raises(SelectionError, match="--project-dir"):
        validate(("real_claude_smoke",), project_dir=None)
    validate(("real_claude_smoke",), project_dir=tmp_path)


def test_real_claude_smoke_project_dir_must_exist(tmp_path):
    with pytest.raises(SelectionError, match="directory"):
        validate(("real_claude_smoke",), project_dir=tmp_path / "missing")


def test_project_dir_is_only_for_the_real_claude_smoke(tmp_path):
    with pytest.raises(SelectionError, match="real_claude_smoke"):
        validate(("handshake",), project_dir=tmp_path)
    validate(("handshake",), project_dir=None)


# --- v0.2 (09 §9.3): SD, migration and Launcher scenarios, --state and --expect-launcher ----------


def test_v02_groups_and_canonical_order():
    assert STORAGE == ("sd_empty", "sd_store", "sd_bulk")
    assert MIGRATE == ("migrate_seed", "migrate_check", "migrate_drained")
    assert LAUNCHER == ("launcher_boot",)
    assert UI == ("back_keys", "nav", "menu", "lock", "lang")  # v0.4.0 (docs/13 V40-H) after #15; docs/21
    assert CANONICAL == (
        LAUNCHER
        + STORAGE
        + AUTOMATED
        + UI
        + ("soak",)
        + RECORDING
        + REC_G0
        + REC_NOSD
        + CALIBRATION
        + COLD_START
        + MIGRATE
        + MANUAL_ONLY
    )


def test_the_sd_acceptance_list_runs_storage_before_the_v01_ten():
    spec = "sd_empty,sd_store,sd_bulk," + ",".join(AUTOMATED)
    assert parse_scenarios(spec) == STORAGE + AUTOMATED


def test_launcher_boot_runs_first_in_the_launcher_acceptance_list():
    assert parse_scenarios("handshake,capture,reboot,launcher_boot") == ("launcher_boot", "handshake", "capture",
                                                                            "reboot")  # fmt: skip


def test_all_still_means_only_the_v01_ten():
    assert not set(parse_scenarios("all")) & set(STORAGE + MIGRATE + LAUNCHER + UI)


@pytest.mark.parametrize("phase", ["migrate_seed", "migrate_check", "migrate_drained"])
def test_each_migration_phase_runs_alone(phase):
    with pytest.raises(SelectionError, match=f"{phase} must run alone"):
        parse_scenarios(f"{phase},handshake")
    assert parse_scenarios(phase) == (phase,)


@pytest.mark.parametrize("phase", ["migrate_seed", "migrate_check", "migrate_drained"])
def test_migration_phases_need_state(phase):
    with pytest.raises(SelectionError, match="--state"):
        validate((phase,), project_dir=None, state=None)


def test_migrate_seed_state_may_be_new_but_its_directory_must_exist(tmp_path):
    validate(("migrate_seed",), project_dir=None, state=tmp_path / "mig.json")
    with pytest.raises(SelectionError, match="directory"):
        validate(("migrate_seed",), project_dir=None, state=tmp_path / "missing" / "mig.json")


@pytest.mark.parametrize("phase", ["migrate_check", "migrate_drained"])
def test_later_phases_need_the_state_file_migrate_seed_wrote(tmp_path, phase):
    with pytest.raises(SelectionError, match="migrate_seed"):
        validate((phase,), project_dir=None, state=tmp_path / "mig.json")
    (tmp_path / "mig.json").write_text("{}")
    validate((phase,), project_dir=None, state=tmp_path / "mig.json")


def test_state_is_only_for_the_migration_phases(tmp_path):
    with pytest.raises(SelectionError, match="--state is only used"):
        validate(("sd_store",), project_dir=None, state=tmp_path / "mig.json")


def test_launcher_boot_needs_expect_launcher():
    with pytest.raises(SelectionError, match="--expect-launcher"):
        validate(("launcher_boot", "handshake"), project_dir=None)
    validate(("launcher_boot", "handshake"), project_dir=None, expect_launcher=True)


def test_expect_launcher_alone_is_allowed_for_the_v01_scenarios():
    validate(("handshake", "capture", "reboot"), project_dir=None, expect_launcher=True)


# --- v0.3 (docs/12 M3-H): recording scenarios -------------------------------------------------


def test_recording_groups():
    assert RECORDING == ("rec_memo", "rec_meeting", "rec_resume", "rec_live_hold", "rec_reboot", "rec_rotation",
                         "rec_soak")  # fmt: skip
    assert REC_NOSD == ("rec_nosd",)


def test_the_g0_memo_scenarios_need_the_card():  # gh #51
    assert REC_G0 == ("rec_memo_grace", "rec_memo_handsfree")
    assert parse_scenarios("rec_memo_handsfree,rec_memo,rec_memo_grace") == ("rec_memo",) + REC_G0
    with pytest.raises(SelectionError, match="rec_nosd needs"):
        validate(("rec_memo_grace", "rec_nosd"), project_dir=None)


def test_the_m3h_acceptance_list_parses_in_canonical_order():
    spec = "rec_memo,rec_meeting,rec_resume,rec_reboot,rec_live_hold,rec_rotation,rec_soak"
    assert parse_scenarios(spec) == RECORDING


def test_the_nosd_acceptance_list_runs_the_v01_ten_then_rec_nosd():
    spec = "rec_nosd,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk"
    assert parse_scenarios(spec) == AUTOMATED + REC_NOSD


def test_all_does_not_select_any_recording_scenario():
    assert not set(parse_scenarios("all")) & set(RECORDING + REC_NOSD)


def test_rec_nosd_is_not_combined_with_the_sd_recording_scenarios():
    with pytest.raises(SelectionError, match="nosd"):
        validate(("rec_memo", "rec_nosd"), project_dir=None)
    validate(("handshake", "rec_nosd"), project_dir=None)
    validate(("handshake", "rec_memo"), project_dir=None)


def test_long_recording_scenarios_get_room_for_the_recording_and_the_pull():
    assert TIMEOUT_S["rec_soak"] >= 10 * 60 + 600
    assert TIMEOUT_S["rec_rotation"] >= 5 * 60 + 300
    assert all(TIMEOUT_S[n] >= 600 for n in RECORDING + REC_NOSD)
    assert "handshake" not in TIMEOUT_S


# --- v0.3.1 (mic gain brief §4, §6): the mic calibration -----------------------------------------


def test_mic_cal_is_a_calibration_that_runs_alone_and_only_by_name():
    assert CALIBRATION == ("mic_cal", "mic_verify")
    assert parse_scenarios("mic_cal") == ("mic_cal",)
    assert "mic_cal" not in parse_scenarios("all")
    with pytest.raises(SelectionError, match="mic_cal must run alone"):
        parse_scenarios("mic_cal,rec_memo")


def test_mic_verify_runs_alone_too():
    assert parse_scenarios("mic_verify") == ("mic_verify",)
    assert "mic_verify" not in parse_scenarios("all")
    with pytest.raises(SelectionError, match="must run alone"):
        parse_scenarios("mic_cal,mic_verify")


def test_mic_cal_gets_room_for_eleven_captures_and_the_lead_phase():
    assert TIMEOUT_S["mic_cal"] == 1200.0
    assert TIMEOUT_S["mic_verify"] == 600.0  # the B1 probe (maybe twice), a reboot, one capture, a reboot


# --- v0.3.2 (issue #31): the codec's cold start --------------------------------------------------


def test_rec_cold_start_plays_the_tone_so_it_runs_alone_with_the_speaker_group():
    assert COLD_START == ("rec_cold_start",)
    assert SPEAKER == CALIBRATION + COLD_START
    assert parse_scenarios("rec_cold_start") == ("rec_cold_start",)
    assert "rec_cold_start" not in parse_scenarios("all")
    with pytest.raises(SelectionError, match="rec_cold_start must run alone"):
        parse_scenarios("rec_cold_start,rec_memo")
    assert TIMEOUT_S["rec_cold_start"] == 600.0  # three memos and their pulls, the drain on a failure
