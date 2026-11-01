**English** · [中文](04-prd.zh-CN.md)

# 04 · Product requirements document (PRD)

> Working product name: **Cardputer Pocket Assistant**. The repo name, the Mac command `cpa`, the daemon `cpad` and the firmware `cpa-fw` are all placeholders (see §16 open item 9).
> Status: **Final v0.1**, 2026-10-19. Merged from three drafts: the PRD draft, the reuse survey and the architecture draft. Every review comment was handled; the record is in §17.
> Basis: [02-landscape-report.md](02-landscape-report.md), [03-hardware-bringup.md](03-hardware-bringup.md), [01-mac-setup.md](01-mac-setup.md), `firmware/hwtest/`, and today's measurements of Claude Code CLI 2.1.272 on this machine (`--help`, `agents --json`, enum values inside the binary).
> Split of responsibilities: this doc says **what to build and how far**; **how to build it** is set by [05-architecture.md](05-architecture.md), including protocol fields, data structures, directories and versions. When the two conflict, 05 wins for protocol and data definitions, and this doc is then corrected.
> Conventions:
> - User-facing text is in Chinese; identifiers, protocol fields and commands stay in English.
> - Priority: **P0** = must be in v0.1; **P1** = should be in v0.1, may slip to v0.1.x; **P2** = nice to have.
> - **Gate Gn**: something that must be verified before building, see §14.
> - Evidence marks: ✅ measured on this machine or the real device; 📚 from docs or source code; ⏳ not yet verified (each has a fallback).

---

## 0. In one sentence

Turn the Cardputer ADV into a "pocket side screen + quick-note keyboard" for the Mac that does three things:
- See at a glance which Claude Code session is running and which one is waiting for you, in every project;
- Jot down an idea or some feedback and file it into that project's queue, or, after you confirm, start a new session for that project right away;
- Keep taking notes while offline; they sync automatically when you plug back into the Mac. Anything written offline **only becomes a note or a queued item. It is never executed automatically.**

---

## 1. Background

### 1.1 The problem
You work on several projects at once (A, B, C), and each has its own ideas, to-dos and a few Claude Code sessions running in parallel. There are four pain points today:

1. **You do not know who is waiting for you.** You have to switch terminals or windows on the Mac one by one to find which session has finished and which is stuck on a permission prompt or a question. Every minute a session sits idle is wasted.
2. **Ideas get lost.** Inspiration can come at any time: while coding, walking or in a meeting. Writing it down means taking out the phone, unlocking it, finding the app and picking the project. Too many steps, and the phone pulls your attention away.
3. **"Thinking of it" and "handing it to the AI" are disconnected.** When you think of something to follow up on, you first have to go back to the Mac, enter the project directory, start a session and paste a description before it is really handed off.
4. **Away from the computer, nothing moves forward asynchronously.** With no network or no Mac nearby, ideas have no reliable place to land.

### 1.2 Why now, and why this device
- **The device fits**: the Cardputer ADV has a full keyboard, fits in a pocket, lasts a day on battery, has a microphone and an SD card, and needs no USB driver.
- **The interfaces are enough**: the Claude Code command line can read session status and start background sessions with no configuration at all (L2 to L6).
- **There is a gap to fill**: about 60 "Cardputer + AI coding assistant" projects appeared in 2026, and almost all of them only do "status display + approve/deny". **Nobody has built a per-project asynchronous workflow** yet: a project board, a feedback queue, starting sessions from the device, chord-based quick notes, offline voice sent back later. This is what sets us apart (02 report TL;DR).

### 1.3 Known facts and what they mean for the product

| # | Fact | Source | Impact on the product |
|---|---|---|---|
| L1 | No PSRAM; free heap on the test firmware is about 281KB when idle | ✅ 03 §1 | The device only handles input, display and temporary storage. Grouping, calling the CLI and storage all live in the Mac daemon |
| L2 | `claude agents --json` takes about 0.1 s of CPU per call; fields are `cwd kind name pid sessionId startedAt status`. Interactive sessions have **no** short id. Current desktop-app sessions are listed too. Session names are often Chinese, e.g. "设置页面和字体修复" (meeting recording and transcript missing) | ✅ measured today | v0.1 can build the board and inbox **without installing hooks or changing config**. Chinese display is raised to P0 |
| L3 | `status` values are `busy/shell/idle/waiting`; `kind` values are `interactive/bg/daemon/daemon-worker` | ✅ enums in the 2.1.272 binary (only `idle` and `busy` actually observed) | Definition of "waiting for you": `waiting` counts immediately; `idle` counts after 60 s or more. Exact semantics confirmed by G4 |
| L4 | Behavior of `--bg --resume <id>`: if the session has stopped, it continues under the same ID; if the session is still running, it **starts a copy**. The help for `claude stop` says "`--resume` works once it is stopped" | ✅ `--help` text | An idle background session's process is still alive, so resuming it directly will probably copy it into a new session. So F7 only takes the "stop first, then resume" path, and must pass G3 first. **Never resume an interactive session** |
| L5 | `--permission-mode` accepts `acceptEdits/auto/bypassPermissions/manual/dontAsk/plan`. This machine's `~/.claude/settings.json` sets `defaultMode="auto"` | ✅ | If a session started from the device inherited the default mode, it would run in auto mode. So the daemon **always passes** `--permission-mode` explicitly, with `plan` as the default |
| L6 | The CLI's argument parser is commander-style. An argv element starting with `-` is treated as an option, even when the call does not go through a shell | 📚 inferred, verified by G8 | Put `--` before the text when calling, and reject text that starts with `-` |
| L7 | Opening the serial port on the Mac can reset the chip; HWCDC withholds the first reply; after the handshake the round trip is about 60 ms | ✅ 03 §2–3 | The daemon uses "safe open + handshake retry". Whether the device is online is decided by the protocol heartbeat |
| L8 | Powering up with G0 held locks the chip in download mode until a real power cut | ✅ 03 §2 | G0 is only read while the firmware is running. Docs and UI must **never** tell the user to "hold G0 while powering on" |
| L9 | The keyboard has no diodes: two-key chords are always safe, some three-key chords produce ghost keys. One project reports the TCA8418 locking up after fast typing | 📚 02 §6 | The key scheme uses only single keys and two-key chords; add ghost-key filtering and a keyboard watchdog; device verification in G1 |
| L10 | The device has no RTC clock | 📚 02 §1 | Item IDs do not depend on time. Wall-clock time is sent down by the Mac; when time is unknown, it is marked as unknown explicitly |
| L11 | The microphone problem is narrower than first thought: what breaks is the switch "use the speaker first, then record through M5Unified". HWCDC in Arduino 3.2.1 is the same logic as in 2.0.17; the real fix only arrives in 3.3.9 | 📚 ESP-IDF #18621, arduino-esp32 PR #12606 | Voice moves to v0.2. Which toolchain is used is decided by G2 on day 0 |
| L12 | When the HWCDC receive queue is full it **silently drops bytes**. The default loop stack is only 8KB | ✅ core 2.0.17 source | Receive buffer set to 8KB, longest line 2048 bytes, flow control (credit) on the Mac side, loop stack changed to 16KB |
| L13 | FatFS rename is not atomic; LittleFS rename is | 📚 source, measured in G10 | v0.1's outbox lives only in LittleFS on internal flash. SD card moves to v0.2 |
| L14 | The `efontCN_12` font is 213,444 bytes; each app partition is 3.2MB | ✅ M5GFX 0.2.33 source, `default_8MB.csv` | Chinese display goes into P0. G5 is closed |
| L15 | No existing project can serve directly as a base. Code from license-compatible projects can be borrowed; projects with no license can only lend design ideas | ✅ gh api | Mature libraries are used directly; single-maintainer projects are borrowed file by file, keeping NOTICE (05 §4) |

---

## 2. Users

**Main user: the multi-threaded developer (that is you)**
- Works on 3 or more projects at once, with 1 to 3 Claude Code sessions per project. Uses both the terminal and the Claude desktop app.
- Dictates mostly in Chinese, mixed with English technical terms. Types only English or pinyin on the device.
- Wants: fewer window switches, less reaching for the phone; ideas not lost; the AI moving forward while away from the Mac; everything already sorted when back at the Mac.
- Does not want: reading long text or reviewing code on a 240×135 screen; complex configuration; data in the cloud.

**Contributors: Cardputer hobbyists or open-source contributors** (a focus from v0.2)
- Want to add their own small tools, such as a Pomodoro timer, a Home Assistant switch, or adapters for other AI assistants.
- Want: clear module boundaries; tests that run locally; docs that explain how to flash and debug.

**Similar users: heavy Claude Code users** (after v0.2)
- Want a "waiting for you" notifier plus a quick-note keyboard, running within 15 minutes of cloning the repo.

---

## 3. Jobs to be done

| # | Situation | I want | So that | Version |
|---|---|---|---|---|
| J1 | Sessions are running in several projects | To see at a glance which one is waiting for me, and for how long | Sessions do not sit idle, and I do not have to switch windows | v0.1 |
| J2 | An idea suddenly comes up | To write it down within 5 seconds, filed under the right project | Not interrupting what I am doing | v0.1 |
| J3 | A session has finished and is waiting for my next step | To reply from my pocket, or queue it first | It keeps running, or I handle it later in one go | Queueing in v0.1; direct reply to idle background sessions in v0.1 (needs G3 to pass); any session in v0.2 |
| J4 | I want a project to start something new | To start a new session without sitting back down at the Mac | The AI gets going first, and I check the result when I am back | v0.1 |
| J5 | I am out with no network and want to say an idea | Hold and speak; it turns into text and tasks automatically when I am back | Moving forward away from the computer | v0.2 |
| J6 | I want to add a new feature to the device | Write a module by convention without touching core code | The community can extend it | v0.2 SDK |

---

## 4. Product principles

