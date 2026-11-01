#!/usr/bin/env python3
"""End-to-end HIL for Cardputer Assistant (06 WP5, 05 §9.3).

Real device (test firmware, -DCPA_TEST_HOOKS=1) + the real cpad daemon in this process on a
temporary $CPA_HOME + daemon/tests/fixtures/bin/fake-claude. Run from the repository root:

    uv run --project daemon python tools/hil/run_e2e.py --scenarios all --report "$TMPDIR/hil.json"
    uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 60 --report "$TMPDIR/soak.json"
    uv run --project daemon python tools/hil/run_e2e.py --scenarios real_claude_smoke --project-dir "$(mktemp -d)"

The last one is manual only: it starts one real Claude Code session (plan mode) and removes it.
"""

import sys

from harness.runner import main

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
