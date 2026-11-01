"""run_e2e.py wiring that needs no device: every selectable name has a scenario, bad input exits 2."""

import pytest

from harness.runner import main, parse_args
from harness.select import CANONICAL
from scenarios import SCENARIOS


def test_every_selectable_name_has_a_scenario_and_nothing_else():
    assert tuple(SCENARIOS) == CANONICAL


def test_defaults_match_the_acceptance_commands():
    args = parse_args([])
    assert (args.scenarios, args.minutes, args.flips, args.taps, args.project_dir) == ("all", 60.0, 100, 30, None)


def test_bad_selection_exits_2_before_touching_any_port(capsys):
    assert main(["--scenarios", "bogus"]) == 2
    assert "unknown scenario" in capsys.readouterr().err


def test_real_claude_smoke_without_project_dir_exits_2(capsys):
    assert main(["--scenarios", "real_claude_smoke"]) == 2
    assert "--project-dir" in capsys.readouterr().err


def test_v02_flags_default_off():
    args = parse_args([])
    assert (args.state, args.expect_launcher) == (None, False)


def test_v02_flags_parse(tmp_path):
    args = parse_args(["--scenarios", "migrate_seed", "--state", str(tmp_path / "m.json"), "--expect-launcher"])
    assert (args.state, args.expect_launcher) == (tmp_path / "m.json", True)


def test_migration_without_state_exits_2(capsys):
    assert main(["--scenarios", "migrate_check"]) == 2
    assert "--state" in capsys.readouterr().err


def test_launcher_boot_without_expect_launcher_exits_2(capsys):
    assert main(["--scenarios", "launcher_boot,handshake"]) == 2
    assert "--expect-launcher" in capsys.readouterr().err


def test_recording_scenarios_get_their_own_timeouts():
    from harness.runner import _timeout
    from scenarios.common import Options

    assert _timeout("rec_soak", Options()) == 1800.0
    assert _timeout("rec_meeting", Options()) == 600.0
    assert _timeout("handshake", Options()) == 300.0
    assert _timeout("soak", Options(minutes=10)) == 1200.0
    assert _timeout("mic_cal", Options()) == 1200.0


def test_mic_cal_preflight_refusal_exits_2_before_touching_any_port(monkeypatch, capsys):
    import harness.runner as runner

    monkeypatch.setattr(runner, "mic_cal_preflight", lambda: "2 recording(s) still on the device")
    monkeypatch.setattr(runner, "port_holders", lambda: (_ for _ in ()).throw(AssertionError("port touched")))
    assert main(["--scenarios", "mic_cal"]) == 2
    assert "2 recording(s) still on the device" in capsys.readouterr().err


def test_mic_verify_has_the_same_preflight(monkeypatch, capsys):
    import harness.runner as runner

    monkeypatch.setattr(runner, "mic_cal_preflight", lambda: "cpad is still pulling a recording")
    monkeypatch.setattr(runner, "port_holders", lambda: (_ for _ in ()).throw(AssertionError("port touched")))
    assert main(["--scenarios", "mic_verify"]) == 2
    assert "cpad is still pulling a recording" in capsys.readouterr().err


def test_rec_cold_start_has_the_same_preflight(monkeypatch, capsys):
    import harness.runner as runner

    monkeypatch.setattr(runner, "mic_cal_preflight", lambda: "default output is 'AirPods', not the built-in speakers")
    monkeypatch.setattr(runner, "port_holders", lambda: (_ for _ in ()).throw(AssertionError("port touched")))
    assert main(["--scenarios", "rec_cold_start"]) == 2
    err = capsys.readouterr().err
    assert "rec_cold_start preflight" in err and "built-in speakers" in err


@pytest.mark.parametrize(
    ("name", "told"), [("mic_cal", True), ("mic_verify", True), ("rec_cold_start", True), ("handshake", False)]
)
def test_a_calibration_run_ends_with_the_way_back_to_the_release_build(monkeypatch, capsys, name, told):
    import harness.runner as runner

    async def ran(names, args):
        return 1

    monkeypatch.setattr(runner, "mic_cal_preflight", lambda: None)
    monkeypatch.setattr(runner, "port_holders", dict)
    monkeypatch.setattr(runner, "_run", ran)
    assert main(["--scenarios", name]) == 1
    err = capsys.readouterr().err
    assert ("pio run -e cardputer-adv -t upload" in err and "cpa pause 1" in err) is told


def test_the_report_path_reaches_the_scenarios(tmp_path):
    from harness.runner import _options

    assert _options(parse_args(["--report", str(tmp_path / "r.json")])).report == tmp_path / "r.json"


def test_the_v040_ui_scenarios_are_named_only_and_the_recording_ones_get_600_s():
    from harness.runner import _timeout
    from harness.select import parse_scenarios
    from scenarios.common import Options

    assert parse_scenarios("lock,nav,menu") == ("nav", "menu", "lock")
    assert not {"nav", "menu", "lock"} & set(parse_scenarios("all"))
    assert [_timeout(n, Options()) for n in ("nav", "menu", "lock")] == [300.0, 600.0, 600.0]