1. **Heavy work goes to the Mac; the device is only hands and eyes.** The device captures, displays and temporarily stores; grouping, calling the CLI and storage are all done by the Mac daemon.
2. **Capture any time; execution must be confirmed online.** "Execution" operations mean starting a session and sending feedback to a session. They can only be sent while the device is online, through a confirm box, as an **online request**. They **never go into the outbox and are never replayed automatically after reconnecting**. Any execution request made offline is stored as a queued item.
3. **v0.1 only uses the stable CLI and does not touch your configuration.** No hooks, no settings changes, **no network ports listened on**, no CLI upgrade required.
4. **Sessions started from the device have an explicit permission mode, never higher than ones you start by hand.** The daemon always passes `--permission-mode`, taken from the project config, default `plan`. `bypassPermissions` is never used.
5. **Data does not leave your machine.** No cloud, no account, no telemetry. The Mac is the only authoritative store; the device keeps only what has not synced yet.
6. **Modular, but no abstractions for imagined needs.** Only a compile-time module list plus a very small interface. Runtime plugins and a plugin marketplace can come later.
7. **The small screen only shows what can be read at a glance.** No long text, diffs or conversation transcripts on the device.

---

## 5. Version slices

### 5.1 v0.1 "USB loop"

**In one sentence**: while plugged into USB (or when plugged back in after some time offline), the device, daemon and Claude Code form a closed loop, supporting quick notes, the project board, the waiting-for-you inbox, queueing and starting new sessions. If G3 passes, it can also reply to idle background sessions.

| ID | Feature | Priority |
|---|---|---|
| F1 | Quick note: press G0 or an Opt chord on any screen; the content goes into the outbox on internal flash | P0 |
| F2 | Sync: handshake, time sync, idempotent outbox upload, append-only event log on the Mac | P0 |
| F3 | Project registry (managed with the CLI on the Mac) and project board | P0 |
| F4 | Waiting-for-you inbox: `waiting` sessions enter immediately; `idle` for more than 60 seconds enter too | P0 |
| F5 | Project queue: add, view, mark done, and start a session from a queued item | P0 |
| F6 | Start a new session: online request, runs `claude --bg --name … --permission-mode … -- <text>` | P0 |
| F7 | Reply to an idle background session: `stop` first, then `--bg --resume`, and confirm the ID did not change; fall back to queueing when conditions are not met | P1 (G3) |
| F8 | Status bar, online/offline state, fault notices | P0 |
| F9 | Daily notes markdown, readable directly on the Mac | P1 |
| F10 | Keyboard reliability: ghost-key filter + watchdog | P1 (G1) |
| F11 | Chinese display (efontCN 12px) | P0 |
| F12 | LaunchAgent autostart; `cpa pause` to release the serial port while flashing | P1 |
| F13 | System info page | P2 |
| F14 | Device identification and port courtesy: check the firmware, bind the serial number, let go of other boards | P0 |

**Milestones**: every step ends with a demo on the real device. The order is quick note → sync → board → start session. Work packages are in 05 §10.

| Milestone | Content | Demo |
|---|---|---|
| M0 Preparation | `git init`, add LICENSE, write the protocol v0 contract and test vectors (WP1). Day-0 gates: G2 toolchain, G1, G3, G4, G7, G8, G9, with conclusions written into ADRs | CI all green; ADR-0001 settles the toolchain |
| M1 Quick note + sync | Firmware pure core (WP2) and daemon (WP3) developed in parallel, plus the firmware device layer (WP4) for input, Compose and outbox | Write 3 items offline, all survive a power cut and reboot; plug in USB and they appear in the Mac's daily notes within 5 seconds |
| M2 Board + inbox | Poll `agents --json`, push snapshots, build the board, project pages and inbox | When a session's state changes on the Mac, the device updates within 5 seconds at p95 |
| M3 Start session + queue | Online request `agent.spawn`, queue handling, offline fallback. Add F7 if G3 passes | A session started with Opt+S appears on the board, with permission mode plan |
| M4 Wrap-up | All HIL scenarios, 1-hour soak, manual checklist, 15-minute README timing (WP5) | Meets the definition of done in §12 |

### 5.2 v0.2 "Offline voice + reply to any session"
- **Reply to interactive sessions**: upgrade to Claude Code ≥2.1.287 and inject with the mods `$.prompt.submit`. The daemon detects the capability first, and falls back to the v0.1 approach if it is not available.
- **Optional hooks**: connected through "a command-forwarding script + a token file with permissions 0600", **not** the native http hook, because 2.1.272 does not interpolate environment variables it judges to be credentials into headers. Installing requires an explicit command, backs up first, and can be fully uninstalled. The Notification hook is used to show "why a session is waiting"; `last_assistant_message` from the Stop hook is used to show a progress summary.
- **Approval on the device**: keys by risk level: routine operations Enter to approve, Esc to deny; high-risk operations long-press Fn+Y. Key presses within 400 ms of a confirm box opening are ignored. **If the device is absent or times out, no decision is made and the terminal asks as usual**; there is no default deny. Every decision is written to an audit log.
- **Offline voice** (requires G2 to pass):
  1. Long-press G0 to record, written to the SD card, split into a segment every 5 minutes;
  2. Uploaded in chunks during sync, with SHA-256 checks;
  3. Transcribed on the Mac; the transcription engine is replaceable;
  4. The transcript becomes a note or a queued item. Execution operations still need online confirmation.
- **SD card history**: synced items are archived to the SD card, each record with a checksum, each file sealed separately with the Mac's public key.
- **Other**: power saving (screen off, light sleep), Leader key, module SDK (template module and contribution guide), redelivering queued feedback to the original session, MCP server (each queued item records its source, see the §14 risks).

### 5.3 Later (from v0.3)
- **Wireless**: WiFi and BLE. First do proper pairing over USB (a key in NVS + HMAC challenge), then use a Noise encrypted channel.
- **Compatibility and extension**: a Hardware Buddy compatible module; other adapters such as Codex; daemon plugins; OTA; USB mass storage (MSC) mode.
- **Toolchain and platform**: migrate to Arduino 3.3.x / IDF 5.5, with the microphone moved to a self-written `i2s_std` driver; evaluate Tactility.

### 5.4 Explicitly out of scope

**Not in v0.1:**
- Device pairing. It carries no key and so achieves nothing; it moves to v0.3 to be done properly with wireless.
- Hooks, and any change to Claude Code configuration.
- Any TCP or HTTP port.
- mods.
- SD card, voice, recording.
- Permission approval on the device.
- Typing Chinese on the device.
- Browsing history or editing submitted items on the device.
- Viewing conversation transcripts or diffs on the device.
- Multiple devices or multiple Macs, accounts, cloud sync, telemetry.
- Loading modules at run time, OTA, Codex, sleep, Leader key.
- Queue markdown output.

**Not in the whole project:**
- Recreating a terminal or chat interface on a 240×135 screen.
- Exposing any interface to the public internet.
- Turning on flash encryption or Secure Boot by default.
- Becoming a general Cardputer operating system, or a phone app.

---

## 6. User stories and acceptance criteria (v0.1)

"Online" means: the link is in the Ready state, i.e. the handshake is done and data from the daemon arrived within the last 15 seconds. Every latency metric uses p95; the measurement methods are in §12.

