"""cpa-stt configuration (docs/22 §4.3, §4.4): engine parameters are data, checked before they are saved."""

from __future__ import annotations

import json
import os
import stat
from pathlib import Path

import pytest

from cpa_stt import config, template

GOOD = ["/bin/echo", "-m", "/bin/sh", "--offset", "{offset_ms}", "{wav}"]


def _cfg(**doc):
    return config.parse_config({"provider": "command", "argv": GOOD, **doc})


def test_default_is_off_and_transcribes_meetings_too():
    cfg = config.parse_config({})
    assert cfg.provider == "off" and not cfg.on
    assert cfg.argv is None and cfg.meetings is True and cfg.timeout_s == 120
    assert cfg.sandbox_mach == ("com.apple.MTLCompilerService", "com.apple.system.opendirectoryd.membership")
    assert cfg.drop_lines == () and cfg.recordings_dir is None


def test_command_provider_keeps_argv_as_a_tuple():
    cfg = _cfg()
    assert cfg.on and cfg.argv == tuple(GOOD)


@pytest.mark.parametrize(
    "doc",
    [
        {"provider": "auto"},
        {"provider": "command"},
        {"provider": "command", "argv": []},
        {"provider": "command", "argv": "whisper-cli {wav}"},
        {"provider": "command", "argv": ["whisper-cli", "{wav}"]},
        {"provider": "command", "argv": ["/bin/echo", "{wav}", "{wav}"]},
        {"provider": "command", "argv": ["/bin/echo", "-f"]},
        {"provider": "command", "argv": ["/bin/echo", "--file={wav}"]},
        {"provider": "command", "argv": ["/bin/echo", "{offset_ms}", "{offset_ms}", "{wav}"]},
        {"provider": "command", "argv": ["/bin/echo", "x{offset_ms}", "{wav}"]},
        {"provider": "command", "argv": ["/bin/echo", 3, "{wav}"]},
        {"provider": "command", "argv": ["/bin/echo", "a\x00b", "{wav}"]},
        {"timeout_s": 0},
        {"timeout_s": True},
        {"meetings": "yes"},
        {"sandbox_mach": ['com.apple.x") (allow network*']},
        {"sandbox_mach": "com.apple.MTLCompilerService"},
        {"drop_lines": "x"},
        {"drop_lines": [1]},
        {"recordings_dir": 5},
        {"timout_s": 30},
        [],
    ],
)
def test_bad_configs_are_refused(doc):
    with pytest.raises(config.SttConfigError):
        config.parse_config(doc)


def test_dump_and_load_round_trip_with_private_modes(tmp_path):
    paths = config.SttPaths.at(tmp_path / "home")
    cfg = _cfg(drop_lines=["谢谢观看"], meetings=False, timeout_s=60)
    config.save_config(paths.config, cfg)
    assert config.load_config(paths.config) == cfg
    assert stat.S_IMODE(paths.config.stat().st_mode) == 0o600
    assert stat.S_IMODE(paths.dir.stat().st_mode) == 0o700
    assert json.loads(paths.config.read_text())["argv"] == GOOD


def test_missing_config_file_means_off(tmp_path):
    assert config.load_config(tmp_path / "nope.json") == config.SttConfig()


def test_unreadable_config_file_is_an_error(tmp_path):
    path = tmp_path / "config.json"
    path.write_text("{", encoding="utf-8")
    with pytest.raises(config.SttConfigError):
        config.load_config(path)


def test_paths_layout(tmp_path):
    paths = config.SttPaths.at(tmp_path)
    assert paths.dir == tmp_path / "stt"
    assert (paths.config, paths.state, paths.log) == (tmp_path / "stt/config.json", tmp_path / "stt/state.json",
                                                      tmp_path / "stt/log/cpa-stt.log")  # fmt: skip
    assert paths.models == tmp_path / "models"


def test_missing_paths_checks_argv0_and_every_absolute_argument(tmp_path):
    model = tmp_path / "model.bin"
    model.write_bytes(b"x")
    link = tmp_path / "linked.bin"
    link.symlink_to(model)
    argv = ("/bin/echo", "-m", str(link), "-x", str(tmp_path / "gone.bin"), "relative.bin", "{wav}")
    assert config.missing_paths(argv) == (str(tmp_path / "gone.bin"),)
    assert config.missing_paths(("/nope/whisper-cli", "{wav}")) == ("/nope/whisper-cli",)
    dangling = tmp_path / "dangling.bin"
    dangling.symlink_to(tmp_path / "moved.bin")
    assert config.missing_paths(("/bin/echo", str(dangling), "{wav}")) == (str(dangling),)


def test_prompts_are_read_from_the_prompt_flag():
    argv = ("/bin/w", "--prompt", "Cardputer, WhisperWoof", "--prompt=Acmeshop", "-f", "{wav}")
    assert config.prompts(argv) == ("Cardputer, WhisperWoof", "Acmeshop")
    assert config.prompts(("/bin/w", "--prompt")) == ()


