**English** · [中文](cli-gates.zh-CN.md)

# CLI gates measured: G7 · G8 · G3 · G4 (plus part of G9)

> Measured on this Mac on 2026-10-19, with no device connected. Task source: the Mac/CLI gates at the end of [06-work-packages.md](../06-work-packages.md); spec in [05-architecture.md](../05-architecture.md) §7.5 and §6.5.
> Below, `<T>` stands for the realpath of `$TMPDIR` (`/private/var/folders/…/T`). Names and cwds of other sessions have been hidden.

## 0. Environment and preconditions (read first)

| Item | Value |
|---|---|
| CLI | `~/.local/bin/claude` → `~/.local/share/claude/versions/2.1.272`, `claude --version` = `2.1.272 (Claude Code)` |
| System | macOS 26 |
| Run from | An agent shell inside the Claude desktop app (not an ordinary terminal) |
| Auth | `claude auth status` → `loggedIn:false, authMethod:"none"`; daemon.log: `auth: no token found, will re-check keychain every 30s` |

**Impact**: every background session started here got only one synthetic reply in its first turn, `Login expired · Please run /login`. In the transcript this reply has `model:"<synthetic>"` and `isApiErrorMessage:true`, which shows that **the model was never called**. So:

- Questions about the output format, the handling of `--`, the permission mode, and whether the session ID is kept can all be answered with certainty.
- "Did the model really reply to the new prompt" and "the status while waiting for a permission / a question" cannot be reproduced here. Section 7 has a re-test script; running it once in an ordinary, logged-in terminal fills the gap.
- There are two possible causes: the standalone CLI was never logged in (the desktop app uses its bundled 2.1.299, with auth provided by the host); or the agent shell's sandbox cannot read the Keychain. Per the safety rules, credentials were not inspected this time; the user can confirm by running `claude auth status` in a terminal.

**Safety measures**: every session was opened in a new empty folder created with `mktemp -d "$TMPDIR/cpa-gate-*.XXXXXX"`; apart from G8's control case, which deliberately omits `--`, every one passed `--permission-mode plan` explicitly; all prompts were harmless text such as "Reply with the single word OK"; bypassPermissions and `--dangerously-*` were not used. **5 sessions** in total (including 1 copy made by resume), all removed with `claude stop` + `claude rm`. At the end `claude agents --json --all` was back at the baseline (only the 2 interactive desktop-app sessions), `claude daemon status` = `not running`, and the 4 temporary folders were deleted.

## 1. Summary of results

| Gate | Result | Impact on the spec |
|---|---|---|
| G7 short id | ✅ Pass | stdout line 1 is `backgrounded · <8hex>[ · <name>][ (…)]`, `<8hex>` = `sessionId[:8]` = the `id` in `agents --json`. **`kind` actually outputs `"background"`, not `"bg"`** |
| G8 `--` and plan | ✅ Pass | With `--`, `-c test` is taken as the prompt, and the log shows `⏸ plan mode on`. In the control case without `--`, `-c` is taken as `--continue` and the session crashes, **but the CLI exit code is still 0** |
| G3 stop→resume | ✅ Pass, but **argv must change** | `claude --bg --resume <sessionId> -- <text>` (**with no other flag**): the ID is unchanged, the new prompt enters the same transcript, and plan mode is kept. **Adding `--permission-mode` as 05 §7.5 writes it makes a copy as a new session**. The model reply itself was not verified (auth issue) |
| G4 status meaning | ⚠️ Partial | `busy` / `idle` observed; background sessions' `state` showed `blocked`, `stopped`, `failed`. `waiting`, `shell`, waiting for a permission and questions could not be reproduced (the model could not be called). Desktop-app sessions are listed; `daemon` and `daemon-worker` never appeared in the JSON |
| G9 (only the cheap part) | ⚠️ Partial | A folder never trusted before **does not block**; the prompt is submitted directly. LaunchAgent not tested. A new risk found: the session process's environment is inherited from the shared transient daemon |

