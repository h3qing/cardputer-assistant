"""Is the configured command able to run here? (docs/22 §4.6). I/O.

env_problem(): model_missing when argv[0] or any absolute path in argv is gone; engine when sandbox-exec is
gone. selftest(): the command on a built-in second of silence, in the same sandbox; any failure is an
environment problem (engine), so recordings wait with status blocked and never get a failed result for it.
It runs once per process and configuration, after any failure, and after `cpa-stt retry`.
"""

from __future__ import annotations

import hashlib
import json
import os
from collections.abc import Callable

from cpa_stt import config, runner
from cpa_stt.scan import wav_header

SELFTEST_SAMPLES = 16000


def env_problem(cfg: config.SttConfig, env: runner.Env) -> str | None:
    if cfg.argv is None or config.missing_paths(cfg.argv):
        return "model_missing"
    return None if os.access(env.sandbox_exec, os.X_OK) else "engine"


def selftest(cfg: config.SttConfig, env: runner.Env, tick: Callable[[], bool] = lambda: True) -> str | None:
    problem = env_problem(cfg, env)
    if problem or cfg.argv is None:
        return problem or "model_missing"
    with runner.workdir() as wd:
        wav = wd / "selftest.wav"
        wav.write_bytes(wav_header(2 * SELFTEST_SAMPLES) + bytes(2 * SELFTEST_SAMPLES))
        timeout = runner.timeout_for(cfg.timeout_s, 1)
        out = runner.run(cfg.argv, wav, 0, timeout, env, wd, tick=tick, stdout_max=runner.stdout_cap(1))
    return None if out.ok else "engine"


def signature(cfg: config.SttConfig, pid: int) -> str:
    """Which process checked which configuration (a new process or a changed config re-runs the self-test)."""
    text = json.dumps(config.dump_config(cfg), sort_keys=True)
    return f"{pid}:{hashlib.sha256(text.encode()).hexdigest()[:16]}"
