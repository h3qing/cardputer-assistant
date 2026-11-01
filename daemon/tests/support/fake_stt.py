"""A fake transcriber command for cpa-stt tests (docs/22 §4.3): prints sentences on stdout like whisper-cli.

Usage: fake_stt.py [flags] --offset OFFSET_MS WAV
  --say TEXT         a timestamped line "[HH:MM:SS.mmm --> ...]   TEXT", 2 s apart from the offset (repeatable)
  --plain TEXT       a line without a timestamp (repeatable)
  --describe         "samples=N offset=O" for the WAV it was given
  --exit N / --sleep S / --stderr TEXT / --flood (1 MiB of stdout)
  --sleep-loud S     sleep only when the WAV is not all silence (the self-test stays fast)
  --fail-while DIR   exit 1 while DIR/broken exists (an environment that gets fixed later)
  --net PORT         connect to 127.0.0.1:PORT, print net-ok or net-denied
  --write PATH       try to create PATH, print write-ok or write-denied; --write-cwd does it in the cwd
  --env / --cwd / --stdin   describe the environment, the working directory, what stdin gives
  --fail-unless-silent      exit 1 unless every PCM byte is zero (cpa-stt's self-test WAV is silence)
  --log PATH         append "<samples>" to PATH (counts the calls; only outside a real sandbox)
  --prompt TEXT      accepted and ignored, like an engine's prompt hint
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time


def _stamp(ms: int) -> str:
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d}.{ms % 1000:03d}"


def _try(action) -> str:
    try:
        action()
        return "ok"
    except OSError:
        return "denied"


def _parse(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--say", action="append", default=[])
    p.add_argument("--plain", action="append", default=[])
    for flag in ("--describe", "--flood", "--env", "--cwd", "--stdin", "--fail-unless-silent", "--write-cwd"):
        p.add_argument(flag, action="store_true")
    p.add_argument("--exit", type=int, default=0)
    p.add_argument("--sleep", type=float, default=0.0)
    p.add_argument("--sleep-loud", type=float, default=0.0)
    p.add_argument("--fail-while")
    p.add_argument("--stderr", default="")
    p.add_argument("--net", type=int)
    p.add_argument("--write")
    p.add_argument("--log")
    p.add_argument("--prompt")
    p.add_argument("--offset", type=int, default=0)
    p.add_argument("wav")
    return p.parse_args(argv)


def _describe_env(args: argparse.Namespace, out: list[str]) -> None:
    if args.env:
        out.append(f"env {','.join(sorted(os.environ))} PATH={os.environ.get('PATH')} LANG={os.environ.get('LANG')}")
    if args.cwd:
        out.append(f"cwd entries={len(os.listdir('.'))} mode={oct(os.stat('.').st_mode & 0o777)} path={os.getcwd()}")
    if args.stdin:
        out.append(f"stdin={len(sys.stdin.buffer.read())}")
    if args.net is not None:
        out.append("net-" + _try(lambda: socket.create_connection(("127.0.0.1", args.net), timeout=2).close()))
    if args.write:
        out.append("write-" + _try(lambda: open(args.write, "w").close()))  # noqa: SIM115
    if args.write_cwd:
        out.append("write-cwd-" + _try(lambda: open("scratch.txt", "w").close()))  # noqa: SIM115


def main(argv: list[str]) -> int:
    args = _parse(argv)
    with open(args.wav, "rb") as fh:
        pcm = fh.read()[44:]
    if args.log:
        with open(args.log, "a") as fh:
            fh.write(f"{len(pcm) // 2}\n")
    broken = args.fail_while and os.path.exists(os.path.join(args.fail_while, "broken"))
    if (args.fail_unless_silent and any(pcm)) or broken:
        return 1
    time.sleep(args.sleep + (args.sleep_loud if any(pcm) else 0))
    sys.stderr.write(args.stderr)
    out = [f"[{_stamp(args.offset + 2000 * k)} --> {_stamp(args.offset + 2000 * k + 1500)}]   {text}"
           for k, text in enumerate(args.say)] + list(args.plain)  # fmt: skip
    if args.describe:
        out.append(f"samples={len(pcm) // 2} offset={args.offset}")
    _describe_env(args, out)
    sys.stdout.write("\n" + "\n".join(out) + "\n")
    if args.flood:
        sys.stdout.write(("x" * 1023 + "\n") * 1024)
    sys.stdout.flush()
    return args.exit


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