## 2. G7: can the short id be taken from `claude --bg`'s stdout

```sh
D1=$(mktemp -d "${TMPDIR%/}/cpa-gate-g7.XXXXXX"); cd "$D1"
claude --bg --name cpa-gate/g7 --permission-mode plan -- "Reply with the single word OK" >out 2>err   # exit code 0, 0.73s (including daemon cold start)
```

stdout is a pipe (not a TTY), as it is when cpad calls it. Below is the content verbatim; the separator is U+00B7 `·`, bytes `\xc2\xb7`:

```
backgrounded · 2a9c4f17 · cpa-gate/g7
  claude agents             list sessions
  claude attach 2a9c4f17    open in this terminal
  claude logs 2a9c4f17      show recent output
  claude stop 2a9c4f17      stop this session
```

Only when the daemon is not running yet does stderr have this line: `Starting background service…`

Two other shapes of line 1 were seen (from G3 and G8):

```
backgrounded · 9b0d2e4c                                                        ← the copy made by resume, no name
backgrounded · 7a3f5c21 · cpa-gate/g8-nodash (idle — send a prompt to start)   ← no prompt
```

What it looks like in `claude agents --json` (`<T>` substituted):

```json
{"pid":96190,"id":"2a9c4f17","cwd":"<T>/cpa-gate-g7.c02ZIJ","kind":"background","startedAt":1792455500000,
 "sessionId":"2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38","name":"cpa-gate/g7","status":"idle","state":"blocked"}
```

**Conclusions**:
- Parse only the id, not the name: `^backgrounded · ([0-9a-f]{8})\b` (line 1 only). This id always equals `sessionId[:8]`, so `sid` and `short` are in practice the same value.
- Fields of a background session: `pid id cwd kind startedAt sessionId name status state`. An interactive session has only `cwd kind name pid sessionId startedAt status`, consistent with 05 §7.5.
- **Values of `kind`**: `agents --json` outputs `"interactive"` and `"background"`. The `~/.claude/sessions/<pid>.json` registry says `"bg"`, and the enum in the binary is `interactive|bg|daemon|daemon-worker`. The daemon's parser must map `background` to `bg` (and accept `bg` too). Written literally as 05 §6.5 says, "equals `bg`", every background session would be treated as an unknown kind.
- `agents --json` takes about 0.18 s (wall clock) each time, and works even when the daemon is not running, because it reads the registry.

## 3. G8: does `--` end option parsing; does plan mode work together with `--bg`

```sh
D2=$(mktemp -d "${TMPDIR%/}/cpa-gate-g8.XXXXXX"); cd "$D2"
claude --bg --name cpa-gate/g8 --permission-mode plan -- "-c test"
# stdout: backgrounded · b10007f5 · cpa-gate/g8      exit code 0
claude logs b10007f5      # after stripping ANSI:
```

```
❯ -c test
⏸ plan mode on (shift+tab to cycle) · ← for agents
⏺ Login expired · Please run /login
```

In the transcript the first user message is `'-c test'`, with `permissionMode:"plan"`.

**Control case** (the 5th session): the same text, without `--`:

```sh
claude --bg --name cpa-gate/g8-nodash --permission-mode plan "-c test"
# exit code 0
# stdout: backgrounded · 7a3f5c21 · cpa-gate/g8-nodash (idle — send a prompt to start)
# stderr: note: started a copy of that conversation as 7a3f5c21. To continue a session under its own id, pass its full session id …
# agents --json --all: {"id":"7a3f5c21",…,"state":"failed"}
# daemon.log: bg settled 7a3f5c21 (crashed): exit 1 before init — error: unknown option '- test'
```