### US-01 Quick note (F1, P0)
I want to start a note with one key on any screen, and get an idea down within 5 seconds.
- **AC1**: On any screen, a short press of G0 (held less than 500 ms) opens the capture page (Compose) within 300 ms at p95, ready for typing. Pressing G0 on the capture page means submit.
  - From v0.4.1 (gh #51, [docs/20 U26](20-ux-review.md)): after a short press the device waits 0.35 s (`kDoubleTapMs`) to see whether it is a "double tap", so the capture page opens 0.35 s after release, and the p95 limit is relaxed to 650 ms. If another key is pressed before then, the capture page opens at once and that key goes in; not a single character is lost.
- **AC2**: The default type is "note". The default target is the current project, such as the project page being viewed or the project of the selected session; when not in any project, the target is "unassigned".
- **AC3**: Enter or G0 submits; empty text is not submitted. Only single-line ASCII, at most 500 bytes; the page shows bytes used and stops accepting input at the limit.
- **AC4**: On submit, the item is **fully written and renamed to disk first**, and only then does it show "Saved · N waiting to sync". Save latency p95 no more than 100 ms (200 runs on the real device). Power-loss acceptance has two parts:
  - native tests inject a crash at every storage step, with 0 confirmed items lost and 0 duplicated;
  - 10 real power cuts by hand (side switch to OFF and USB unplugged), each right after "Saved" appears, with 0 lost.
- **AC5**: Fn+` (Esc) cancels. If text has been typed, it asks again: Enter discards, Esc returns to editing.
- **AC6**: Quick notes work as usual while offline and while the daemon is not running.
- **AC7**: When the outbox is full (200 items reached, or less than 64KB free), submitting is refused with "Storage full, connect to the Mac to sync". Items not yet synced are never overwritten.

### US-02 Change type and target (F1, P0)
- **AC1**: On the capture page, Opt+N, Opt+Q and Opt+S switch the type to "note", "queue" and "new session" respectively, and the title bar changes immediately.
- **AC2**: Opt+1 to Opt+9 change the target to the project with that number; Opt+0 changes it to "unassigned". "Queue" and "new session" cannot be "unassigned"; pressing Enter then asks you to pick a project first.
- **AC3**: Projects can be picked by number while offline too. The device persists "number → project key and display name", but **no paths and no session information**.
- **AC4**: When the type is "new session" or "feedback", the text cannot start with `-`. The capture page shows a hint and does not allow submitting.

### US-03 Sync (F2, P0)
- **AC1**: When the daemon finds the device, it first opens the serial port the safe way, then sends `hello`. With no reply within 2 seconds it resends, up to 3 times. After a successful handshake, the daemon sends down the time and board snapshot, and the device uploads the outbox in ID order with at most 4 items in flight.
- **AC2**: Within 5 seconds of the handshake, all 100 items in the outbox are acknowledged (HIL test).
- **AC3**: The device only deletes an item after receiving `outbox.ack`. If the ack is lost, or the daemon crashes and the device resends, the Mac dedupes by `(device, item ID)`, giving 0 duplicates (HIL: dropped-ack scenario).
- **AC4**: The daemon appends the event to `events.jsonl` and fsyncs before acking. When the device does not know the time, `wall_ms` is empty and the Mac records the receive time `received_at`; **time is never made up**.
- **AC5**: Items submitted while online arrive within 1 second at p95, and ↑N in the status bar goes back to 0.
- **AC6**: Items written offline can only be of type "note" or "queue". **No** type that can trigger execution exists in the outbox (guaranteed by the contract tests).

### US-04 Device identification and port courtesy (F14, P0)
- **AC1**: The daemon only keeps a serial port when the `hello` reply meets two conditions: `fw` starts with `cpa-fw/`, and `v` equals 0. Otherwise it releases the port within 6 seconds, writes nothing more, and only retries that port after 30 seconds.
- **AC2**: After the first successful handshake, the daemon binds this device's USB serial number in the config. From then on it never touches other `303A:1001` ports. `cpa device forget` removes the binding.
- **AC3**: If the two sides' protocol major versions differ, the device shows "Firmware and Mac versions are incompatible, please update", the daemon releases the port, and `cpa status` states the reason.
- **AC4**: The daemon never lets (RTS, DTR) be (1, 0) at any time. Verified two ways: a unit test checks the sequence of control-line changes; a HIL test checks that `up_ms` is continuous after reopening the port.

### US-05 Project registration (F3, P0)
- **AC1**: The registration command is `cpa project add <key> <path> --slot N [--name 显示名] [--mode plan|manual|acceptEdits|dontAsk|auto]` (`显示名` is the display name). Every rule below must hold, or it reports an error and writes nothing:
  - `path`: an existing directory, saved as an absolute path (realpath). **It does not have to be a git repo**, since this project itself is not a git repo right now.
  - `key`: matches `^[a-z0-9][a-z0-9-]{0,11}$` and is not taken.
  - `slot`: between 1 and 9 and not taken.
  - `name`: at most 24 bytes of UTF-8, may be Chinese, defaults to `key`.
  - `mode`: defaults to `plan`. Passing `bypassPermissions` is refused.
- **AC2**: `cpa project ls` and `cpa project rm <key>` work. While online, the device updates within 5 seconds of a registry change.
- **AC3**: The device only gets `slot`, `key`, `name` and the counts; it never gets the path or permission mode.

### US-06 Project board (F3, P0)
- **AC1**: The home page (Opt+H) lists projects by number (from v0.4.0 this page is called the project board and opens with Opt+P; Opt+H becomes the home launcher, see §8.2). Each project shows three numbers: running, waiting for you, queued. The last row is "Other", counting sessions whose working directory belongs to no registered project. Sessions under "Other" do not enter the inbox.
- **AC2**: After a session's state changes on the Mac, the device updates within 5 seconds at p95 (HIL: fake-claude flips the state 100 times).
- **AC3**: Opt+1 to Opt+9 open the matching project page. A project page has two tabs, "Sessions" and "Queue", switched with `,` and `/`. The Sessions list shows name, state, duration, and a `bg` or `tty` mark.
- **AC4**: After disconnecting, the board keeps the last data and marks it "N min ago". After a device reboot it only shows the cached project names, with the numbers shown as `--`. Session information is not written to flash.
- **AC5**: If Claude Code cannot be found on the Mac, the board shows "Claude Code not found on the Mac", and starting sessions and replying are greyed out.

### US-07 Waiting-for-you inbox (F4, P0)
- **AC1**: Opt+I opens the inbox. A session enters the inbox when:
  - it belongs to a registered project;
  - its state is `waiting` (enters immediately), or its state is `idle` and has lasted at least 60 seconds.

  Order: `waiting` first, then `idle`; within each group, the longest-waiting first.
- **AC2**: The status bar shows !N, where N is the number of inbox items. When a new item enters, a 2-second notice pops up: "<key>/<session name> is waiting for you".
- **AC3**: Select an item and press Enter to reply (US-10). Opt+D dismisses that session until its state changes again. Dismissals are kept in daemon memory and are lost when the daemon restarts.
- **AC4**: Background sessions show the hint `claude attach <short>`; interactive sessions show "Handle on the Mac: <name>". v0.1 has no approval on the device.
- **AC5**: Dismissing only works online; offline it is greyed out.

### US-08 Queueing (F5, P0)
- **AC1**: Queued items have three sources: items of type "queue" on the device, start-session or feedback requests that fell back, and `cpa queue add`. They appear in the matching project's "Queue" tab, newest first. The device shows at most 4 per project and at most 12 in total; the number shown is the full count.
- **AC2**: Pressing Enter on a queued item goes through the US-09 confirm flow. The prompt uses the **full text** saved on the Mac, not the device's truncated text. On success, the item is marked "session started" and disappears from the list; the event log keeps the record.
- **AC3**: Opt+D marks an item done. Online only; greyed out offline.
- **AC4**: On the Mac you can use `cpa queue ls [key]`, `cpa queue add <key> <text>`, `cpa queue done <qid>`. The device follows within 5 seconds.

### US-09 Start a new session (F6, P0)
- **AC1**: Flow:
  1. Press Opt+S in a project, or Enter on a queued item;
  2. Type the task description and press Enter;
  3. A confirm box shows the project key, the first 60 characters of the text, and the permission mode;
  4. Press Enter to confirm.

  Key presses within 400 ms of the confirm box opening are ignored.
- **AC2**: The confirm box only offers "Start" when two conditions both hold: the link is online, and the daemon reports Claude as available. Otherwise the confirm box says "Offline, will be saved as queued", and Enter saves a queued item marked "originally meant to start a session".
- **AC3**: While online, the device sends an **online request**. It is not written to the outbox and is never retried automatically. The daemon runs it in the project root directly with an argument list, not through a shell:
  ```
  claude --bg --name <key>/<slug> --permission-mode <mode> -- <text>
  ```
  On success the device shows "Started · <short id>". The session appears on the board within 5 seconds at p95.
- **AC4**: If the link drops before the result comes back, the device shows "Result unknown, check the board", and saves the text as a queued item marked "uncertain". This may duplicate a session that did start, but the text is never lost and never retried automatically.
- **AC5**: The device cannot specify a path, arguments or permission mode; it can only send the project key and text. The daemon rejects text starting with `-` and never uses `bypassPermissions`. The permission mode comes from the project config, default `plan`.
- **AC6**: Failures include: Claude not found, project does not exist, start failed. The device only shows an error code and one line of Chinese hint; details are written only to the Mac's local log. The text is saved as a queued item.
- **AC7**: The contract tests use these malicious texts: `--dangerously-skip-permissions`, `-c`, `--permission-mode=bypassPermissions`, `$(rm -rf ~)`, `; ls`, control characters, a 501-byte text. Each is either refused or arrives as **one verbatim argv element**.

### US-10 Reply to a session (F7, P1, G3)
- **AC1**: Select a session in the inbox or on a project page and press Enter to open the capture page with type "feedback" and this session as the target. After typing, press Enter; the confirm box says whether this will be "send directly" or "save as queued".
- **AC2**: It sends directly only when all four conditions hold:
  - it is a background session (`kind=bg`);
  - its state is `idle`;
  - the link is online;
  - the daemon declares reply support (`caps.reply`), i.e. G3 passed and it is turned on in the config.

  Sending: the daemon first queries again to confirm it is still idle, then runs `claude stop <short>`, then `claude --bg --resume <sessionId> -- <text>` (it **must not** carry `--permission-mode`, or the CLI starts a separate copy; the original session's plan mode is kept. See docs/gates/cli-gates.md G3), and finally confirms the session ID did not change. On success the session goes back to "running".
- **AC3**: In every other case the text is saved as a queued item linked to this session, and you are told why: interactive sessions are only supported from v0.2, the session is running, or the device is offline.
- **AC4**: If the daemon finds that resume copied the session into a new one (a new sessionId appeared, or the output contains a copy notice), it returns `E_REPLY_FORKED`, turns off replying for the rest of this run, logs it, and saves the text as a queued item.
- **AC5**: Never resume an interactive session.

### US-11 Status bar (F8, P0)
- **AC1**: The status bar always shows:
  - Link state: ● Online / ⟳ Syncing / ◐ daemon not responding / ○ Offline
  - Items waiting to sync ↑N
  - Items waiting for you !N
  - Battery
  - Time: `--:--` before the first sync
  - Keyboard warning count: ⚠kN, only when greater than 0
- **AC2**: With no data from the daemon for more than 15 seconds, it switches to ◐ (still plugged into a USB host) or ○ (not plugged in). After a new handshake it returns to ● within 1 second.

### US-12 Daily notes (F9, P1)
- **AC1**: Each note appends one line to `<notes_dir>/YYYY-MM-DD.md`, formatted `- HH:MM #<key> 文本 ^cpa-<id>` (`文本` is the note text). Unassigned notes get no tag. When the device time is unknown, the Mac's receive time is used, written as `~HH:MM`.
- **AC2**: Writing is idempotent: if the file already has the same block ID, it is skipped. If a write fails, the daemon fills in the missing lines from the event log on startup.
- **AC3**: `notes_dir` is configurable, default `~/CardputerNotes`. It does not default to `~/Documents` or iCloud, because those directories are under macOS privacy protection (TCC) and a LaunchAgent cannot write there.
- **AC4**: v0.1 does not generate queue markdown; use `cpa queue ls` instead.

### US-13 Keyboard reliability (F10, P1, G1)
- **AC1**: If a newly pressed key and the keys already held form a complete rectangle on the matrix, the whole frame is judged ambiguous: the frame is dropped and counted. The count shows as ⚠k in the status bar and on the system page.
- **AC2**: Watchdog: if some key is down but the set of pressed keys has not changed for more than 10 seconds, reinitialize the keyboard and count it. Refine the trigger once G1 reproduces the lock-up.
- **AC3**: G1 acceptance: 10 minutes of copying a known text, character error rate (CER) no more than 0.5%, and no input stall longer than 1 second.

### US-14 Chinese display (F11, P0)
- **AC1**: Chinese in project names, session names and queue text displays correctly with efontCN_12. Firmware no larger than 2.5MB.
- **AC2**: The daemon truncates text on UTF-8 character boundaries; the device never shows half a character.
- **AC3**: Characters missing from the font show as `□`; if M5GFX cannot detect missing glyphs, its default behavior is kept, recorded by HIL. HIL checks rendering with real session names from this machine.

### US-15 Background service (F12, P1)
- **AC1**: `cpa service install` and `cpa service uninstall` install and remove the LaunchAgent, which starts the daemon when you log in to the Mac. The config records the absolute path of `claude`; at install time it captures the login shell's environment once, keeping only a whitelist such as `PATH`.
- **AC2**: `cpa pause [seconds]` (default 120 seconds) tells the daemon through a local unix socket to release the serial port for a while; it reconnects automatically when the time is up. The firmware project's pre-upload script calls it automatically.
- **AC3**: `cpa status` shows whether the daemon is running, device state, the bound serial number, Claude's version and capabilities, whether each project path is valid, and outbox statistics. **It shows no content.**

### US-16 System info (F13, P2)
- **AC1**: Opt+M opens Settings, and `/` goes to the device page (v0.4.3, docs/21 §4.5), which shows: firmware version, `dev`, boot id, outbox count (including bad items), free heap, lowest heap ever, largest contiguous block, loop stack headroom, ghost-key count and keyboard reinit count, battery voltage, daemon version; the last sync time and waiting count are on the "Sync" row of the settings page.

---

## 7. Core flows

### 7.1 Quick note: from key press to the Mac
1. On any screen, short-press G0 (or press Opt+N, Opt+Q, Opt+S) to open the capture page. The type is set by the key pressed, and the target is the current project.
2. Type text: Shift for capitals, the Fn layer for symbols, Backspace to delete (hold to delete repeatedly). If needed, change the target with Opt+0…9 and the type with Opt+N/Q/S.
3. Press Enter or G0 to submit. If the type is "new session", continue with step 3 of 7.6.
4. The device generates the item ID (MonoId, formatted `epoch-bootcount-sequence`), writes the item as `<id>.tmp`, closes the file, renames it to `<id>.json`, and only then shows "Saved · N waiting to sync".
5. While online, the device sends `outbox.item` at once. The daemon then: validates → dedupes → appends the event and fsyncs → replies `outbox.ack` → renders the daily notes. When the device receives the ack it deletes the file and ↑N goes down by one.
6. While offline, the item stays in the outbox and is uploaded in ID order after the next handshake.

### 7.2 Connection and handshake
1. The daemon finds the serial port with VID:PID `303A:1001`. If a serial number has been bound, only that one is accepted. It opens in the safe order: open with `dsrdtr=True`, then `rts=False`, then `dtr=False` (03 §2).
2. The daemon sends `hello`. With no reply within 2 seconds it resends, up to 3 times. If the reply is not this project's firmware, it releases the port and retries after 30 seconds.
3. After a successful handshake, the daemon sends `time.set`, `projects.snapshot`, `sessions.snapshot`, `queue.snapshot` in turn; meanwhile the device starts uploading the outbox.
4. The daemon sends a `ping` every 5 seconds. In `pong` the device reports the version number (rev) of each snapshot it holds; if one is behind, the daemon pushes it again. If either side receives nothing from the other for 15 seconds, the link is considered dropped.
5. If the device's boot id changes or `up_ms` gets smaller, the device has rebooted: go back to step 3. The daemon never resets the device by toggling control lines.

### 7.3 Project board
1. Only while the device is online does the daemon run `claude agents --json` every 3 seconds. With the device offline there is no polling at all.
2. For each session, take the realpath of `cwd` and match it against the registered project roots by longest prefix. If nothing matches, try again with `git rev-parse --git-common-dir` (the result is cached). If still nothing matches, file it under "Other".
3. The daemon records when each session entered its current state, counts running, waiting for you and queued per project, and builds a bounded snapshot. When the snapshot content changes, `rev` is incremented and it is sent down. A change in durations alone does not count as a content change; the device counts durations up by itself.

### 7.4 Waiting-for-you inbox
1. The daemon decides whether each session "needs you": its state is `waiting`, or its state is `idle` and has lasted more than 60 seconds; and the session is not on your dismiss list.
2. The device compares the previous and current snapshots; when it finds a new "needs you", it shows a notice and updates !N.
3. With an item selected:
   - Enter: reply (7.5)
   - Opt+D: dismiss
   - Background sessions show `claude attach <short>`; interactive sessions show "Handle on the Mac".

### 7.5 Feedback: send directly, or save as queued
1. Select a session, press Enter, type the feedback, press Enter.
2. The confirm box says whether this will be "send directly" or "save as queued". It is "send directly" only when all four conditions of US-10 AC2 hold.
3. Send directly: the device sends `agent.reply`, the daemon runs "stop → resume → check ID", then replies `agent.result`.
4. Save as queued: the device writes an item with `kind=queue`, `intent=feedback` into the outbox, linked to this session. In v0.1 such a queued item can only be used to start a new session; sending it back to the original session comes in v0.2.

### 7.6 Start a new session
1. Press Opt+S in a project, or Enter on a queued item.
2. Type the task description and press Enter.
3. The confirm box shows: "Start a new session in <key> (plan mode): 'first 60 characters' Enter to start / Esc to cancel". Key presses within 400 ms of it opening are ignored.
4. Offline: save as a queued item (`intent=spawn`), show "Offline, queued", and the flow ends.
5. Online: the device sends `agent.spawn{project, text, qid?}`; this request does not go into the outbox.
6. The daemon checks each item: link Ready, project registered, path valid, Claude available, text valid (length, UTF-8, not starting with `-`). If any check fails, it replies `agent.result{ok:false, code}`.
7. The daemon generates the session name `<key>/<slug>`, then runs the argv given in 7.6. It parses the short id from stdout; if that fails, it diffs `agents --json` from before and after the run and finds the new session by name. Then it writes the event and replies `agent.result{ok:true, name, short}`.
8. After the next poll, the new session appears on the board.

### 7.7 Offline voice (v0.2, requires G2 to pass)
1. Long-press G0 (≥500 ms) to start recording, keeping 300 to 500 ms of pre-roll from before the press.
2. The recording is written to the SD card: file name `/rec/<id>.wav.part`, a new segment every 5 minutes. When done it is renamed, its SHA-256 computed, and it is registered in the outbox. Without an SD card nothing is recorded, and a notice is shown.
3. Once connected, it is uploaded in chunks; the daemon checks the SHA-256 before acking, then transcribes. The transcript becomes a note or a queued item; execution operations still need online confirmation.

---

## 8. Key scheme (final in v0.1; from v0.4.0 the tables in this section apply)

> **v0.4.1 changes** (docs/20 D1–D9, decided 2026-10-22): on Home, R and L need Opt; in the editor ` exits and Fn+` types a backtick; on the start-session and reply confirm boxes ` returns to the editor; the meeting start and end boxes have Opt+L and close by themselves; holding G0 under an overlay also records a memo; with the menu open, a chord the menu shows runs; on battery, screen off means locked. The tables below already include these, with changed rows marked v0.4.1; the comparison with v0.4.0 is under "Key changes" in docs/releases/v0.4.1.md.

> v0.3 added the recording keys (§8.6); v0.4.0 added Home, the Tab menu and the lock screen (§8.2, §8.3, §8.7, §8.8; design in [13 §5–§7](13-assistant-v2-plan.md)).
> Every binding is in `kKeymap` in `firmware/app/lib/input/keymap.cpp`, and the description of each action is in `firmware/app/lib/help/action_help.cpp`. The tables in this section and the key table in the README must match those two places; change them together when changing a key (the PR checklist in [CONTRIBUTING](../CONTRIBUTING.md) has this line).

### 8.1 Constraints
- **Only single keys and two-key chords.** No three-key chords at all: Fn+Shift, Ctrl+Opt, Ctrl+Alt plus a third key all produce ghost keys (02 §6). Every chord in the keymap must pass the native static check (05 §5.7).
- **G0 is the dedicated capture key**, read only while the firmware is running.
- **Opt is a layer reserved for this app**, because Opt means nothing in the HID keyboard protocol. Ctrl and Alt are left unused for now, kept for later.
- **Accidental-press guard**: key presses within 400 ms of a confirm box opening are ignored.
- **Only one key table** (v0.4.0): the Tab menu is generated from `kKeymap`. A menu row and a Home tile both "press that key for you", so the effect is the same as pressing it directly. `test_action_help` ensures every action has a description.
- **One letter stands for one thing** (v0.4.0). Letters reserved but not bound yet are in [13 §5.2](13-assistant-v2-plan.md); Z is not used (tests use Opt+Z as the "unbound" example).
- **Key dispatch order**: lock gate → wake screen → Tab menu → Tab and Opt+L → recording overlay → overlays (capture page, confirm box) → global chords → foreground module, then at most one key is replayed (05 §5.6). So Opt+1…9 on the capture page changes the target; it does not switch projects.

### 8.2 Global keys (when there is no overlay)

| Key | Action | Change |
|---|---|---|
| G0 short press (<500 ms) | Opens a quick note of type "note", saved into today's notes. After release it waits 0.35 s for a second press before opening; any other key pressed meanwhile opens it at once and that key goes in. Holding G0 while the quick note is still empty closes it and records a voice memo | From v0.4.1 waits 0.35 s (gh #51) |
| Hold G0 (≥500 ms) | Records a voice memo, ending 0.6 s after release (§8.6; pressing again within 0.6 s continues the same one, v0.4.1, gh #51) | From v0.3 (v0.1 and v0.2 only said "voice comes in v0.3") |
| Double-tap G0 | Hands-free voice memo: recording starts the moment the second press goes down and keeps going after release; press G0 once more to end (§8.6). Both presses must be shorter than 500 ms, and the second must come within 0.35 s of the first release; a slower one (within 1 s, no other key in between) also counts, and an already open empty note is closed. If the second press is held to 500 ms, it counts as hold-to-talk (release ends it) | New in v0.4.1 (gh #51) |
| Tab | Opens the menu (§8.7) | New in v0.4.0 |
| Opt+H | Home (launcher, §8.3) | **Meaning changed in v0.4.0**: it used to be the project board |
| Opt+P | Project board | New in v0.4.0; this is the old Opt+H |
| Opt+1 to Opt+9 | Opens the project page with that number | — |
| Opt+I | Opens the waiting-for-you inbox | — |
| Opt+M | Settings: WiFi (explanation), Language, Connection, Sync; `/` is device diagnostics (docs/21 §4) | **Renamed in v0.4.3**: used to be called "System" and went straight to diagnostics |
| Opt+N / Opt+Q / Opt+S | New note / queued item / new session | — |
| Opt+R | Meeting recording: shows a confirm box first, starts only on Enter (§8.6) | From v0.3 |
| Opt+L | Lock screen (§8.8) | New in v0.4.0 |
| `` ` `` (top-left key) or Fn+` | Cancel or back (a single key is enough from v0.2, #15) | v0.4.1: in the editor `` ` `` exits and Fn+` types a backtick (§8.4) |

Undefined Opt chords are simply ignored, and no character is typed.

### 8.3 List pages (project board, project page, inbox) and Home

Project board, project page, inbox:

| Key | Action |
|---|---|
| `;` / `.` | Move up / down |
| `,` / `/` | Previous / next tab |
| Enter | Main action: open a project, reply to a session, start a session from a queued item |
| Opt+D | On a queued item, mark done; in the inbox or on a session, dismiss |
| `` ` `` or Backspace | Back one level |

**Home** (v0.4.0, launcher) is the root of the navigation stack; pressing back on Home goes nowhere further:

| Key | Action |
|---|---|
| P, I, N, Q, S, M | The letter on a tile, same as Opt+that letter (either case): Projects, Inbox, Note, AI queue, New session, Settings |
| R, L | Do nothing (v0.4.1, docs/20 D1: hitting them while typing does not start a meeting or lock). For meeting and lock press Opt+R, Opt+L, or select the tile and press Enter |
| Other letters | Do nothing; each time you enter Home, a hint says once how to write a note (v0.4.1) |
| `;` `.` `,` `/` | Move up, down, left, right in the 3×3 grid, wrapping at the edges |
| Enter | Same as pressing the selected tile's key; on the "Tab menu" tile it opens the menu |
| 1 to 9 | Opens project page N, same as Opt+1 to Opt+9 |

### 8.4 Capture page and confirm boxes

| Key | Action |
|---|---|
| Letters, digits, symbols | Input. Shift for capitals; Fn-layer symbols follow M5Cardputer 1.2.0 |
| Backspace | Deletes one character. Held for 500 ms, it deletes one every 60 ms. **When the input is empty, a fresh press of Backspace exits** (v0.2, #15; repeat-delete never exits by accident) |
| Enter or G0 | Submit |
| `` ` `` | Exit (v0.4.1, docs/20 D5; used to be Fn+`). With text already typed it first asks "Discard what you typed?": Enter discards (an Enter within 400 ms of it opening does not count), `` ` `` or Fn+` goes back to editing; pressing Tab on it shows exactly these three rows in the menu, the first being back to editing, and Opt+L does nothing |
| Fn+` | Types the backtick character (v0.4.1) |
| Hold G0 | Records a voice memo and keeps the draft (v0.4.1, docs/20 D4); the same 0.6 s grace applies after release, and the press that resumes does not submit. Holding in a quick note that is still empty: the note closes and a memo records (gh #51) |
| Opt+0…9, Opt+N/Q/S | Change target, change type |
| Tab | Opens the menu with only the "Here" and "Control" pages; the draft is untouched (v0.4.0) |
| Opt+L | Lock screen, the draft stays in memory (v0.4.0) |
| Confirm box: Enter / `` ` `` or Fn+` | Confirm / cancel (key presses within 400 ms of it opening are ignored). On confirm boxes reached from the editor (start session, reply), `` ` `` returns to the editor and keeps the text and target (v0.4.1, docs/20 D6); the box and the menu use the same words: "Enter start · ` back to edit" |
| Confirm box: Tab | Opens the menu with only the "Here" and "Control" pages (v0.4.0). Opt+L does nothing on a confirm box; deal with the box first |

v0.1 has no cursor movement, no multiple lines and no Leader key (the Tab menu is not a Leader key either, see 05 §5.7); apart from Backspace, no key repeats when held.

### 8.5 Screen sketches (240×135, sketches only)

The sketches show the English UI. The Chinese UI has the same layout; see the Chinese version of this doc.

Project board (from v0.4.0 open with Opt+P, or P on Home):
```
● online ↑0 !2                13:42 83%
1 cpa   Cardputer helper  run1 wait1 q2
2 blog  Blog              run0 wait0 q0
3 th    Thesis            run0 wait1 q5
  Other                   run0 all1
[Enter]open [;.]pick [Opt+1-9]proj [Tab]menu
```
```
Inbox (2)                    ● online
! cpa/architecture  waiting  9m   tty
  th/outline        idle    40m   bg
Handle on the Mac: architecture
[Enter]reply [Opt+D]hide [`]back [Tab]menu
```
```
New session → cpa (plan)   ↑3 ○ offline
write native tests for the chord
detector_
                             52/500
