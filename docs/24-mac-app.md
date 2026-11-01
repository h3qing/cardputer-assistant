**English** · [中文](24-mac-app.zh-CN.md)

# 24 · Cardputer Assistant for Mac: its own Mac app, and a template for "any app can connect"

> Status: **v1 implemented** (`mac/`; defaults from §10) · Date: 2026-10-22 (revised the same day after three reviews: what the user sees, integration principles, stability; see "Revision log" at the end) · Role: architecture (Mac app) · Based on origin/main `995821d`, branch `feat/mac-app` · Related #34, #54, #61, ADR 0002, ADR 0004, docs/25.
> Requirement (2026-10-22): the goal is not an integration for one particular app; it should make it easy for every app to integrate, and there is still no Mac app to open. An earlier voice memo M1 (see the header of docs/23) asked for: unfiled recordings go into a queue that shows the transcript, which device and when; from there you can file them or kick off a workflow; ideally a small local model pre-sorts them so you only have to approve.
> Basis: docs/15 §1–§3; docs/19 §2.1, §5; docs/22 §4 (`wt-stt`); docs/23 §2–§5, §8; `protocol/filing/v1/README.md` (`wt-filing`, P1-A/B/C committed, not merged); daemon's `cli/main.py`, `cli/rec.py`, `mcp/cli.py`, `archive/cli.py`, `app.py`; `cpa_stt/cli.py` (`wt-stt`); read-only measurements on this machine.
> Marks: **[src]** code or docs read · **[meas]** measured on this machine · **[asm]** design assumption, can change · **👤** needs you.
> Scope: design only. The code is in `mac/` on the same branch and follows this doc.

## 0. One-page summary

1. **What**: a native Mac app "Cardputer Assistant": one window (Inbox, Recordings, Device, Integrations, Settings) plus a menu bar icon, installed in `~/Applications`, opened with a double-click.
2. **It is just a client**: it uses only public interfaces: `cpa … --json`, the open files in the notes folder, `cpa mcp`, `cpa export`, `cpa-stt`. It does not connect to cpad's private socket, does not read `$CPA_HOME`, and has no code specific to WhisperWoof or any app. **Anything it can do, any app can do with the same commands**; WhisperWoof, Codex, Claude and Cursor are on equal footing with it (§1).
3. **The Integrations page** turns that sentence into buttons: first "Destinations" (send what you say to any app; once `cpa dest` exists, connecting an app is one click in a list, docs/25 §5.7), then five "for developers" cards (MCP, command line + JSON, files, transcriber, export), with snippets copied in one click and availability visible at a glance; "Copy the connection guide" packs all this into a piece of Markdown to paste into any app's coding assistant (§2.4).
4. **Inbox** follows the interaction in docs/23 §5: text, which device, which Mac, when; one row of filters (state, date ▾, memo/meeting ▾, project ▾, text); press 1–9 to file in one go (names written on the buttons), give to AI (queue as a to-do / start work), seen, hide, play, ⌘Z undo (the Edit menu says which step it undoes). Until a small local model's suggestions are in the public interface (G3), "the text mentions exactly one project" serves as the suggestion, approved with Enter (§2.1).
5. **Works with parts missing**: without `cpa filing` it falls back to `cpa rec ls --json`'s "recordings without a project" (read-only), and says what to install to unlock more; without `cpa-stt` the local transcription switch is grey (§4).
6. **Light on resources**: while the window or menu is visible, it asks `cpa` every 3 s; when not visible, it only asks `cpa status --json` once a minute (the menu bar dot must be true); file reads are never on the main thread (§5).
7. **How it is built**: SwiftPM (`mac/`), needs only the command line tools, not Xcode; the tests are an executable; every screen is drawn as a PNG with ImageRenderer for review; a script assembles the `.app` and ad-hoc signs it (§7, §8).
8. **16 decisions** all have defaults (§10.1); **3 things need your call**: merge `feat/filing-p1`, whether to "open at login", and whether the WhisperWoof page in docs/23 is still built (§10.2).

## 1. Principle: it is just a client

### 1.1 Who uses which interface

| Public interface | Contract | What this app uses it for | How others use the same thing |
|---|---|---|---|
| `cpa status --json` | Fields only added [src cli/main.py:105-112] | Device page, the menu bar dot | Any script, status bar tool |
| `cpa filing ls/show --json`; `file/done/hide/reopen/hand --json` | `protocol/filing/v1` | Inbox | WhisperWoof, launcher plugins, your scripts |
| `cpa project ls --json` | `projects` in the same | Who is 1–9 | Same |
| `cpa host name --json [-- name]` | `host` in the same (`feat/filing-p1`) | What this Mac is called; naming it | Same |
| `cpa rec ls --json [--hidden]`, `cpa rec unhide` | Row shape [src cli/rec.py] | Recordings page; Inbox fallback | Same |
| Notes folder `recordings/<date>/<time>-<session>/` | Recording contract v1 | Play `seg-NNN.wav`, full transcript, last pulled, where transcription is stuck (`status.json`, `result.json`), `.transcriber` | Obsidian, Finder, grep, transcribers |
| `cpa mcp serve` (stdio, read-only by default) | docs/15 §3 | Not used by itself; the Integrations page shows how to connect: tabs come from `cpa mcp setup --list --json` (G8), otherwise from the clients listed by `cpa mcp setup --help`, plus "Other MCP clients"; `cpa mcp tools/audit --json` shows the state | Any MCP client |
| `cpa export` | docs/15 §2.4 | "Export" on the Integrations page | Backup, migration |
| `cpa-stt status --json`, `use`, `off`, `setup`, `service install`, `retry` | docs/22 §4.11 | The local transcription switch on the Settings page; "Retranscribe failed ones" | Any program that keeps the "transcriber" role of the recording contract |

### 1.2 Hard rules (pinned by the tests in §8.1)