**Conclusions**:
- `--` ends option parsing, and text starting with `-` becomes the prompt unchanged. The PRD's inference L6 is confirmed: without `--`, `-c` is taken as `--continue`.
- `--permission-mode plan` works together with `--bg`: the footer shows plan, and the transcript records `permissionMode:"plan"`.
- **New finding**: the session crashes right after it starts, yet `claude --bg` still returns 0 and prints `backgrounded`. So cpad cannot judge success from the exit code alone. After getting the short id it should check `agents --json --all` once more, and treat `state` `failed` or `crashed` as a failure.
- Both of 05's layers of protection stay: argv always carries `--`, and text starting with `-` is also refused. Also confirmed in passing: text starting with `!` (`"!sleep 15"`) in argv does **not** enter bash mode; it is sent to the model as ordinary text.

## 4. G3: stop, then resume, a background session that is idle but whose process is still alive

The precondition is `2a9c4f17` from section 2: `status:"idle"`, process 96190 still alive.

```sh
claude stop 2a9c4f17                    # stdout: "stopped 2a9c4f17", exit code 0, the process exits at once
claude agents --json                    # 2a9c4f17 no longer appears
claude agents --json --all              # {"id":"2a9c4f17",…,"state":"stopped"} (no pid, no status)
```

**4a: written as 05 §7.5 step 3 says (with `--permission-mode`) → makes a copy ❌**

```sh
claude --bg --resume 2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38 --permission-mode plan -- "Reply with the single word SECOND"
# stdout: backgrounded · 9b0d2e4c
# stderr: note: background session 2a9c4f17 keeps its own saved options, so the flags you passed started a copy as 9b0d2e4c. Without flags, the same command continues 2a9c4f17 itself.
```

`--all` shows both `2a9c4f17` (stopped) and `9b0d2e4c` (a new sessionId, its name copied from `cpa-gate/g7`). The copy's transcript first has the original history, followed by SECOND. `9b0d2e4c` was then stopped and removed with rm.

**4b: no other flags → the ID is unchanged ✅**

```sh
claude --bg --resume 2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38 -- "Reply with the single word THIRD"
# stdout: backgrounded · 2a9c4f17 · cpa-gate/g7
# stderr: Starting background service…
#         note: woke session 2a9c4f17 with its saved options (--name, --permission-mode).
# agents --json: {"pid":96191,"id":"2a9c4f17",…,"sessionId":"2a9c4f17-d3b8-4e21-a6f5-7c0e9b1d2f38","status":"idle","state":"blocked"}
```

In `claude logs 2a9c4f17` and the transcript, the same file shows in order: `OK` → `Continue from where you left off.` (`isMeta:true`, inserted automatically by the CLI on resume) → `No response requested.` (synthetic) → `THIRD` (`permissionMode:"plan"`). The footer shows `plan mode on`.

**Conclusions**:
- As long as a resume **passes only `--resume <full lowercase sessionId>` and `-- <text>`**, the ID is unchanged, the new prompt enters the same conversation, and the `--name` and `--permission-mode` saved at spawn are kept.
- 05 §7.5's reply step 3 and 04 US-10 must both change to `claude --bg --resume <sessionId> -- <text>`, dropping `--permission-mode <mode>`. The permission mode is fixed at spawn, which fits the "plan by default" safety requirement.
- Detect a fork with these signals; if any one appears, return `E_REPLY_FORKED`: stderr contains `started a copy`; the id on stdout differs from the target short id; `--all` has an extra new sessionId.
- `claude logs <id>` fails for a stopped session (`job not found — it may have already exited`, exit code 1).
- **Not verified**: whether the model really processed and answered THIRD (auth issue). What is certain is that the prompt was submitted as a new user turn into the same sessionId. Fill the gap with the script in section 7.

## 5. G4: what each status value means

