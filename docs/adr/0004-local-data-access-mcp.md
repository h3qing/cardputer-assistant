# 0004 · Local data access: open files are the backup, a stdio MCP server is the interface, no app-specific integrations

- Status: Proposed. The direction is the user's decision of 2026-10-21 (#34). The transport, SDK, permission and audit details below are the Lead's proposal.
- Date: 2026-10-21
- Related: [docs/15](../15-local-data-access.md) (the design), issue #34, #19 (context profiles), [docs/13](../13-assistant-v2-plan.md) §9.5 / §11.3 / §15, [docs/11](../11-m3-design.md) §6.7–§9, [recording contract v1](../../protocol/recording/v1/README.md), [SECURITY.md](../../SECURITY.md), [ADR 0002](0002-protocol-v0.md)

Evidence marks: ✅ measured · 📚 read in the repo, a spec or official docs · ⏳ to be verified.

## Background

**The user's decision** (2026-10-21): integrations should not be software-specific, for example "a WhisperWoof integration". They should be an MCP server or a standard interface, so that Codex, ChatGPT and other programs can read the data easily. Security matters: the goal is a local backup that is easy to get the data out of, not putting the data online.

**What exists today** 📚:
- `~/CardputerNotes` (`notes_dir`):
  - daily notes `YYYY-MM-DD.md`, which cpad only appends to, with block ids `^cpa-<id>` (05 §7.8);
  - `recordings/<date>/<HHMM>-<sid>/`, laid out by recording contract v1: `seg-NNN.wav` and `manifest.json` written by cpad; `status.json`, `transcript.json|md`, `summary.md` and `result.json` written by the transcriber (WhisperWoof today, any program tomorrow).
  - Directories are 0700 and files 0600. Everything is plaintext.
- `$CPA_HOME`:
  - `events.jsonl`: append-only and the only authority. It holds the captured note and queue text, the queue state, and the `rec.*` events with titles and memo text.
  - `projects.json`: project keys, names, slots and local root paths.
  - `config.json`: the claude path, the login environment and the device serial.
  - `cpad.sock`: AF_UNIX, 0600. Commands `status`, `pause` and `rec`.
- The daemon's runtime depends on `pyserial` only. It has no listening TCP or UDP port (SECURITY.md).
- docs/13 planned app-specific paths:
  - WhisperWoof as a destination provider over a private `cpad.sock v1` (§11.3);
  - WhisperWoof MCP plugins for cloud todos (§9.5);
  - a WhisperWoof panel as the v0.7 Mac app (§15).

**Constraints from research** (📚 the MCP spec 2026-07-28 and 2025-11-25 with their official JSON schemas, client docs and OWASP MCP Top 10, all read 2026-10-21; details and links in docs/15, appendix):
- **Clients are split across two protocol eras.**
  - Claude Code requests 2026-07-28 and falls back to the handshake.
  - Codex CLI uses the 2025-06-18 handshake, and Cursor supports handshake versions up to 2025-11-25.
  - A server must therefore answer both the stateless 2026-07-28 requests and the `initialize` handshake.
- **Transports.** Claude Code, Claude Desktop, Codex CLI and Cursor can all start a stdio server. ChatGPT can only reach remote HTTPS servers, so a local server would need OpenAI's hosted tunnel or a public tunnel.
- **The official Python SDK** (`mcp` 2.3.0):
  - resolves to 28 packages, with native wheels and a full HTTP and OAuth stack;
  - renamed its main class in 2.0;
  - once shipped an insecure HTTP default (CVE-2025-66416).
- **Returned content is untrusted.** Meeting transcripts contain other people's speech. Whatever the server returns is therefore untrusted input to an LLM that may also hold outbound tools: Willison's "lethal trifecta", OWASP MCP06 and MCP10.

## Options

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| A. App-specific integrations: a WhisperWoof plugin, a private `cpad.sock v1`, adapters per app | Rich UI in one app | Every new program needs new code; the data is tied to one app; the user rejected it | Rejected |
| B. Open files only | Already exists; Obsidian, grep, Finder and Time Machine work | AI clients need a tool interface; reads are not bounded or audited; queue items and captures live only in `events.jsonl` | Kept as layer 1, not enough alone |
| C. MCP server over stdio, standard library only, answering both eras | One standard interface for every local AI client; no port; no new dependency; full control of validation and limits | We follow the spec ourselves (it has a 12-month deprecation window) | **Chosen** |
| D. As C, built on the official `mcp` SDK | Less protocol code to write | 28 packages including native wheels and the HTTP/OAuth stack; API churn; an insecure HTTP default in the past; breaks the "pyserial only" runtime | Rejected |
| E. Streamable HTTP on localhost | Reachable by clients that cannot start a process | A listening port, DNS rebinding and token handling. No current client needs it, and it does not help ChatGPT, which needs a public HTTPS endpoint | Not now (rules kept in docs/15 §3.12) |
| F. Bridge `cpad.sock` (pause, future device commands) to MCP | Device control from any agent | A confused deputy: any prompt-injected agent could drive the daemon or the device | Rejected. The only exception is one constant, read-only status request |
| G. ChatGPT through OpenAI's Secure MCP Tunnel or ngrok | ChatGPT gets access | The traffic goes through a hosted endpoint, so the data goes online | Rejected. Use Codex CLI, or upload files from `cpa export` by hand |