def test_recordings_root_defaults_to_the_notes_dir(tmp_path):
    assert config.recordings_root(config.SttConfig(), tmp_path) == tmp_path / "recordings"
    custom = config.parse_config({"recordings_dir": str(tmp_path / "elsewhere")})
    assert config.recordings_root(custom, tmp_path / "notes") == tmp_path / "elsewhere"


# --- example templates (data, not code) -------------------------------------------------------


def _inputs(models: Path, projects=(), which=lambda name: "/bin/echo"):
    return template.Inputs(models_dir=models, projects=tuple(projects), which=which)


def _models(tmp_path, *names):
    models = tmp_path / "models"
    models.mkdir(parents=True, exist_ok=True)
    for name in names:
        (models / name).write_bytes(b"m")
    return models


def test_example_is_whisper_cpp_large_v3_turbo_auto_greedy(tmp_path):
    models = _models(tmp_path, "ggml-large-v3-turbo.bin")
    doc = template.fill(template.example(), _inputs(models))
    cfg = config.parse_config(doc)
    argv = list(cfg.argv)
    assert argv[0] == "/bin/echo"
    assert argv[argv.index("-m") + 1] == str(models / "ggml-large-v3-turbo.bin")
    assert argv[argv.index("-l") + 1] == "auto"
    assert (argv[argv.index("-bs") + 1], argv[argv.index("-bo") + 1]) == ("1", "1")
    assert "{wav}" in argv
    assert "--vad" not in argv and "--prompt" not in argv


@pytest.mark.parametrize("names", [("ggml-large-v3-turbo.bin",), ("ggml-large-v3-turbo.bin", "ggml-silero-v5.1.2.bin")])
def test_example_never_skips_the_lead_with_ot(tmp_path, names):
    """whisper.cpp 1.8.3 applies -ot to the audio VAD kept, not to the file: with --vad, -ot 150 cut the first
    words of real memos and turned one into a phantom line (so nothing heard). The lead is left to whisper."""
    argv = template.fill(template.example(), _inputs(_models(tmp_path, *names)))["argv"]
    assert "{offset_ms}" not in argv and "-ot" not in argv


def test_example_adds_vad_when_a_silero_model_is_there_and_projects_as_the_prompt(tmp_path):
    models = _models(tmp_path, "ggml-large-v3-turbo.bin", "ggml-silero-v5.1.2.bin", "ggml-silero-v6.2.0.bin")
    argv = template.fill(template.example(), _inputs(models, ["Cardputer", "WhisperWoof"]))["argv"]
    assert argv[argv.index("-vm") + 1] == str(models / "ggml-silero-v6.2.0.bin")
    assert argv[argv.index("--vad") + 1] == "-vm"
    assert argv[argv.index("--prompt") + 1] == "Cardputer, WhisperWoof"


def test_a_symlinked_model_counts(tmp_path):
    real = _models(tmp_path / "real", "ggml-large-v3-turbo.bin") / "ggml-large-v3-turbo.bin"
    models = tmp_path / "models"
    models.mkdir()
    (models / "ggml-large-v3-turbo.bin").symlink_to(real)
    argv = template.fill(template.example(), _inputs(models))["argv"]
    assert str(models / "ggml-large-v3-turbo.bin") in argv


def test_missing_model_or_binary_says_what_to_do(tmp_path):
    with pytest.raises(template.TemplateError, match="ggml-large-v3-turbo.bin.*cpa-stt setup"):
        template.fill(template.example(), _inputs(_models(tmp_path)))
    models = _models(tmp_path, "ggml-large-v3-turbo.bin")
    with pytest.raises(template.TemplateError, match="whisper-cli"):
        template.fill(template.example(), _inputs(models, which=lambda name: None))


def test_plain_configs_pass_through_unchanged(tmp_path):
    doc = {"provider": "command", "argv": GOOD, "timeout_s": 30}
    assert template.fill(doc, _inputs(tmp_path)) == doc


@pytest.mark.parametrize("bad", ["<MODEL:../x.bin>", "<MODEL:a/b.bin>", "<NOPE>", "<MODEL>"])
def test_bad_tokens_are_refused(tmp_path, bad):
    with pytest.raises(template.TemplateError):
        template.fill({"argv": ["/bin/echo", bad, "{wav}"]}, _inputs(tmp_path))


def test_nested_groups_must_be_flat_strings(tmp_path):
    with pytest.raises(template.TemplateError):
        template.fill({"argv": ["/bin/echo", [["x"]], "{wav}"]}, _inputs(tmp_path))
    with pytest.raises(template.TemplateError):
        template.fill({"argv": "x"}, _inputs(tmp_path))


def test_model_patterns_name_the_files_setup_looks_for():
    assert template.model_patterns(template.example()) == ("ggml-large-v3-turbo.bin", "ggml-silero-*.bin")


def test_example_file_is_valid_json_shipped_in_the_package():
    path = Path(template.__file__).with_name("examples") / "whisper-cpp.json"
    assert json.loads(path.read_text(encoding="utf-8")) == template.example()
    assert not os.access(path, os.X_OK)
