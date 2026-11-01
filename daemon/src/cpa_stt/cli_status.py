"""`cpa-stt status`: on or off, the claim, whether anything runs, the command, the sandbox, the self-test,
recent timings and second writers (docs/22 §4.11). Never any transcript text."""

from __future__ import annotations

import argparse
import json
import os
import time
from dataclasses import asdict
from typing import TYPE_CHECKING, Any

from cpa_stt import claim, config, service, state

if TYPE_CHECKING:
    from cpa_stt.cli import Ctx


def status_doc(ctx: Ctx) -> dict[str, Any]:
    cfg, st = config.load_config(ctx.paths.config), state.load(ctx.paths.state)
    root = config.recordings_root(cfg, ctx.notes)
    seen = claim.read(root) if root.is_dir() else None
    held = seen.claim if seen is not None else None
    return {
        "on": cfg.on, "recordings": str(root), "recordings_exists": root.is_dir(),
        "command": cfg.argv[0] if cfg.argv else None, "missing": list(config.missing_paths(cfg.argv or ())),
        "sandbox": os.access(ctx.deps.sandbox_exec, os.X_OK), "sandbox_path": ctx.deps.sandbox_exec,
        "claim": None if seen is None else {"app": held.app if held else None, "pid": held.pid if held else None,
                                            "beat_age_s": int(time.time() - seen.beat)},
        "selftest_ok": st.selftest_ok, "selftest_at": st.selftest_at, "blocked": st.blocked,
        "retrying": len(st.failures), "recent": [asdict(r) for r in st.recent], "foreign": list(st.foreign),
        "busy_by": st.busy_by, "service": service.plist_path(ctx.agents()).exists(),
    }  # fmt: skip


def _claim_text(held: dict[str, Any] | None) -> str:
    if held is None:
        return "none"
    return f"{held['app'] or 'unreadable'} pid {held['pid']}, beat {held['beat_age_s']} s ago"


def status_lines(doc: dict[str, Any]) -> list[str]:
    held = doc["claim"]
    selftest = {True: f"ok ({doc['selftest_at']})", False: f"failed ({doc['blocked']})", None: "not run"}
    lines = [
        f"cpa-stt: {'on' if doc['on'] else 'off'}",
        f"recordings: {doc['recordings']}{'' if doc['recordings_exists'] else ' (missing)'}",
        f"command: {doc['command'] or '-'}",
        f"paths: {'missing: ' + ', '.join(doc['missing']) if doc['missing'] else 'ok'}",
        f"sandbox: {'ok' if doc['sandbox'] else 'MISSING (' + doc['sandbox_path'] + ')'}",
        f"claim: {_claim_text(held)}",
        f"self-test: {selftest[doc['selftest_ok']]}",
    ]
    if doc["on"] and (held is None or held["beat_age_s"] > claim.FRESH_S):
        lines.append("running: no, new recordings wait (cpa-stt watch, or cpa-stt service install)")
    lines.append(f"service: {'installed' if doc['service'] else 'not installed'} (LaunchAgent)")
    lines += [f"blocked: {doc['blocked']}"] if doc["blocked"] else []
    lines += [f"retrying: {doc['retrying']} recording(s)"] if doc["retrying"] else []
    lines += [f"busy: {doc['busy_by']} held the claim at the last pass"] if doc["busy_by"] else []
    lines += [f"recent: {r['session']} {r['dur_ms'] / 1000:.1f} s in {r['took_ms'] / 1000:.2f} s exit {r['exit']}"
              for r in doc["recent"]]  # fmt: skip
    if doc["foreign"]:
        lines.append(f"warning: another transcriber also writes recordings/: {', '.join(doc['foreign'])}")
    return lines


def status(ctx: Ctx, args: argparse.Namespace) -> int:
    doc = status_doc(ctx)
    print(json.dumps(doc, ensure_ascii=False) if args.json else "\n".join(status_lines(doc)))
    return 0