| Situation | kind | status | state | Source |
|---|---|---|---|---|
| A desktop-app session running a turn (this orchestration session) | interactive | `busy` | — | Measured |
| A desktop-app session after a turn ends | interactive | `idle` | — | Measured |
| A background session just spawned or resumed, with this turn ended (because "not logged in") | background | `idle` | `blocked` | Measured, all 5 sessions |
| A background session that was stopped (visible only with `--all`) | background | (none) | `stopped` | Measured |
| A background session that crashed at start (visible only with `--all`) | background | (none) | `failed` | Measured |
| Waiting for permission confirmation / an AskUserQuestion question | — | presumably `waiting` | presumably `blocked` | **Not reproduced**: needs the model to make a tool call |
| REPL idle, but a shell is still running | — | presumably `shell` | — | Code read only: `status = idle && <condition> ? "shell" : status`; the agent view shows both `busy` and `shell` as working |

Enums read from the 2.1.272 binary:

- status: `busy shell idle waiting`
- state: `starting running resuming adopted crashed working blocked done stopped failed`
- `waitingFor` wording: `"input needed"`, `"dialog open"`, `"sandbox request"` and so on
- The agent view's rule: when `tempo === "blocked"` or `status === "waiting"`, it shows "needs attention"

**Are desktop-app sessions listed**: yes. Both Claude desktop-app sessions appeared, with `kind:"interactive"`, `entrypoint:"claude-desktop"` in the registry, version 2.1.299, sharing `~/.claude/sessions/` with CLI 2.1.272. A claude process started by a third-party app through the Agent SDK (the ChatCut one on this Mac) was **not listed**; in-process subagents are not listed separately either.

**What `daemon` and `daemon-worker` are**: the background service's own processes. `claude daemon run --origin transient` (the supervisor) starts on demand at the first `claude --bg`, with `claude bg-pty-host` and `claude bg-spare` (a pool of pre-warmed spare sessions) under it. 5 s after the last client disconnects, the supervisor exits by itself (daemon.log: `idle 5s with no clients — exiting`). Over two daemon lifetimes, these processes **never wrote to the registry and never appeared in `agents --json`**. According to `claude daemon --help`, the resident service mode is disabled in this version, so these two kinds are most likely reserved for it. 05 §7.5's rule "skip these two kinds" can simply stay.

**Conclusions (partial pass)**:
- 05's fallback rule (inbox = every session that is neither `busy` nor `shell`) can be used as is.
- Suggest that WP3 also reads `state` when parsing:
  - a background session's `state:"blocked"` should count as "waiting for you". In this case blocked came from "not logged in", which really does need a person to act.
  - a `state` of `failed` or `crashed` should be reported as a spawn failure.
- Once the user has run section 7's script in a terminal, fill in the meanings of `waiting` and `shell`.

### 5a. The mapping in 2.1.306 (2026-10-21, #5; from reading `printAgentsJson` in the binary plus one read-only `agents --json` run)

| Situation | status | waitingFor | state (background) | cpad's `need` |
|---|---|---|---|---|
| Waiting for permission confirmation, ExitPlanMode plan approval, entering plan | `waiting` | `permission prompt` | `blocked` | Counts at once |
| AskUserQuestion, MCP asking for input | `waiting` | `input needed` | `blocked` | Counts at once |
| sandbox / worker / goal requests | `waiting` | `sandbox request` / `worker request` / `goal proposal` | `blocked` | Counts at once |
| A dialog is open (reason unknown) | `waiting` | `dialog open` | `blocked` | Counts at once |
| A background session's turn failed (for example not logged in) | `idle` | — | `blocked` | Counts at once |
| A turn ended normally | `idle` | — | `done` | Counts only after `inbox_idle_s` of idleness |
| Running | `busy` (2.1.306 also outputs `shell` as `busy`) | — | `working` | Does not count |

