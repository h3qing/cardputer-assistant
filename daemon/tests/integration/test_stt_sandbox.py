"""The real macOS sandbox around a transcriber command (docs/22 §4.5): it cannot reach the network or write
outside its work directory. Skipped where /usr/bin/sandbox-exec does not exist (Linux CI). The real
whisper-cli tests run only with CPA_TEST_WHISPER=1 (the lead test also needs CPA_TEST_VAD=/path/to/a
ggml-silero-*.bin), on synthesized WAVs in a temp dir."""

from __future__ import annotations

import os
import shutil
import socket
import subprocess
from array import array
from pathlib import Path

import pytest

from cpa_stt import config, gate, manifest, parse, process, runner, template
from tests.support import hermetic, stt_tree

pytestmark = pytest.mark.skipif(not os.access(runner.SANDBOX_EXEC, os.X_OK), reason="needs macOS sandbox-exec")


def _env(tmp_path: Path) -> runner.Env:
    cache = runner.darwin_cache_dir(lambda argv: subprocess.run(argv, capture_output=True, text=True).stdout)
    return runner.Env(runner.SANDBOX_EXEC, cache, config.DEFAULT_MACH, str(tmp_path / "home"))


def _run(tmp_path: Path, *flags: str) -> str:
    wav = tmp_path / "in.wav"
    wav.write_bytes(stt_tree.tone(1600))
    with runner.workdir() as wd:
        out = runner.run(stt_tree.fake_argv(*flags), wav, 0, 30, _env(tmp_path), wd)
    assert out.ok, out
    return out.stdout.decode()


def test_no_network_from_inside(tmp_path):
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        text = _run(tmp_path, "--net", str(server.getsockname()[1]))
    assert "net-denied" in text


def test_no_writes_outside_the_workdir(tmp_path):
    outside = tmp_path / "outside.txt"
    text = _run(tmp_path, "--write", str(outside), "--write-cwd", "--say", "still parsed")
    assert "write-denied" in text and "write-cwd-ok" in text and not outside.exists()
    assert parse.parse(text.encode())[0].text == "still parsed"


MODEL = Path(hermetic.original_home(), ".cache/openwhispr/whisper-models/ggml-large-v3-turbo.bin")
VAD = Path(os.environ.get("CPA_TEST_VAD") or "/nonexistent/ggml-silero.bin")


def real_whisper(test):
    """Opt-in (CPA_TEST_WHISPER=1): the real whisper-cli, which tests otherwise never run (#71)."""
    skip = pytest.mark.skipif(os.environ.get("CPA_TEST_WHISPER") != "1", reason="set CPA_TEST_WHISPER=1")
    return skip(pytest.mark.real_programs("whisper-cli")(test))


def _which(name: str) -> str | None:
    """whisper-cli from the PATH pytest started with, not the stub the hermetic plugin puts first."""
    return hermetic.real_program(name) if name in hermetic.GUARDED else shutil.which(name)


def _speech(tmp_path: Path, text: str) -> bytes:
    """`say` speaking `text` as raw 16 kHz / 16-bit PCM (skips when whisper-cli, the model or say is missing)."""
    say = shutil.which("say")
    if not _which("whisper-cli") or not MODEL.exists() or not say:
        pytest.skip("whisper-cli, the large-v3-turbo model or say(1) is missing")
    wav = tmp_path / "say.wav"
    subprocess.run([say, "-v", "Samantha", "-o", str(wav), "--data-format=LEI16@16000", text], check=True)
    pcm = wav.read_bytes()
    return pcm[pcm.index(b"data") + 8 :]


def _example(tmp_path: Path, projects: tuple[str, ...], *models: Path) -> config.SttConfig:
    folder = tmp_path / "models"
    folder.mkdir()
    for model in models:
        (folder / model.name).symlink_to(model)
    return config.parse_config(template.fill(template.example(), template.Inputs(folder, projects, _which)))


@real_whisper
def test_real_whisper_cli_in_the_sandbox(tmp_path):
    data = _speech(tmp_path, "Please fix the settings page and run the tests.")
    cfg = _example(tmp_path, ("Cardputer",), MODEL)
    wav = tmp_path / "speech.wav"
    wav.write_bytes(stt_tree.wav_header(len(data)) + data)
    with runner.workdir() as wd:
        out = runner.run(cfg.argv, wav, 0, 120, _env(tmp_path), wd)
    said = gate.keep(parse.parse(out.stdout), config.prompts(cfg.argv))
    assert out.ok and "settings" in " ".join(s.text for s in said).lower()


@real_whisper
def test_real_whisper_keeps_the_first_word_after_the_lead(tmp_path):
    """150 ms of lead, then "Two." at once. whisper.cpp 1.8.3 applies -ot to the audio VAD kept, so -ot 150 with
    --vad dropped the "Two" here and the first words of real memos; the example must not skip the lead with -ot."""
    if not VAD.is_file():
        pytest.skip("set CPA_TEST_VAD to a ggml-silero-*.bin")
    speech = _speech(tmp_path, "Two. Please fix the settings page.")
    start = 2 * next(k for k, x in enumerate(array("h", speech)) if abs(x) > 200)  # say's own silence cut off
    data = bytes(2 * 2400) + speech[start:] + bytes(2 * 16000)
    folder = tmp_path / "rec"
    folder.mkdir()
    (folder / "seg-000.wav").write_bytes(stt_tree.wav_header(len(data)) + data)
    run = manifest.Run((manifest.Seg(0, "seg-000.wav", 44 + len(data), len(data) // 2, ""),), 0, 150)
    ctx = process.Ctx(_example(tmp_path, (), MODEL, VAD), _env(tmp_path), now=lambda: "", tick=lambda: True)
    heard = process.hear(folder, run, ctx)
    text = " ".join(s.text for s in heard.sentences).lower()
    assert heard.failed is None and ("two" in text or "2" in text), text