[Enter]save  [`]exit  [Tab]menu
```

Home (v0.4.0). The selected tile is marked with a highlighted background:
```
● online ↑0 !2                13:42 83%
Home · 10/21 Wed
P Projects run3  I Inbox !2   R Meeting
N Note           Q AI queue   S Session
M Settings       L Lock       Tab Menu
! cpa/architecture            waiting 9m
Hold G0 to talk · Opt+letter jumps
letters jump [;.,/]move [Enter]open [Tab]menu
```

Menu · Here (opened on the meeting page while recording, so the status bar row stays):
```
● online ↑0 !2 ●42:10         13:42 83%
Menu · Meeting               [Here] Control
G0      Add ★
Hold G0 Add ★
Space   Add ★
0-9     To project, 0: none (press 0-9)
Opt+R   End meeting…
`       End meeting…
Opt+L   Lock, keep recording
[;.]pick [Enter]do [,/]page [Tab]close
```

Menu · Control (when not recording, the menu covers the whole screen):
```
Menu · Control             Here Go [Control]
Opt+L  Lock (unlock: Opt+L then Enter)
Sound  Off (silent)
Bat    84% · 4021mV
SD     412G free · R2 · ↑3
Mac    online · cpad/0.1.0
[;.]pick [Enter]do [,/]page letters jump [Tab]close
```

Lock card (v0.4.0, counts only):
```
              Locked
              13:42
         10/21 Wed · Bat 83%
       Waiting 2 · R2 · ↑0

   Unlock: Opt+L, then Enter within 3 s
     Talk: tap G0, then hold G0
```

### 8.6 Recording overlays (v0.3)

The recording design is in [11 §4.1](11-m3-design.md). During recording these overlays take every key; keys not in the table do nothing.

| Where | Key | Action |
|---|---|---|
| Memo (while holding G0) | 1 to 9 / 0 | File to project N / no project |
| Memo | Release G0 | Ends and saves after 0.6 s; pressing G0 again within those 0.6 s continues the same memo (v0.4.1, gh #51), and that press belongs only to this memo (the same for memos recorded on the editor, a confirm box or the menu). The top of the page says "Let go · hold within 0.6 s" (in the last 30 seconds, "hold within 0.6 s"). Anything under 1 second (`rec_min_ms`, grace included) is discarded |
| Hands-free memo (double-tap G0) | G0 | Ends and saves on press (the release does not count as a second press; a tap within 0.35 s after it ends does not count either and does not open a quick note); also while the Tab menu is open, which then closes |
| Hands-free memo | 1 to 9 / 0 | File to project N / no project |
| Hands-free memo | Tab | Opens the menu with only the "Here" and "Control" pages; the "G0 stop and save" row is only a description and Enter does not end; Opt+L closes the menu and locks, recording continues (docs/20 D9) |
| Hands-free memo | Opt+L | Lock screen, recording continues |
| Hands-free memo | Other keys | Do nothing (guards against pocket presses), do not end the recording |
| Meeting confirm box | Enter / `` ` `` or Fn+` | Start / cancel. An Enter within 400 ms of it opening does not count; the screen says "Too fast: press Enter again" (v0.4.1) |
| Meeting confirm box | Other keys (letters, digits, Space…) | Cancel (v0.4.1, docs/20 D3); 15 seconds with no key also cancels, counted from the last key press |
| Meeting confirm box, end confirm box | Tab | Opens the menu with only the "Here" and "Control" pages (v0.4.0); the box does not close by itself while the menu is open |
| Meeting confirm box / end confirm box | Opt+L | Cancel and lock / keep recording and lock (v0.4.1, docs/20 D3) |
| Meeting confirm box, end confirm box | Hold G0, double-tap G0 | Does not record; says "Answer the box first" |
| Meeting page | G0 (short press or hold), Space | Add ★ |
| Meeting page | 1 to 9 / 0 | File to project N / no project |
| Meeting page | Opt+R | End meeting: shows the end confirm box first, Enter ends, `` ` `` keeps recording; after 10 seconds with no key it returns to the meeting page (v0.4.1) |
| Meeting page | `` ` ``, Enter | Only shows "In a meeting: Opt+R ends it" (v0.4.1, docs/20 D3: `` ` `` used to open the end confirm box) |
| Meeting page | Tab | Opens the menu with only the "Here" and "Control" pages (v0.4.0) |
| Meeting page | Opt+L | Lock screen, recording continues (v0.4.0) |