- `waitingFor` appears only with `waiting`. It is parsed into `SessionInfo.waiting_for`: permission / question / dialog (unknown values fall into dialog and still count as waiting for you), used by hooks (#33) as the fallback reason when merging.
- New rule (`waiting()` in `inbox/need.py`): `waiting`, or a background session whose `state` is `blocked` and that is not running, counts as "waiting for you" at once. Tests reproduce these rows with `tests/fixtures/agents/agents-2.1.306.json` and fake-claude.
- **Not measured yet**: the output while really waiting for a permission / a question in a logged-in state (section 7's G4a script). The `waiting` rows in the table above come from the binary; in measurements only `waiting` + `dialog open` has been seen.

## 6. G9 (only the cheap part)

- **Folders never trusted before**: all 5 sessions were opened in brand-new temporary folders; none got stuck on a trust dialog, and every prompt was submitted at once. ✅
- **Environment under a LaunchAgent**: not tested. It needs installing a LaunchAgent, which is persistent configuration and outside this scope.
- **New risk (evidence from daemon.log and ps; the cause is inferred)**: the session process is a `bg-spare` that the daemon forked **in advance**. The log first has `bg spare spawned host`, and only later `bg claimed-spare <id>`. So the session's environment most likely comes from the `claude --bg` caller that started the daemon, not necessarily from the `spawn_env` of cpad's call this time. For example, if the user already started a background session in a terminal, the daemon was started by the terminal, and the session cpad opens uses the terminal's environment. Take this into account when WP5 runs the `which uv pio node` scenario.
- **Auth is a hard precondition**: when the daemon starts it must find the CLI's token in the Keychain, otherwise every session only answers `Login expired`. `cpa status`'s self-check should preferably also check `loggedIn` from `claude auth status`.

## 7. Re-test script (in an ordinary, logged-in terminal, at most 1 session per block)

First confirm that `claude auth status` says `"loggedIn": true`. After each block, run `claude stop <id>; claude rm <id>`.

```sh
poll() { for i in $(seq 1 40); do claude agents --json | python3 -c \
  "import json,sys;[print(x.get('id'),x.get('status'),x.get('state'),x.get('waitingFor')) for x in json.load(sys.stdin) if x.get('name')=='$1']"; sleep 1; done; }

# G3: the model really replied; the ID is unchanged
D=$(mktemp -d "${TMPDIR%/}/cpa-g3.XXXXXX"); cd "$D"
claude --bg --name cpa-gate/g3 --permission-mode plan -- "Reply with the single word OK"   # note the <id>
poll cpa-gate/g3                       # wait for idle; claude logs <id> should contain OK
SID=$(claude agents --json | python3 -c "import json,sys;print([x['sessionId'] for x in json.load(sys.stdin) if x.get('name')=='cpa-gate/g3'][0])")
claude stop <id>; claude --bg --resume "$SID" -- "Reply with the single word SECOND"
claude agents --json --all | grep -c "$SID"   # should be 1; claude logs <id> should contain SECOND, and stderr must not contain "started a copy"

# G4a: waiting for permission (manual mode, empty folder, writes a harmless file)
D=$(mktemp -d "${TMPDIR%/}/cpa-g4.XXXXXX"); cd "$D"
claude --bg --name cpa-gate/g4perm --permission-mode manual -- "create a file named a.txt containing hi"; poll cpa-gate/g4perm

# G4b: a question
claude --bg --name cpa-gate/g4ask --permission-mode plan -- "Use the AskUserQuestion tool to ask me whether I prefer red or blue. Do nothing else."; poll cpa-gate/g4ask
```

## 8. What this run left behind

- `~/.claude/projects/-private-var-folders-…-T-cpa-gate-{g4-NXOvYc,g7-c02ZIJ,g8-H4ZBod}/`: `claude rm` does not delete transcript folders. They contain only this run's test prompts; they are harmless to keep, and the user can delete them.
- `~/.claude/daemon.log`: created when the daemon first started in this run, about 3 KB, recording the daemon's lifecycle.
- `~/.claude/settings.json` was not changed, no `daemon.json` was created, and no real project folder was touched.