## Decision rules

These were fixed before implementation. The implementation PR must show each one:

1. **No new runtime dependency.** `[project] dependencies` stays `["pyserial==3.5"]`.
2. **No listening socket.** `cpa mcp serve` and `cpa export` never bind or listen. A static test checks the imports, and an integration test checks that the child process holds no inet socket.
3. **Both eras conform.** Every response in the golden stdio transcripts validates against the vendored official schemas (`$defs` of `2026-07-28` and of `2025-11-25`). `initialize` with 2025-06-18 and with 2025-11-25 both work.
4. **Read-only by default.** With no `mcp.json`, `tools/list` has only tools annotated `readOnlyHint: true`, and a full run writes nothing outside `$CPA_HOME/logs/`.
5. **Confinement.** Path traversal, a symlink at every level, dot files, non-regular files and oversized files are each refused, with a test for each.
6. **No content in the audit or the logs.** A canary string placed in notes, transcripts and search queries never appears in the audit log or on stderr.
7. **Bounded output.** A tool's structured JSON (`structuredContent`) is at most 24 KiB. The spec asks for the same JSON again as a text block, so a whole reply can be about twice that; clients usually give the model one of the two (⏳ to be measured with real clients). Every truncation says so and can be resumed with a cursor or an offset.
8. **Real clients** ⏳. A manual smoke test in Claude Code, Codex CLI and Cursor lists the tools and calls each one. This is a checklist, not CI.

## Decision

Option C, with option B as the data layer.

1. **Layer 1: the open files are the backup.**
   - `~/CardputerNotes` keeps its layout. It is now documented as *archive layout v1* (docs/15 §2), with the protocol's versioning rule: additive changes keep v1, and changing or removing a meaning makes v2.
   - `cpa export` writes a folder or a zip with `CPA-EXPORT.json` and `SHA256SUMS`. It holds the archive plus the content that only lives in `events.jsonl`: captures, queue items and project names.
   - It never exports `config.json`, the raw `events.jsonl`, logs, device serials or project paths.
   - It refuses targets that sync online unless `--allow-cloud` is given: iCloud Drive and `~/Library/CloudStorage`, `~/Desktop` and `~/Documents` while iCloud "Desktop & Documents" keeps them, legacy `~/Dropbox…`, `~/Google Drive…` and `~/OneDrive…` folders, and any folder below one that a macOS File Provider marked (docs/15 §2.4). This is best effort.
