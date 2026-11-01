**English** · [中文](25-route-to-any-app.zh-CN.md)

# 25 · Speak and send it to any app: destination = generic kind + one preset per app

> Status: **draft design, waiting for your decision** (§8) · Date: 2026-10-22 (revised the same day after four reviews: user, privacy and security, contract and architecture, feasibility; disagreements between the reviews and the trade-offs are in §9) · Role: architecture (destinations and routing) · Based on origin/main `d330465` · Tracks #61, related #34, #53, #52, #54, #24.
> **Change of direction (2026-10-26, D23, D24, §7.6)**: requirement: sending speech straight to a coding agent is not the right first step; speech should first land in our own app and then be placed into different agents by hand. So: **R2's "device sends straight to the agent when you finish speaking" (auto-fill a draft at the desk, press `X` while talking) is on hold**; speech first goes into the Mac app's "Inbox", and you pick a destination on the Mac and send it with ⌘↩ (R1 can already do this). Projects become **named groups** created on the Mac; on the device you group with 1–9, and linking a code folder is optional.
> Requirement (2026-10-22): route spoken input to any chat or agent app, e.g. Grok. While talking, press keys to choose a project, as in the current design. The mechanism must not be an integration built for one app; it should make it easy to integrate every app, with speech as the main input.
> Basis: docs/13 §9.5–§9.6; docs/14 (only on the local branch `feat/destinations`); docs/15 §3.4–§3.12, §4, §5; docs/18 §3.5–§3.6, §7, §12; docs/20 D3, U11; docs/22 §3.1, §5.4, §5.9, §8; docs/23 §2.1, §3.1–§3.6, §5.2; docs/24 §1.2, §2.1, §2.4, §5.2 (`wt-mac`, not merged); three research reports: A each app's entry points, B generic mechanisms and local experiments, C reconciliation with existing designs.
> Tags: **[src]** code, app bundles or docs read · **[meas]** measured on this Mac · **[res]** external docs or forums · **[calc]** calculated · **[inf]** inference · **[asm]** design assumption, can change · **[todo]** to verify · **👤** needs you.
> Scope: design only. No other app's settings or files were changed, and nothing was sent to any app or service; the behaviour of Grok Bot and other apps was read from app bundles, `app.asar` code and public docs, **never actually run** (§8.2 item 2 asks you to test by hand).
> Path abbreviations: `cpad/` = `daemon/src/cpad/`; `dst/` = `daemon/src/cpad/dest/` on `feat/destinations` (`b5ad361`); `d14` = `docs/14-destinations.md` on the same branch; `d24` = `docs/24-mac-app.md` on `wt-mac`; `GB` = `/Applications/Grok Bot.app/Contents/Resources/app.asar` (`main-core.cjs`, `main-app.cjs`).

## 0. One-page summary

