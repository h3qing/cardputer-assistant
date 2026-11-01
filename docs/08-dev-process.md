**English** · [中文](08-dev-process.zh-CN.md)

# 08 · Development process and workflows

> How this project is actually developed: who does what, how a milestone goes from an idea to a GitHub Release, and which quality gates apply at each step. New contributors (people or agents) start here.
> A record of every workflow that has actually run is in [devlog/](devlog/).

## 1. Roles

| Role | Who | Responsibilities |
|---|---|---|
| Product owner | The user | Sets direction and priorities; does what only a person can do (plugging hardware in and out, signing in, manual checklists); can veto any default decision |
| Lead | The main Claude Code session | Splits milestones; writes and launches workflows; reviews the output of every workflow stage; fixes docs and CI; commits, opens PRs, merges, tags |
| Workflow sub-agents | Sent out by workflow scripts | Research, write designs, implement, adversarial review, fixes, tests on the real device. **Each agent may only change the directories it owns** |
| CI / CD | GitHub Actions | Runs every check on every push and PR; when a `v*` tag is pushed, builds the firmware and publishes the Release |
| The app's Auto-fix | Claude Code desktop | Watches the PR and wakes the Lead when CI fails, a conflict appears, or there are review comments |

## 2. Life cycle of a milestone

```
Idea / user feedback
   │
   ▼
GitHub: milestone + issues (the roadmap stays on GitHub)
   │
   ▼
① Discovery: research workflow (search from several angles → dedupe → adversarial check), plus device or CLI gate experiments
   │
   ▼
② Design: design draft → adversarial review (report only blocking problems and scope to cut) → final → docs/NN-*.md
        Output work packages: the directories each package owns do not overlap, dependencies are explicit, acceptance commands are written first (TDD)
   │
   ▼
③ Implementation: work packages that can run in parallel are built at the same time (firmware ∥ daemon); each follows RED → GREEN → REFACTOR
   │
   ▼
④ Review: one adversarial code review per package → a fix agent fixes the findings one by one (add the test first, then change the code)
   │
   ▼
⑤ HIL: an agent that holds the device exclusively runs end-to-end scenarios and long-running tests on the real device; problems found go to fix agents by owner → rerun
   │
   ▼
⑥ Release: Lead reruns all tests → commits per module following conventional commits → release/vX.Y.Z branch → PR
        → CI all green (on failure Auto-fix wakes the Lead to fix it) → merge → tag → CD publishes the Release
   │
   ▼
⑦ Record: devlog, CHANGELOG, docs/releases/vX.Y.Z.md; unfinished work becomes GitHub issues on the next milestone
```

**Principles**
- **After every workflow stage, the Lead reads the result before starting the next one.** Do not chain several stages into one huge black box.
- **Evidence first**: every claim about hardware or the CLI needs a source: measured data, a source line number, or official docs. A guess must be marked as a guess.
- **Adversarial checking**: research findings, designs and code each get a separate agent whose only job is to refute them.
- **Ask a person only for what only a person can do.** Everything else moves forward on defaults, and the doc records which default was used.

## 3. Quality gates (all in CI)

| Gate | Rule | Where it is checked |
|---|---|---|
| Protocol contract | Every message type has at least 1 valid and 1 invalid vector; execution intents must not appear in the outbox; limits and schema must agree | `protocol/tools/check_vectors.py` |
| Test coverage | Pure logic ≥ 80% (today firmware is about 96%, daemon about 96.7%) | gcovr / pytest-cov |
| File and function size | Files over 400 lines warn, over 800 lines fail; functions ≤ 50 lines | `tools/repo/check_sizes.py`, lizard |
| Pure core isolation | `lib/core` and similar directories must not include Arduino/M5/esp headers | grep in CI |
| Firmware budget | Static RAM ≤ 96KB; app image within its limit; stack budget along call chains | `scripts/check_budget.py`, stack check script |
| No test hooks in release builds | `debug.key` must not appear in release firmware | grep in CI |
| daemon dependencies | The only runtime dependency is pyserial | `uv tree` |

Real-device HIL **is not in CI**: it needs a physical device, and is only run in step ⑤ by an agent or a person. Results go into `docs/07-hil-checklist.md`.

## 4. Device rules (every agent that touches the real device must follow them)
1. The background service `cpad` (a LaunchAgent) normally holds the serial port. Before using the device, run `cpa pause <seconds>`, then confirm with `lsof /dev/cu.usbmodem*` that the port is free.
2. Only open the serial port the "safe open" way: after opening, drop RTS first, then DTR (see docs/03 §3).
3. Flash with `pio run -t upload`. **Never press G0, and never ask anyone to press it** (G0 latches download mode, see docs/03 §2).
4. If the device is stuck in download mode and a normal reset does not bring it back, stop and report "needs a power cut".
5. When done, flash the **release firmware** back, restore the daemon, and confirm `link: ready` with `cpa status`.

## 5. CI / CD

- **CI** (`.github/workflows/ci.yml`): every push and PR runs three groups of checks: protocol contract and repo tools; firmware (native tests, coverage, build, budget, size); daemon (lint, tests, coverage, unit tests for the HIL tools).
- **CD** (`.github/workflows/release.yml`): triggered when a `vX.Y.Z` tag is pushed. It builds the release firmware, then produces `-app.bin` (flashed at 0x10000, also used by Launcher), `-full.bin` (whole-flash at 0x0) and `SHA256SUMS`, and finally publishes the GitHub Release with `docs/releases/vX.Y.Z.md` as its notes. **Without release notes, this workflow fails outright**, so every version is guaranteed to have a record.
- Third-party Actions are always pinned to a commit SHA, with the matching version number in a comment. Before changing a version, check the SHA with `gh api`.

## 6. Branches and commits
- `main` only takes merges through PRs. Each milestone gets a `release/vX.Y.Z` branch; larger independent work inside a milestone can get its own `feat/*` branch.
- Commit messages follow conventional commits (`feat`/`fix`/`docs`/`test`/`ci`/`chore`/`refactor`/`perf`), split into several commits by module, ending with Co-Authored-By.
- Version numbers follow SemVer: when firmware and protocol change incompatibly, bump the protocol major version (the `v` field) and update the contract test vectors.

## 7. Doc map
| Doc | Content |
|---|---|
| 01 | Mac connection and setup (summary of official docs) |
| 02 | Survey of existing projects |
| 03 | Hardware bring-up lessons (measured) |
| 04 / 05 / 06 | v0.1 PRD, architecture and protocol, work packages |
| 07 | HIL checklist |
| 08 | This doc: development process |
| 09+ | Design docs for each later milestone |
| adr/ | Architecture decision records |
| gates/ | Gate experiment records (CLI, Launcher, SD card, etc.) |
| releases/ | Release notes for each version (used by CD) |
| devlog/ | Workflow run logs |