2. **Layer 2: `cpa mcp serve`.** A stdio MCP server that the client starts, written with the standard library only. It lives in `daemon/src/cpad/mcp/`, on top of a read-only archive library in `daemon/src/cpad/archive/`.
   - **Protocol.**
     - 2026-07-28 is served per request through `_meta`, with `server/discover`.
     - 2025-11-25 and 2025-06-18 are served after `initialize`.
     - One result shape is valid in both eras, because handshake-era results allow extra fields. So `resultType`, `ttlMs` and `cacheScope: "private"` are always sent, and `structuredContent` is always an object.
   - **Reads.** Ten read tools and three resource templates (docs/15 §3.4–§3.5).
     - Data is addressed by validated ids (dates, session ids, project keys), never by a path.
     - No audio bytes and no prompts.
   - **Writes.**
     - A write tool exists only when `$CPA_HOME/mcp.json` lists it in `write_tools`. The list is empty by default, and each tool is enabled one at a time.
     - Phase A (this branch) ships one write tool, `queue_add`, off by default. `add_note` and `add_todo` come in phase B, after v0.4.1's `note.added` event; until then `mcp.json` refuses them ("not available yet"), as it refuses `send_to_device` (v0.7).
     - Writes are appended to `events.jsonl`, the same path `cpa queue add` uses. Nothing is ever executed.
     - A write's audit line (phase `intent`) is written before it runs. If that line cannot be written, the write does not happen.
     - Launch flags can only narrow what `mcp.json` allows, never widen it.
   - **Audit.**
     - Every call is appended to `$CPA_HOME/logs/mcp-audit.jsonl` (0600). Each line has the time, the client's self-reported name, the parent pid and process name, the method, the tool, the argument keys, the ids, the counts, the bytes and the error code.
     - It never records content or query text.
     - If the audit line cannot be written, the call fails and no data is returned. The one exception is a write that already happened (its `intent` line is on record): if only its `outcome` line fails, the client is still told what was written, with a warning, so that it does not repeat the write.
   - **Content marking.** Every tool result carries `"trust": "untrusted-user-data"` inside its JSON. Every resource text starts with a fixed header line saying the same, because clients inline resource text without its `_meta`. Tool descriptions and the server instructions are static strings that never include data.
   - **Context profiles** (#19, v0.8). `--profile` narrows what the server returns. It is a launch flag in the user's client config and is never taken from the client's self-reported name.
3. **No app-specific integration.**
   - WhisperWoof stays a client of recording contract v1 and, when it wants more, of `cpa mcp`, like any other program.
   - `cpad.sock` stays private to the `cpa` CLI, and the planned `cpad.sock v1` for WhisperWoof (docs/13 §11.3) is dropped.
   - This repository may ship standalone clients of the recording contract, such as `cpa-stt` ([docs/22](../22-voice-captures.md) §4). They have the same standing as a third-party transcriber: they write only through the recording contract and never talk to the daemon. `cpa-stt` also reads two of cpad's files, read-only: `$CPA_HOME/config.json` (where the notes folder is) and `projects.json` (project names as the engine's prompt), and it reuses cpad's small helper modules from the same Python package (config files, validators, the contract reader, launchd). Engine parameters live only in the user's configuration. (Added 2026-10-22, docs/22 §4.12, §4.13.)
   - Amended 2026-10-22 (docs/23 P1-B, #54): there is a third local contract, the JSON that `cpa filing … --json` and `cpa project ls --json` print, versioned like the recording contract with schemas, vectors and a checker in [`protocol/filing/v1/`](../../protocol/filing/v1/README.md). It is the channel a person drives (no `mcp.json` opt-in; its `rec.*` records carry `by:"cli"`, a hand to AI is `queue.added{origin:"inbox"}` and its withdrawal `queue.done{by:"undo"}`; one record per action, each undoable; it never starts a session), and it is generic: WhisperWoof's Cardputer page runs `cpa` with an argument array, as a terminal or a script would, instead of using `cpa mcp`.
4. **No HTTP transport now.** A localhost transport is built only when a named local client needs it, and then with the rules in docs/15 §3.12:
   - a Unix socket is preferred;
   - otherwise bind 127.0.0.1 only, require a bearer token from a 0600 file, and check `Origin` and `Host`;
   - send no CORS headers, and keep it off by default.
5. **ChatGPT is not connected.** Use Codex CLI (local stdio), or upload chosen files from `cpa export` by hand.

## Consequences

- **Benefits.**
  - One interface serves every local AI client.
  - The archive stays readable without any of our code.
  - No new port, dependency or account.
  - Every AI read is on record.
- **Costs and risks.**
  - **Code we must maintain.** About 2,500 lines of protocol and archive code, plus tests, and we follow the MCP spec ourselves. Mitigation: the official schemas vendored into the tests, golden transcripts for both eras, and a re-check of the spec every six months.
  - **"Local" ends at the client.** What a cloud-model client reads through a tool call is sent to that model's provider. The server cannot prevent this. docs/15 §1, §4 and §5 say it plainly, and every setup step keeps the client's approval prompts on.
  - **Prompt injection through transcripts cannot be fully prevented.** Mitigations:
    - read-only by default;
    - no network and nothing executed on our side;
    - labelled, bounded output;
    - the audit.
  - **Plaintext files of the same user.** The audit log, the archive and exports are plaintext files of the same macOS user, so any process running as that user can read or edit them. This is the same trust boundary as SECURITY.md.
- **Way back.**
  - The server is a separate subcommand. Removing it means deleting `cpad/mcp/` and its CLI registration line.
  - Moving to the SDK later is possible behind the same tool catalogue, if its dependency picture changes.
  - Re-evaluate when:
    - a client we need speaks only HTTP;
    - the spec drops the handshake era (12 months after 2026-07-28 at the earliest);
    - v0.8 adds profiles.
- **Documents and code to update.**
  - docs/15 (new). docs/13 §9.5, §11.3 and §15 get dated notes, done together with this ADR.
  - When the implementation lands: README and SECURITY.md get a "local data access" section, and the CHANGELOG an entry.
  - Code:
    - new `daemon/src/cpad/archive/` and `daemon/src/cpad/mcp/`, with their tests;
    - the subcommand registration in `daemon/src/cpad/cli/main.py`, a few lines;
    - no change to `pyproject.toml`.