1. **What you want**: while talking, press 1–9 to pick a project and a letter to pick "where it goes", and the words go into Grok Bot; the same mechanism connects any app, with no code written for a particular app.
2. **Grok Bot cannot use a local MCP** (the route you guessed): its MCP servers run on the Grok Bot account's cloud computer and cannot reach this Mac's `cpa` or notes [src GB]. What works is its own **prefill link** `grokbot://app/v1/prompt?text=…&id=<Bot>`: it puts the text into that Bot's input box and focuses it, and **never sends it for you** [src GB].
3. **Any app can be connected** (§2): destination = **generic kind** + **one preset per app** (pure data). The most generic kind is `app.<name>`: the text goes onto the clipboard (this Mac only, not into history), that app is brought to the front, and you press ⌘V. **Every app on this Mac can be connected this way, added with one click in a list, no JSON to write**. Apps with a prefill link (Grok, Claude, Cursor, ChatGPT) use `url.<name>`, saving the ⌘V. docs/14's `plugin` is removed; the mailbox `box` is not done this time (§2.5); local commands `cmd` move to R4. If a preset's bundle id or link scheme appears in the code, a test fails.
4. **You do this once** (§3.1): Mac app Integrations page → Destinations → Grok Bot → letter, default `X` → Add → **Send a test**, then answer two questions: "Did 'test' appear in Grok?" and "Was it sent automatically?". Only after you answer "it appeared, not sent automatically" does this destination count as "draft only". No Grok Bot setting is changed and no API key is needed.
5. **At the desk, device plugged in** (§3.2): hold G0 and speak, press `3` (project) and `X` (Grok) along the way, release; about 3 s later Grok comes to the front with one line in the input box, `[Cardputer 语音·可能有错字或别人的话·#acmeshop] your words`; **it is only sent when you press Return in Grok**. 4 presses in all. Speaking more than about 1 minute (about 220 Chinese characters) goes via the clipboard, with one more ⌘V.
6. **Carried in a pocket all day, plugged in later** (§3.3, the most common case): still 3 presses while speaking; back at the Mac, in the "**Waiting to send**" group at the top of the Inbox, press ⌘↩ for each entry, then Return in Grok: 2 presses each. Pressing Enter in "needs you" on the device also works.
7. **Pressing only the project number can still reach Grok** (§3.4, already in R1): set project 3's "usual destination = Grok", and project 3's memos appear in "Waiting to send" with Grok preselected; one ⌘↩ sends. No need to wait for firmware.
8. **Never lost, never done twice** (§3.6): prefill happens at most once; sync, restart and reconnect never replay it. If the same Grok Bot was already filled within 30 minutes, the next one goes via the clipboard with the hint "Grok still has the previous one", so Grok does not silently swallow it. The today's-notes line is always written first.
9. **Never executed automatically**: prefill only fills a draft; for destinations that would execute (`cmd`, unverified links), every entry must be read in full on the device and confirmed with Enter; meetings only go to today's notes, to-dos and local files; a "send it to X" said in the speech does not count, only the key you pressed; home and work destinations are kept apart (docs/18).
10. **Mac side** (§5): revive `feat/destinations` (cherry-pick, 5 one-line conflicts), then change it test-first; cpad opens links via LaunchServices, only handing them to the app recognised when the destination was added (bundle id + signing Team ID); the body never goes into any process's arguments; no Accessibility permission, no AppleScript. (2026-10-22 implementation note: in R1, links sent from the Mac are opened by the sending `cpa` process with the same `LSOpenFromURLSpec`; cpad opening them waits for R2 to wire it into the hub; see §7.4, D7.)
11. **Phases** (§7): **manual test first** (you, 2 minutes) → merge docs/23's `feat/filing-p1` → **R1 Mac only** (registers in the firmware codec table, no firmware release): `cpa filing send`, Waiting to send, usual destinations, about 1,100 lines of code → **R2 device**: press `X` while talking, auto-fill a draft at the desk. R3 pull (optional), R4 `cmd`, R5 paste executor.
13. **2026-10-26 change of direction** (D23, D24, §7.6): the device sending straight to an agent (R2's `X`, auto-fill at the desk) is **on hold**; speech first goes into the Mac app, and you then send it by hand to different agents. A project is a named group created on the Mac (`cpa project add -- name`, code folder optional), grouped on the device with 1–9.
12. **22 decisions** all have defaults (§8.1), split into 11 "you will notice" and 11 "engineering internals"; the 6 you need to answer are in §8.2.

## 1. What you want, and how today falls short

### 1.1 The requirement, broken down

- Spoken words go straight into a chat or agent app such as Grok (§3).
- A local MCP was one possible route; it does not work for Grok Bot (§2.5); Claude Code, Cursor and Codex can already read today through the read-only tools of docs/15 and docs/23 (§5.8).
- Press keys while talking: digits pick the project, letters pick the destination (§4.1).
- Reuse the keys and routing of the current design, docs/14 (§5.1).
- Not limited to one app: the mechanism is generic, and an app is only configuration (§2).
- Speech is the main input; typing is secondary (§3, §4).
- No app-specific code; connecting an app = one click in a list (§2.1, §2.3).

### 1.2 Goal: count presses by your standard (up to "sent in Grok")

| Task | Presses | Count |
|---|---|---|
| At the desk, device plugged in: say something to Grok, under project 3 | Hold G0 and speak, press `3`, `X` along the way, release; about 3 s later Grok pops up; Return | 1 hold + 2 keys + Return in Grok = **4** (R2) |
| Carried all day, back and plugged into USB | Same 3 presses while speaking; back at the Mac: ⌘↩ on each entry in "Waiting to send", Return in Grok | 3 on the device; **2 per entry** on the Mac |
| Only the project number, project 3's usual destination is Grok | Hold G0 and speak, press `3` along the way; on the Mac this entry already has Grok selected in "Waiting to send": ⌘↩, Return in Grok | 2 on the device; 2 per entry on the Mac (**already in R1**) |
| Speaking more than about 1 minute (about 220 Chinese characters) | Same; when Grok pops up the input box is empty with the hint "full text on the clipboard": ⌘V, Return | **+1** (⌘V) |
| The same Bot was already filled within 30 minutes | This one goes via the clipboard, hint "Grok still has the previous one: this one is on the clipboard, ⌘V to add it after" | **+1** (⌘V) |
| Recorded while the device was locked | No auto-fill; ⌘↩ in "Waiting to send" when at the Mac | Same as "back from the pocket" |
| Change your mind after speaking | Auto-filled: delete it in Grok; still in "Waiting to send": ⌥D on the Mac, or Opt+D in "needs you" on the device | 1 |
| Type something to Grok | Opt+X, type, Enter; Return in Grok | When connected to the Mac, Enter fills the draft (D8) |
| Send an old memo to Grok from the Mac | Select it in the Inbox, ⌥X; Return in Grok | 2 |
| Hand to Claude Code | As today: press `Q` while recording to queue it, or `S` to open it on the board (plan mode) | Unchanged |

Every press shows its result: the "Save to" pill on the device changes at once, and a rejected key gets a quiet one-line hint (§4.2); the Mac shows a 5-second hint "Filled into Grok · press Return in Grok to send"; no sound.

### 1.3 How things are today

- **docs/14's destination design and code exist only on the local branch** `feat/destinations` (`b5ad361`, 56 commits behind origin/main [meas `git rev-list`]). Cherry-picking onto main gives only 5 one-line conflicts; keeping both sides, all 2725 daemon and 526 protocol tests pass [meas, run by the feasibility review in a temporary clone, since deleted].
- **docs/23's `cpa filing` is on `feat/filing-p1`**: 7 commits ahead of main and 25 behind, with 9 conflicts against main (`cli/commands.py`, `cli/main.py`, `hub/snapshots.py`, `queue/fold.py`, one test, docs/15, ADR 0004, two protocol READMEs) [meas `git merge-tree`]. R1 has to merge it first.
- **Transcription `cpa-stt` is already on main** (#62, `d330465`), **off by default**; you run `cpa-stt use` once to turn it on [src docs/22 §4.13]. Without it memos have no `memo.text`, and every destination falls back to today's notes (precheck `no_text`).
- **On main the device never sends any destination other than `todo`**: while recording a memo only 0–9 are bound [src firmware/app/lib/input/keymap.cpp:99-104]; `RecMeta` only has `slot`, no `dest` [src firmware/app/lib/rec/rec_meta.h:37]; the "Save to" pill on the recording page only shows the project [src firmware/app/lib/shell/rec_pages.cpp:257-258]. But `outbox.item` on the wire and memo `file.items` already carry a `dest` field [src protocol/README.md item 20].
- **docs/14's `plugin` is a WhisperWoof MCP plugin** [src d14:53], exactly the app-specific integration we no longer want after #34 [src the revision in docs/13-assistant-v2-plan.md:503-513].
- **App names already appear in the code**: `cpad/mcp/cli.py:18` hard-codes 4 MCP clients (claude-code, codex, cursor, claude-desktop) and their config paths; `cpad/agents/claude_code/**` is ADR 0002's session executor [src].
- **Grok Bot 0.75.0**: built on Cursor's Electron base, bundle `com.anysphere.sand`, link schemes `grokbot://` and `sand://`; its MCP config form has stdio and http, stored in the account, not in a local file [src GB].
- **Also on this Mac**: ChatGPT.app (actually Codex, `com.openai.codex`), Claude.app, Cursor, Notion, Obsidian, the `claude` CLI, and xAI's `grok` CLI (`~/.grok/bin/grok`, not Grok Bot) [meas].

## 2. Model: kind + preset

### 2.1 Principles

- **Destination = kind + name**, for example `url.grok`, `app.notion`. The kind sets the rules (whether to confirm, what to do offline, who executes); the name is just your configuration; the id carries its kind, so an id recorded offline can never later turn into another kind [src d14:59-64].
- **Every app has a route**: `app.<name>` = clipboard + bring the app forward + you press ⌘V. It works for every app and needs no permission. For apps with a prefill link, `url.<name>` is an upgrade that saves a press, not a prerequisite.
- **Preset = one JSON file**: kind, template, length limits, suggested letter, whether it leaves the Mac, how to recognise "this app is installed", whether the link sends automatically (`effect`). Shipped in cpad's wheel (`cpad/dest/presets/v1/`, read with `importlib.resources`; #57 forbids `__file__`); your own go in `$CPA_HOME/presets/*.json` (0600, the same strict schema, cannot replace an id shipped with cpad). Adding a new app does not wait for a cpad release: `app.<name>` is one click in a list, and an app with a link gets a preset (§2.3).
- **No apps in the code** (tests; the list is read from `presets/v1/*.json`, not hand-written):
  1. All of `cpad/` and `mac/`: no preset bundle id or `<scheme>://` may appear (case-insensitive).
  2. `cpad/dest/**` and `mac/**/*.swift`: also preset ids and `label` (case-sensitive, whole word). `label` is not checked in `cpad/mcp/**` or `cpad/filing/**`: `cursor` there is a paging cursor.
  3. Exempt: `cpad/agents/claude_code/**` (ADR 0002's session executor). `CLIENTS` in `cpad/mcp/cli.py` passes 1 and 2, but it is still app-specific code; the test comment records it as a dated known exception, and R3 moves it into data (§5.8). The 4 labels hard-coded in Swift for the first card in d24 §2.4 likewise change to come from `cpa mcp setup --list --json`.
- **The bar for adding a kind**: it can be done on this Mac; the body never goes into any process's arguments; no key injection; what "delivered" means can be stated clearly. Only a new **mechanism** adds a kind; a new app only ever adds a preset or a click in the list.

### 2.2 Kinds, version 2

`fl` is a string of flags read by both the device and the Mac: `l` live (only done while you are present; never made up later by sync, restart or retry), `c` leaves the Mac, `p` needs a project, `k` each entry must be read in full on the device and confirmed with Enter.

| Kind | Class | What it does | Executed by | `fl` | Compared with docs/14 |
|---|---|---|---|---|---|
| `daily`, `todo` | Archive | One line in today's notes | NotesWriter | — | Unchanged |
| `file.<name>` | Archive | Append a line to a file; R4 adds `mode:"drop"`: one file per entry | FileAdapter | `c` by default, none only with `--local` | Unchanged (R4 adds drop) |
| `queue` | Record | `queue.added`, `intent=todo` | Router | `p` | Unchanged |
| `session` | Execute | Only queues `intent=spawn`; opens only on Enter on the device board (ADR 0002) | Router | `p` | Unchanged |
| `clip` | Live | Copy to the clipboard | ClipWriter | `l` | Now written via ClipWriter (§5.3) |
| **`app.<name>`** | Live (draft only) | Copy to the clipboard + bring that app forward; **you press ⌘V; it never presses it for you** | AppAdapter | `l`; `c` chosen by you when adding | **New** |
| **`url.<name>`** (`effect:"draft"`) | Live (draft only) | Open the prefill link from the template | UrlAdapter | `lc` (depends on the preset) | **New** |
| **`url.<name>`** (`effect:"act"`) | Execute | Same, but the target app may act by itself | UrlAdapter | `lck` | **New** |
| `paste`, `paste.<name>` | Live / execute | ⌘V into the front app or a named app; never presses Return | External executor (R5) | bare `paste`: `lk`; `paste.<name>`: `l` | Name added |
| `cmd.<name>` (R4) | Execute | Run a local program with fixed arguments; the body only goes through stdin | CmdAdapter | `lck` | **New** |
| ~~`plugin.<name>`~~ | — | Removed; WhisperWoof's plugin becomes an `app` / `url` preset or an R4 `cmd` | — | — | **Removed** |

- The policy now reads `fl` instead of hard-coding `ARCHIVE_KINDS`, `live`, `session` [src dst/policy.py:17, 42, 73-82]; `KindSpec.custom` splits into `builtin` + `custom`, because `paste` is both [src dst/model.py:49]; `PluginOpts` is replaced by `UrlOpts`, `AppOpts` (R4 adds `CmdOpts`) [src dst/model.py:74-76].
- Names are ≤12 characters, matching main's wire pattern `^[a-z][a-z0-9-]{0,11}(\.[a-z0-9-]{1,12}){0,2}$` [src protocol/limits.json:72]; docs/14's 16 characters would be rejected by main's firmware [src dst/model.py:32-33].
- **Device capacity**: `dests.snapshot` has at most 12 rows; minus the 5 built-in ones (`daily`, `todo`, `queue`, `session`, `clip`), the device holds at most 7 custom destinations; there are 9 bindable letters, `C` is the clipboard, so at most 8 custom. More destinations are used only on the Mac (⌥letter, "Send to…").

### 2.3 What a preset looks like

```json
{"v": 1, "preset": "grok-bot", "label": "Grok", "kind": "url", "effect": "draft",
 "detect": {"bundle": "com.anysphere.sand"},
 "template": "grokbot://app/v1/prompt?text={text}&id={p}",
 "focus": "grokbot://app/v1/agent?id={p}",
 "max_text_utf16": 1000, "max_url": 2048, "overflow": "clip",
 "map": {"pattern": "^[A-Za-z0-9_-]{1,128}$",
         "from_link": "^grokbot://app/v1/(?:agent|prompt)\\?(?:[^#]*&)?id=([A-Za-z0-9_-]{1,128})"},
 "leaves_mac": true, "key_hint": "x"}
```

- **Template variables**: `{text}` must appear exactly once; `{project}` is the project key; `{p}` is the value you set for this project in `map` (Grok uses it to pick the Bot; if unset, `map["*"]`). A query parameter with an empty value is removed entirely, so with no Bot set the link has no `&id=`. `map` accepts a value matching `pattern`, or a whole link from which `from_link` extracts the id, so you can copy a Bot's link in Grok and paste it straight in.
- **Templates may only have these shapes** (parsed with `urllib`, not string concatenation): `{text}`, `{p}` and `{project}` may each only be a complete query value; scheme, host and path are literals. Literal query parameter names may not look like `key|token|auth|secret|sig`, and literal values are at most 16 characters.
- **Fixed header**: cpad adds a header in front of every text handed to another app (`url`, `app`, `paste.<name>`, clipboard overflow), **on the same line** as the body, separated by one space:
  - Speech: `[Cardputer 语音·可能有错字或别人的话·#acmeshop] body` (the literal header means "Cardputer speech · may contain typos or other people's words · #project")
  - Typed: `[Cardputer 打字·#acmeshop] body` ("Cardputer typed · #project")
  - Without a project, `·#…` is dropped. The first character is always `[`, so the special meanings of a leading `!` (shell), `/` (command), `@` or `#` in an agent's input box are never triggered; the receiving agent also knows this is a speech-recognition draft.
  - No line breaks, because Grok rejects any query value with a control character (`\n` included): the whole link is dropped while LaunchServices still reports success [src GB `ob()`, `Fg()`].
- **`effect`**: `"draft"` = may only fill a draft, never acts by itself; `"act"` = may act. **Missing means `act`**. Only presets shipped with cpad with a reviewed template may say `draft`; your own `url` presets are always `act`, and a test cannot promote them. Even for a preset that says `draft`, the destination added on each Mac starts as `act` until, after "Send a test", you answer "it appeared, not sent automatically" (§3.1), which records `dest.changed{verified:"draft"}`; if you answer "it was sent", it stays `act`. `act` destinations belong to the execute class (`fl` has `k`): each entry is read in full on the device and confirmed with Enter; a click on the Mac does not count (D14), and Enter after typing does not send directly (D8).
- **`overflow: "clip"`**: when, after cleaning and adding the header, the body exceeds `max_text_utf16` (UTF-16 code units, `len(s.encode("utf-16-le"))//2`, the same as Grok's JS `.length`: an emoji counts 2), or the whole encoded link exceeds `max_url`, it takes the `app` route: the full text goes on the clipboard, the `focus` link brings that Bot forward (with no Bot id, only the app comes forward), and the hint says "Too long: full text on the clipboard, press ⌘V in Grok". No truncation, nothing dropped. **Only when the clipboard destination is on** (if you turned the clipboard off, it fails, records `E_TOO_LONG`, and the entry stays in Waiting to send marked "too long").
- **`destinations.json` stays strict** (unknown fields rejected, 0600 [src d14:122-142]). `cpa dest add` **copies** the preset's `template`, `focus`, limits, `overflow`, `effect` and `map` into the destination and records `preset:"grok-bot"` as its origin; changing or deleting the preset file does not change destinations already added (same reasoning as d14's "the id carries its kind"), and `cpa dest ls` hints "a newer preset is available".
- **What it may connect to** (three gates; the tables are data, in `presets/v1/deny.json`):
  1. The scheme must be registered in the `CFBundleURLTypes` of the app named by the preset's `detect.bundle`.
  2. Forbidden schemes: `http`, `https`, `file`, `data`, `javascript`, `applescript`, `shortcuts`, `x-apple*`, `smb`, `vnc`; forbidden routes: `*/mcp/install*`, `run-shortcut`, `advanced-uri`, `x-callback-url`; forbidden handler apps: `com.apple.shortcuts`, `com.apple.Terminal`, `com.googlecode.iterm2`, `com.apple.ScriptEditor2`.
  3. Presets you write are always `act` (see above).
- **`map` values**: at most 128 per Grok's rule [src GB `Tu=/[A-Za-z0-9_-]{1,128}/`]; values starting with a known secret-key prefix are rejected (`xai-`, `crsr_`, `sk-`, `sk_`, `ghp_`, `github_pat_`, `AKIA`, `xox`; the table is data). No field is meant for secrets.
- **Grok test vectors**: `presets/v1/grok-bot.vectors.json` copies Grok's own checks: every raw character in the link is in 0x21–0x7E; no `#` or `\`; the whole link ≤2048; no control characters after decoding, matching `\s*\S[\s\S]*`; empty parameters removed [src GB `NB()`, `Rle`].
- **The preset format is a public contract**, `protocol/dest-preset/v1/` (schema, valid and invalid vectors, README); `cpa dest add --preset-file PATH` validates against it first.

### 2.4 Which kind each app on this Mac uses

Legend: ✓ works · ✗ not available. Until tested by hand, "does it send automatically" is always added as `act` (§2.3).

| App (version) | `url` prefill (no send) | Readable by the existing read-only MCP (docs/15, docs/23) | `app` clipboard + bring forward | Default |
|---|---|---|---|---|
| Grok Bot 0.75.0 | ✓ `grokbot://app/v1/prompt?text=&id=`; body ≤1000 UTF-16 code units, whole link ≤2048, about 220 Chinese characters [src GB] | ✗ the MCP servers run on its cloud computer [src GB] | ✓ (used on overflow) | `url.grok`, draft only after testing |
| ChatGPT.app 26.1007 (Codex) | ✓ `codex://threads/new?prompt=` (`q` also maps to `prompt`); whether it sends automatically [todo] [src] | ✓ `~/.codex/config.toml` | ✓ | `url.codex`, draft only after testing |
| Claude 3.1623.4 | ✓ `claude://claude.ai/new?q=`, `claude://code/new?q=&folder=`, about 16 KiB; whether it sends automatically [todo] [src] | ✓ `claude_desktop_config.json` | ✓ | `url.claude`, draft only after testing |
| `claude` CLI 2.1.306 | — | ✓ | — | Existing `queue`, `session` |
| Cursor 3.24.18 | ✓ `cursor://anysphere.cursor-deeplink/prompt?text=&workspace=`, about 1,100 Chinese characters; rejects control characters and words that look like keys [src] | ✓ `~/.cursor/mcp.json` | ✓ | `url.cursor`, draft only after testing |
| Notion 7.34.0 | ✗ can only open pages | ✗ only cloud Custom Agents [res] | ✓ | `app.notion` |
| Obsidian 1.8.9 | `obsidian://daily?append=` works but is not needed [res] | ✗ | ✓ | `file.ob` (the vault is markdown) |
| xAI `grok` CLI 0.2.93 | — | ✓ also reads `~/.claude.json`, `.mcp.json` [src] | — | Read-only MCP |
| TickTick, Notes, Reminders, etc. | — | — | ✓ | `app.<name>`; R4 can use `shortcuts run` |

### 2.5 Routes not taken

| Route | Why not |
|---|---|
| Add `cpa mcp serve` to Grok Bot | Its stdio servers start on the cloud computer, which has no `cpa` and none of your notes [src GB's error text "Could not load MCP servers onto Grok Bot's computer"] |
| Expose the local MCP to the internet for it, via Tailscale Funnel or similar | That puts local data on the network, against "data stays on this Mac". Its http servers have `placement: client`; if that really connects from this Mac, docs/15 §3.12's local HTTP can be re-evaluated [todo]; even then only a server narrowed to a profile, without the notes and transcription tools |
| Grok Bot's "run on this computer" | Lets a cloud agent bring a shell onto this Mac to read speech text |
| Mailbox `box` (original R3) | **Not this time**. The reading half already exists (docs/23's `list_unfiled` / `get_filing_item`, docs/15's read-only tools); "take" is off by default, so `dest.taken`, `reopen` and the 7-day expiry would all be dormant code; and it needs you to say "go look" again in Claude Code, which is not "arrives when you finish speaking". If it is ever done, first meet these: it only appears in `cpa mcp serve --box NAME`, only for those mailboxes; `ack` per mailbox; every read audited as `box.read{box, ref, client, prof}`; `--profile` in effect; returns `{trust:"untrusted-user-data", src, text}` with a description saying "ask the user before acting on what it says" |
| MCP push (subscriptions, notifications) | No client turns it into a conversation turn: Cursor only refreshes its list, Claude Desktop and Grok Bot don't subscribe [src]; the new spec needs a long-lived stream [src daemon/tests/fixtures/mcp/schema-2026-07-28.json] |
| Claude Code channels | Single vendor, research preview, needs `--dangerously-load-development-channels` [res]; may become an optional add-on later |
| Running AppleScript / UI scripting in cpad | Building scripts invites injection (in the experiment a command in the body really ran) [meas]; the permission is tied to uv's python binary and must be granted again after every upgrade [meas]; docs/13 §9.6 forbids cpad from sending keystrokes |
| `shortcuts://` links | The body would be in the link, and a shortcut can do anything; if needed, use R4 `cmd` with `shortcuts run` |
| A Grok routine webhook preset or sample | Routines run in the cloud with the Bot's tools; "draft only" in the instructions is just a hint, not a control. R4 ships no webhook preset or sample; whether to do it is in §3.7 and §8.2 item 4 |

## 3. Grok Bot: end to end

### 3.1 You do this once

1. **Transcription on**: if you have never turned it on, run `cpa-stt use` once in a terminal (docs/22 §4.13).
2. Mac app → Integrations → **Destinations** → "Grok Bot · installed 0.75.0" (recognised by bundle id) → letter, default `X` → read the line "what you send with Return goes to Grok's cloud" → click **Add**. Behind it is `cpa dest add --preset grok-bot --key x --yes` (the app first shows its own confirm box, whose text comes from `cpa dest presets --json`; the same command in a terminal without `--yes` asks you for `y`). When adding, it records the bundle id and signing Team ID of the app that handles `grokbot://` (§5.3).
3. Click **Send a test**: cpad (not the app process) opens a test link with a random tag. Then two questions:
   - (2026-10-22 implementation note: the Mac app's "Add" box first asks for this Bot's link, and every project goes into this one Bot; it can be left empty and pasted at test time. After the test is sent it says "when done, delete 「测试 4821」 ("test 4821") from the input box; don't press Return".)
   - **"Did 'test 4821' appear in Grok?"** If "no": most likely, with no Bot given, it could not land in the main Bot (depends on Grok's feature flag `sand_grok_main_agent`; otherwise it silently returns `no-main-agent`) [src GB]. Then it asks "paste this Bot's link or id"; you copy the Bot's link in Grok and paste it, cpad extracts the id with the preset's `from_link`, stores it as `map["*"]`, and tests again.
   - **"Without you pressing Return, did it send by itself?"** If "no": this destination is recorded as draft only (`dest.changed{verified:"draft"}`). If "it was sent": it stays `act`, and each entry must be read in full on the device and confirmed with Enter (R2; in R1 this Mac does not send to it).
4. (Optional) Set a **usual destination** for a project: project 3 → Grok (§3.4).
5. The next time the device connects it receives the new destination table (`dests.snapshot`); nothing on the device needs to change.

**Not needed**: no MCP added to Grok Bot, no API key, no Grok Bot "run on this computer", none of its settings changed.

### 3.2 At the desk, device plugged in (R2)

| Moment | Device | cpad | Grok Bot |
|---|---|---|---|
| Hold G0 | Recording starts; the "Save to" pill shows `#–` | — | — |
| Press `3`, `X` along the way | The pill becomes `#3 acme → Grok cloud` | — | — |
| Release | The memo is written with `slot:3`, `dest:"url.grok"`, `lock:0` | Pulled at once if connected (about 0.6 s for 10 s of speech) | — |
| +about 3 s | Back on home (the current end hint) | Transcription produces text (about 1.7 s for 10 s [src docs/22:726]); **the today's-notes line is written first**, then `decide()`: judged "live" (§5.2), opens the prefill link, records `dest.handed{via:"url", leaves_mac:true}` | Comes to the front; the input box holds `[Cardputer 语音·可能有错字或别人的话·#acmeshop] your words` with the cursor in it |
| You at the Mac | — | — | You glance, edit, and **it is only sent when you press Return** |

Auto-filling a draft requires **all** of these (if any one fails, the entry goes to "Waiting to send", §3.3):
- this destination was tested by hand and recorded as draft only (`effect:"draft"` and `verified`);
- when the memo ended, the device was connected to this Mac, unlocked, in the same boot, and no more than 60 s passed from release to text (device `up_ms`) [asm];
- the Mac is not locked (`CGSSessionScreenIsLocked` from `CGSessionCopyCurrentDictionary`) [todo], and nobody has pressed a key on the Mac keyboard in the last 3 s (`CGEventSourceSecondsSinceLastEventType`, no permission needed [todo]): so if you are typing elsewhere, Grok does not steal focus and have your Return send it;
- it is not a meeting; the same Bot has not been filled with anything else in the last 30 minutes (§3.6).

This is D5's default. The cost: Grok comes to the front by itself about 3 s after release, also while screen sharing or presenting (§8.2 item 3). cpad is a background LaunchAgent; whether macOS 26 lets it bring Grok's window to the front [todo] is tested with a real LaunchAgent in R1-C's acceptance.

### 3.3 Carried all day, plugged in later (the most common case, docs/20 U11)

- **While speaking**: hold G0, `3`, `X`, release, as usual. Letters work even while the device is locked [src d14:475]. The memo is written with its `dest`, and nothing pops up.
- **After plugging in**: cpad pulls, transcribes and writes the today's-notes line; these memos are not "live", so they are held (`hold`), and **sync never opens Grok** [src d14:216].
- **On the Mac**: the top of the Inbox page gains a group "**Waiting to send · 4**"; each row says "→ Grok · #3 acmeshop · 08:10" and the first few sentences, with Grok already selected. ⌘↩ sends this one (Grok pops up, you press Return), and the selection moves to the next. 2 presses each.
- **On the device too**: select it in "needs you" and press Enter (1 press on the device + 1 Return in Grok per entry).
- **Several entries into the same Bot at once**: from the second one on it goes via the clipboard with the hint "Grok still has the previous one" (§3.6). So send the previous one in Grok first, then ⌘↩ the next; the top of the "Waiting to send" group says so.
- In the terminal likewise: `cpa filing triage` goes through the "Waiting to send" entries first; ↩ sends to the chosen destination, `g` then a letter picks another destination.

### 3.4 Only the project number: the project's usual destination (R1)

- In the "Destinations" card on the Integrations page, set a project's "usual destination": project 3 → Grok (stored in `destinations.json` as `usual:{"acmeshop":"url.grok"}`).
- Project 3's memos (with text, not a meeting, no destination decided yet, within 24 h) appear in "Waiting to send" with Grok selected and marked "suggested". ⌘↩ sends; if ignored, nothing happens; after 24 h it is no longer suggested, and nothing is recorded.
- **Only explicit keys count**, unchanged: the usual destination is only a preselection; the real choice is the ⌘↩ you press on the Mac (`how:"pick"`); it never sends automatically (precheck `not_explicit`).
- So R1 does not wait for firmware: today, press `3` on the device while speaking, ⌘↩ back at the Mac, and Grok has the draft open. The device's `X` (R2) is only needed when an entry should go somewhere other than the project's usual destination.

### 3.5 What Grok Bot sees and does

- **It sees** only a draft in its input box, starting with the header. It knows nothing about Cardputer and cannot read your other notes.
- **It does** nothing until you press Return. Once sent, the text goes to the Grok Bot account's cloud computer ("Bots and chats stay on the account's cloud computer") [src GB]—**it leaves the Mac because of the press you chose**.
- Text and project arrive together: the project is in the header (`#acmeshop`); if you set a Bot per project, the Bot separates projects.
- Can Grok Bot read things on this Mac in return? No, unless you turn on its "run on this computer" (not recommended, §2.5).

### 3.6 How it is never lost and never done twice

| Mechanism | Semantics |
|---|---|
| `url` | **At most once**. A capture is decided only once (docs/14's fold); right after opening the link it records `dest.handed{via:"url"}`, not "delivered": LaunchServices accepting the link does not mean Grok accepted the draft. Restart, reconnect and retry never open it again [src d14:216 invariant 2]. To send again, only you can do it, with ⌘↩ on the Mac or Enter again in "needs you" on the device, recorded with `by`. |
| Drafts for the same Bot | cpad remembers when each (destination, Bot) was last prefilled. Filling the same Bot again **within 30 minutes**: this entry goes via the clipboard instead (`dest.handed{via:"clip"}`), `focus` brings that Bot forward, and the hint says "Grok still has the previous one: this one is on the clipboard, ⌘V to add it after". With the clipboard destination off, this entry stays in Waiting to send marked "Grok still has the previous one". |
| `app`, `clip`, `paste` | At most once; no automatic retry; a crash midway records `E_INTERRUPTED` and waits for you to confirm. |
| `cmd` (R4) | Same. |
| `file` | At least once + block-id de-duplication, which equals once. |

**When Grok silently does not accept** (all return success, cpad cannot see it) [src GB]: the input box already holds unsent text (it keeps the old one, `kept-draft`); the same link twice within 2 s (`CU=2e3`); the Bot id cannot be resolved; no Bot id and no main Bot enabled (`no-main-agent`); a control character in the link (`query-value`). The first two are covered by the "30 minutes" above; the last three by the test when adding and the cleaning in §2.3. If the Bot is not in your list or you lack permission, it shows a notice. Only hidden team Bots show a confirmation card; your own Bots do not [src GB].

The today's-notes line is always written first, so whichever route fails, the words are still there.

### 3.7 Getting it to work while you are away from the Mac: routine webhooks (not done, only a question for you)

- Each Grok Bot can have "routines": run on a schedule, on receiving email, or on receiving a webhook. The webhook path `/automations/webhook/<routine id>` is in the app bundle [src GB `main-app.cjs`], and needs `Authorization: Bearer crsr_…` [res: Hookdeck guide and Cursor forum].
- It runs in the cloud with the Bot's tools; the body always leaves the Mac, and "draft only" in the instructions cannot stop it. Not done by default; R4 ships no preset or sample.
- If you answer "yes" to §8.2 item 4, the conditions are: only typed entries, or speech read in full on the device; ≤500 B; the connected Bot has no tools with side effects (no sending email, no MCP writes, no "run on this computer"); the confirmation card says "the cloud will execute"; the key lives in the Keychain and is read by your own script; cpad does not store it.

### 3.8 Honest limits

- **One draft per Bot at a time**: §3.6's "30 minutes" is a guess; cpad cannot see whether you pressed Return in Grok. A wrong guess costs only one extra ⌘V.
- **About 220 Chinese characters**: longer goes via the clipboard (§2.3 `overflow`), with one more ⌘V. Speaking more than about 1 minute does this, which is not rare.
- **Whether a draft syncs to the cloud before you press send** [todo]: so Grok presets always count as "leaves the Mac".
- **Grok only works when you press send**; to have it act on its own, only cloud routines can (§3.7).
- **The Mac must be awake and you logged in**: cpad is a LaunchAgent in the login session; memos that arrive with the lid closed stay in Waiting to send.

## 4. Device

### 4.1 Keys

- **While recording a memo** (holding G0, or `fix/memo-hold`'s hands-free mode `RecLatch`; both layers are bound): digit = project (as today); **a letter bound to a destination = destination, the last one pressed wins**; `N` goes back to today's notes. G0 is outside the keyboard matrix, so G0 + one key is a two-key combination with no ghosting [src docs/13-assistant-v2-plan.md:127; docs/04-prd.md:374].
- **An unbound letter**: a quiet one-line hint "Z has no destination", nothing changes.
- **A destination that needs a project** (`Q`, `S`) without a digit: the pill becomes a reminder "needs a project · press 1–9".
- **Letters do nothing in a meeting**: a meeting contains other people's words [src d14:263; docs/23-mac-inbox.md §3.4].
- **Typing**: Opt+X opens an editor aimed straight at Grok; Opt+letter in the editor changes the destination; Opt+G lists all destinations [src d14:91-100]. Speech fragments added by holding G0 in Grok's editor are merged into the same entry per docs/22 (draft-only destinations accept fragments, `k` destinations do not; §7.2 revises docs/22).
- **Letter budget**: fixed `N T Q S`; only 9 bindable, `b c f j k o u x y`; `G` is reserved for the picker, so Grok uses `X` (xAI) [src dst/model.py:29-31]. `X`, Opt+X and Opt+G are free on every firmware branch [meas].
- **⌥ + letter on the Mac**: the Mac's Inbox page already uses `j k x`, and `cpa filing triage` uses `u x y n` (docs/23 §5.2; `feat/filing-p1` `cli/triage.py:236-237`), so 5 of the 9 bindable letters collide. So in the Mac app it is ⌥X, and in the terminal `g` then `x` (⌥ in a terminal types other characters depending on the keyboard layout).

### 4.2 Screen (240×135, memo recording page)

```
y14   [hands-free / grace pill, as today]
y95   Save to [#3 acme] → [Grok cloud]      ← destination pill: cloud = leaves the Mac
y110  X Grok  C clip  O sb  …               ← only bound custom letters, at most 5, current one inverted
y123  1cpa 2blog 3acme 4th 0none            ← #53: who 1–9 are, replacing today's fixed hint
```

- **y95 fits**: "Save to" starts at x60, the project pill at x89; the destination pill is at most 4 ASCII or 2 Chinese characters plus "cloud" (≤48 px), and the project pill is truncated to about 91 px, ending at x234 [calc]. (On the Chinese UI the labels are 存到 and 云.)
- **y110 fits**: a row is about 228 px, and 5 entries are about 210 px; with more it shows "…", and Opt+G shows all [calc].
- Today y123 is the fixed "G0松开结束 1-9选项目 0不归属" ("[G0]let go: stop [1-9]project 0 none") [src firmware/app/lib/shell/rec_overlay.cpp:109]; #53 replaces it with project names, hides empty slots, with each slot ⌊(38−n)/n⌋ ASCII characters wide [calc]. y110 is empty today [src firmware/app/lib/module_api/canvas.h:110-116]. docs/14's row "while holding: T to-do C clipboard…" merges into the same layout as #53; #53 waits for the recording page from `feat/ux-decisions` to merge.

### 4.3 Confirmation card: only for the execute class

- After a memo ends, the device returns to home and shows a hint [src firmware/app/lib/shell/runtime_rec.cpp:125-130]; text only arrives about 3 s later (0.6 s release grace, about 0.6 s pull, about 1.7 s transcription). By then you may have opened another page.
- **Only destinations whose `fl` contains `k` show a card** (`cmd`, `url` with `effect:"act"`, bare `paste`). Draft-only destinations show none: auto-fill at the desk (§3.2), and otherwise they go to Waiting to send. Archive destinations with only `c` (leaves the Mac, `file`) show none either: they were confirmed once when turned on (d14 §2.1).
- **The card only appears on home with nothing else open**; otherwise only the "needs you" count goes up by one. An Enter within 500 ms of the card appearing does not count, so an Enter meant for the editor or a list never becomes "send".
- **The card pages through the full text**: you page through to the end, and Enter only works on the last page; when cpad receives `dest.send go` it checks that the device fetched this entry's full text in this connection (`dest.get` → `dest.text{ref, text, sha8}`, sha8 matches), and rejects it otherwise (per docs/22 §5.4 item 2). Speech over 40 s or a body over 500 B is not sent to this class; it falls back to today's notes with reason `too_long`.
- Enter sends; `` ` `` puts it into "needs you"; after 15 s with no response the card closes itself, and **the entry stays in "needs you"** (the same duration as docs/20 D3's start box, but it does not cancel).
- **While locked**: letters still work [src d14:475]; the card and "needs you" are only visible after unlocking. A memo recorded while locked carries `lock:1` and is never auto-filled (§3.2). Random presses in a pocket (docs/20:670's fuzz test) can never send anything off the Mac.

### 4.4 Offline and expiry

- On the device the memo is written with its `dest` id (`RecMeta.dest` → `.M`); after connecting, cpad decides it as `queued`: every live destination is held, appearing in the Mac's Waiting to send and the device's "needs you", and **sync never opens any app automatically** [src d14:216].
- After 24 h untouched it records `dest.expired{item, dest}` (d14 already has these two fields [src d14:386]).
  - **Without a project**: back in the Inbox as "new" (docs/23 §2.1), with the row saying "Suggested: Grok" (read from `dest.expired.dest`); ⌥X sends it again in one press.
  - **With a project**: it already counts as filed, so it does not go back to the Inbox; it is visible in `cpa dest pending --expired` and `cpa audit`, and the filed row has a small tag "Not sent: Grok (expired)".
- Changing a binding never sends to the wrong place: the id is recorded, not the letter [src d14:59-64]; an id that has been deleted falls back to today's notes with reason `unknown`.

### 4.5 Home and work (docs/18)

- **Device**: one destination cache per profile, `/cfg/p<n>/dests.json`; `dests.snapshot` is adopted or only cached per docs/18 §3.5 item 5 [src docs/18 §3.6]. The confirmation card, `dest.send` and `dest.get` only appear and are sent when the current profile ∈ ok(H) (docs/18 §3.5 item 4 already lists `dest.send`). Each row in "needs you" carries `src` and is cleared on a profile switch.
- **Mac**: every destination carries `profiles:[…]`. Required when `served` in `profile.json` has more than one entry; `cpa dest add` takes the only one by default and asks you if there are several. Precheck `other_profile`: if the entry's `prof` is not in the destination's `profiles`, it falls back to today's notes; `cpa filing send` and the Mac app use the same check. Every `dest.*` audit line carries `prof`.
- **Tests**: add one to docs/18 §12's canary tests: the home canary string never appears in a work destination's link, clipboard write or `cmd` stdin.

### 4.6 What the firmware needs to add (F5)

- `/cfg/p<n>/dests.json` cache; `RecMemo` and `RecLatch` both bind the bindable letters and `N`; `RecMeta.dest` and `RecMeta.lock` written into `.M`.
- The pill, the letter row (≤5), the #53 legend; a "Go" page; pending-send rows in "needs you"; the confirmation card (home only, 500 ms accidental-press guard, full-text paging); `dest.send`, `dest.get`.
- **Unknown kinds are still shown** (d14 §10.4 originally hid them [src d14:473]); behaviour depends only on `fl`, and unknown letters are ignored. A missing or unreadable `fl` is treated as `lck` and shown as "cloud" (prefer to confirm more). So adding kinds later needs no firmware release.
- The three booleans become one `fl` string, so the worst-case line length for 12 rows must be recalculated (1843 B today [src d14:350]; `fl` makes it shorter).

## 5. Mac side

### 5.1 Routing: revive `feat/destinations`, don't rewrite

- `git merge-tree origin/main feat/destinations` has only 5 conflicts, all one-line registrations (`cli/main.py`, `protocol/specs.py`, two tests, `protocol/README.md`); the 29 new files are clean [meas `git merge-tree`, temporary objects only].
- Method: branch from origin/main, cherry-pick `b5ad361`, resolve conflicts, get green first; then small commits one at a time (failing test first): remove `plugin` → decide by `fl` → names ≤12 → `leaves_mac` and `profiles` on every custom destination → `origin:"mac"` → new prechecks → `url`, `app` → presets.
- No rewrite: the 96-cell truth table is the tests' reference answer, and the 11 fixes from reviews are in it.
- The firmware only touches its codec table: `dests.snapshot` is the 31st message, and `test_codec_vectors` asserts the vector count equals `kMsgTypeCount` (30 today) [src firmware/app/test/core/test_codec_vectors/test_main.cpp:55; firmware/app/lib/protocol/messages.h:52]. R1 is "Mac + the firmware codec table", **no firmware release**.

### 5.2 `decide()`

**One more input origin**: `origin` was `live`, `queued`, `confirmed`; add **`mac`**: someone confirmed this entry on this Mac (`cpa filing send`, `cpa dest send`, with `by` `cli` or `app`). Only these two commands set it; sync, replay and restart never do. (Not called `here`: docs/23's `env.link:"here"` already means "connected to this Mac while recording".)

**Speech `live`** (D5; before, speech was always `queued`): the router judges a memo `live` only when the conditions in §3.2 hold; `decide()` itself stays a pure function, reading neither the clock nor the Mac's lock state.

**Prechecks** (in order; the first that fails is the reason, falling back to today's notes, whose line has already been written):

| # | Condition | `reason` | |
|---|---|---|---|
| 0 | Meetings may only go to `daily`, `todo`, or a `file` with `leaves_mac:false` | `meeting` | New |
| 1 | The id is not in the catalogue | `unknown` | |
| 2 | No executor in this version (`paste`, `paste.<name>` before R5; `cmd` before R4) | `unavailable` | List changed |
| 3 | Turned off | `disabled` | |
| 4 | The entry's `prof` is not in the destination's `profiles` | `other_profile` | New |
| 5 | Leaves the Mac but has no `optin` | `no_optin` | |
| 6 | `how` is not `key` / `pick`, while `fl` has `l`, `k` or `c` | `not_explicit` | Now reads `fl` |
| 7 | No text: only `memo.text` or typed text counts, **never a stitched-together transcript** | `no_text` | |
| 8 | Needs a project but has none | `no_project` | |
| 9 | `fl` has `k`, and the speech is over 40 s or the body over 500 B | `too_long` | New |

The routing layer also has d14's `bad_text` (the queue body is unusable after cleaning).

**Truth table** (after all prechecks pass). Legend: **D** deliver; **H✓** hold, waiting for you to confirm on the device or the Mac; **H⏚** hold, waiting for the device to connect; **Q** queue `intent=todo`; **S** queue `intent=spawn`; **v** add `voice:true` (docs/22 §5.4 item 8: `uncertain` is no longer used); **E** reply `E_NEEDS_DEVICE`, the entry stays H✓; **D\*** needs the device to have fetched the full text (sha8).

| Kind | Typed·live·on | Typed·live·off | Typed·queued·on | Typed·queued·off | Speech·live·on | Speech·live·off | Speech·queued·on | Speech·queued·off | Conf·on | Conf·off | Mac·on | Mac·off |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Archive: `daily` `todo` `file` | D | D | D | D | D | D | D | D | D | D | D | D |
| `queue` | Q | Q | Q | Q | Qv | Qv | Qv | Qv | Q / Qv | Q / Qv | Q / Qv | Q / Qv |
| `session` | S | S | S | S | Sv | Sv | Sv | Sv | S / Sv | S / Sv | S / Sv | S / Sv |
| `clip` | **D** | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D** | H⏚ | **D** | **D** |
| Draft only: `url`(draft), `app.<name>`, `paste.<name>` | **D** | H⏚ | H✓ | H⏚ | **D** | H⏚ | H✓ | H⏚ | **D** | H⏚ | **D** | **D** |
| Execute: `cmd`, `url`(act), bare `paste` | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D\*** | H⏚ | E | E |

- Columns: Typed / Speech; live = `live`, queued = `queued`; Conf = confirmed on the device; on / off = device link Ready or not. "Typed·live·off" and "Speech·live·off" never occur (`live` requires the link), but are defined anyway. Tests cover everything: 10 row classes (`daily`, `todo`, `file`, `queue`, `session`, `clip`, `url` draft, `app`, `url` act, `cmd`) × 12 columns, times meeting or not and right profile or not (prechecks 0, 4).
- `Mac·off` can deliver too: that is why R1 can send into Grok from the Mac without the device plugged in. A `session` from the Mac is still only queued (as in docs/23 §3.4); opening the session is still Enter on the device board (ADR 0002).
- `clip`'s "Speech·live·on" is still H✓: only the draft-only class is done automatically at the desk (D5); writing speech to the clipboard automatically gains nothing.
- **Invariant 2 becomes**: a live destination is D only in these cases: confirmed on the device and online; typed `live` (`clip`, draft only); speech `live`, only for verified draft-only destinations; `origin:"mac"`. Sync, restart and retry never open, copy or paste. The execute class only accepts a confirmation made after reading the full text on the device.
- Two new failures at delivery: the app was deleted after a `url` destination was added (LaunchServices returns `-10814` [meas]), recorded as `dest.failed{code:"E_DEST_MISSING"}`; the app handling the scheme changed, or its signing Team ID changed, recorded as `dest.failed{code:"E_HANDLER_CHANGED"}`. Both open nothing and do not retry.

### 5.3 Adapters

| Adapter | How | Basis |
|---|---|---|
| `UrlAdapter` | 1) Add the header (§2.3); replace every C0, DEL and C1 control character (including `\n`, `\t`) with a space; strip Cf characters and bidi controls; fail if empty; 2) check `max_text_utf16` in UTF-16 code units, encode (`quote(…, safe="")`, `+` becomes `%2B`, since Cursor treats `+` as a space [meas]) then check `max_url`; over the limit takes `overflow`; 3) check draft occupancy (§3.6); 4) find the app recorded when adding with `LSCopyApplicationURLsForBundleIdentifier`, check its signing Team ID with the Security framework (ctypes, `SecStaticCodeCreateWithPath` → `kSecCodeInfoTeamIdentifier`), then call `LSOpenFromURLSpec` via ctypes **with `appURL` set**, so an app installed later cannot grab the scheme; **does not run `open`**: any process of the same user can see the body in its arguments, and a long link fails with "argument list too long" [meas]; no PyObjC, no permissions; 5) record `dest.handed{via:"url"}` | Research B §1(2); security review |
| `ClipWriter` | ctypes to `NSPasteboard`: `prepareForNewContentsWithOptions:` with "this host only" (`NSPasteboardContentsCurrentHostOnly`, not synced to an iPhone via Universal Clipboard) [res][todo], plus the `org.nspasteboard.ConcealedType` and `TransientType` markers (clipboard history tools don't record it); after 2 minutes, if the clipboard's `changeCount` has not changed, clear it. If ctypes does not work, fall back to `pbcopy` (body via stdin); then it only writes after you press once to confirm, with the hint "clipboard history may record this" | User and security reviews |
| `AppAdapter` (`app.<name>`, overflow, draft occupancy) | `ClipWriter` writes the clipboard, with the header; then the preset's `focus` link (Grok: `grokbot://app/v1/agent?id={p}`) or just the bundle brings the app forward (`LSOpenFromURLSpec`, with only the app's location); never presses ⌘V; records `dest.handed{via:"clip"}` | User review |
| `ClipAdapter` | Unchanged; now uses `ClipWriter` | d14 adapters |
| `FileAdapter` | Unchanged (`O_APPEND`, `O_NOFOLLOW`, one write per line [meas 8 processes × 2000 lines, no tearing]); R4's `drop` mode writes a temp file, fsyncs, `link()`s it to `<ref>.md`; writing the same entry again is a no-op | Research B §1(5) |
| `CmdAdapter` (R4, its own ADR) | See below | Research B §1(4); security review |

**`CmdAdapter` rules (R4)**: stdin alone is not enough; the receiving program might run stdin as code.
- `cpa dest add` rejects these `argv[0]`: shells (`/bin/sh`, `bash`, `zsh`), interpreters (`osascript`, `python3`, `node`, `ruby`, `perl`), argument builders (`xargs`, `env`), agent CLIs (`claude`, `codex`, `grok`, `cursor-agent`). The table is data (`presets/v1/cmd-deny.json`), so there are still no app names in the code. Agents go through `queue`, `session` or `url`.
- `argv[0]` is your own regular file, and neither it nor its directory is writable by group or others; when adding, its sha256, size and mtime are recorded, and if they change at send time it fails (`E_CMD_CHANGED`) and must be added again. No `{…}` placeholders allowed in the arguments.
- `shell=False`, `close_fds=True`, `start_new_session=True`; the environment has only a fixed `LANG`, `PATH` and `CPA_DEST`, `CPA_REF`, `CPA_PROJECT`, `CPA_SOURCE`; `cwd=/`; `killpg` after a 10 s timeout; exit code 0 = delivered; output discarded, only stderr's length recorded [meas: hostile bodies pass through unchanged, no processes left after a timeout].
- Shortcuts: the preset stores the shortcut's identifier (`shortcuts list --show-identifiers`), not its name (names change and can be taken by another shortcut); the device card shows the name. `shortcuts run` takes `-i <path>`, not stdin [meas `shortcuts run --help`]: try `-i /dev/stdin` first; if that fails, write a 0600 temp file in `$TMPDIR` and delete it after the run. The ADR states: a shortcut that runs "Run Shell Script" or "Run AppleScript" on its input is executing code.
- Child processes inherit cpad's privacy permissions (TCC). The docs say so, and advise you not to give cpad's python "Full Disk Access".

### 5.4 Sending from the Mac: Waiting to send

- `cpa dest pending --json` gives, in one call, the rows for the Mac list and triage; each row's `why` is:
  - `held`: held (waiting for confirmation or for the device);
  - `usual`: the project has a usual destination and no destination has been decided yet (§3.4);
  - `expired`: expired, only with `--expired`.
- **docs/23 §2.1 item 3 becomes**: an effective destination excludes held ones (`await_confirm` / `await_device`). Held rows go into the "Waiting to send" group with or without a project; those without a project go back to "new" after expiring or being dropped.
- Send: `cpa dest send REF [DEST]` or `cpa filing send ID DEST`, both with `origin:"mac"`, `how:"pick"`, through `decide()`; the execute class replies `E_NEEDS_DEVICE`. Drop: `cpa dest drop REF` (`dest.dropped`; the today's-notes line remains).
- `cpa filing send` replies `E_BAD_ARG` (`field:"id"`) for a meeting sent anywhere other than today's notes, to-dos or local files, same as `hand`.
- The body is never in the arguments: `send` only takes ids, and cpad fetches the body itself.

### 5.5 Command line (public contract, used by the Mac app and your scripts)

| Command | What it does |
|---|---|
| `cpa dest ls [--json]` | List destinations: letter, kind, cloud / local, `effect`, verified or not, profile, last send, whether a newer preset exists |
| `cpa dest presets [--installed\|--apps] [--json]` | List presets; `--installed` lists only installed ones (`LSCopyApplicationURLsForBundleIdentifier`, finds both `/Applications` and `~/Applications`, version from `CFBundleShortVersionString`); `--apps` lists the name and `CFBundleIdentifier` of every app on this Mac, for "Other app…"; includes each preset's confirmation text, so the app writes no words of its own |
| `cpa dest add` | `--preset P \| --app BUNDLE \| --preset-file PATH`, `--key K`, `[--map project=value or link]`, `[--profile X]`, `[--local]`, `[--yes]`; for one that leaves the Mac, without `--yes` it asks for `y` |
| `cpa dest test ID`, `cpa dest verify ID --draft\|--act` | Sends a test with a random tag via cpad (LaunchAgent); records `dest.changed{verified}` from your answer |
| `cpa dest rm ID` | Remove |
| `cpa dest pending [--expired] [--json]`, `cpa dest send REF [DEST]`, `cpa dest drop REF` | §5.4 |
| `cpa filing send ID DEST` | Send an Inbox entry to any destination (docs/23 P1-G), `origin:"mac"` |
| `cpa filing triage` | Goes through Waiting to send first: ↩ sends to the chosen one, `g` then a letter changes destination, `n` skips |
| `cpa audit --leaves-mac` | What has left this Mac |

### 5.6 Audit

- As docs/14: `dest.decided / delivered / failed / dropped / expired / changed`, recording only lengths and hash prefixes, never the body [src d14:378-400].
- Add `dest.handed{item, dest, kind, via:"url"|"clip", by, bytes, sha8, leaves_mac, prof}`: handed to another app, without knowing whether it was accepted. `fold_pending` treats it as finished, like `delivered`. `cpa audit --leaves-mac` lists both.
- Every `dest.*` carries `prof`; `dest.changed` records changes to `verified` and `handler{bundle, team}`.

### 5.7 Mac app (d24)

- **Card 6 on the Integrations page, "Destinations"**: the top half lists added destinations (letter, kind, cloud / local, verified or not, last send, project usual destinations); the bottom half lists "what this Mac can connect" (`cpa dest presets --installed --json`), each with an **Add** and a **Send a test** button, with the two questions after the test; the last row is "**Other app…**": lists `cpa dest presets --apps --json`; pick an app, a letter, and "does it go to the cloud", and it is added as `app.<name>`. Everything behind it is `cpa dest …`; the app has no app-specific branches.
- **Inbox page**:
  - the "Waiting to send" group at the top (`cpa dest pending --json`);
  - ⌘↩ sends to the chosen destination, ⌥+letter sends to that letter's destination, ⌥D stops it from being sent; Enter still opens details (docs/23 §5.2);
  - meeting rows have no "Send to…";
  - a confirmation on the Mac **only counts for destinations without `k`** (D14);
  - a 5-second hint "Filled into Grok · press Return in Grok to send" or "On the clipboard · press ⌘V in Grok".
- When the app runs `cpa dest add`, stdin is closed (d24 §5.2), so the app shows its own confirm box first, then passes `--yes`.
- Paste executor (R5): `cpa dest exec --kinds paste --json` stays connected; when the app receives `{key, bundle, text}` it brings the app forward, confirms it is frontmost, presses ⌘V, restores the clipboard, and **never presses Return**. It needs the Accessibility permission, so it is a switch in settings, off by default, and this revises d24 §1.2 "only writes cpa commands you click".

### 5.8 Local AI reads it (R3, optional, about 200 lines)

- No mailbox (§2.5). Claude Code, Cursor, Codex, Claude Desktop and the `grok` CLI can already read via `cpa mcp serve` today: docs/15's read-only tools and docs/23's `list_unfiled`, `get_filing_item`.
- R3 does only three small things:
  - `cpa filing ls` and `list_unfiled` gain a `--dest` / `dest` filter, so "the entries I sent to Grok" are easy to find;
  - `cpa mcp setup generic` prints a generic stdio JSON snippet that any local MCP client can paste;
  - move the 4 clients hard-coded in `cpad/mcp/cli.py` into data (one each: `format` is one of `json-mcpServers`, `toml-mcp_servers` or `cli`, plus `where`, `stdio_type`), with three generic render functions; `cpa mcp setup --list --json` generates the tabs in the Mac app. This removes the known exception to "no apps in the code".

## 6. Security and privacy

| Rule | How it holds | Basis |
|---|---|---|
| The body never goes into any process's arguments | Links via `LSOpenFromURLSpec`; `cmd` and `pbcopy` via stdin; apps are brought forward with only a bundle or app location | [meas]; d14:494 |
| Draft only, never sent | `url` only opens prefill links, and counts as draft only once verified; `app` and executors never press Return; Grok's prefill link never sends | [src GB]; docs/13 §9.5 |
| Links cannot be intercepted by another app | When adding, the handler app's bundle id and Team ID are recorded; at send time that app is named to open it, and a mismatch gives `E_HANDLER_CHANGED` | Security review |
| Links can only be draft entry points | Templates may only put the body in a query value; the scheme must be registered by that app; the deny table (§2.3); presets you write are always the execute class | Security, contract and feasibility reviews |
| Leaves the Mac only when you choose | Every custom destination has `leaves_mac`, true by default; adding needs `y` or a confirmation in the app; marked "cloud" on the device; `cpa audit --leaves-mac` | d14:131; §5.6 |
| Speech is never executed automatically | The execute class needs each entry read in full on the device and confirmed with Enter (sha8 check, 40 s / 500 B limit); only verified draft-only destinations are auto-filled at the desk; sync and restart never open an app | d14:216; docs/22 §5.4; ADR 0002 |
| Stolen focus never sends for you | Auto-fill needs 3 s with no key press on the Mac keyboard; the header's first character is `[` | §3.2, §2.3 |
| Pocket presses cannot send | Letters work while locked, but memos recorded while locked are never auto-filled, and the card and "needs you" only appear after unlocking | d14:475; docs/20:670 |
| A "send to X" in the speech does not count | The destination comes only from the key you pressed, never from the body | d14 invariant 3 |
| Meetings stay local | Precheck 0: only today's notes, to-dos, local files; letters do nothing in a meeting; `cpa filing send` refuses; no "Send to…" on the Mac; text handed to another app only uses `memo.text`, never a stitched transcript | docs/23 §3.4 |
| Home and work don't mix | Destinations carry `profiles`, precheck `other_profile`; one device cache per profile; canary tests | docs/18 §3.5–§3.6, §12 |
| The clipboard doesn't spread | Overflow and draft occupancy only use the clipboard when the clipboard destination is on (`app.<name>` chose the clipboard when you added it); this-host-only and no-history markers; cleared after 2 minutes; if that can't be done, you press once | §5.3 |
| No third-party secrets stored | `destinations.json` has no field for secrets; literal parameters and `map` reject values that look like keys; webhook and API keys stay in the Keychain, read by your script | docs/13 §9.5; §2.3 |
| No key injection | cpad runs no AppleScript and needs no Accessibility; pasting only happens in the Mac app, off by default | docs/13 §9.6; [meas] |
| `cmd` can only be added by you, and cannot stop everything | Only `cpa dest add` + `y` can create one; deny table; file pinned; child processes inherit cpad's privacy permissions | §5.3 |
| A confirmation on the Mac cannot stop local programs | A click on the Mac, or `y` piped into `cpa dest add`, can be done by any local program with a shell; but such a program can already do anything you can (open the link directly). So a confirmation on the Mac is only used for the draft-only class; the execute class only trusts the device | docs/23 §3.4 |
| Unknown kinds get the strictest handling | A missing or unreadable `fl` is treated as `lck` | §4.6 |
| Transcripts are untrusted data | Control and Cf characters stripped; links encoded; previews go through `display_safe()`; header says "may contain typos or other people's words" | docs/23 §3.3 |

## 7. Phases and work packages

Always write the failing test first (RED → GREEN → REFACTOR). R1 and R3 only touch the Mac and can be fully tested without the device; R2's firmware and daemon can run in parallel in their own worktrees, with real-device testing (HIL) last and on its own.

### 7.1 Work packages

| Package | Content | Code / tests (lines) [calc] | Depends on |
|---|---|---|---|
| **R0 docs** | This doc; the revisions in §7.2 | — | — |
| **R1-0 manual test (you, 2 minutes)** | The 4 links in §8.2 item 2. The result sets the Grok preset's `effect` and "does it work without a Bot" | — | — |
| **Merge `feat/filing-p1`** | docs/23 P1 (7 commits, 9 conflicts with main) | Already written | — |
| **R1-A revive D5-A** | cherry-pick `b5ad361`, resolve 5 conflicts; register the 31st message in the firmware codec table (no firmware release) | 50 / 50 (plus the existing 5,580) | — |
| **R1-B kinds v2** | Remove `plugin`; decide by `fl`; `url`, `app` specs; names ≤12; `leaves_mac`, `profiles`; `origin:"mac"`; prechecks 0, 4, 9; 12-column truth table | 300 / 550 | R1-A, R1-0 |
| **R1-C links, clipboard, presets** | `UrlAdapter` (cleaning, header, UTF-16 length, template parsing, deny table, pinned handler app), `ClipWriter`, `AppAdapter`, draft occupancy; `presets/v1` (Grok, Claude, Cursor, Codex, Obsidian) and Grok's test vectors; `protocol/dest-preset/v1`; your preset folder; `cpa dest presets/add/test/verify`; the "no apps in the code" test. Acceptance: send a test via the real cpad LaunchAgent | 550 / 700 | R1-B |
| **R1-D send from the Mac** | `cpa filing send`, `cpa dest pending/send/drop`; Waiting to send (held, usual destination, expired); docs/23 §2.1 item 3 revision; triage's ↩ and `g`+letter | 200 / 300 | R1-C; `feat/filing-p1` |
| **R1-E Mac app** | Card 6, "Other app…", the Waiting to send group, ⌘↩, ⌥letter | 400 / 300 (Swift) | R1-D; d24 v1 |
| **R2-A wiring** | D5-I: replace `_downgraded` [src cpad/hub/reduce.py:139-142]; load, reload and expiry in `app.py`; D5-B's `pending.snapshot`, `dest.send`; speech `live` (Mac lock screen, keyboard idle); `dest.get` / `dest.text`; the profile gate | 650 / 850 | R1-B |
| **R2-B firmware F5** | All of §4.6 | 1,000 / 800 | `feat/ux-decisions`, `fix/memo-hold` merged |
| **R2-C real device** | Add "speak and send to Grok" to HIL H5: auto-fill at the desk, back from the pocket, locked, too long, two in a row to the same Bot, text arriving with another page open, profile switch | — | R2-A, R2-B |
| **R3 local AI reads (optional)** | `--dest` filter; `cpa mcp setup generic`, `--list --json`; clients moved into data | 200 / 250 | R1-B |
| **R4 commands** | `CmdAdapter` and its own ADR; shortcut presets (identifier); `file`'s `drop` mode; no webhook | 400 / 550 | R1-B |
| **R5 paste executor** | `protocol/dest/v1`, `cpa dest exec`, Accessibility paste in the Mac app | 250 + 400 Swift / 400 | d24 v1; revise d24 §1.2 |

**The fastest route to talking to Grok**: manual test → merge `feat/filing-p1` → R1-A → R1-B → R1-C → R1-D. Mac only (plus the codec table), about 1,100 lines of code and 1,600 lines of tests [calc]. Once installed: on the device, hold G0 and speak and press `3` as today; on the Mac, ↩ in `cpa filing triage` in a terminal (project 3's usual destination is Grok), or `g`, `x`; Grok has the draft open. Once R1-E exists, ⌘↩ in the Mac app. After R2 merges, `X` can be pressed while talking, with auto-fill at the desk.

```
manual test ─┐
filing-p1 ─┐
R1-A ─► R1-B ─► R1-C ─► R1-D ─► R1-E (also needs d24 v1)
           ├──► R2-A ─┐
           ├──► R3     ├─► R2-C real device
           └──► R4    R2-B (waits for ux-decisions, memo-hold) ─┘
d24 v1 ─► R5
```

### 7.2 Docs to change alongside (merged in the same PR as the code change)

| Doc | Revision | With package |
|---|---|---|
| docs/14, whole doc | A dated banner: `plugin` removed (along with reason `cloud_optin`, the `unavailable` list in precheck 2, §1.2's WhisperWoof v0.7 executor → R5, §4.2's `plugin` fields → `url` / `app` / `cmd` options, §9's `add plugin.*` → `add --preset`); kinds v2 and `fl` (§7.2's `live/cloud/proj` → `fl`); names ≤12; add `url` and `app` to the "leaves the Mac" lists in §8.2, §11.1; §10.4 becomes "unknown kinds are still shown; a missing `fl` is `lck`"; §5.2 adds prechecks 0, 4, 9; §5.3 replaced by this doc's 12-column table; §5.4 invariant 2 replaced by §5.2's new wording (`origin:"mac"`, speech `live`); `uncertain` → `voice`; new event `dest.handed`; recalculate §7.6's 1843 B | R1-A / R1-B |
| docs/13 §9.5 | "Hand to WhisperWoof" → "any executor"; links opened by cpad; a click on the Mac counts as confirmation for the draft-only class (D14) | R1-D |
| docs/23 | §2.1 item 3: held does not count as an effective destination and goes to "Waiting to send"; §3.1, §3.4: `cpa filing send` uses `origin:"mac"`, meetings only go local; line 228 and §6.1 line 392 "excluding cloud plugins" → "any destination you turned on and marked cloud"; §5.2, §3.6: ⌘↩, ⌥letter, triage's `g`; §3.5: `list_unfiled` gains a `dest` filter (R3) | R1-D / R3 |
| docs/22 | §3.1 line 141: the draft-only class (`url`, `app`) also accepts fragments, the execute class still stores them as separate memos; §5.9 "fragments only appear in … four destinations" and "audio and text don't leave the Mac holds", §8 item 4: becomes "text only leaves the Mac through a destination marked cloud that you chose for this entry" | R2-A |
| docs/18 | §7.1 adds a line: destinations carry `profiles`, required when `served` has more than one entry; §12 canary tests add destination links, clipboard and stdin | R1-B |
| docs/15 | §4 how each client connects: `cpa mcp setup generic` and data-driven clients | R3 |
| d24 | §2.4 card 6 and "Other app…"; card 1's labels come from `cpa mcp setup --list --json` (R3); §2.1 Waiting to send, ⌘↩, ⌥letter; §1.2 executor switch (only changed in R5) | R1-E / R3 / R5 |

### 7.3 Implementation notes (2026-10-22, R1-B, R1-C)

Where this differs from the text above, or the text above left it open, this section wins:

- **How links are opened**: within the cpad process, ctypes calls `LSOpenFromURLSpec` (`daemon/src/cpad/dest/macos.py`), with `appURL` set to the app recognised when adding; the body stays in this process's memory, never in any process's arguments, no child processes, no PyObjC, no permissions. Before opening, `LSCopyApplicationURLsForBundleIdentifier` finds the app, `SecStaticCode` (without resource validation) checks its signature and Team ID, and `Info.plist` is read to confirm the scheme is still registered; a mismatch gives `E_HANDLER_CHANGED`, a missing app gives `E_DEST_MISSING`, and nothing is opened. Apple's own apps have no Team ID, recorded as `null`.
- **Clipboard**: also ctypes to `NSPasteboard` (`pasteboard.py`): `prepareForNewContentsWithOptions:` with "this host only", the body tagged with the `ConcealedType` and `TransientType` markers, cleared after 2 minutes only if `changeCount` has not changed. The fallback to `pbcopy` (body via stdin) only happens when someone just pressed (`by` is not `auto`); otherwise `E_CLIP_PLAIN`. **The native write has not been run against the real clipboard** (tests never touch the real clipboard); only that these methods exist and that reading `changeCount` works was verified; it needs one manual test in R1-C's acceptance.
- **`effect`**: the `url` presets shipped with cpad **do not set** `effect` (= unknown), so once added they are `lck`, and each entry must be confirmed on the device; `cpa dest test ID --send` sends a "测试 NNNN" ("test NNNN") with a random tag, and only after you answer "it appeared, not sent automatically" and run `cpa dest verify ID --draft` does this destination become draft only (`lc`). `verify --draft` requires that a test was sent since this destination was added (looks for a `dest.handed` in `events.jsonl` whose `item` starts with `test-`). Presets that say `act`, and your own presets (`$CPA_HOME/presets/*.json` or `--preset-file`), are always `act`. The `"effect": "draft"` in the Grok preset example in §2.3 is not written into the file shipped with cpad until you have done the manual test in §8.2 item 2.
- **Where the test is sent from**: `cpa dest test ID --send` now opens the link from the `cpa` process itself (same adapter, same pinning checks), not yet through the cpad LaunchAgent; "can the LaunchAgent bring Grok's window to the front" is still [todo] in R1-C's acceptance.
- **Header**: `[Cardputer 语音·可能有错字或别人的话·#project] body`, typed is `[Cardputer 打字·#project]`; control characters become spaces, Cf characters are stripped; length in UTF-16 code units. Every link in `presets/v1/grok-bot.vectors.json` passes Grok's own checks (printable ASCII, no `#` or `\`, ≤2048, no control characters after decoding, empty parameters removed); about 200 Chinese characters fit, and about 220 overflow to the clipboard.
- **Presets shipped with cpad**: `grok-bot` (`url.grok`, letter `x`), `cursor` (`u`), `claude` (`k`), `codex` (`b`, the Codex in ChatGPT.app), `obsidian` (`file.ob`, `o`, needs `--root vault`). The length limits for Cursor, Claude and Codex are estimates from §2.4 [todo].
- **Contracts**: `protocol/dest-preset/v1/` (schema, valid and invalid vectors, README); `dests.snapshot` rows carry `fl` (README item 24).
- **Not done yet** (from R1-D on): `cpa filing send`, `cpa dest pending/send/drop`, usual destinations (`usual`), Waiting to send; wiring into the hub (R2-A). Today `origin:"mac"` only exists in `decide()` and the truth table; no command sets it yet. (2026-10-22) R1-D did the first four; see §7.4; wiring into the hub is still R2-A.

### 7.4 Implementation notes (2026-10-22, R1-D: sending from the Mac)

- **Commands** (public contract `protocol/dest-cli/v1/`: six documents `pending`, `sent`, `dropped`, `usual`, `tested`, `error`, with vectors and `check_dest_cli.py`, run in CI):
  - `cpa dest pending [--expired] [--json]`: Waiting to send. `why` is `held` (held or failed to send, `st` is `hold` / `failed`, failures carry `code`), `usual` (§3.4's suggestion), `expired` (only with `--expired`, last 7 days). Held first; oldest first within each group.
  - `cpa dest send REF [DEST] [--by cli|app] [--json]`: with no DEST, sends to the preselected one (the held entry's original destination, or the project's usual destination).
  - `cpa filing send ID DEST [--by cli|app] [--json]`: any memo in the Inbox.
  - `cpa dest drop REF [--by …] [--json]`: `dest.dropped`; also works on "suggested" rows, which are then no longer suggested.
  - `cpa dest usual [PROJECT [DEST|-]] [--json]`: the project's usual destination, stored in `usual` in `destinations.json`; only destinations with an executor on the Mac (`clip`, `file.*`, `url.*`, `app.*`), and the project must be registered on this Mac.
  - `cpa dest test ID --send [--json]`: in a terminal, after sending it asks "did it appear?" and "was it sent automatically?", then runs `verify --draft` or `--act` from the answers; if it did not appear and the preset can pick a Bot, it asks you to paste the Bot's link or id, stores it as `map["*"]` and tests again. `--json` asks nothing (the Mac app asks itself) and prints `tested`.
  - `cpa dest map ID PROJECT=value or link… | PROJECT=-`: set a Bot per project for `url.*`.
  - `cpa filing triage`: goes through Waiting to send first (↩ send, `g` then a letter to change destination, `d` don't send, `n` skip), then the Inbox; Inbox memos can also be sent with `g` plus a letter.
- **Sending rules**: the body only comes from `memo.text` (typed entries from the body of `item.received`), never argv; `decide(…, origin="mac")` runs first, and a refusal writes nothing (`E_NEEDS_DEVICE`, `E_NO_TEXT`, etc.); under the lock it writes the `dest.decided` that starts delivery (`E_BUSY` if the same entry is being sent), then hands it over, then records `dest.handed` / `delivered` / `failed`. No retry, no replay; failures stay in Waiting to send.
- **Where this differs from the text above**:
  - `cpa filing send` does not accept `daily`, `todo`, `queue`, `session` (`E_BAD_ARG` `dest`): handing to AI still goes through `cpa filing hand` (docs/23 §3.4 updated).
  - A destination that may act by itself (`fl` contains `k`, for example an untested `url.grok`) **writes nothing** when sent from the Mac, replying `E_NEEDS_DEVICE` with a hint to test first; it does not leave a "waiting for device" entry in Waiting to send, because before R2 the device cannot confirm it, and leaving one would only get stuck.
  - One fill per Bot per 30 minutes (D10) also holds across processes: `dest.handed{via:"url"}` also records `to` (the first 8 hex digits of the sha256 of the Bot value, never the value itself), and a new `cpa` process reads it from the log. The test entry stops counting once answered (see "Changes after review" below).
  - In text handed to another app, besides control characters, U+2028 / U+2029 also become spaces (still one line).
  - Clearing the clipboard after 2 minutes: the `cpa` process does not live 2 minutes, so it hands off to a detached `python -m cpad.dest.clip_clear <changeCount>`, whose only argument is that number; it does not read the clipboard and only clears it if you have not copied anything since.
  - When a held entry fails to send from the Mac, `fold_pending` gives it a skeleton without the body, so Waiting to send still lists it; on resend the body is read again from `rec.result`.
- **Changes after review (2026-10-22)**:
  - An error while sending (ctypes or framework load failure) records `dest.failed{E_INTERRUPTED}`; when interrupted by ^C or SIGTERM (the Mac app's 30-second timeout) it writes `dest.failed{E_INTERRUPTED, retryable:false}` before exiting; a Mac-sent `running` that did not even get that far and is over 60 seconds old is listed as failed by `pending.settled()`, and can be sent again or dropped. Before, these cases stayed `running` forever, vanished from Waiting to send, and every resend gave `E_BUSY` until cpad restarted.
  - An entry already handed to (or written into) this destination replies `E_ALREADY` on resend; `--again` sends it again (Mac app: press once more); one that was still in Waiting to send when read, but found under the lock to have been sent or dropped by another `cpa`, replies `E_UNKNOWN_ID` instead of opening a second time.
  - cpad's `resume()` does not retry or downgrade Mac-sent entries (`origin:"mac"`, or a skeleton without body): those still `running` at startup are recorded as `E_INTERRUPTED`, and failures stay in Waiting to send for someone to resend.
  - Stricter app recognition: the signature must satisfy `anchor apple generic` (a certificate issued by Apple; ad hoc and self-signed don't count), and one without a Team ID is only accepted if it is Apple's own (`anchor apple`); when one bundle id has several copies, the first that passes every check is used; the `handlers` in `deny.json` apply to `--app` and to every preset (case-insensitive).
  - One-off test: only a test handed over via a link lets `verify --draft` take effect; if this Bot was filled by a real memo in the last 30 minutes the test is not handed over (`E_DRAFT_BUSY` in `tested`); after answering (`verify`) the test entry no longer occupies the draft, and the first real memo goes via the link as usual. The terminal questions are now in Chinese, as in the Mac app.
  - `cpa dest ls --json` url rows gain `takes_map` (the preset can pick a Bot), and `cpa dest presets --json` has it too; text output for url rows shows the current state: `draft` (tested), `untested`, `acts`.
  - A memo with a lone surrogate code point (allowed by JSON) has it replaced with U+FFFD before being handed over, instead of failing while building the link or in `pbcopy`; `clip_clear` runs with `python -I` from `/`, so it never imports a `cpad/` in the current directory.
- **Not yet verified** (still [todo], tested by hand in R1-C's acceptance): real clipboard writes, opening links from the LaunchAgent. Today both `cpa dest send` and `cpa dest test --send` open links in the `cpa` process, not yet through cpad.

### 7.5 Implementation notes (2026-10-22, R1-E: Mac app)

- How it was done: see "What R1-E built" in docs/24 §2.4. Only through the public `cpa dest … --json` and `cpa filing send --json`; the Swift code contains no app names (pinned by `RulesTests`); names, letters and descriptions all come from cpa. Contract copy `mac/Fixtures/dest-cli-v1` (`aae8694`); CardputerCoreTests runs all 31 of its vectors.
- Differences from §5.7: the two test questions are asked in place on the card, not in a pop-up (visible in screenshots, blocks nothing else); "Other app…" and changing letters were not done; the link for `cpa dest map` is the only value in argv that you paste (printable ASCII only, no spaces, ≤512, never a memo body).
- The device's `X` (R2), opening links via cpad, and real clipboard writes are still [todo].
- Changes after review (2026-10-22): the one-off test asks per destination (an app only asks "did it paste?"; for your own presets "not sent" does not record draft either; only when the preset can pick a Bot does "didn't appear" ask for a link, and the wording does not hard-code "Bot"); every code cpa dest can return has Chinese words and a next step (pinned by the daemon's `test_dest_codes_have_words.py`); it no longer says things like "confirm on the device" that R1 cannot do, and a grey link that only lacks a test goes straight to Integrations → Destinations to start testing with one click; the clipboard line stays until "Got it", with "Copy again" (`--again`); with `E_ALREADY`, pressing once more sends again; after grouping by digit into a project with a usual destination, its row in Waiting to send is selected (digit, then ⌘↩: two presses); for a memo without a project, "Send to…" says "sent without #project"; the "Add" box asks for the Bot's link (`cpa dest add --map`). Contract copy updated to `8eddf6a` (33 vectors). Not done: an "Sent to Grok · 14:31" mark on Inbox entries (needs a new data source).

### 7.6 Implementation notes (2026-10-26: change of direction; projects are groups)

- **Requirement** (2026-10-26): sending speech straight to a coding agent is not the right first step; first bring it into our own app, then place it into different agents by hand. Different projects can be created on the computer, and while speaking, pressing a digit on the device groups the memo into a project. This is the first step.
- **On hold**: the "send straight when you finish speaking" part of R2-A's wiring (auto-fill a draft at the desk, the device's `X`, `dest.send`), R2-B firmware F5 and R2-C real device are not scheduled. R1 is unchanged: speech first goes into the "Inbox", and you pick a destination in the Mac app and send with ⌘↩; a project's usual destination (D21) is still only a preselection. The old path of opening a session from a queued item on the device board still works (only for projects with a linked code folder, always plan mode, docs/22 §5.4), but speech fragments never open a session by themselves (docs/22 §5.4's 2026-10-26 addendum).
- **Projects are groups** (D24): `cpa project add [--key K] [--slot N] [--folder dir] -- name`; the name can be Chinese (at most 24 letters or 8 Chinese characters); the key comes from the English letters and digits in the name (≤12), or `p<number>` if there are none; without a number the first free slot is used. `rename`, `move` (swaps with the project in that slot), `folder dir|--none`, `rm` (recordings and queued items keep the key). All take `--json`, printing filing/v1's `projects` document (with `linked` and `folder` added: the last component of the folder path, never the whole path) or `error`. The old form `cpa project add KEY path --slot N` still works.
- **Only projects with a linked folder** get hooks, project detection by cwd, and opening sessions from the device; opening a session from the device for an unlinked project replies `E_UNKNOWN_PROJECT` (no firmware or protocol change). The device's 1–9 still come from the projects snapshot, and unlinked projects are `ok:true`.
- **Mac app**: a new "Projects" page (docs/24 §2.6): 1–9 as on the device; new (first free slot, or "Put here" on an empty row), rename (or double-click the name), click the number to pick a new one or drag ≡ onto another row to change number (both swap), Link folder… / Unlink folder, delete (deletes only on the second click). The Inbox's 1–9 buttons and descriptions use these names. The UI is in Chinese and English (docs/24 §2.7).
- **Added after review (2026-10-26)**: a deleted project's key is never given to a new project—keys still carried in history (recordings, filings, queued items, usual destinations) are avoided automatically on create (delete chores `p1` and create reading, and reading gets `p1-2`, so it does not inherit the chores recordings); a hand-written `--key` that collides with history needs `--reuse` (saying explicitly "give these recordings to it"). `rm` also forgets that project's usual destination (records `dest.changed op:usual id:null`). The old form (a second positional argument) combined with `--key` or `--folder` is always refused (field `argv`); names with spaces need quotes.
- **Follow-up (firmware, not scheduled)**: opening a session on the device for a project without a linked folder replies `E_UNKNOWN_PROJECT`, and the firmware now shows "No project" (`failure_text` in `runtime_outbox.cpp` only looks at code and does not show `why`), even though the project exists. Next time the firmware is touched, change this text to "project has no linked folder" (or "missing / no linked folder"); the protocol does not change.
- An old cpad that reads a projects.json with `root:null` rejects the whole file (the log has `projects.json rejected`): after installing the new version, restart once with `cpa service install`.

## 8. Decisions, and what needs your call

### 8.1 Decisions

If you don't reply, the defaults are used.

**What you will notice**

| # | Decision | Default (recommended) | Alternatives | Why | See |
|---|---|---|---|---|---|
| D1 | How to connect "every app" | Generic kinds + presets (pure data); every app can at least use `app.<name>` (clipboard + bring forward, you press ⌘V), added with one click in a list; apps with a prefill link save a press; tests make sure no app bundle id or scheme is in the code | One adapter per app | The requirement; adding an app needs no code and no release | §2.1 |
| D3 | Grok Bot's main route | `url` prefill link, draft only | Local MCP; routine webhook; paste | MCP runs in its cloud; webhooks are in the cloud too; paste needs the Accessibility permission | §3 |
| D4 | Grok's letter | `X` on the device; ⌥X in the Mac app; `g` then `x` in the terminal | Any of `b c f j k o u y` | `G` is reserved for the picker; single letters on the Mac are already taken by `j k u x y` | §4.1 |
| D5 | How it is sent after speaking | With the device connected, neither side locked, no key pressed on the Mac keyboard for 3 s, and the destination verified as draft only: **fill the draft directly**; otherwise it goes to Waiting to send, ⌘↩ on the Mac or Enter on the device | Press Enter on the device for every entry | Grok never sends without Return, so Enter on the device would be a second confirmation of the same thing; saves one press per entry | §3.2 👤 |
| D6 | How the project travels | A header on the same line, `[Cardputer 语音·可能有错字或别人的话·#project] body`; with a Bot per project, the Bot separates them | Send only the body | Grok rejects line breaks; a leading `[` avoids the agent input box's special characters; the receiving agent knows it is a speech draft | §2.3 |
| D8 | Enter after typing with Opt+X | Fills the draft directly when connected to the Mac (only for verified draft-only destinations) | Also confirm | Enter itself is the confirmation, and the target app is named | §5.2 |
| D9 | Too long (about 1 minute or more) | Full text to the clipboard (this host only, no history, cleared after 2 minutes) + bring that Bot forward + you press ⌘V; with the clipboard destination off it stays in Waiting to send | Truncate | Nothing lost; if you turned the clipboard off, it is not used behind your back | §2.3 |
| D10 | One draft per Bot | Filling the same Bot again within 30 minutes goes via the clipboard with the hint "Grok still has the previous one" | Send one at a time and rely on you remembering | Grok silently swallows the second one; a wrong guess costs one ⌘V, not guessing loses words | §3.6 |
| D18 | Meetings | Only today's notes, to-dos, local files; letters do nothing in a meeting | Meetings can be sent too | They contain other people's words | §5.2 |
| D20 | What first | Manual test → merge `feat/filing-p1` → R1 Mac only (Waiting to send, usual destinations) → R2 device | Firmware first | R1 doesn't wait for firmware or the real device, so it is usable soonest | §7 |
| D21 | Project usual destinations | Do it (R1-D): project 3's memos have Grok preselected in Waiting to send, one ⌘↩ sends; never sent automatically | Don't; rely only on R2's `X` | No need to wait for firmware; the real choice is still the press you make | §3.4 👤 |
| D23 | Speech straight to an agent (2026-10-26) | **On hold**: speech first goes into the Mac app's "Inbox", and you send it by hand to different agents on the Mac (R1's ⌘↩, ⌥letter); R2's direct send from the device is not scheduled | Do R2 as planned | The requirement: bring it into our own app first, then place it by hand | §7.6 👤 |
| D24 | What a project is (2026-10-26) | A named group created on the Mac, grouped on the device with 1–9; a code folder is optional, and only a linked one gets hooks and session opening | A project must be a code folder | The requirement: create projects on the computer, group by pressing digits while speaking | §7.6 |

**Engineering internals**

| # | Decision | Default (recommended) | Alternatives | Why | See |
|---|---|---|---|---|---|
| D2 | Kinds | Add `url`, `app`; remove `plugin`; `cmd` in R4; `paste` can take a name; no `box` | Also add an `http` kind | `http` would need cpad to store secrets; `app` already gives every app a route | §2.2 |
| D7 | Who opens links, and what is recorded | cpad via `LSOpenFromURLSpec`, naming the app recognised when adding (bundle + Team ID); records `dest.handed` | The Mac app with NSWorkspace; record `delivered` | No waiting for the Mac app, no permissions, body never in arguments; other apps cannot grab the scheme; cpad cannot see whether the app accepted it | §5.3, §5.6. 2026-10-22 implementation note: in R1, sends from the Mac are opened the same way by that `cpa` process (same Team ID check, body never in arguments); cpad opening them waits for R2 to wire it into the hub (§7.4). |
| D11 | Mailbox `box` | Not done; R3 only adds a `dest` filter to the existing read-only tools, `cpa mcp setup generic`, and moves clients into data | Mailbox, `ack`, 7-day expiry | The reading half exists; "take" is off by default; it needs you to say something again, not "arrives when you finish speaking"; saves about 600 lines of code and tests | §2.5, §5.8 |
| D12 | Can a link act by itself? | Preset field `effect`; missing means `act`; only presets shipped with cpad with a reviewed template can say `draft`, and it only takes effect on each Mac after a test where you answer "not sent automatically"; yours are always `act` | Treat all as draft only | Whether it only fills a draft depends on the target app, not the mechanism | §2.3 |
| D13 | `cmd` | Do it (R4), with its own ADR; deny table, file pinning, identifier, full text read on the device for each entry | Don't | Shortcuts and every CLI come in through this one door; stdin can be run as code too | §5.3 |
| D14 | Does a click on the Mac count as confirmation? | For destinations without `k`, yes (`origin:"mac"`, can send without the device); the execute class only trusts the device | Always go back to the device | Draft only executes nothing; local programs can fake clicks | §5.2 |
| D15 | Unknown kinds on the device | Still shown, behaviour from `fl`; a missing `fl` is `lck` | Hidden (d14:473) | Adding kinds later needs no firmware release; when in doubt, confirm more | §4.6 |
| D16 | Name length | ≤12, matching main | docs/14's 16 | main's firmware would reject them | §2.2 |
| D17 | `feat/destinations` | Cherry-pick, then change in small steps | Rewrite | Keeps the 96-cell truth table and the review fixes; 5 conflicts, all tests pass | §5.1 |
| D19 | Routes not taken | Nothing in §2.5 is done | Do Claude Code channels first | All break the local, revocable or no-injection principles | §2.5 |
| D22 | Home and work | Each destination carries `profiles`; precheck `other_profile`; one device cache per profile; `dest.send` only sent when the current profile ∈ ok(H) | One destination table for both profiles | When one Mac serves two profiles, words from one profile would otherwise go into the other profile's Grok | §4.5 |

### 8.2 Your call

1. **In Grok, one Bot receives everything, or one Bot per project?** 👤 Default one, with `#project` in the header to tell them apart. For one per project, fill in the Bot for each project in §3.1 step 4: copy that Bot's link in Grok and paste it.
2. **Please try 4 links by hand** (copy each line into a terminal and press Return; we have never sent anything to any app). 👤 For each, tell me three things: **did the text appear, did a confirmation card pop up, was it sent**.
   ```
   open 'grokbot://app/v1/prompt?text=%E6%B5%8B%E8%AF%95'
   open 'grokbot://app/v1/prompt?text=%E6%B5%8B%E8%AF%95%0A%E7%AC%AC%E4%BA%8C%E8%A1%8C'
   open 'grokbot://app/v1/prompt?text=%E5%86%8D%E4%B8%80%E6%9D%A1'
   open 'grokbot://app/v1/prompt?text=%E6%B5%8B%E8%AF%95&id=wrong-id-123'
   ```
   Link 1 is "测试" ("test"); link 2 has a line break and should do nothing; run link 3 while link 1 is still unsent, to see whether the old draft gets replaced; link 4 has a wrong Bot id. If link 1 shows no text, copy a Bot's link from Grok and paste it to me.
3. **D5: after speaking at the desk, fill Grok directly?** 👤 Default yes: saves one Enter per entry, and Grok never sends without Return. Cost: Grok comes to the front by itself about 3 s after release, also while screen sharing or presenting (not while you are typing on the Mac; then it stays in Waiting to send). If no, one more press per entry on the device or the Mac.
4. **While you are away from the Mac, should Grok act on its own in the cloud (routine webhook, §3.7)?** 👤 Default no; if yes, it goes into R4, only for typed entries or speech read in full, and the connected Bot may not have tools that act.
5. **Project usual destinations (§3.4)?** 👤 Default yes: set project 3 to "usual: Grok", and memos where you only pressed `3` go into Grok with one ⌘↩ on the Mac, usable in R1 without waiting for firmware.
6. **Which app after Grok?** 👤 Default: Cursor and Claude (prefill links, each draft-only after its own test), Obsidian via `file`. Claude Code as today with `Q` / `S`. Any other app is one click in "Other app…" any time.

## 9. Disagreements between reviews, and trade-offs

The fixes from the four reviews (user, privacy and security, contract and architecture, feasibility) are all written into the sections above. Below are the places where they disagreed or had to be combined.

| Disagreement | Chosen | Why |
|---|---|---|
| The origin of a Mac confirmation: `mac` (contract) or `here` (feasibility) | `mac` | docs/23's `env.link:"here"` already means "connected to this Mac while recording" |
| Sending a `session` from the Mac: queue only (contract, docs/23 §3.4) or `E_NEEDS_DEVICE` (feasibility) | Queue only; the session is still opened on the device board | It only writes a queue item and executes nothing; consistent with docs/23's settled `send ID session` |
| Mailbox `box`: add `--box`, profiles, per-mailbox `ack`, trust marker (security, contract) or don't do it (user) | Don't; the security review's conditions are written into §2.5 as prerequisites if it is ever done | The reading half exists; "take" is off by default; the user's goal is "arrives when you finish speaking", not "go fetch it"; saves about 600 lines of code and tests |
| "No apps in the code": exempt `mcp/cli.py` (user, feasibility) or move the clients into data (contract) | Both: the test with §2.1's scope can be green today, `mcp/cli.py` is recorded as a dated known exception, removed after R3 moves it into data | The test is not red on day one, and the principle is kept in the end |
| D5: Enter on the device (first draft) or fill directly at the desk (user) | Fill directly at the desk, with four gates: verified draft only, neither side locked, no key on the Mac keyboard for 3 s, the Bot has no occupied draft | Grok never sends without Return; the user review is right that Enter on the device is a duplicate confirmation. The keyboard gate was added this time: when focus is stolen, a Return you press elsewhere would send the draft |
| How long a Bot's draft counts as occupied: 10 minutes (user) or 30 minutes (feasibility) | 30 minutes | Guessing "occupied" costs one ⌘V; guessing "free" silently loses words |
| Can a link act by itself: per-Mac `draft_only` test (user) or an `effect` field, untested treated as `act` (contract, security) | One field `effect`, missing means `act`; only presets shipped with cpad with a reviewed template can say `draft`, effective on each Mac only after a test answered "not sent automatically"; yours are always `act` | The two asks don't conflict: the preset says "can it", the test on this Mac says "it really does" |
| Overflow to the clipboard: Enter at the desk too (security) or just one more ⌘V (user) | Just one more ⌘V when the clipboard destination is on and the "this host only, no history" markers can be written; if they can't (fallback to `pbcopy`), you press once | The security review's worry was clipboard history and Universal Clipboard, which the markers block; only ask more when they can't |
| Clipboard: clear after 2 minutes (user) or add markers (security) | Both, plus "this host only" (`currentHostOnly`) | Each blocks a different kind of spread |
| `map` values: ≤40 and reject high entropy (security) or ≤128 (feasibility) | ≤128 (per Grok's rule), reject known key prefixes, no entropy check | Bot ids are random strings themselves, so an entropy check would reject real ids; the prefix table catches keys pasted in the wrong place |
| How the project travels: `#project` on the same line (feasibility) or a fixed header (security) | Combined: a header on the same line containing `#project`, then one space and the body | Solves both: no line breaks (Grok rejects them), no special character at the start, and the receiver knows it is speech |
| Expiry: return with the choice (user) or only entries without a project return (contract) | Entries without a project return to "new" with "Suggested: Grok"; entries with a project don't go back to the Inbox and are tagged "not sent" | `dest.expired` already records `dest`; entries with a project already count as filed, so listing them again would duplicate |
| Sending in Waiting to send with Enter (user) or ⌥X (feasibility) | ⌘↩ sends to the preselected one, ⌥letter to a named one; Enter still opens details | The same key meaning different things in different groups leads to wrong presses; single letters on the Mac are already taken |
| `cmd` and webhooks: scale down (user) or harden (security) | Both: `cmd` stays in R4, hardened per the security review; webhooks get no preset or sample, only a question | The user review said "don't do too much now", the security review said "how to do it when you do" |
| Which schemes `url` may use: those registered by that app (security) or only the apps in presets shipped with cpad (feasibility) | Both: the scheme must be registered by that app; only presets shipped with cpad with a reviewed template can be `draft`; yours are always `act` | You can still connect any app; ones we haven't read just need confirmation per entry |
| What to record after handing over: `dest.handed` (feasibility) or `dest.delivered{via}` (security) | `dest.handed`, with `via` | States clearly "handed over, unknown whether accepted"; `via` keeps link and clipboard apart |
| Dependency: cpa-stt not merged (feasibility, at review time) | Already on main (`d330465`), off by default; you just run `cpa-stt use` once | #62 merged after the review |
| How many links to test by hand: 1 (user) or 4 (feasibility) | 4, three questions each | Shows the three silent failures (line break, old draft, wrong id) in one go |

## Appendix: evidence index

- Local branch `feat/destinations` (`b5ad361`): d14:44-64, 80-100, 122-142, 165, 176-218 (216 invariant 2), 242-257, 263, 283-358 (350 line length), 378-407 (386 `dest.expired`), 450, 473, 475, 494; `dst/model.py:16, 29-33, 44-63, 74-76`; `dst/policy.py:17, 40-42, 53, 60-67 (`_live`), 70-82`.
- `feat/filing-p1` (`e76330f`): `cli/triage.py:236-237`; 9 conflicts with main [meas `git merge-tree`].
- main (`d330465`): `cpad/mcp/cli.py:18-63`; `cpad/hub/reduce.py:125-142`; `protocol/limits.json:72`; `protocol/README.md` item 20; `docs/13-assistant-v2-plan.md:499-518`; `docs/15-local-data-access.md` §3.4, §3.6–§3.8, §3.11, §3.12, §4; `docs/18-context-profiles.md` §3.5, §3.6, §7.1, §12; `docs/20-ux-review.md:631, 670`; `docs/22-voice-captures.md:141, §4.13, §5.4, §5.9, §8, :726`; `docs/23-mac-inbox.md` §1.3, §2.1, §3.1, §3.4, §5.2; firmware `input/keymap.cpp:99-104`, `rec/rec_meta.h:37`, `shell/rec_pages.cpp:257-258`, `shell/rec_overlay.cpp:109`, `shell/runtime_rec.cpp:125-130`, `module_api/canvas.h:110-116`, `test_codec_vectors/test_main.cpp:55`; `input/keymap.cpp:111-112` on `fix/memo-hold` (`RecLatch`).
- Other branches: d24 §1.2, §2.1, §2.4, §5.2 (`wt-mac`).
- App bundles [src]: Grok Bot 0.75.0's `app.asar`: the link routing table in `main-core.cjs` (`/v1/agent?id=`, `prompt`'s `text`, `id`), `ob()` / `Fg()` (query values with control characters always rejected), `NB()` (link ≤2048, printable, no `#` or `\`), `Tu` (Bot id ≤128), `Rle`, `Ale=1e3`, `CU=2e3`; `kept-draft`, `no-main-agent`; `/automations/webhook/` in `main-app.cjs`; the MCP form fields, the "Grok Bot's computer" text, the "run on this computer" setting. Cursor 3.24.18 `extensions/cursor-deeplink/dist/main.js`, `extensions/cursor-mcp/dist/main.js`; Claude 3.1623.4 `app.asar`; ChatGPT.app 26.1007's `codex://threads/new` (`q` → `prompt`) and its bundled codex; strings in the Claude Code 2.1.306 binary. Read-only, using `asar_grep.py` and `asar_cat.py` in `wt-route` (not in the repo).
- This Mac [meas] (macOS 26, read-only or in temporary folders, since deleted): `shortcuts run --help` (`-i <path>`, takes an identifier); the argument length limit and `ps` showing arguments; `LSOpenCFURLRef` and `LSCopyDefaultApplicationURLForURL`; AppleScript injection and JXA reading stdin; `codesign` on uv's python; `cmd` stdin, timeout, `killpg`; 8 processes appending concurrently; `launchctl print` on cpad's session; test counts after cherry-picking `feat/destinations` (feasibility review, temporary clone deleted).
- External [res]: Hookdeck "Using Hookdeck with Grok Bot"; two Cursor forum posts (webhook address and key not shown); Claude Code channels reference; Obsidian URI and CLI help; Notion "connect Custom Agents to MCP"; AppKit `NSPasteboardContentsCurrentHostOnly`; the nspasteboard.org `ConcealedType` / `TransientType` conventions.