- Does not connect to `cpad.sock`; does not read or write any file in `$CPA_HOME` (`~/Library/Application Support/CardputerAssistant`).
- The notes folder is read-only, and only `manifest.json`, `status.json`, `transcript.json`, `transcript.md`, `result.json`, `summary.md`, `seg-NNN.wav`, `recordings/.transcriber` are opened; the app finds the folder by id itself (the contract has no path; it first computes cpad's `<date>/<HHMM>-<id>` from the start time, then looks at that day and the days before and after, and only then scans everything); the realpath must be inside the root, `O_NOFOLLOW`, size limited.
- There is only one kind of write: running the one `cpa` / `cpa-stt` command you clicked. No background writes, no unconfirmed batch writes.
- External commands only via `Process`, with an argument array, never through a shell; the `cpa` path is resolved explicitly, with a timeout (§5.2).
- No network, no open ports, no serial port, no starting or stopping cpad.
- No app-specific code: no app's name appears in the source, comments or tests of `mac/` (`RulesTests` checks case-insensitively); the client tabs and snippets on the Integrations page all come from `cpa mcp setup`, and tab names are generated from ids.

### 1.3 What is missing goes into the public interface

When the app needs something the public interface does not have, **add it to `cpa … --json`** so all apps benefit; do not work around it in the app. After review there are ten:

| # | Gap | Fix (in cpa) | What v1 does for now |
|---|---|---|---|
| G1 | `cpa status --json` has no notes folder | Add `notes_dir` (absolute path) | In order: chosen in Settings → `notes_dir` → parent of `recordings` from `cpa-stt status --json` → `~/CardputerNotes` |
| G2 | This Mac's name | **Can already be read**: `cpa host name --json` (`feat/filing-p1`, contract `host` document); `host_label` in `cpa status --json` is optional | The app reads with `cpa host name --json` and names with `cpa host name --json -- name` (1–24 bytes); grey on an old cpa |
| G3 | Suggester (a small local model roughly sorts by existing projects) | Add optional `suggest{project, why}` to rows; contract additions only | When the text mentions exactly one project (name or key), use it as the suggestion, labelled "mentioned in the text", Enter to approve |
| G4 | Cannot list only "projects of another computer": `--orphans` only adds rows, and new rows get pushed out beyond 200 | `cpa filing ls --orphans-only` or `--asked other` | After `--orphans`, keep the `asked` rows in the app |
| G5 | No "how many today" | `counts.today` in `filing ls` | The top line does not say today |
| G6 | Errors outside filing cannot be read: `cpa export`, `rec unhide`, `cpa-stt use/off/setup/service install` only give an exit code | Give them `--json` error documents (`E_EXISTS`, `E_CLOUD`, `E_INSIDE`, `E_PARENT`, `model_missing`…); keep exit code 2 for usage errors only | Say which program failed, and for common commands add the most likely cause (export: the target must be new and not inside an iCloud-synced folder or the notes folder; `cpa-stt use`: most likely a missing model) |
| G7 | It cannot say what this cpa supports; every integrator has to probe with "exit code 2" | Add `contracts: {filing:1, recording:1, host:1}` and a command table to `cpa status --json` (or `cpa --version --json`) | Probing: only while probing is exit code 2 read as "no such command"; everywhere else it is an error |
| G8 | `cpa mcp setup` mixes instructions and config, and has no generic client | `cpa mcp setup CLIENT --json` gives `{where, format, body}`; a `generic` client gives `{command, args, env}`; `cpa mcp setup --list --json` | The first line is "where to paste", the rest is the body, and only the body is copied; "Other MCP clients" is generated by the app in the generic stdio shape |
| G9 | Before the first `cpa-stt use`, it cannot say whether the model is linked | Add `models` or `ready` to `cpa-stt status --json`; `cpa-stt use --json` gives `model_missing` / `selftest` | Show "Choose model file…" when never used (`selftest_ok` empty) or `missing` is non-empty; also after `use` fails |
| G10 (low) | `cpa rec ls --json`, `cpa status --json` have no `v` and schema | Add a version like filing/v1 | Read leniently; fields only added |

## 2. Window

Sidebar + list + detail, minimum 960×600. The app draws the sidebar itself (solid Sheet colour) instead of using `NavigationSplitView`: on macOS 26 that turns the page's scroll area into the scroll edge of a glass toolbar, the page gets pushed under the title bar, and it cannot be drawn off-screen either; a self-laid-out sidebar is the same every time, and the screenshots draw exactly this real window (§8.2). Opening the app shows Inbox, with the first "new" item selected and focus on the list.

| Screen | Data | Main actions | Screenshot states (§8.2) |
|---|---|---|---|
| Inbox | `cpa filing ls/show --json`, `cpa project ls --json`, WAV | 1–9 file, give to AI, seen, hide, play, undo | With text, no text, another computer's project, hint bar, all filed, lite version |
| Recordings | `cpa rec ls --json`, open files | Play, show in Finder, unhide | List, detail |
| Projects (2026-10-26) | `cpa project list --json`; changes via `cpa project add/rename/move/folder/rm --json` | New, rename, change number (number menu or drag ≡), link folder, delete | With linked folders and plain groups, renaming and awaiting delete confirmation, cpa too old |
| Device | `cpa status --json`, `cpa project ls --json`, manifest | Copy command | Connected, not connected, cpad not running |
| Integrations | `cpa mcp setup/tools/audit`, `cpa status --json`, `.transcriber` | Copy snippet, copy connection guide, export | All available, command line card, missing `cpa filing`, missing `cpa-stt`, each MCP tab |
| Settings | `cpa-stt status --json`, `cpa --version` | Local transcription switch, computer name, choose folders | On, off, not installed |
| Menu bar popover | The latest refresh result | Open window, jump to an item | With inbox items, all filed, cpad not running |
| cpa not found | — | Copy install command, look again | One |

### 2.1 Inbox

- **Top line**: "12 to file · 1 of them for another computer's project · 2 segments still on the device" (`counts.new + counts.other_mac`, the same number as the sidebar, the menu bar and the "Inbox" button). "How many today" waits for G5.
- **Filters, one row, no typing**: state is a segmented control (To file · Given to AI · Seen · All); "Other computer N" only appears when there are some, and "No text" only appears when the latest list has a failed transcription; date ▾, memo/meeting ▾, project ▾ are menus (the 1–9 buttons are in the detail); a search box (`/` to enter; the words are passed via stdin `--text -`). All of these map to `cpa filing ls` arguments; the app does not recompute filing itself.
- **A row**: title or first sentence (within two lines); with no text it says "Memo at 13:25" (the state beside it says why); "Memo · 24s"; relative and absolute time (with ~ when uncertain); which Mac pulled it ("This Mac" when `env.host` equals this Mac's name, "Unnamed Mac" when there is no name); device (short id's last 4 characters, shown only with two or more devices); transcription state (icon plus text plus colour).
- **Detail**: full text; a capsule "Office pulled · connected while recording / not connected while recording"; transcript by sentence (`[mm:ss]`, click a sentence to play from there); a play bar; **a row of 1–9 buttons with number and name**, current project highlighted, suggested project outlined; "Give to AI · queue as to-do" (q), "Give to AI · start work (press Enter on the device)" (s), "Seen", "Hide…" (needs confirmation), "Show in Finder". After giving to AI it shows the queued text read back by qid ("Queue no. q12"). When transcription failed or cannot run, it says why (`reason` in `status.json`, `error.code` in `result.json`), and with `cpa-stt` installed offers "Retranscribe failed ones" (`cpa-stt retry`).
- **Suggestion card**: until rows carry `suggest` (G3), when a new memo without a project mentions exactly one project in its text (the name, or the key as a whole word), the top of the detail shows "Suggested: 3 Thesis (mentioned in the text) · press Enter to approve", with the button outlined; approve = `cpa filing file ID KEY`, no separate write path. No suggestion when two are mentioned, for one-character names, or for another computer's projects.
- **Keys** (only with focus on the list; the same set as docs/23 §5.2): ↑↓ / `j` `k` move; `1`–`9` file and jump to the next "new" item; `0` back to no project; Space play / stop; Enter approve the suggestion; `q` queue as to-do; `s` start work; `x` seen; `/` search; ⌘Z undo, repeatable. Two hint lines under the list list these keys.
- **Feedback**: a 5-second toast "Filed to 3 acmeshop · ⌘Z to undo"; failures are explained in place ("cpad is older than cpa: run `cpa service install` in Terminal"), no jumping; write commands time out after 30 s, and a timeout says "Not sure whether it went through; refreshed" and rereads the list. Undo uses the `undo` object in the result; the app rebuilds the command with its own verb table and re-checks the id, instead of copying it; each step is registered with the window's undo manager, the Edit menu says "Undo "File to 3 Thesis"", and is grey when nothing can be undone; ⇧⌘Z and undo in text fields work as usual.
- **Lite version** (no `cpa filing`): only listen and show in Finder; the file, give-to-AI and seen buttons do not appear, and one line says "Filing, handing to AI and Done need cpa filing".
- **Guarding against mistakes**: for another computer's project, press Enter after the number (docs/23 D15), and the command gets `--other`; "Give to AI" is grey for meetings, saying "Meetings contain other people's words and cannot be given" (D16); `q` and `s` are grey on rows without text, saying "Not transcribed yet: file it, or wait for the transcript".
- **Empty**: Mando and "All filed".

### 2.2 Recordings

`cpa rec ls --json`: each row's title is text (the title in `result.json` or the full memo text, otherwise the first sentences of the transcript; each recording is read once per state); with no text it says "Memo at 13:25"; below it: memo / meeting, duration, time, project, ★, state. Top: "23 recordings · 2 segments still on the device" (segment count from `cpa status --json`, same as the Device page). A switch "Show hidden" (`--hidden`); hidden rows have "Unhide" (`cpa rec unhide`). The detail reads open files: transcript by sentence, `summary.md` (links shown as text, images not loaded), why there is no text, play, show in Finder.

### 2.3 Device

- **One line in large type**: "Device connected · firmware v0.4.1" / "Device not connected" / "cpad not running". `link` is translated: connected, handshaking, not connected, paused N s.
- **Numbers**: N segments still on the device (`recordings.on_device`; when not connected, "last seen on the device"), N waiting to send (`device.pending`), N segments failed to transcribe (with "Retranscribe failed ones" when there are any), last pulled (`written_at` of the newest manifest in the newest day), last transcribed (`recordings.last_result_at`), memory card, cpad version.
- **1–9 table**: all nine numbers are listed; ones without a project say "empty"; number, name, key, folder (linked ones show the folder name, `ok:false` says "folder is gone", unlinked says "no folder linked"). From `cpa project ls --json`; without it, the keys from `cpa status --json`. Projects are changed on the "Projects" page (§2.6).
- **Which Mac, which network**: this Mac's name (`cpa host name --json`); each recording's `env.host`, `env.link` are in Inbox. No network is recorded: a Wi-Fi name counts as location history, and the contract deliberately does not store it [src docs/23 §4.1].
- `warnings` listed as is (through display_safe). **`device.dev` is not shown**: it is the device's factory MAC address [src app.py:59-69; docs/23 §3.2].

### 2.4 Integrations

Opening line: "Cardputer's data is open to every app; this app itself only uses the interfaces below." Then two parts:

**Destinations: send what you say to any app** (docs/25 §5.7). This is what "every app can connect" means for you: press a letter while speaking, and the words land in that app's input box; it never presses send for you. Once `cpa dest` (docs/25 R1) exists, this card lists the apps on this Mac that can connect (`cpa dest presets --installed --json`), each with "Add" and "Send a test", and a last line "Other app…"; behind it is all `cpa dest …`. Before that, the card says "Needs cpa dest first (R1)" and lists these three steps in grey.

What R1-E (2026-10-22) built; this is authoritative:
- **Probe**: `cpa dest` counts as present only when `cpa dest pending --json` answers (not exit code 2); before that everything stays as it was.
- **Integrations → Destinations**: destinations already added (⌥letter, kind, "leaves this Mac / stays on this Mac", a one-line state, "Send a test" for url and app), each project's usual destination (one row per project, one button per destination; one click is `cpa dest usual project destination`, clicking again or "None" is `-`), and those that can connect on this Mac ("Installed 0.75.0 · letter X", "Add"). "Add" first expands a confirmation in the card (the cpa preset's own description, whether it leaves this Mac, "send a test after adding"), and only after confirming runs `cpa dest add --preset P --yes`.
- **One-time test**, asked in place, no popup: `cpa dest test ID --send --json` → "Did "Test NNNN" appear in <app>?" (Appeared / Did not appear) → "Was it sent automatically?" (No, only filled in → `verify --draft`; it was sent → `verify --act`); when a link destination did not appear, you are asked to paste the bot's link or id, `cpa dest map ID '*=link'`, and test again.
- **Inbox**: above the list is "Waiting for you to send N" (`cpa dest pending --json`); ↑↓ walks it and the inbox as one list; with an item selected, its detail is on the right: ⌘↩ sends to the preselected one (`cpa dest send REF --by app`), ⌥D don't send, and below "or send to…". A memo's detail gets an extra "Send to…" under "Next": one button per destination, links first (the project's usual destination first), with ⌥letter on the button; ⌥ plus the letter is the same as clicking the button (`cpa filing send ID destination --by app`). Meetings, items without text, and the read-only list do not have it, or have it greyed; destinations that might send on their own (`fl` contains `k`, e.g. not yet tested) are grey, and clicking says "Send a test first"; it never confirms for the device on the Mac. The 5-second toast after sending uses the `sent` that cpa prints: "Filled into <app> · press Return in <app> to send", "On the clipboard · press ⌘V in <app> (<app> still has the previous draft)". When an item waiting to be sent is sent or dropped, focus moves to the next.
- **Not done yet**: "Other app…" (`--apps`), changing the letter on the card (today the preset's letter is used; cpa does not bind it when it is taken), `cpa dest add` has no `--json` (the app only looks at the exit code and one line of text).

**For developers: read Cardputer's data**, five cards, each with one sentence, one state (available / install X first) and a copyable snippet; "Copy the connection guide" at the top right packs the following into a piece of Markdown (cpa's absolute path, each command and whether it works today, the rules, where the contracts are, the generic MCP setup, the notes folder layout) to paste into any app's coding assistant, which then connects by it.

1. **AI clients (MCP)**: tabs come from cpa: `cpa mcp setup --list --json` (G8), and when this cpa lacks it, the clients listed in `cpa mcp setup --help`; tab names are generated from ids. Each tab shows "Paste into: …" (first line of `cpa mcp setup <client>`), and the body below is what gets copied; JSON ones say "If mcpServers already exists, merge in only the cardputer entry". The last tab, "Other MCP clients": transport stdio, command `<absolute path of cpa>`, args `mcp serve`, `CPA_HOME` (only if set), and a generic `mcpServers` JSON. Below are the tools from `cpa mcp tools --json` (read / write) and the latest calls from `cpa mcp audit --json --last 20` (which client, which tool, when; no body; `E_DISABLED` says "Refused: writes are off"). The app never changes another app's config.
2. **Apps with a UI (command line + JSON)**: first where cpa is (an app opened from Finder has no `~/.local/bin` in its PATH, so copied commands all use absolute paths), then seven commands, each saying whether it works: `status --json`, `rec ls --json`, `rec show SID --json` work today; `filing ls/show/file --json`, `project ls --json` say "after installing cpa filing". Rules: argument array, no shell, timeout, body via stdin, reject the whole document if `v` is not 1, ignore unknown fields. The contract is at `protocol/filing/v1/README.md` in the repo. This app is the example.
3. **Files**: where the notes folder is ("Show in Finder"), the layout (`manifest.json`, `seg-NNN.wav`, `transcript.json`, `transcript.md`, `result.json` (`memo.text` is the full memo text), `summary.md`, `recordings/.transcriber`), contract: `protocol/recording/v1/README.md` in the repo. Notes apps and grep can read it without any code.
4. **Transcriber**: who holds the claim on `recordings/`: read the claim file `recordings/.transcriber` (`transcriber` in `cpa status --json` is the same information): cpa-stt / another program / none / claim expired. Any local transcriber that keeps the recording contract's "transcriber" role can connect (same contract, transcriber section); `cpa-stt` can run any engine that follows the conventions in docs/22 §4.3.
5. **Export**: "Export to…" (folder or zip, audio optional) = `cpa export DEST [--zip] [--no-audio]`, with the command written under the button; the target must be new, and not inside an iCloud-synced folder or the notes folder.

### 2.5 Settings

- **Local transcription**: one switch. On = `cpa-stt use`, and after the self-test passes, `cpa-stt service install`; with a missing model (`missing` non-empty or `blocked:"model_missing"`), never turned on (no config yet, G9), or when the last `use` failed, "Choose model file…" appears (system dialog) → `cpa-stt setup path` → turn on again. Off = `cpa-stt off`. Below it shows `cpa-stt status --json`: command, sandbox, self-test, the `blocked` reason, timings of the last few segments. When another program holds the claim on `recordings/`, the switch is grey, saying "WhisperWoof is transcribing; only one of the two can be on" (docs/22 §4.10). Off by default, same as `cpa-stt`.
- **This Mac's name**: two buttons, Home / Office (家 / 公司) → `cpa host name --json -- name`, reading back the `host` document; names are 1–24 bytes (8 Chinese characters). Grey on an old cpa (without `cpa host name --json`), saying "Comes with cpa filing, not merged into main yet".
- **cpad service**: running or not, version; when not running or too old, a copyable `cpa service install`; the app does not start or stop it (D8).
- **cpa location**, **notes folder**: show the resolved paths, with "Look again" or choose by hand; the chosen one comes first (§5.2), so you can try a branch's `cpa` first.

### 2.6 Projects (2026-10-26, docs/25 D24)

A project is a **named group** created on this Mac: while recording, press 1–9 on the device and the voice goes to that project, first staying in this app's "Inbox", from where you send it by hand to some agent or app (docs/25 D23). A code folder may or may not be linked: only linked ones get hooks, project recognition by cwd, and starting sessions from the device.

- **New**: type a name (Chinese works too, at most 24 letters or 8 Chinese characters); "New" or Return puts it in the first empty number; with a name typed, empty rows show "Put here", which puts it at that number → `cpa project add --json [--slot N] -- name`. With no name typed, an empty row says "Drag a project here, or type a name above first" (no greyed-out button). The key is derived by cpa from the name (a Chinese name gets `p<number>`; keys used by deleted projects are never given out again). With all 9 numbers taken, "New" is grey.
- **1–9**: nine rows, same as the device. Each row: ≡ (drag to another row to swap numbers), number (click for a 1–9 menu, pick one to move there, and the project on that number moves here; two clicks, `cpa project move`), name (double-click to rename; hover to see the key. The key does not get its own column: a Chinese name's `p3` stays `p3` after changing number, which looks like a mistake), folder (linked shows the folder name, green; gone, yellow; not linked, grey), rename (type in place, `cpa project rename --json KEY -- new name`), Link folder… (system dialog to choose a folder → `cpa project folder --json KEY path`) or Unlink folder (`--none`), delete (the first click turns into "Delete 'name'? Recordings are kept · Delete · Cancel", only the second click runs `cpa project rm`, and the project's usual destination is forgotten with it).
- Each command's answer is the `projects` document after the change, drawn right away; the 1–9 buttons in "Inbox", the help text and the Device page all use the same names. When refused, it says why (number taken, key taken, folder gone, name not allowed).
- On an old cpa (without `cpa project list`) the whole page is read-only: the row buttons, ≡ and the number menu do not appear, and the top says how to update, with the same command as in Settings and Inbox (`Setup.updateCpa`, now `uv tool install --force ./daemon`; once the install script from docs/27 is merged, only this one place changes).
- Screenshots: `ImageRenderer` cannot draw drag-and-drop or text fields (they come out as yellow blocks), so in snapshots (`isSnapshot`) drag-and-drop is not attached, the number is not a menu, and text fields are drawn as the same box with text; the real window works as usual.
- The name is what you typed: it goes last in argv, after `--`, and is checked first like `cpa host name` (1–24 bytes, not starting with `-`, no control or direction characters); paths must be absolute.

### 2.7 Language (2026-10-26)

- The UI is in **Chinese** and **English**. By default it follows the system: if the first preferred system language is Chinese (zh-Hans, zh-Hant…) it uses Chinese, otherwise English; in Settings, "语言 · Language" lets you choose Follow the Mac (the button says which one it is now: "跟随 Mac（现在：中文）", "Follow the Mac (English now)") / 中文 / English, stored in this app's own defaults; the window and menu bar switch immediately.
- Every UI string in the code is written as `tr("中文 {0}", value)`: the Chinese itself is both the key and the Chinese table; there is one English table (`CardputerCore/Localization/EnglishTable*.swift`, split into two files to keep each under 400 lines). `{0}`, `{1}` are values substituted in order.
- `L10nTests`: every `tr()` has English, the placeholders match on both sides, there is no Chinese in the English; there are no Chinese literals in the source that bypass `tr`. Recording text, project names and this Mac's name are your content and are not translated.
- The English must read like English (review 2026-10-26): sentences with numbers are one whole sentence with placeholders ("{0} still on the device"), not two pieces glued together; name length says "24 letters (8 Chinese characters)", not bytes; older dates are written "Sep 16", "Aug 12, 2025", not 9/16; "1 segment / 3 segments". `EnglishTests` pins these.
- Screenshots are drawn in both languages: English in `out/shots/`, Chinese in `out/shots/zh/` (`--lang en|zh` draws just one).

## 3. Menu bar

- **Icon**: Mando's head (in its own colours, readable on both light and dark menu bars, not a template image) plus a dot: green = device connected; grey = not connected; red = cpad not running or `cpa` error; grey ring = uncertain (`cpa status` has not answered for over 90 s, or errored). A number beside it when there are inbox items. Not colour alone: the first line of the popover says it in words. Even when not visible it asks `cpa status --json` once a minute, so the dot never stays green by mistake.
- **Open**: a status line ("Device connected · 2 segments still on the device", no firmware version); the 5 latest inbox items (first sentence + time), click one to open the window with it selected; "Open Cardputer Assistant"; "Quit". The lite version has no list.
- After the window is closed the app stays only in the menu bar and leaves the Dock; it comes back to the Dock when the window opens.

## 4. Capability detection and fallbacks

Probe once at launch, when opening the window and when clicking "Check again", then every 60 s; when `cpa` was not found, look again every time. The method is "run it and look at the exit code": argparse exits 2 on an unknown subcommand. **Only probing reads exit code 2 this way**: `cpa` and `cpa-stt` also exit 2 on bad values, config or file errors, so exit code 2 anywhere else is always "something went wrong", naming which program (G6, G7).

| Capability | How it is tested | Without it | How to unlock |
|---|---|---|---|
| `cpa` | The lookup in §5.2 | The whole window is one "cpa not found" page, with a copyable install command and "Look again" | `uv tool install ./daemon` in the repo |
| cpad running | `running` in `cpa status --json` | Red dot; Inbox works as usual (`cpa filing` does not need cpad), the Device page says "cpad not running" | `cpa service install` |
| `cpa filing` | `cpa filing ls --json --limit 1` exits 0 with `v:1` | **Lite version "recordings without a project"**: rows in `cpa rec ls --json` without a project and not hidden; text read from open files (`memo.text` in `result.json`, otherwise the first sentences of `transcript.json`, through display_safe, ≤280 characters); listen and view only, action buttons grey, saying "Install `cpa filing` (docs/23 P1) to file" | Reinstall `cpa` after docs/23 P1 is merged |
| `cpa project ls --json` | Exit code | Use the keys from `cpa status --json` as names | Same |
| A new cpad | `hand` returns `E_UNAVAILABLE` (`field:"cpad"`) | Hint "Restart cpad first" | `cpa service install` |
| `cpa project list` (2026-10-26) | `cpa project list --json` is a `projects` document | The Projects page is read-only and says how to update | Reinstall after this branch is merged |
| `cpa host name` | `cpa host name --json` is a `host` document | Grey in Settings; "This Mac" on rows only goes by the name | Reinstall after `feat/filing-p1` is merged |
| `cpa-stt` | Same lookup as `cpa`, then `cpa-stt status --json` | The switch is grey, saying "Install cpa-stt (docs/22 VC-1) for local transcription" | Reinstall after `wt-stt` is merged |

- The lite version is not called "Inbox", and the sidebar has no number: the filing decision is not redone in the app (docs/23 §5.3).
- On this machine today [meas]: `~/.local/bin/cpa` is a link into the uv tools folder; `cpa-stt` is not installed; the installed `cpa` has no `filing` yet. So opening the app before the merge shows the lite version.

## 5. Refreshing data and running `cpa`

### 5.1 Refresh

- **Visible** (window not covered, or the popover open): every 3 s `cpa status --json` plus the current page's list; `show` when a row is selected; the project table every 60 s; `cpa-stt status --json` every 10 s while the Settings page is open.
- **Not visible**: only `cpa status --json` once a minute (about 0.08 s each; the menu bar dot must be true). In addition FSEvents (file-level events) watch `<notes folder>/recordings` (2 s debounce, read-only; never watches `$CPA_HOME`; dot files do not count, so the transcriber's per-minute heartbeat does not wake it): on a change it runs `cpa filing ls --json --limit 1` once to update the menu bar number. The app reads all of this once at launch, even with no window open. It refreshes immediately on wake and when the window comes to the front.
- Only one call of each kind at a time; if the previous one has not returned, this round is skipped, not queued. On failure the last data is kept, with "N seconds ago"; repeated failures back off to every 15 s. If the result is the same as last time nothing is replaced, so the page is not redrawn every 3 s.
- Whenever a filter changes or an action is taken, the list's "generation" goes up by one; reads sent before and returned after are dropped, so an old filter or a just-filed item never comes back.
- Reads of the notes folder (detail, transcript, `.transcriber`, last pulled) are never on the main thread; last pulled only looks at the newest day, every 60 s while visible. The lite version's text is remembered by (id, state, result), and only recordings that changed are reread; the lite version also respects `limit`; the menu does not read the list in the lite version.
- Playback: each segment's length is computed from the file size (16 kHz mono 16-bit, minus the 44-byte header); only the segment being played is read into memory, and the next one is read when it finishes.
- After an action completes, the list refreshes immediately instead of waiting for the next round.

### 5.2 Running `cpa`

- **Lookup**: chosen in Settings → `~/.local/bin/cpa` → in the process `PATH` → `/opt/homebrew/bin`, `/usr/local/bin`. The chosen one comes first, and if it fails the checks the lookup goes on. Take the realpath; it must belong to you or root and not be writable by group or others. An app opened from Finder has only system folders in `PATH`, so `~/.local/bin` comes before `PATH`. Same for `cpa-stt`. A failure to start (e.g. its Python environment was deleted) counts as "not found".
- **`Process`**: argument array; environment allowlist `HOME`, `USER`, `LOGNAME`, `LANG`, `TMPDIR`, `CPA_HOME` (only if set), `PATH` = the folder `cpa` is in + `/usr/bin:/bin:/usr/sbin:/sbin` (`cpa mcp setup` relies on it to fill in real paths); stdin closed by default, search words via stdin; timeout 5 s (filing writes 30 s, `export` 10 min, `cpa-stt use` 2 min), on expiry SIGTERM to its whole process group, SIGKILL 1 s later; the timeout is hard: after the program exits, output is waited for at most 1 s more (child processes it left behind may still hold the pipe), then whatever was received is used; it only counts as a timeout if the program did not exit. stdout is read up to 4 MiB (`cpa rec ls` has no limit option: 32 MiB); each output is parsed once; stderr is not shown and not logged.
- **Arguments checked first**: ids are MonoIds; keys match `^[a-z0-9][a-z0-9-]{0,11}$`; enums are in the table; names go after `--`, and a leading `-` is refused.
- **Output**: parsed with `Codable`; the whole document is refused if `v` is not the integer 1; unknown fields are ignored, unknown enum values show as "Unknown" (the contract's `x-knownValues`); text for people goes through display_safe again (C0, DEL, C1, bidi controls, U+2028/9).
- **Logs**: only the subcommand name, duration, exit code and error code; no argv, no text of any kind.

## 6. Look

- **Dark**, tokens per docs/19 §2.1 (from WhisperWoof's dark theme): background `Bg` #120e0b, card `Card` #221c18, selection `Select` #463525, body text `Fg` #f3ede6, second level `Ink2` #bfb2a6, secondary `Dim` #96897b, accent `Accent` #e2a867 (the only accent: primary button, focus, current project), `Ok` #72c291, `Warn` #dcc15a, `Error` #f4675b. Tests compare each colour value in `Theme.swift` against `tools/theme/tokens.py`.
- **Type and spacing**: system fonts (SF, PingFang), body 13 pt, titles 17 pt; 8 pt grid, card padding 16, corner radius 10; sidebar and content are both solid (sidebar in Sheet colour, D15).
- **Mando**: `mac/scripts/export_mando.py` (Python standard library only) reads the committed `firmware/app/lib/theme/gen/mando64_gen.h` (36×64, 4-bit) and `tools/theme/mando_colours.json`, scales up nearest-neighbour to PNG, and `iconutil` makes `AppIcon.icns`; empty states use the same frames. It does not read WhisperWoof; source and license are in NOTICE (docs/19 §5.3).
- **Copy**: short, Chinese, says the result ("Filed to 3 acmeshop" (已归到 3 acmeshop)); every state is icon plus text plus colour.

## 7. Packaging

```
mac/
├── Package.swift            # swift-tools-version 5.9，platforms macOS 14
├── Sources/
│   ├── CardputerCore/       # 纯逻辑：模型、解析、筛选→argv、撤销、能力检测、display_safe、刷新调度、Runner 协议
│   ├── CardputerUI/         # SwiftUI 各屏、Theme、文案表
│   ├── CardputerAssistant/  # 可执行：App、窗口、MenuBarExtra、真正的 ProcessRunner
│   ├── CardputerCoreTests/  # 可执行：自带断言的测试程序
│   └── CardputerShots/      # 可执行：用假数据把每一屏画成 PNG
├── Fixtures/                # filing/v1 向量的副本（带 SOURCE）、假的 status / rec ls / cpa-stt 输出，没有真文字
└── scripts/
    ├── build-app.sh         # swift build -c release → build/Cardputer Assistant.app，ad-hoc 签名并校验
    ├── install-app.sh       # 换进 ~/Applications（先放临时名再改名），不要 sudo
    └── export_mando.py      # sprite → PNG → AppIcon.icns
```

(Tree comments, in order: `CardputerCore` pure logic: models, parsing, filters → argv, undo, capability detection, display_safe, refresh scheduling, the Runner protocol; `CardputerUI` SwiftUI screens, Theme, copy table; `CardputerAssistant` executable: App, window, MenuBarExtra, the real ProcessRunner; `CardputerCoreTests` executable: a test program with built-in assertions; `CardputerShots` executable: draws each screen as PNG from fake data; `Fixtures/` copies of the filing/v1 vectors (with SOURCE), fake status / rec ls / cpa-stt output, no real text; `build-app.sh` swift build -c release → build/Cardputer Assistant.app, ad-hoc signed and verified; `install-app.sh` swaps into ~/Applications (temporary name first, then rename), no sudo.)

- **`.app`**: `Contents/Info.plist` (`CFBundleIdentifier` `com.cardputer-assistant.mac`, `LSMinimumSystemVersion` 14.0, `CFBundleIconFile`, version), `Contents/MacOS/CardputerAssistant`, `Contents/Resources/AppIcon.icns`; `codesign --force --sign -`, then `codesign --verify --strict`.
- **No App Sandbox**: the sandbox passes on to child processes, so `cpa` could not write `$CPA_HOME`, and `cpa-stt` could not install a LaunchAgent. No-network is pinned by the source-scan test (§8.1).
- Built and installed on this machine, with no quarantine attribute, so Gatekeeper does not block it; not notarized, not distributed (D12).
- CI's `mac` job (macOS runner) runs only when `mac/` or repo files it reads (`tools/theme/`, the firmware sprites, `ci.yml`) change: `swift build`, the test program, screen drawing (PNGs uploaded as an artifact), `build-app.sh`; it does not install [src .github/workflows/ci.yml]. The version comes from the repo's latest `v*` tag.

## 8. Tests

### 8.1 Core (failing tests first: RED → GREEN → REFACTOR)

`swift run CardputerCoreTests` prints each failed assertion and where; any failure gives a non-zero exit code.

- **Contract**: all valid filing/v1 vectors in `Fixtures/` parse; a wrong `v`, an unknown `filing`, `handed` without `queue` are refused; rows with unknown fields and enums still display as "Unknown".
- **argv**: each action only produces ids, keys and enums; invalid ones are refused before running the command; search words only go through stdin; `--other` is only added for another computer's projects.
- **Undo**: `undo` object → argv, with its own verb table; unknown `op` is refused; with `undo:null`, ⌘Z says "This step cannot be undone".
- **Filters and selection**: each button combination maps to fixed `cpa filing ls` arguments; after an action the next "new" item is selected.
- **Fallback**: rows of `cpa rec ls --json` → the lite list; reading open files refuses escaping the root, symlinks and oversized files.
- **Capability detection**: a fake Runner giving exit code 2, timeout, bad JSON, `v:2` each yields the right "missing / error".
- **Lookup and run**: a fake `cpa` in a temp folder (order, realpath, group-writable refused); a fake executable run directly with an argument array, testing timeout kill, 4 MiB truncation, the environment allowlist.
- **Refresh scheduling**: every 3 s when visible, zero when not; calls of the same kind do not overlap; backoff on failure.
- **display_safe**, **Chinese times** ("3 分钟前", "~08:10"), **the menu bar dot** (derived from status).
- **Rule scan**: no `URLSession`, `import Network`, `NWConnection`, `cpad.sock`, `Application Support/CardputerAssistant`, `/bin/sh`, `/bin/zsh` in `Sources/`; no app names, case-insensitively (docs/25 §2.1); processes are only started in `ProcessRunner.swift`; each file is at most 400 lines.
- **Tokens**: colour values in `Theme.swift` equal `tools/theme/tokens.py`.

### 8.2 UI: drawn as PNG for review

`swift run CardputerShots --out DIR` uses the fake data in `Fixtures/` to draw every state of every screen in the §2 table, plus an overview sheet; open them and look at each one:

- **Real window**: `MainWindow` goes into an off-screen `NSWindow` (title bar, sidebar and pages are the same ones as in the app), `cacheDisplay` reads back the pixels, 1200×800, no screen recording permission needed, nothing appears on screen.
- **Whole page**: each page is drawn again at its own height (`NAME-full.png`, `ImageRenderer`), so cards cut off at the bottom of the window can be seen too; one per MCP tab on the Integrations page.
- **Menu bar**: `MenuBarLabel` (Mando, dot, number) in one row each on light and dark menu bars, with all four dots; the popover drawn separately.
- **`--live`**: the same screens drawn with this Mac's real `cpa`, `cpa-stt` and notes folder (read-only: commands go through a read-only allowlist first, and write commands are never started); the images contain real text, stay on this machine and are not committed.

The tests only check that every image was produced and is not a single solid colour; whether it is usable and looks good is judged by people.

### 8.3 Local end-to-end

Launch the app with a temporary `CPA_HOME` and notes folder (with a few fake recordings), click through 1–9, `x`, ⌘Z, then check with `cpa filing ls --json`. The real `~/CardputerNotes` is opened read-only once for a look; during development no write action is clicked on real data.

## 9. Phases

| Phase | Content | Depends on |
|---|---|---|
| **v1 (now)** | All of §2–§8, Chinese | Viewing: works today (lite version); filing actions: docs/23 P1 merged; local transcription: `wt-stt` merged |
| v1.1 suggestions | Suggestion card and "approve"; "file all as suggested" (list them first, confirm once); open at login | G3: the suggester in the public interface |
| v1.2 English | English in the copy table, following the system language; light theme | — |
| Later | Network stamp (docs/23 §4.3), filter by profile (docs/18 v0.8), voice capture rows (#52) | Their own designs |

## 10. Decisions and things you need to call

### 10.1 Decisions

Without a reply, the defaults apply.

| # | Decision | Default (recommended) | Alternative | Why |
|---|---|---|---|---|
| D1 | The app's identity | Its own native app, an ordinary client of the public interfaces | A page inside WhisperWoof (docs/23 §5) | What you want is "every app can connect"; an own app that uses only public interfaces is the proof |
| D2 | Technology | SwiftUI + SwiftPM, command line tools only | Electron / local web page | Native, light on memory, no open port; builds on this Mac without Xcode |
| D3 | Read/write boundary | Read `cpa … --json` and the open files in the notes folder; write only by running the `cpa` command you click | Connect to `cpad.sock` | The socket is cpad's own; if the app used it, other apps could not follow |
| D4 | What is missing | Add it to `cpa … --json` (G1–G3); the app uses public fallbacks first | The app reads `config.json` and guesses | Add once, every app has it |
| D5 | Refresh | Every 3 s when visible; no polling when not, only FSEvents to update the menu bar number | Poll every 30 s all the time | Saves power, does not disturb cpad |
| D6 | No `cpa filing` | Lite version "recordings without a project", read-only, says how to unlock | Show nothing | Usable today; filing decisions are not redone in the app |
| D7 | Where a session starts after giving to AI | Only queued; press Enter on the device board, always plan (docs/23 D7) | Start in the app | ADR 0002: only Enter on the device proves a person is there |
| D8 | cpad service | Only show the state and a copyable `cpa service install`; the app does not start or stop cpad | A "Restart cpad" button | A restart interrupts a recording being pulled; rarely needed |
| D9 | Local transcription | One switch in Settings, backed by `cpa-stt use/off/setup/service install`; off by default | The app runs whisper itself | ADR 0004; only one transcriber at a time |
| D10 | Integrations page | One card per interface, snippets from `cpa mcp setup`, copied in one click; never changes another app's config | Write into Claude / Codex config in one click | Changes to someone else's config should be seen by you; no client-specific code in the app |
| D11 | Menu bar | Icon + status dot + inbox count; only the menu bar after closing the window | Window only | Know whether there is anything to file without opening the window |
| D12 | Packaging | A script assembles the `.app`, ad-hoc signs, installs to `~/Applications`; no sandbox, no notarization | Sandbox + Developer ID notarization | The sandbox would stop `cpa` writing its own folder; used only on your Mac |
| D13 | Look | Dark, Mando tokens and sprites, system fonts | Follow system light / dark | Consistent with the device and WhisperWoof; light goes to v1.2 |
| D14 | Language | v1 Chinese, copy collected in one table; v1.2 adds English | Bilingual from the start | First get right what you use every day |
| D15 | Sidebar | A self-laid-out sidebar + pages | System `NavigationSplitView` (macOS 26 glass sidebar) | The glass sidebar pushes pages under the title bar and cannot be captured off-screen; the self-laid-out one is the same every time, and the screenshot is the real window |
| D16 | Suggestions (before G3) | Suggest a project when the text mentions exactly one, Enter to approve, labelled "mentioned in the text" | Wait for a small local model | The requirement is "just approve"; start with the dumbest but most predictable method, and ⌘Z undoes it |

### 10.2 Your call

1. **Merge `feat/filing-p1`** (docs/23 P1) 👤: today the `cpa` on main cannot file yet, and opening the app shows the read-only "recordings without a project". After the merge, run `uv tool install --force ./daemon` once in the repo folder, and Inbox, 1–9 and this Mac's name all work.
2. **"Open at login", yes or no?** 👤 **Default: not added**. Without it, after a restart the menu bar has no number until you open the app once; with it, the app has to write a login item, which is the first exception to "only the cpa command you click writes anything", so it waits for you.
3. **Is docs/23's P2 (a Cardputer page inside WhisperWoof) still built?** 👤 **Default: not for now**. This app is the inbox UI; if WhisperWoof wants to connect, it connects the way the Integrations page shows, like Codex and Cursor. docs/23 §5, D12 and §7.1's P2-a/b get a dated revision; in P2-c the recording contract vectors keep syncing as before (WhisperWoof transcription needs them). Alternative: build both, with the WhisperWoof page per docs/23 §5.

## Revision log

- **2026-10-26 (after review)**: Projects page: the number is a 1–9 menu (two clicks to change number), ≡ drag, double-click the name to rename, key only on hover; with no name typed, empty rows have no grey button and say what to do; on an old cpa none of the buttons appear, and the update command is the same as in Settings; whole-page screenshots are no longer yellow blocks. English: sidebar "2 still on the device", name length not in bytes, dates with month names, "Follow the Mac" says which one it is now, "Give to AI: queue as a to-do for which project".

- **2026-10-26**: Added the "Projects" page (§2.6: a project is a named group; new, rename, change number, link folder, delete, all via `cpa project … --json`); the Device page's 1–9 table points to it; UI in Chinese and English (§2.7); `cpa project list` added to the capability table; contract copies moved to `e812e79` (adds `linked`, `folder` and 2 vectors).

- **2026-10-22 (after three reviews)**: the top count matches the sidebar and menu bar (including another computer's projects); filters in one row; buttons renamed (queue as to-do / start work), the unwritten `h` removed; the lite version keeps only what it can do; failed transcriptions say why and can be retranscribed; Recordings page titles use the text; the Device page lists all of 1–9, "no network recorded"; this Mac's name now uses `cpa host name --json` (≤24 bytes), "This Mac" only goes by the name; the Integrations page starts with "Destinations" (docs/25 §5.7), then the five "for developers" cards and "Copy the connection guide", MCP tabs from cpa, snippets copy only the body, "Other MCP clients" added, commands with absolute paths each saying whether they work; undo in the Edit menu; exit code 2 means "missing" only when probing; the chosen path comes first; timeouts made hard, file reads off the main thread, playback read segment by segment, old reads never overwrite new ones, status asked once a minute when not visible; self-laid-out sidebar (D15); the interim suggestion method (D16); contract copies moved to `8baed54` (adds the `host` document and 9 vectors); the gap table extended to G1–G10; screenshots draw the real window, whole pages, the menu bar and `--live`.