Which project each of 1 to 9 / 0 means is always written on the memo page and the meeting page (gh #53): in the order of the number row, the digit is the keycap followed by the project key, and `0` is "none"; the current choice is lit, pressing a digit switches to it, and a digit with no project shows `?`. With many projects it takes one more line, and long project keys are cut short (no "…"); on the meeting page, if there are too many projects, 0 is left out (the title shows the destination).

During a memo (while holding G0), Tab and Opt+L do nothing. Hands-free memos have the same 5-minute limit as hold-to-talk memos; when reached they end and save automatically ("5 min limit reached · saved 5:00").

### 8.7 Tab menu (v0.4.0)

Press Tab to open it, on any page (except while holding G0 for a memo and while locked; a hands-free memo is fine). The menu has three pages:

| Page | Content |
|---|---|
| Here | The keys for this page: rows the app writes itself (`help()`; when the app writes none, the list-page keys); on an overlay, the bindings of that layer in `kKeymap`; Opt+L added at the end (if this page has not listed it yet). Tab is not listed |
| Go | Bindings of the global layer: first Home, project board, Inbox, Opt+1-9, Settings, then the rest. Opt+L is on the "Control" page |
| Control | A lock row, then read-only facts: sound (off, recording is silent), battery, SD card free space and waiting, Mac link and cpad version |

When opened on a capture page, a confirm box or the meeting page there is no "Go" page; you cannot jump elsewhere from the menu, and drafts and ongoing recordings are not affected. A confirm box has no Opt+L row.

| Key in the menu | Action |
|---|---|
| `;` / `.` | Previous / next row |
| `,` / `/` | Previous / next page |
| Enter | Closes the menu, then presses this row's key for you, with the same effect as pressing it directly. Rows for a group of keys (such as `Opt+1-9`) and the "Hold G0" row are only descriptions, and Enter does nothing |
| Letter | On the "Go" and "Control" pages: same as Opt+that letter (only if this page has that row). On "Here": the letters for global keys and lock are the same as Opt+that letter, so Tab then P twice gets you to the project board; on a capture page, confirm box or meeting page, letters on "Here" are swallowed by the menu |
| G0 (short press or hold) | Closes the menu, and G0 works as usual: in a meeting it adds ★, otherwise it writes a note or records a memo |
| Tab, `` ` ``, Backspace, Fn+` | Close the menu |
| A chord shown in the menu (such as Opt+L) | Closes the menu, then works as usual (v0.4.1, docs/20 D9) |
| Other keys | Swallowed by the menu, do nothing |

While recording, the menu keeps the status bar row, so ● and the timer are always visible.

### 8.8 Lock screen (v0.4.0)

After Opt+L the screen goes dark at once and the keyboard is locked, **even while plugged into the Mac**. The page from before the lock, the capture-page draft and an ongoing meeting all stay as they were, and recording does not stop.

| Key while locked | No meeting | In a meeting |
|---|---|---|
| Opt+L | Lights the lock card and opens a 3-second window; Enter within the window unlocks and returns to the page from before the lock | Same |
| G0 short press | Lights the lock card for 3 seconds; holding G0 within those 3 seconds records | Add ★ |
| Hold G0 without a tap first | Only lights the lock card, saying "Not recording / Let go, tap, then hold", and does not record (nothing is recorded from this press to its release; from v0.4.1 pressing like this while the card is lit does not record either) | Add ★ |
| Hold G0 after a tap | Records a voice memo; 0.6 s of grace after release (gh #51); when done the card lights for 2 seconds with the result, then goes back to dark and locked | Add ★ |
| Double-tap G0 on a card lit by a key press | Hands-free voice memo (v0.4.1, gh #51; the first press while dark, or while the card lit by itself, only lights the card and does not count); once started the card goes dark and the top-left shows "● Hands-free 0:12" | Add ★ |
| G0 while a hands-free memo is recording | Card lit: ends and saves, stays locked, the card lights for another 3 seconds with the result ("√ Saved 0:06"); screen dark: only lights the card (saying "Hands-free: G0 ends it", not "Not recording"), recording continues | — |
| Any other key | Does nothing and does not light the screen | Same |

- On battery, when the screen turns off after 60 seconds without a key press, the device locks automatically (v0.4.1, docs/20 D2); not while plugged into the Mac.
- When the card lights by itself (v0.4.1, docs/20 D7): 2 seconds at the moment of locking ("Locked"); 2 seconds when a recording ends while locked, with the result; 3 seconds while plugged into the Mac when the waiting-for-you count goes up. Enter and Space while locked do not light it.
- When locked on the capture page the draft is kept; tap then hold G0 records a memo as usual, the draft is not affected, and it is still there after unlocking.
- The lock card only shows time, date, battery and counts (waiting for you, recording segments waiting to transfer, ↑N); it shows no project names, session names or any content. No notices pop up while locked.
- While recording, the backlight only goes as low as "dimmed", and ● and the timer always show at the top left; a memo shows "● Wait" until the microphone opens, so speak once you see ● REC.
- **The lock screen is not a security lock**: there is no PIN; it only guards against pocket presses and keeps others from seeing content ([SECURITY.md](../SECURITY.md)).

---

## 9. Online and offline states

| State | How it is decided | What you can do | Status bar |
|---|---|---|---|
| Online | Link Ready, and data from the daemon arrived within 15 seconds | Everything | ● online |
| Syncing | Online, and the outbox still has items | Everything | ⟳ ↑N |
| daemon not responding | Plugged into a USB host (`isPlugged` is true), but not handshaken yet, or no data for 15 seconds | Quick notes work; the board shows the last data; execution operations fall back to queueing | ◐ |
| Offline | Not plugged into a USB host. A charger alone also counts | Same as above | ○ offline |

**Rule**: while offline, no execution operation is cached to run later when connected (principle 2).

---

## 10. Fault states

| # | Situation | On the device | On the Mac | Is data lost | Recovery |
|---|---|---|---|---|---|
| E1 | daemon not running | Shows ◐ or ○; quick notes work; the board shows the last data | `cpa status` shows "not running" | The outbox keeps accumulating, nothing lost | `cpa service install`, or `cpa daemon` by hand |
| E2 | Claude Code not installed on the Mac, or `agents --json` fails | The board shows "Claude Code not found on the Mac"; starting sessions and replying are greyed out | The log states the reason | Quick notes and queued items sync as usual | The daemon re-checks every 60 seconds |
| E3 | Wrong CLI version, some feature missing | Related operations greyed out | Capability check at handshake, written to the log | — | Upgrade the CLI |
| E4 | Device reset, power loss, watchdog, low voltage | Back to the home page after reboot; submitted items are all there; unsubmitted drafts are lost (v0.1 accepts this) | Notices the boot id changed, syncs again | 0 submitted items lost | Recovers automatically |
| E5 | G0 held at reset, entered download mode | Black screen | Handshake fails, log says "probably download mode" | Not affected | Switch to OFF, unplug the USB cable, wait a few seconds, plug back in (spelled out in the README) |
| E6 | Serial port held or grabbed by another program, e.g. `pio device monitor`, esptool | Shows ○ or ◐ | Log says "port busy". Note: pyserial's exclusive lock is only advisory; another program may silently take some of the data or reset the device | Syncs again automatically after the device reboots | `cpa pause` before flashing (the pre-upload script calls it automatically) |
| E7 | Protocol major versions differ | Says "Versions incompatible, please update" | Releases the port, `cpa status` reports the reason | Outbox kept | Upgrade whichever side is behind |
| E8 | Outbox full | Refuses new items, says "Storage full, connect to the Mac to sync" | — | Nothing lost | Connect to the Mac and sync once |
| E9 | Starting a session or replying fails | Shows an error code and a Chinese hint, e.g. "Claude 启动失败（E_SPAWN）" (Claude failed to start) | Details only in the local log | Text saved as a queued item | Read the log, fix, retry |
| E10 | Result of starting a session unknown (link dropped midway) | Says "Result unknown"; text saved as a queued item marked "uncertain" | — | Not lost, may be duplicated | Check on the board whether the session started |
| E11 | A registered project path is no longer valid | That project is marked ⚠, starting sessions greyed out | Shown in red in `cpa project ls` | — | Fix the path, or remove the project |
| E12 | Device does not know the current time | Time shows `--:--`, items have an empty `wall_ms` | Notes use the receive time, written `~HH:MM` | — | Synced automatically at the next handshake |
| E13 | Keyboard locked up | Reinitializes automatically; if that fails, says "Keyboard not responding, press BtnRst to restart" | — | Submitted items not lost | Recovers automatically, or press BtnRst |
| E14 | Invalid message received (too long, not UTF-8, unknown type) | Dropped and counted, no crash | Same | — | — |
| E15 | Battery below 10% | Battery turns red with a notice | — | Submitted items are already on disk | When charging, the side switch must be ON |
| E16 | Another ESP32-S3 board is plugged in | — | Releases the port within 6 seconds, retries after 30 seconds; once a serial number is bound it is never touched again | — | — |
| E17 | Resume copied the session into a new one when replying | Says "Reply failed, queued" | Replying turned off for the rest of this run, logged | Text saved as a queued item | Check the G3 conclusion |

---

## 11. Privacy and security requirements

**v0.1 trust boundary**: a physical USB connection, plus the macOS user currently logged in. The daemon **listens on no network port**; local control only goes through a unix socket with permissions 0600 in `$CPA_HOME`.

- **S1 Only accept our own firmware, and bind the serial number.** The daemon only takes over ports whose handshake reply is `cpa-fw/`; after the first successful handshake it binds the USB serial number (US-04). v0.1 **does no pairing**: a pairing code with no key behind it lets any firmware impersonate the same `dev`, so it would be for show only. Real pairing comes in v0.3 together with wireless.
- **S2 The device cannot control commands.** The device can only send a project key, text, and a `qid` or `sid` the daemon itself sent down. Paths, arguments and permission mode are all decided by the daemon from the registry.
- **S3 Argument injection defence.** The daemon calls the CLI with an argument list, not through a shell. The text goes after `--`, as one argument. Text starting with `-` is refused outright. A `sid` must match exactly one entry in the latest `agents --json` result.
- **S4 Explicit permission mode.** `--permission-mode` is always passed, from the project config, default `plan`. `bypassPermissions` is refused; auto is used only if the project config explicitly says `auto`. So the worst a session started from the device can be is "a session in plan mode".
- **S5 Execution only online.** Starting sessions and replying can only be sent as online requests, need a key press on a confirm box, and require the link to be Ready. The outbox has no type that can trigger execution, so nothing written offline can be executed while you are not there (US-03 AC6).
- **S6 Both sides validate input.**
  - The message type must be on the whitelist;
  - a line is at most 2048 bytes, text at most 500 bytes;
  - it must be valid UTF-8;
  - control characters are replaced with spaces;
  - IDs must match their format.

  Invalid messages are dropped and counted.
- **S7 Error messages leak no details.** The device only gets error codes. Details such as paths and command output are only written to the Mac's local log. The log records only the length and the first 8 characters of the hash of any text, unless `CPA_LOG_CONTENT=1` is set explicitly.
- **S8 Data stays on this machine.** The `$CPA_HOME` directory has permissions 0700 and files 0600, checked at startup. No cloud, no account, no telemetry.
- **S9 Keep as little data on the device as possible.** Outbox items are deleted on ack; session information is only in memory; the device never gets project paths.
- **S10 No keys.** The v0.1 device holds no keys, and the repo has no hardcoded keys.
- **S11 No flash encryption and no Secure Boot.** Known risk: whoever has the device can read items in LittleFS that **have not synced yet**. Mitigation: delete right after syncing; the status bar shows the number waiting to sync; v0.2 evaluates a PIN lock.
- **S12 No user content in debug logs.** Release firmware sets `CORE_DEBUG_LEVEL=0`; only test firmware has debug messages.

---

## 12. Non-functional and engineering requirements

### Performance (all p95)

| Metric | Target | How measured |
|---|---|---|
| G0 press until the capture page accepts input | ≤650 ms (from v0.4.1 including the 0.35 s double-tap window, gh #51; previously ≤300 ms) | HIL: inject key presses, read the `debug.state` timestamps of the test firmware |
| Submit until "Saved" shows | ≤100 ms | Real-device test: 200 saves in a row |
| Syncing 100 outbox items after the handshake | ≤5 s | HIL |
| State change on the Mac until the device updates | ≤5 s | HIL: fake-claude flips the state 100 times |
| Confirming a session start until it appears on the board | ≤5 s | HIL, with fake-claude |

### Resource budget
- Device:
  - free heap ≥150KB, largest contiguous block ≥64KB
  - static RAM ≤96KB
  - loop stack 16KB, headroom shown on the system page
  - `AppState` ≤4KB
  - firmware ≤2.5MB
- Outbox storage: a 1.5MB LittleFS data partition, at most 200 items, and free space never below 64KB.
- daemon CPU: no polling with no device connected; with the device online, average use no more than 5% of one core. At one poll every 3 seconds and about 0.1 s each, that is about 3%.

### Reliability
- **v0.1**:
  - native tests inject a crash at every storage step, with 0 lost and 0 duplicated;
  - 10 manual power cuts, 0 lost;
  - 1-hour soak connected over USB, with no serial hang and no lost messages.
- **v0.1.x**: 8-hour soak, 100 power cuts. These two were in the v0.1 definition of done and moved here, because every power cut requires switching to OFF and unplugging the cable, and 100 of them is not realistic for v0.1.

### Engineering requirements (matching your development rules)
- **Immutable data.**
  - Every state on the Mac side is derived from an append-only event log and represented with frozen dataclasses.
  - All business logic in the firmware is pure functions: `old state + input → new state + side effects`. State is only replaced whole, never modified in place.
  - Hardware buffers (serial send/receive rings, display sprites) are inherently mutable and are only allowed in `hal/` and `usb_transport/` (§16 open item 5).
- **Code size.**
  - Each file 200 to 400 lines, at most 800; functions under 50 lines; nesting no deeper than 4 levels.
  - Directories organized by feature.
  - Checked in CI with `tools/repo/check_sizes.py`, lizard and ruff from WP1 on; clang-tidy's function size rule is added in WP4.
- **TDD (RED → GREEN → REFACTOR)**, three layers of tests:
  - Unit tests: `pio test -e native` for firmware pure logic, pytest for the daemon.
  - Integration tests: the daemon against a device simulated with a pty, plus a fake `claude` on PATH.
  - End-to-end tests: real-device HIL scripts, plus a manual checklist.

  Coverage target 80%, measured separately for firmware pure logic and the daemon.
- **Security.** Before every commit confirm: no hardcoded keys; all inputs validated; no sensitive content in error messages.
- **Commit messages** use the conventional commits format (`feat:`, `fix:`, etc.).

### v0.1 definition of done (DoD)
- Every P0 story meets its acceptance criteria on the real device. P1 stories either meet theirs or are explicitly moved to v0.1.x.
- All three test layers green, coverage ≥80%. 1-hour soak, power-loss fault injection and 10 manual power cuts done.
- The README's 15-minute goal is measured for real: in a **brand-new macOS user account**, starting from cloning the repo, through flashing, `cpa service install`, `cpa project add`, until the board shows on the device, timed end to end.
- LICENSE (Apache-2.0), NOTICE (credits and licenses), a CONTRIBUTING draft, ADR-0001 (toolchain) and ADR-0002 (protocol v0) exist.

---

## 13. Success metrics (two weeks of own use, counted locally only)

| Metric | Target |
|---|---|
| Items captured through the device per day | ≥5 |
| Sessions started from the device plus feedback sent, per week | ≥5 |
| Submitted items lost | 0 |
| Duplicated items | 0 |
| Restarts needed per week because of device problems | ≤1 |
| Median time from an inbox item appearing to being handled | Observed and recorded only, no target. Used to judge whether the "waiting for you" reminder is useful and how noisy it is |

---

## 14. Risks and verification gates

| Gate | What to verify | How | When | If it fails |
|---|---|---|---|---|
| G1 | Ghost keys and keyboard lock-up | Record raw key codes with hwtest and test:<br>• Opt plus every key<br>• `Ctrl+Shift+S`, `Fn+Shift+A`, `Ctrl+Alt+W`<br>• copy a known text for 10 minutes, requiring CER ≤0.5% and no stall over 1 second | Day 0 (manual) | The key map already uses only two-key chords; strengthen the filter; write our own TCA8418 driver that drains the FIFO every round |
| G2 | Toolchain spike: the microphone on option A (6.10.0) and option B (pioarduino 54.03.21-2), **including speaker first, then recording**; B must also pass four checks: CDC round trip ≤100 ms, microphone RMS not 0, SD write and rename, all three libraries compile | Modify hwtest, run once on each toolchain | Day 0, half a day at most | Use B only if it passes all four checks that day, otherwise A. Write the conclusion into ADR-0001 and update 03 §3 to match |
| G3 | For a background session that is **idle but whose process is still alive**, after `claude stop <short>` then `claude --bg --resume <sessionId> -- <text>`, whether the session ID stays the same and the new prompt is handled | Run it by hand in a temporary project, check with `agents --json --all` | Day 0 | Drop F7, queue all feedback |
| G4 | Semantics of `status`: whether `waiting` means waiting for permission or a question; when `idle` appears; whether desktop-app sessions are always listed; what the `daemon` and `daemon-worker` kinds are | Create each situation and compare the `agents --json` output | Day 0 | The inbox degrades to "every session that is neither busy nor shell" |
| G5 | How much flash efontCN_12 takes | — | **Closed**: 213,444 B | — |
| G6 | Whether HWCDC is stable over long connections, and whether bytes are lost with an 8KB receive buffer | 1-hour soak (v0.1), 8-hour soak (v0.1.x) | M4 | Switch to IDF's `usb_serial_jtag` driver, or upgrade to Arduino 3.3.x (the microphone must be re-verified per G2) |
| G7 | Whether the short id can be read reliably from `claude --bg` stdout | Measure it and record the CLI version | Day 0 | Diff `agents --json` before and after the run, match the new session by name |
| G8 | Whether `--` is treated as the end of options; whether `--permission-mode` and `--bg` work together | In a temporary project, start a session in plan mode with the text `-- "-c test"` | Day 0 | Do without `--`, relying only on refusing text that starts with `-` to prevent injection |
| G9 | Whether a session started from the LaunchAgent has correct environment variables and project trust state | Start a session from the LaunchAgent and have it run `which uv pio node`; also try once in a directory never trusted | Before M3 | Capture the login shell's environment at install time (whitelist); require the project to have been opened with Claude in a terminal once beforehand; self-check in `cpa status` |
| G10 | Whether write-then-rename on LittleFS is atomic under power loss; whether save latency p95 is ≤100 ms | Device tests plus 10 manual power cuts | M1 | Pre-format the partition; keep items small; add a "Saving" state |

**Other risks:**

| Risk | Response |
|---|---|
| `agents --json` and `--bg` are both still research previews, and fields may change | Parse leniently, record unknown values as `unknown`; keep redacted samples per CLI version for tests; detect capabilities at startup |
| Inbox noise: the session you are using on the Mac right now also counts as "waiting for you" | `idle` has to last 60 seconds to count; manual dismiss; "Other" does not enter the inbox; `waiting` sorts first |
| The ghost filter may drop real key presses | Backstopped by the G1 CER test; drops are counted and shown as ⚠k |
| The user mainly runs sessions in the Claude desktop app | v0.2's mods injection may not cover it; needs your answer to §16 open item 3 |
| A one-person plan easily outgrows its scope | Pairing, hooks, HTTP, SD and the Leader key have already been cut after review (§17) |

---

## 15. Decision table

| # | Decision | Default choice | Reason | When to revisit |
|---|---|---|---|---|
| D1 | Open-source license | Apache-2.0 | Has a patent grant clause; can legally reuse code from cli-buddy, cardputer-claude-os (both Apache-2.0) and MIT projects | Before the first release |
| D2 | Which AI assistants to support | Claude Code only for now. The daemon keeps a very thin adapter interface: `probe`, `list_sessions`, `spawn`, `reply` | It is the one you use | v0.3 |
| D3 | Connection | USB CDC only in v0.1 | No driver, already verified; plugging in the cable is itself a form of trust | v0.3 |
| D4 | Chinese | Display only, no input method on the device | No PSRAM; Chinese input comes later through voice | After v0.2 |
| D5 | Where the logic lives | All heavy logic on the Mac | L1 | — |
| D6 | Firmware toolchain | Decided by G2 on day 0: B if it passes all four checks, otherwise A. Exact versions pinned either way | L11; IDF 4.4 is EOL; HWCDC in Arduino 3.2.1 is the same as in 2.0.17, so the kick is still needed | Move to 3.3.x in v0.3 |
| D7 | daemon tech stack | Python 3.12 + uv + `pyserial`, otherwise standard library only | The safe serial-open code is already verified; fewest dependencies | When v0.2 adds hooks or MCP |
| D8 | How v0.1 connects to Claude Code | CLI only: `agents --json`, `--bg`, `stop`, `--resume`. No hooks, no ports | Zero configuration, can be uninstalled at any time | v0.2 |
| D9 | Mac-side storage | Append-only `events.jsonl`, with every other view derived from it | Idempotent, auditable, fits the immutability rule | When the data volume grows |
| D10 | Device-side storage | LittleFS only in v0.1, deleted on ack. SD card moves to v0.2 | L13; plaintext history conflicts with S9 | v0.2 |
| D11 | Execution operations while offline | Always saved as queued items; execution only through online requests | Principle 2, S5 | No plan to change |
| D12 | Permission for started sessions | `--permission-mode` passed explicitly, default `plan`; `bypassPermissions` refused | L5 | You confirm before M3 (§16 open item 1) |
| D13 | Communication protocol | NDJSON, `v:0`, line ≤2048 bytes, text ≤500 bytes; 05 §6 is authoritative | Continues the hwtest format; fits HWCDC buffer sizes | When bandwidth is not enough (v0.2 voice) |
| D14 | How modularity is done | A static compile-time module list plus a very small interface: `begin`, `onKey`, `render` | No manifest validation, no auto-generated caps | When the SDK is built in v0.2 |
| D15 | Device pairing | Not in v0.1; rely on trusting the USB connection, plus firmware check and serial-number binding | Pairing without a key is only for show | v0.3 together with wireless |
| D16 | Flash encryption / Secure Boot | Off | One-way operation; once on, the community's flashing tools stop working | No plan to change |
| D17 | Telemetry | None | Privacy | No plan to change |
| D18 | Keys | G0 dedicated to capture; Opt as this app's own layer; only single keys and two-key chords | L9 | After G1 |
| D19 | Replying to interactive sessions | Done with mods in v0.2 (needs CLI ≥2.1.287) | v0.1 does not require a CLI upgrade | When v0.2 work starts |
| D20 | Approval when the device is absent (v0.2) | Make no decision, let the terminal ask as usual | A default deny would interrupt your normal work at the Mac | v0.2 |
| D21 | Where notes are written | `~/CardputerNotes/YYYY-MM-DD.md`, each item with a block ID | Avoids TCC-protected directories | After you confirm the Obsidian path |

---

## 16. Questions for you to decide

Each item below has a default. With no answer, the default is used; items marked ★ need confirming before their milestone.

1. ★ **Which permission mode for sessions started from the device?** Default `plan`, can be changed to `acceptEdits` per project; `auto` only if written explicitly; `bypassPermissions` never. (Confirm before M3)
2. **Toolchain**: accept the risk that pioarduino has a single maintainer? Default: use B if G2 passes, and pin the release package URL.
3. **How feedback is delivered**: do you mainly run sessions in the terminal or in the Claude desktop app? v0.1 by default does not upgrade the CLI and feedback goes into the queue; v0.2 reconsiders upgrading to ≥2.1.287.
4. **Where notes are written**: default `~/CardputerNotes`. To write straight into an Obsidian vault that lives under `~/Documents` or iCloud, uv's Python needs "Full Disk Access".
5. **Exception to the immutability rule**: hardware buffers only allowed in `hal/` and `usb_transport/`, acceptable? Default: accepted.
6. **Inbox rule**: `waiting` enters immediately, `idle` enters after 60 seconds, and nothing is auto-hidden for being idle too long. That is the default.
7. **Default target of a quick note**: follow the current project, or always "unassigned"? Default: follow the current project.
8. **Risk if the device is lost**: items not yet synced are plaintext, acceptable? Default: accepted in v0.1, PIN lock evaluated in v0.2.
9. **Naming**: are `cpa`, `cpad`, `cpa-fw`, `$CPA_HOME=~/Library/Application Support/CardputerAssistant` fine? Default: use these placeholders.
10. **Where sessions start**: directly in the project's main directory, or in a separate git worktree? Default: the main directory.

---

## 17. Review comment log

**Blocking issues, all accepted (15):**
1. Offline start-session and feedback requests no longer go into the outbox; they become online requests with confirmation (S5, US-09, US-10).
2. G3 rewritten to test only "an idle background session whose process is still alive", verifying the ID is unchanged after stop then resume; interactive sessions are never resumed.
3. Add `--`, refuse text starting with `-`, require `sid` to match, and contract-test with malicious text (S3, US-09 AC7).
4. Pass `--permission-mode` explicitly, default `plan` (S4, §16 open item 1).
5. v0.1 installs no hooks and opens no TCP port; `cpa pause` goes through a unix socket instead.
6. Device pairing removed, replaced by firmware check + serial-number binding.
7. Outbox uses LittleFS only, deleted on ack. A `.tmp` with an existing `.json` of the same name next to it is not deleted (05 §5.9).
8. Receive buffer set to 8KB, longest line 2048 bytes, credit-based flow control on the daemon side.
9. Registering a project does not require a git repo.
10. Chinese display raised to P0, G5 closed.
11. The `claude attach` hint is only shown for background sessions.
12. 05 §6 is authoritative for the protocol; `queue.done` and `inbox.dismiss` added.
13. Loop stack changed to 16KB, reducers split by slice, `sizeof(AppState)` assertion added.
14. Toolchain decided by G2 on day 0, written as an ADR.
15. daemon uses only pyserial and the standard library, with jsonl storage.

All acceptance criteria were also rewritten in testable form (CER, fault injection, p95 with its measurement, timing in a new account).

**Scope cuts, all accepted:** pairing, hooks/HTTP, SD card, Leader key, manifest validation, runtime jsonschema, dependencies such as Starlette, SDL screenshot tests, a session pull protocol, offline polling, queue markdown, the 8-hour soak and 100 power cuts, and battery saving reduced to "screen off + wake on key".

**Not accepted or modified (with reasons):**
- **`time.set` keeps `tz_min`.** The status bar shows local time, and this is a single integer field at negligible cost. `device.reboot` has been removed; HIL uses the test-firmware-only `debug.reboot` instead.
- **The ghost filter was not changed to "hold first, decide after release".** The most common case is holding a modifier and tapping a letter, where the real key and the ghost key appear and disappear together, so holding cannot tell them apart either. So "drop + show the count" stays, backstopped by the G1 CER test. No "beep on dropped key" either: v0.1 does not enable the speaker, to keep clear of v0.2's microphone switching problem.
- **Lint in CI is not all pushed to after M2.** `check_sizes`, ruff and lizard are cheap and run from WP1; only clang-tidy waits for WP4.
- **"Mark a queued item done" is no longer written to the outbox offline**; it is online only. This is a simplification (the device's queue data may be stale offline anyway); the review did not raise it.
- **The "quick note module" merged into the capture page shared by the shell.** v0.1 has only three modules: `board`, `inbox`, `system`.

---

## Appendix
- **A. Protocol**: message catalog, examples and limits are all in [05 §6](05-architecture.md). The machine-readable limits table is `protocol/limits.json`.
- **B. Data objects**: project registry, event log, queue, see 05 §7.
- **C. Reuse and borrowing**: tables, version numbers, license policy, see 05 §4.

---

## Hidden keys (easter eggs, v0.5.0)

Not in the hint bar and not in the Tab menu; recorded only here. None of them make a sound, pop a toast or write any file (memory only), and none change the protocol.

| Key | Where it works | What it does |
|---|---|---|
| `Opt+W` | Home; when the lock card is lit and showing the clock | Pat Mando: he jumps, a heart appears over his head, and he says a line (Woof! / Hehe / Pat pat / Again, or the Chinese 汪! / 嘿嘿 / 摸摸 / 再来, at random, never the same twice in a row), with a count beside it, "pats ×N" (摸 ×N in Chinese; when the line is "Pat pat" only "×N" is shown, and nothing when it does not fit), for 1.5 seconds; every 100th pat he says "You did great today" (今天也辛苦了). It does nothing while recording, with the editor or menu open, on other pages, or when the lock card is dark or showing a recording result. |

Two more small easter eggs that need no key (`firmware/app/lib/eggs/mando_eggs.h`):

- **Mood by the hour** (unchanged when the time is unknown): from 23:30 to 5:00 the lock card's sign says "Bedtime" (夜深了), with an extra z, and the Home greeting is "Late night" (夜深了); from 5:00 to 7:00 the sign says "Up early" (早起鸟) and Mando is awake; on Saturday and Sunday the sign says "Weekend" (周末啦) and the Home greeting is "Weekend" (周末好).
- **All clear**: while on Home, when the waiting-for-you sessions go from some to none, Mando jumps and the card shows "All clear" (搞定啦) (the greeting already says "All done"), for 2.5 seconds, once only; it does not cover the "?" over his head left by a recording that was too short.
