**English** · [中文](14-destinations.zh-CN.md)

# 14 · Capture destinations (#24): one key goes straight to one place, bindings configured on the Mac

> Status: **design final, to be implemented** · Date: 2026-10-21 · Role: architecture (destinations) · Target version: v0.5.0 (the #24 milestone), developed in parallel with v0.4.1 and wired in after v0.4.1 merges.
> Basis: #24's requirements and comments; docs/13 §9 (all), §11.2, §12, §13, §18 items 6 and 7; docs/13's capture destination research draft (2026-10-21, not in the repo, "the research draft" below); 05 §6.1–§6.9, §7.3, §7.6; the daemon's `notes/render.py`, `notes/writer.py`, `hub/*`, `queue/fold.py`, `events/log.py`, `events/records.py`, `cli/main.py`, `config/settings.py`, `config/files.py`, `protocol/{codec,specs,validate,messages}.py`, `recordings/route.py`, `logging/redact.py`; firmware `lib/input/keymap.cpp:43-100`, `lib/protocol/messages.h`, `test/core/test_codec_vectors/test_main.cpp:56,76`; `protocol/schema/v0/*`, `protocol/tools/*`; `.github/workflows/ci.yml`.
> Tags: **[src]** code or docs read · **[calc]** calculated with a script · **[asm]** design assumption, can change · **[todo]** to verify with tests or on the real device · **👤** needs you.
> Scope: destinations only. No device connected, no firmware run, no code changed. Screen sketches are drawn for 240×135, about 40 ASCII characters or 20 Chinese characters per line.

> **Revision (2026-10-22, docs/25 R1-B, #61)**: the kinds and policy in this doc have been superseded by [docs/25](25-route-to-any-app.md) §2.2, §5.2; when reading the code, go by that doc:
> - **`plugin` removed** (along with reason `cloud_optin`, §1.2's WhisperWoof executor, §4.2's `plugin` field, §9's `add plugin.*`); new kinds `url.<name>` (prefill link) and `app.<name>` (clipboard + bring the app forward, you press ⌘V), both added from presets (pure data) shipped with cpad: `cpa dest add --preset P` / `--app BUNDLE`; `paste` can take a name.
> - **The policy reads `fl`** (`l` live, `c` leaves the Mac, `p` needs a project, `k` each entry confirmed on the device) instead of hard-coding `ARCHIVE_KINDS`, `live`, `session`; §7.2's `live`, `cloud`, `proj` on the wire become one `fl` string; §10.4 becomes "unknown kinds are still shown; a missing `fl` is treated as `lck`".
> - **Names ≤12 characters** (§2.2's 16 would be rejected by main's firmware); §7.6's worst-case line length goes from 1843 B → 1507 B.
> - **Prechecks** add 0 `meeting`, 4 `other_profile`, 9 `too_long`; §5.3's table is replaced by docs/25's 12-column table (one more origin `origin:"mac"`: someone confirmed this entry on this Mac); §5.4 invariant 2 is replaced by docs/25 §5.2's wording; `uncertain` on queued items → `voice` (docs/22 §5.4 item 8).
> - Every destination carries `profiles` (docs/18), and every `dest.*` audit line carries `prof`; new event `dest.handed` (handed to another app, unknown whether it was accepted). Add `url` and `app` to the "leaves the Mac" lists in §8.2 and §11.1.

## 0. One-page summary

1. **What you want** (#24): different shortcut keys send a capture straight to different places, without choosing each time; which key goes where is configured on the Mac. Rules already settled: when online, a bound destination **runs at once**; when offline, it is stored and delivered on sync, but **"live actions"** (copy to the clipboard, paste at the cursor, open a session) wait until connected and need one Enter on the device; offline captures **never open a session**; cloud destinations are **turned on one by one**, and every delivery is audited.
2. **Model**: each capture = content + destination id. There are 8 kinds of destination (§2): today's notes, to-do checkbox, project queue, open session, clipboard, paste at cursor, append to file (e.g. an Obsidian inbox), cloud plugin. Policy is set per kind, not per destination.
3. **Keys**: N T Q S are fixed (today's notes, to-do, queue, session); the 9 letters b c f j k o u x y can be bound freely on the Mac (default C = clipboard); the other letters are reserved for apps and the system (§3). One letter, three uses: global Opt+letter = take a note that goes there; Opt+letter on the capture page = change the destination; hold G0 then press a letter = this speech is saved there.
4. **Configuration**: `$CPA_HOME/destinations.json` (0600, `version:1`, strictly validated, **no secret can fit in it**), changed only with `cpa dest ls/add/rm/bind/test` (§4, §9). When plugged into the Mac, a `dests.snapshot` is pushed to the device, which caches it and follows those bindings even offline (§7).
5. **Decision**: the pure function `decide(capture, dest, online, origin)` (§5). Unknown, off, not in this version, cloud not confirmed, a default selection that is not a local archive, no text, needs a project but has none—all **fall back to today's notes**, never to paste or execute. The clipboard only runs for "online + key just pressed + typed" or "confirmed on the device"; `decide()` never opens a session, it only queues.
6. **File first, then deliver**: every capture first writes its today's-notes line as before (a checkbox for to-dos), then delivers per the decision. Failed deliveries, and ones waiting for confirmation, go into **pending** in the device's "needs you" (`pending.snapshot`, D5-B): Enter runs it, Opt+D drops it, and after 24 hours untouched it expires; the today's-notes line always remains.
7. **Audit**: `events.jsonl` gains `dest.decided / delivered / failed / dropped / expired / changed`, **without the body**, only the byte count and the first 8 hex digits of sha256. `cpa audit --leaves-mac` shows at a glance "what has left this Mac" (§8).
8. **Protocol additive only, `v` still 0, no new error codes**: this branch (D5-A) **delivers just 1 new message**, `dests.snapshot` (≤12 entries, worst case 1843 B). `pending.snapshot` (≤8 entries) and `dest.send` (replies `ack` / `error`) are the follow-up work package **D5-B**, merged in the same batch as their own firmware registration (F5-0b); until then, offline clipboard entries and failed deliveries cannot reach the device's "needs you" and cannot be confirmed on the device. The `note` body and memo `file.items` gain optional `how`, `live`; the device's `revs` gains `dests` (D5-B adds `pending`); the host's `hello.caps` gains `dest` (D5-B) (§7).
9. **Implementation**: a new daemon package `cpad/dest/` (15 new files), and 5 existing files change: `cli/main.py` registers the subcommand, `protocol/specs.py` registers the `dests.snapshot` validator, `protocol/README.md` gains a description, and `protocol/tools/test_schemas.py` and `daemon/tests/unit/test_host_lines_never_start_with_word.py` each learn the new type (§12.4). **Merge constraint**: as soon as this 1 new schema lands on main, the firmware's `test_codec_vectors` goes red, so it must be merged in the same batch as the firmware codec's type registration (F5-0, registering only `dests.snapshot`); D5-B's 2 schemas likewise go with F5-0b (§7.8).

## 1. Starting point and boundaries

### 1.1 Routes that exist today (docs/13 §9.1)

Short press G0 and type → today's notes; Opt+Q → project queue; Opt+S → open a session after confirmation (becomes a queued item offline); reply to a session; hold G0 and speak → recordings folder + one line in today's notes; Opt+R → meeting summary + one line in today's notes. v0.4.1 adds Opt+T and "press T while holding G0" to write a to-do checkbox [src docs/13 §9.3].

### 1.2 What this design covers, and what it doesn't

| Covers (v0.5) | Does not cover |
|---|---|
| The model of 8 destination kinds; `destinations.json`; `decide()`; pending, expiry, restart recovery; the clipboard and "append to file" adapters; audit; `cpa dest`, `cpa audit`; `dests.snapshot` (`pending.snapshot`, `dest.send` come in the later D5-B); display rules for the device's "Go" page, capture page and "needs you" | Executors for paste at cursor and cloud plugins (v0.7, handed to WhisperWoof; this version only reserves the names, and the decision is always "not in this version"); context profile `prof` (v0.8, #19); re-routing any old note (this version can only re-route what is in "pending"); sending meeting-summary to-dos out one by one (v0.6) |

### 1.3 Division of work with v0.4.1 (two parallel branches)

v0.4.1 owns `hub/`, `notes/`, `events/`, `recordings/`, `cli/`, `control/`, `config/`, and will add `note.dest` to `outbox.item` (only ever sends `"todo"`), `file.items.dest`, `hello.caps.todo`; an unknown `dest` writes an ordinary line and records `dest.downgraded{item, dest}` [src docs/13 §11.1, §12.2]. **This design asks only 3 things of v0.4.1** (the v0.4.1 owner please follow them, or v0.5 will have to change its schema):

1. `note.dest` and `file.items[].dest` use a **pattern** in the schema, not `enum:["todo"]`. With an enum, a v0.4.1 cpad receiving `dest:"clip"` from v0.5 firmware would reply `E_BAD_FIELD` and the entry would be rejected, breaking the fallback promise "an old daemon stores it as an ordinary note". Use the pattern `DEST_ID` from §7.5.
2. The `body` in the `item.received` record carries `dest`; the memo's `rec.result` record carries `dest`. v0.5's `cpad/dest` reads the destination from these two records.
3. The format of the today's-notes line does not change because of `dest` (except `todo`, which writes a checkbox).

## 2. Destination model

### 2.1 Eight kinds (policy per kind)

| Kind `kind` | Name on the device | What it does | Class | Needs project | Leaves the Mac | Executor in this version |
|---|---|---|---|---|---|---|
| `daily` | Today's notes | One line in today's notes: `- HH:MM #key text ^cpa-<id>` (as today) | Archive | No | No | Yes (it is NotesWriter) |
| `todo` | To-do | A checkbox in today's notes `- [ ] HH:MM #key text ^cpa-<id>` (§18 item 6; implemented in v0.4.1) | Archive | No | No | Yes (v0.4.1) |
| `queue` | Project queue | `queue.added`, `intent=todo`; `uncertain=true` when from speech | Record, not executed | Yes | No | Yes (writes a record) |
| `session` | Open session | **Only ever becomes** a queued item with `intent=spawn`; actually opening a session is still v0.1's confirm box + `agent.spawn` | Execute | Yes | No (only queues; when the session really opens, the body goes out via Claude Code, which is v0.1's old rule) | Yes (writes a record) |
| `clip` | Clipboard | `pbcopy`, body via stdin | Live | No | No (but clipboard history tools record it) | Yes |
| `paste` | At cursor | Paste at the cursor of the Mac's front app | Live | No | No | **No** (v0.7, via WhisperWoof) |
| `file` | A name you choose | Append a line to a `.md` / `.txt` file you name, idempotent by block ID (e.g. an Obsidian inbox) | Archive | No | Depends on the folder; **counts as "leaving" by default** | Yes |
| `plugin` | A name you choose | A WhisperWoof MCP plugin (Todoist etc.); cpad stores no secrets | Cloud | No | Yes | **No** (v0.7) |

- **Live**: changes the Mac's current state. Offline captures and speech (whose text only arrives later) are never done automatically after the fact; they need Enter on the device.
- **Execute**: `decide()` never executes, it only queues (§5.4).
- **Cloud** and **folders that leave the Mac**: must be turned on explicitly on the Mac (type y in `cpa dest add`), which records a `dest.changed`. Once on, delivery is automatic online and offline, and every delivery is audited (docs/13 §9.2's rule).

### 2.2 Destination ids

- **Built-in**: `daily`, `todo`, `queue`, `session`, `clip`, `paste`.
- **Custom**: `file.<name>`, `plugin.<name>`, where the name is `^[a-z0-9][a-z0-9-]{0,15}$`. For example `file.sb`, `plugin.todoist`.
- **The id carries its kind** (the part before the dot is the `kind`), so an offline capture records the id, and changing keys later can never send it to the wrong place; the same id can never become another kind of destination. That is why the research draft's `drev` (catalogue version) is not needed.
- The generic wire pattern (leaving room for new kinds): `^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,15})?$`, at most 24 characters.

### 2.3 What gets written

| Destination | Written to | Line format |
|---|---|---|
| `daily` | `~/CardputerNotes/YYYY-MM-DD.md` | As today, not a character changed |
| `todo` | Same | `- [ ] HH:MM #cpa text ^cpa-<id>` (v0.4.1) |
| `file.*` | The file you name | `- YYYY-MM-DD HH:MM #cpa text ^cpa-<id>`; a `~` in front when the time is uncertain; control characters become spaces; de-duplicated by the block ID at the end of the line; existing lines are never modified or deleted |
| `queue` / `session` | `queue.added` in `events.jsonl` | `origin:"dest"`, `from_item:<id>`; control characters in the body become spaces, leading and trailing whitespace is trimmed, and it is truncated to `text_max` (500 B UTF-8) without splitting a character (`router.queue_text`), so it can always open a session later; if it is empty after truncation or starts with `-` (could never open a session), it falls back to today's notes with `reason` `bad_text` |
| `clip` | Mac clipboard | The original text (for speech, `memo.text`, at most 2000 characters) |

**The today's-notes line gets no "→ destination" suffix** (the research draft §2.3 suggested one): adding it would mean changing `notes/render.py`, and lines without `dest` must stay byte-for-byte the same as v0.3. Where it went is in `cpa audit`.

## 3. Keys: which letter goes where

### 3.1 Three groups of letters

| Group | Letters | Rule |
|---|---|---|
| Fixed | `n` today's notes, `t` to-do, `q` project queue, `s` open session | Always on, letters cannot change; exactly the same as v0.1 / v0.4.1's Opt+N/T/Q/S |
| Bindable | `b c f j k o u x y` (9) | Bound freely on the Mac; one letter binds only one destination; by default only `c` = clipboard |
| Reserved | `a d e g h i l m p r v w z` | Cannot be bound to destinations: H/I/M/P/R/V/L are app and system keys, D is "act" in lists, G is the picker, A/E/W are reserved in docs/13 §5.2, Z is the "unbound" example in tests [src docs/13 §5.2; keymap.cpp:43-100] |

- The device only accepts letters in this table; if the snapshot gives another letter, the device still shows that row but binds no key (§10.4).
- A static firmware test guards that "bindable letters have no other binding in the global layer or the capture layer".

### 3.2 One letter, three uses (docs/13 §3 principle 2)

| Where | Press | Effect |
|---|---|---|
| Any page (no overlay) | Opt+letter | Opens the capture page with the destination preset to this letter's destination (Opt+N/T/Q/S already work this way) |
| Capture page | Opt+letter | Changes this entry's destination to it (not on the capture page for replying to a session) |
| Capture page | Opt+G | Destination picker: lists every available destination, including those without a letter |
| Holding G0 to record a memo | Letter (no Opt) | This speech is saved there; 1–9 / 0 still set the project |

Order of resolving the destination (most specific first): a key explicitly pressed this time or a choice in the picker (`how:"key"` / `"pick"`) → the app's default (`how:"app"`, only v0.6's to-do app has one) → today's notes. **A default can only be a local archive**: if a destination chosen by `how:"app"` is live, execute or leaves the Mac, cpad always falls back (§5.2).

## 4. `destinations.json`

### 4.1 Example

```json
{
  "version": 1,
  "dests": [
    {"id": "clip", "slot": 5, "key": "c", "enabled": true},
    {"id": "file.sb", "slot": 7, "key": "o", "label": "reading-list", "enabled": true,
     "leaves_mac": true, "optin": "2026-10-22T09:14:03.120+00:00",
     "file": {"root": "~/Notes/reading-list", "path": "inbox/cardputer.md"}}
  ]
}
```

No file = only the defaults for built-in destinations. Built-in destinations missing from the file are filled in per §4.4.

### 4.2 Fields

| Field | Type | Who may have it | Notes |
|---|---|---|---|
| `version` | `1` | Top level, required | Newer than cpad knows: refuse to load and report an error; no guessing |
| `dests` | array, ≤12 entries | Top level, required | Order doesn't matter; sorted by `slot` |
| `id` | string | Each entry, required | §2.2 |
| `slot` | 1–12 | Each entry | Display order on the device and picker number; must not repeat. Built-ins default to 1–6; custom ones default to the first free slot from 7 |
| `key` | a single letter or `null` | Each entry | §3.1. For the fixed four, if written it can only be their own letter; `null` = unbound |
| `label` | 1–24 B UTF-8 | Each entry | The name shown on the device; no control characters, `"` or `\` (so no escaping is needed on the wire, §7.6) |
| `enabled` | boolean | `clip`, `paste`, custom | The fixed four are always on |
| `leaves_mac` | boolean | Only `file.*` | Default `true`. Write `false` only when you are sure the folder is never synced out (`cpa dest add --local`). `plugin.*` is always `true`; built-ins are always `false` |
| `optin` | ISO time or `null` | Those with `leaves_mac` true | When you typed y in `cpa dest add`. When `null`, the destination does not take effect (`cpa dest ls` shows "needs confirmation") |
| `file` | `{root, path}` | Only `file.*`, required | `root`: an absolute path or starting with `~`; `path`: the file path relative to `root`. Rules in §4.3 |
| `plugin` | `{name}` | Only `plugin.*`, required | The WhisperWoof plugin name, `^[a-z0-9-]{1,16}$`. No URL, no token |

### 4.3 Validation (`parse_dests()`, a pure function)

- **Strict**: the top level may only have `version` and `dests`; each entry may only have the fields in the table above. One extra field rejects the whole file. So tokens, API keys and passwords structurally cannot fit—cpad never stores third-party secrets, and plugin secrets stay in WhisperWoof.
- `id` unique; `slot` unique; `key` unique and per §3.1. `daily`, `todo`, `queue`, `session` cannot be `enabled:false`.
- `file`: `path` may not be absolute, may not contain `..`, no segment may start with `.`, and the extension may only be `.md` or `.txt`. Neither `root` nor the final file may fall under `~/Desktop`, `~/Documents`, `~/Downloads`, `~/Library` (including iCloud Drive) or `$CPA_HOME`. At parse time only a `root` starting with `~` can be checked literally; the shell expands `--root ~/Documents` to `/Users/<you>/Documents` before handing it to `cpa`, so `cpa dest add` takes the realpath of `root` and the final file before saving and checks them again against these folders under `$HOME` and against `$CPA_HOME` (`paths.check_place`); if they fail, it reports an error and writes nothing. **Why restrict extensions and hidden segments**: appending to a file is "write a line of what you said into any file"; pointed at `~/.zshrc`, that becomes code execution; allowing only `.md` / `.txt` and no hidden paths closes that door.
- At delivery time (not parse time) it checks again: take the realpath of `root` and the target; the target must be inside `root`; the last component is opened with `O_NOFOLLOW`, and a symlink is refused; if the parent folder doesn't exist it reports "needs setup" and **does not create folders for you**; new files are created with mode 0600.
- When parsing fails (bad JSON, an extra field, a `version` newer than cpad), the daemon does not exit: it keeps running with **only the fixed four** (today's notes, to-do, queue, session), with `clip` and `paste` off and unbound (`catalog.fail_closed()`), and `cpa status` and `cpa dest ls` show the error (the same as a broken `projects.json`) [src cli/main.py `_offline_status`]. **It does not go back to the §4.4 defaults**: the defaults have the clipboard on and holding C; after you turned the clipboard off and gave C to another destination, a mistyped file or one written by a newer cpad would make Opt+C copy private text to the clipboard again. A configuration error always means "off", the same as a refused path.

### 4.4 Built-in defaults

| id | slot | key | label | Default |
|---|---|---|---|---|
| `daily` | 1 | n | Today's notes | Always on |
| `todo` | 2 | t | To-do | Always on |
| `queue` | 3 | q | Project queue | Always on |
| `session` | 4 | s | Open session | Always on |
| `clip` | 5 | c | Clipboard | On (this Mac only) |
| `paste` | 6 | — | At cursor | No executor in this version, not shown on the device |

(On a Chinese UI the labels are 今日笔记, 待办, 项目队列, 开 session, 剪贴板, 光标处.)

There is no built-in `file.*`: **there is no Obsidian inbox by default until you name a file** (§18 item 7).

## 5. Decision: `decide(capture, dest, online, origin)`

### 5.1 Four inputs

| Input | Meaning |
|---|---|
| `capture` | One capture: `ref` (entry id or recording session id), `source` (`typed` / `voice` memo), `dest` (the destination id recorded by the device), `how` (`key` / `pick` / `app`; missing means `key`, because v0.4.1 only carries `dest` when a key is pressed; an unknown value means `app`, the strictest), `project` (treated as none for orphan entries), `text` (empty if a memo has not been transcribed) |
| `dest` | The destination found by id in the catalogue; `None` if not found |
| `online` | Whether the device link is Ready **right now** (whether pending can be shown on the device for you to confirm) |
| `origin` | How this capture reached cpad: `live` (just pressed while connected to the Mac), `queued` (stored offline, arrived late, or speech), `confirmed` (you just pressed Enter on a pending item on the device) |

**How `origin` is computed** (pure function `origin_of()`): a typed entry is `live` only if all three of these hold, otherwise `queued`:

1. `live:true` in the body: the link was Ready when you pressed Enter to save on the device (only new firmware sends it; old firmware doesn't → `queued`, the safe side);
2. the first two parts of the entry id (boot) equal this `hello.boot`;
3. the device's current `up_ms` (the latest `hello` / `pong` value plus the time elapsed since) minus the entry's `at.up_ms` is between 0 and 30 s (`LIVE_WINDOW_MS`).

It does not rely on wall-clock time, so it is accurate even if the device's clock was never set. Speech is always `queued`: there is no text when the key is pressed.

### 5.2 Prechecks: fall back to today's notes if not met

Checked in order; the first that fails is the reason; the result is `downgrade`, with the destination recorded as `daily`. The today's-notes line was already written before this, so a fallback loses nothing.

| # | Condition | `reason` |
|---|---|---|
| 1 | The id is not in the catalogue | `unknown` |
| 2 | No executor in this version (`paste`, `plugin.*`) | `unavailable` |
| 3 | Turned off | `disabled` |
| 4 | Leaves the Mac but has no `optin` | `no_optin` |
| 5 | `how` is not `key` / `pick`, while the destination is live, execute or leaves the Mac | `not_explicit` |
| 6 | No text (memo not transcribed, or only a transcript with no `memo.text`) | `no_text` |
| 7 | Needs a project but has none | `no_project` |

It **never** falls back to paste, copy or execute; it **never** guesses a "similar" destination.

The routing layer (`router`, not in the pure function `decide()`) has one more: when the decision is to queue (`queue` / `session`), the body is first cleaned and truncated per §2.3; if it still could not open a session after truncation (empty, starts with `-`), it falls back to today's notes with `reason` `bad_text`.

### 5.3 Truth table (after all prechecks pass)

Legend: **D** deliver; **H✓** hold, waiting for you to press Enter on the device (`await_confirm`); **H⏚** hold, waiting for the device to connect (`await_device`); **Q** queue `intent=todo`; **Q~** same plus `uncertain=true`; **S** queue `intent=spawn`; **S~** same plus `uncertain=true`.

| Kind | Typed·live·online | Typed·live·offline | Typed·queued·online | Typed·queued·offline | Voice·live·online | Voice·live·offline | Voice·queued·online | Voice·queued·offline | confirmed·online | confirmed·offline |
|---|---|---|---|---|---|---|---|---|---|---|
| `daily` `todo` `file` | D | D | D | D | D | D | D | D | D | D |
| `plugin` (confirmed on) | D | D | D | D | D | D | D | D | D | D |
| `queue` | Q | Q | Q | Q | Q~ | Q~ | Q~ | Q~ | Q / Q~ | Q / Q~ |
| `session` | S | S | S | S | S~ | S~ | S~ | S~ | S / S~ | S / S~ |
| `clip` | **D** | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D** | H⏚ |
| `paste` | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | H✓ | H⏚ | **D** | H⏚ |

- "Typed·live·offline" and "Voice·live" never actually occur (`live` requires the link, and speech is always `queued`), but they are still defined in the table, and tests cover all 8 × 2 × 2 × 3 = 96 combinations.
- In the two `confirmed` columns, `Q / Q~` and `S / S~` depend on whether the entry was originally typed or spoken.
- `clip` runs in only two cases: a key you just pressed while connected to the Mac (the press itself is the confirmation, #24 "run at once when online"); or you pressed Enter on a pending item on the device.
- `paste` needs confirmation on the device every time (the confirm box shows the front app's name, docs/13 §9.5); in this version precheck 2 already downgrades it, so this row sets the rules for v0.7.
- `plugin` delivers even offline, per docs/13 §9.2's "only what you turned on is delivered automatically, every delivery audited"; in this version it is likewise downgraded by precheck 2.

### 5.4 Invariants (asserted directly by tests)

1. The result of `decide()` is never "open a session". Opt+S online still goes through v0.1's confirm box + `agent.spawn`, never through `decide()`; a `session` coming from the outbox, a memo or `dest.send` always becomes a queued item. **Offline captures never open a session.**
2. The live class (`clip`, `paste`) is D only when `online` is true and `origin` is `confirmed` (or a typed `clip` is `live`). Nothing is copied or pasted automatically on sync, after restart, or on retry.
3. The destination comes only from `capture.dest`, never from the body. If someone in a meeting says "send it to Slack", nothing happens.
4. `decide()` is a pure function: the same input always gives the same result; it reads no files and no clock.

## 6. How a capture with a destination travels

### 6.1 Order (following 05 §7.3's "Append before Send")

```
Typed: device outbox.item(note, dest, how, live)
  decide() ─▶ one batch Append: item.received, dest.decided, (when queuing) queue.added
             → fsync → outbox.ack(stored) → WriteNote (one line in today's notes; a checkbox for to-dos)
             └─ result is D and needs an executor (clip, file) → deliver → dest.delivered | dest.failed

Memo: recording pipeline (v0.4.1, recordings/service.py)
  rec.result(dest, how, memo_text) appended in its own batch ─▶ Appended ─▶ router.on_appended:
     fold_pending ─▶ memos still in captures after folding (never decided) ─▶ decide()
     ─▶ second batch Append: dest.decided, (when queuing) queue.added
     └─ result is D and needs an executor → deliver → dest.delivered | dest.failed
```

- Entries without `dest`, or with `dest` `daily`: **exactly today's path**, no `dest.*` events written.
- A memo's `rec.result` and its `dest.decided` are **not in the same batch**. If it crashes between the two batches, then on the next start, after folding history, the memo is still in `captures` (undecided), and it is decided then, exactly once. Conversely, a memo that has been decided is **never decided again**: only the folded state is consulted (`router.undecided_memos`), never the `rec.result` in the `Appended` batch directly—at startup that batch is the whole `events.jsonl`.
- Delivering a `todo` is the checkbox line itself (v0.4.1); no separate executor is called, only `dest.decided` is recorded.
- The device shows a hint as soon as it receives `stored` (§10.5). A clipboard failure appears in "needs you" within a second.

### 6.2 Pending (pure function `fold_pending(events)`, following `queue/fold.py`)

```
dest.decided(hold) ──▶ hold ──(device Enter: dest.send go)──▶ dest.decided(confirmed, deliver) ──▶ running
   │                    │                                                                      ├─▶ dest.delivered (removed)
   │                    ├─(device Opt+D: dest.send drop)──▶ dest.dropped (removed)              └─▶ dest.failed ──▶ failed
   │                    └─(24 hours untouched)──▶ dest.expired (removed)                                │
dest.decided(deliver) ──▶ running ──▶ delivered / failed                                       (Enter retries; Opt+D drops)
```

- The device only sees `hold` and `failed`; `running` exists only on the Mac.
- **Expiry**: `HOLD_TTL_S = 24 h`. Only the delivery is dropped; the today's-notes line remains.
- **Restart recovery**: `running` items with no result—`file` is delivered again (idempotent by block ID, never written twice); `clip` records `dest.failed{code:"E_INTERRUPTED", retryable:false}` and becomes pending, waiting for your Enter. **A restart never copies automatically.**
- **Automatic retry**: only for the archive class (`file`) and retryable failures (`E_DEST_IO`), at daemon start (`resume(startup=True)`) and every time the device connects (`resume(startup=False)`), at most 3 times in total (`MAX_ATTEMPTS`). After that, only Enter on the device retries. When the device connects, a running delivery is **not** treated as interrupted: a file is never delivered twice at once, and the clipboard never records `E_INTERRUPTED` while `pbcopy` is still running. `startup` has no default; the caller must say which it is.
- **Folding is idempotent**: the `dest.decided` that starts a delivery carries its own `attempt`, which folding takes directly instead of incrementing; all other updates overwrite. So folding the same batch of records twice gives the same result. The hub still folds only once: the single `Appended(history)` at startup is the only pass.
- Pending needs the body to deliver, but `dest.*` events don't carry it. So `fold_pending` remembers the body of entries "with a non-daily destination and no final result yet" (taken from `item.received.body.text` or `rec.result.memo_text`) and forgets it once there is a final result. The body is already in these two records (05 §7.3), so destinations never store an extra copy on disk.

### 6.3 Memos

- The destination is recorded in segment 0's `.M` and handed to cpad via `file.items[].dest`, `how` (`dest` since v0.4.1, `how` added in v0.5); recording contract v1 does not change.
- When `rec.result` comes out it is decided per §5, with the text from `memo_text` (≤2000 characters); for a queue or session only the cleaned first 500 B is taken (§2.3), and the full words are still in the today's-notes line. A memo with only a transcript and no tidy-up (`level=transcript`) has no `memo_text`, and in this version falls back with `no_text`: the today's-notes line already links the full transcript.
- Meetings ignore `dest` (end of docs/13 §2.2).

## 7. Protocol additions (additive only, `v` still 0)

### 7.1 Overview

| Change | Direction | Content | Compatibility |
|---|---|---|---|
| `dests.snapshot` (new) | H→D state push, device replies `ack` | `rev`, `dests[≤12]` | Only pushed to firmware with a `dests` key in `revs` |
| `pending.snapshot` (new, **D5-B**) | H→D state push, device replies `ack` | `rev`, `n`, `items[≤8]` | Only pushed to firmware with a `pending` key in `revs` |
| `dest.send` (new, **D5-B**) | D→H online request | `ref`, `op` (`go` / `drop`), `dest?` | The device only sends it when the host's `hello.caps.dest:true`; replies `ack` or `error` |
| Device `hello` / `pong` `revs` gains `dests` (D5-B adds `pending`) | D→H | uint32; 0 = none yet | An old daemon ignores extra keys [src validate.py `obj()`] |
| Host `hello.caps` gains `dest` (D5-B) | H→D | boolean | Old firmware ignores it |
| `note.body` of `outbox.item` gains `how?`, `live?` | D→H | §7.5 | An old daemon ignores them |
| `file.items[]` gains `how?` | D→H | Only memo segment 0 | An old daemon ignores it |

**This branch (D5-A) only delivers `dests.snapshot`**: 1 schema, 1 vector file, 1 validator. `pending.snapshot`, `dest.send` (and `revs.pending`, `caps.dest`) are D5-B: 2 schemas, 2 vector files, 2 validators in `dest/specs.py`, `parse_dest_send` and `pending_snapshot` encoding, two README lines, the two worst-case lines in §7.6, which must be merged in the same batch as the firmware registration F5-0b (§7.8). The routing layer is already prepared: `router.on_send` accepts any object with `id`, `ref`, `op`, `dest` (`SendRequest`).

**No new error codes** (consistent with docs/13 §12.2): `ref` not found → `E_UNKNOWN_ITEM`; destination unknown, off, not in this version, or `todo` (this version cannot re-route to to-do, §7.4) → `E_BAD_FIELD`, `field:"dest"`, nothing recorded; `go` re-decided as a **downgrade** (the destination was deleted, turned off or had its confirmation withdrawn in the meantime, or it was re-routed to a destination that needs a project and there is none, or the body cannot be queued) → still records `dest.decided` (`downgrade`) and replies `E_BAD_FIELD`, `field:"dest"`; the item leaves pending and the today's-notes line remains—the device then shows "Not delivered, kept in today's notes" rather than treating it as copied; the item is being delivered → `E_BUSY`; delivery failed → `E_INTERNAL` (the item stays in pending with status `failed`).

### 7.2 `dests.snapshot`

```jsonl
{"v":0,"t":"dests.snapshot","id":"h12","rev":2918374651,"dests":[{"id":"daily","slot":1,"label":"今日笔记","kind":"daily","key":"n"},{"id":"todo","slot":2,"label":"待办","kind":"todo","key":"t"},{"id":"queue","slot":3,"label":"项目队列","kind":"queue","key":"q","proj":true},{"id":"session","slot":4,"label":"开 session","kind":"session","key":"s","proj":true},{"id":"clip","slot":5,"label":"剪贴板","kind":"clip","key":"c","live":true},{"id":"file.sb","slot":7,"label":"reading-list","kind":"file","key":"o","cloud":true}]}
```

| Field | Rule |
|---|---|
| `rev` | uint32, a **content hash**: CRC-32 of the rows' canonical JSON; if it is 0, use 1. The device caches the snapshot in `/cfg/dests.json` and after restart reports the cached rev in `revs.dests`. A hash instead of a counter, because after a daemon restart a counter starts at 1 again and would collide with an old rev 1 in the device cache; a hash is only equal when the content is the same, and several Macs never get mixed up |
| `dests` | Only **usable** destinations (on, with an executor in this version, confirmed if confirmation is needed), sorted by `slot`; at most 12 entries; `x-uniqueBy: slot` |
| `id` | §2.2's generic pattern, ≤24 characters |
| `slot` | 1–12 |
| `label` | 1–24 B UTF-8, no control characters, `"` or `\` |
| `kind` | `^[a-z]{3,7}$` (a pattern, not an enum: when new kinds are added later, old firmware only hides that row instead of rejecting the whole snapshot) |
| `key` | Optional, `^[a-z]$`; the device only accepts §3.1's letters |
| `live` / `cloud` / `proj` | Optional booleans, present only when true: live class `clip` / `paste` (offline ones need confirming later) / leaves the Mac / needs a project. The "confirm" mark is decided by the device itself from `kind:"session"` |

**When it is pushed**: when `revs.dests` in the device's `hello` or `pong` differs from the host's current rev (which is the moment you plug into the Mac, 05 §6.3 rule 6); after `destinations.json` changes (the daemon reloads by mtime, as with `projects.json`).

### 7.3 `pending.snapshot` (D5-B, not delivered in this branch)

```jsonl
{"v":0,"t":"pending.snapshot","id":"h13","rev":40211337,"n":2,"items":[{"ref":"3QZK1M7-0000C-0005","dest":"clip","st":"hold","src":"typed","text":"周五前给 Alice 发报价"},{"ref":"K3M9Q2T-00042-0A1C","dest":"file.sb","st":"failed","src":"voice","text":"买 USB-C hub，顺便看看扩展坞"}]}
```

| Field | Rule |
|---|---|
| `rev` | Same as above, a content hash (the device does not cache it and reports 0 after restart, so it always receives one) |
| `n` | Total pending (0–65535), may be more than `items` |
| `items` | Oldest first, at most 8 entries; `x-uniqueBy: ref` |
| `ref` | MonoId (typed entry id or recording session id) |
| `dest` | Destination id |
| `st` | `hold` / `failed` |
| `src` | `typed` / `voice` |
| `text` | Body preview, 1–48 B UTF-8, truncated by bytes without splitting a character, control characters replaced with spaces (reuses `hub/snapshots.label`) |

The preview is 48 B: a line holds at most about 20 Chinese characters, and the device cannot show more; and even if all 8 entries are `"`, which needs escaping, they still fit in one line (§7.6).

### 7.4 `dest.send` (D5-B, not delivered in this branch; the routing layer's `router.on_send` is ready)

```jsonl
{"v":0,"t":"dest.send","id":"d21","ref":"3QZK1M7-0000C-0005","op":"go"}
{"v":0,"t":"ack","id":"h22","re":"d21"}
{"v":0,"t":"dest.send","id":"d23","ref":"K3M9Q2T-00042-0A1C","op":"go","dest":"file.sb"}
{"v":0,"t":"dest.send","id":"d25","ref":"K3M9Q2T-00042-0A1C","op":"drop"}
```

- `op:"go"`: run this pending item. With `dest` it is **re-routed** to another destination (`how` recorded as `pick`). It is re-decided with `origin:"confirmed"`; if the decision is a downgrade it replies `E_BAD_FIELD dest` (§7.1), not `ack`.
- **Cannot re-route to `todo`** (`router.NO_REROUTE`): this entry is already an ordinary line in today's notes, and turning it into a checkbox would need the hub to write another line, but `NotesWriter` only writes from `item.received` and de-duplicates by block ID, so that effect does not exist yet. So `dest:"todo"` replies `E_BAD_FIELD dest` and records no event, and the device's re-route list does not include to-do. Once the hub has a "write a checkbox line for an existing entry" effect, this is allowed. Re-routing to `daily` is fine: the line is already there, so it only records `dest.decided` and replies `ack`.
- `op:"drop"`: drop this entry's delivery (the today's-notes line remains).
- Only valid for a `ref` in "pending"; re-routing any old note comes later.
- Shares the "at most one in flight" channel with `queue.done`, `inbox.dismiss` and `rec.*`, with a 10-second timeout (docs/13 §10.2). Delivery waits for the executor to return (`pbcopy` at most 2 s) before replying `ack`.

### 7.5 Optional fields on existing messages

| Field | Where | Rule |
|---|---|---|
| `dest` | `note.body`, `file.items[]` | Added in v0.4.1. Pattern `DEST_ID = ^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,15})?$` (§1.3 item 1) |
| `how` | Same | `^[a-z]{1,8}$`, known values `key` / `pick` / `app`; an unknown value is treated as the strictest, `app` |
| `live` | Only `note.body` | Boolean or `null`: the link was Ready when saved |

`queue` and `agent.*` do not change: typed Opt+Q / Opt+S still send v0.1's shape.

### 7.6 Line lengths (calculated with compact `json.dumps`, `ensure_ascii=False` [calc])

| Line | How it is built | Bytes |
|---|---|---|
| `dests.snapshot` worst case, 12 entries | id 24 characters, `label` 24 B, `kind` 7 characters, all three booleans given as `false`, `key` given as `null` (4 B, 1 B longer than `"z"`), `rev` and `id` at their longest | **1843** |
| Same, but `label` is 24 `"` | For illustration only: which is why `label` forbids `"` and `\` | 2131 (over) |
| The 6-entry example in §7.2 above | — | 539 |
| `pending.snapshot` worst case, 8 entries (D5-B) | Preview of 48 `"` (each escaped to 2 bytes) | About 1676; D5-B re-measures with `rev` 4294967295 |
| `pending.snapshot`, 8 entries, 48 B of Chinese (D5-B) | — | About 1292 |
| `outbox.item` with a 500 B body + `dest` / `how` / `live` | — | 723 (1223 when all `"`) |
| `dest.send` longest (D5-B) | — | About 129 |

The limit is 2048 B in every case. Snapshots still go through `codec.fit_list` (like existing snapshots). This branch's `protocol/tools/test_dest_worst_lines.py` guards the two `dests.snapshot` lines; D5-B adds the worst-case lines for the other two types.

### 7.7 Mixing old and new

| Combination | Result |
|---|---|
| New cpad + old firmware | Old firmware doesn't report `revs.dests`, so cpad doesn't push the new snapshot; no `dest` is sent; everything as before |
| Old cpad (v0.4.1) + new firmware | No `caps.dest`, no snapshot: the device only uses the fixed N/T/Q/S; bound letters don't work; no `dest.send` is sent. If a `dest:"clip"` does go out, v0.4.1 stores it as an ordinary note (given §1.3 item 1) |
| New cpad + new firmware, you changed a binding | The daemon reloads → new rev → push; entries recorded offline under the old binding store the id, are delivered by id, and fall back if the id is gone |

### 7.8 Merge constraint (important)

The firmware's `test_codec_vectors` asserts that the number of files in `vectors/v0/messages` equals `kMsgTypeCount`, that every vector type can be decoded by the firmware, and that the number of schemas in `schema/v0` equals `kMsgTypeCount + 1` [src test_main.cpp:56, 76], and CI runs `pio test -e native` every time [src ci.yml]. On the daemon side, `test_every_message_type_has_a_validator` also requires the schemas and `codec.KNOWN_TYPES` to match one to one. So:

- This branch (D5-A) adds only **1** schema and 1 vector file (`dests.snapshot`), and registers the daemon validator in `protocol/specs.py` at the same time—the daemon and `protocol/tools` checks are green.
- **Firmware CI will be red until the firmware codec knows `dests.snapshot`** (one entry each in the `MsgType` enum, the name table and the decoder table; field validation only, no UI). This step is work package F5-0 (§14); it registers only this one type and must land on main before or in the same batch as this branch. If F5-0 registered 3 types as in the old draft, `schema count == kMsgTypeCount + 1` would still be red. (2026-10-22: F5-0 was done in the same batch as docs/25 R1-A and this branch; `dests.snapshot` is the 31st message; the device only decodes it and still replies `E_UNKNOWN_TYPE` after handshake.)
- The same applies when D5-B adds `pending.snapshot` and `dest.send`: the 2 schemas are merged in the same batch as the firmware registration F5-0b.

## 8. Audit: `dest.*` events and `cpa audit`

### 8.1 Events (`events.jsonl` is still the single source of truth; none **contain the body**)

| Event | Fields | When |
|---|---|---|
| `dest.decided` | `item, src, want, dest, kind, action, reason, how, origin, intent?, attempt?` | Every decision (`want` is what the device asked for, `dest` is what actually applies; on downgrade `dest` is `daily`). `attempt` is present only when this decision starts a delivery, and `fold_pending` takes it directly, so folding is idempotent (§6.2) |
| `dest.delivered` | `item, dest, kind, by, bytes, sha8, ref?, leaves_mac, attempt` | Executor succeeded. `ref` is an opaque reference (block ID) |
| `dest.failed` | `item, dest, kind, code, retryable, attempt` | Executor failed, or was interrupted by a restart |
| `dest.dropped` | `item, dest, by` | You pressed Opt+D on the device |
| `dest.expired` | `item, dest` | 24 hours untouched |
| `dest.changed` | `op(add/rm/bind), id, by, enabled?, key?, leaves_mac, optin?` | `cpa dest add/rm/bind`; turning on a destination that leaves the Mac is recorded here |

- `by`: `auto` (automatically on sync or online) / `device` (you confirmed on the device) / `cli`.
- `bytes`, `sha8`: the body's UTF-8 byte count and the first 8 hex digits of sha256, the same rule as `RedactFilter` [src logging/redact.py].
- `code` is only used on the Mac, never on the wire: `E_DEST_IO` (retryable), `E_DEST_PATH` (path refused), `E_DEST_MISSING` (`pbcopy` not found), `E_DEST_TIMEOUT`, `E_INTERRUPTED`.
- `dest.downgraded{item, dest}` written by v0.4.1 is still read, as `action=downgrade`.

### 8.2 `cpa audit`

```
cpa audit [--since 7d|24h|30m|<ISO>] [--leaves-mac] [--dest <id>] [--json]
```

Lists the events above in time order: time, event, destination, entry, detail (`action/reason` or `code`), byte count, `sha8`, whether it left the Mac, who triggered it. Defaults to the last 7 days. `--leaves-mac` lists only what a **destination** sent out of the Mac: deliveries to and opt-in records for `file.*` destinations that leave the Mac (and `plugin.*` from v0.7), with only lengths and hashes. Opening a session hands the body to Claude Code under v0.1's old rule and is not a "delivery": it is recorded in `agent.spawned` / `queue.spawned`, not in `cpa audit` (§11 item 1). The daemon does not need to be running (it reads `events.jsonl` directly).

## 9. Command line `cpa dest`

| Command | What it does |
|---|---|
| `cpa dest ls [--json]` | Lists the catalogue: slot, letter, id, kind, name, state (on / off / needs confirmation / not in this version), local or leaves the Mac. If `destinations.json` has an error, the error is printed first. Never shows any body text |
| `cpa dest add <id> [--key K] [--label L] [--slot N] [--root DIR --path FILE] [--local] [--yes]` | Built-in: turn on (`clip`) or change name, slot, letter. `file.<name>`: create or modify; `--root` and `--path` are required; without `--local` it counts as leaving the Mac. **Before turning on any destination that leaves the Mac**, it prints a screen of explanation (what is sent: only the text of the entry you take with this key, never audio or full meeting text; where it goes; every send is audited) and asks you to type `y` (or pass `--yes`), then writes `optin` and records `dest.changed`. `plugin.<name>` is refused in this version: "needs WhisperWoof (v0.7)" |
| `cpa dest rm <id>` | Custom: delete; `clip`: turn off; the fixed four: error ("today's notes / to-do / queue / session are fixed destinations"). Records `dest.changed` |
| `cpa dest bind <id> <letter>` / `cpa dest bind <id> -` | Bind or unbind. If the letter is taken by another destination, it reports an error and **does not quietly take it over**: "o is already bound to file.sb, run cpa dest bind file.sb - first". Letters are case-insensitive and stored lowercase |
| `cpa dest test <id>` | Probe only, never touches content: `clip` checks that `/usr/bin/pbcopy` exists; `file` runs §4.3's path rules, checks the parent folder exists, that it is writable, and whether the last component is a symlink; `queue` / `session` hint "captures need a project"; `paste` / `plugin` report "not in this version". Exit code 0 = usable, 1 = not usable |

- All of them only change `destinations.json` (atomic write, 0600); the daemon reloads it by mtime and pushes a new snapshot. The CLI is the only writer of this file; later, WhisperWoof's settings page (W8) also has cpad write it via cpad.sock.
- Output is in English, consistent with the existing `cpa` subcommands.

## 10. What the device shows

### 10.1 The "Go" page of the Tab menu

docs/13 §6's "Go" page lists "which app to go to". v0.5 adds a section "Save to" at its **very top**, with content from the cached `dests.snapshot`:

```
Menu · Go           Here [Go] Control
Save to · set on Mac · synced
▸Opt+N  Today's notes      default
 Opt+T  To-do
 Opt+Q  Project queue      project
 Opt+S  New session        confirm
 Opt+C  Clipboard          online
 Opt+O  reading-list       cloud
[;.]pick [Enter]do [,/]page letter: go [Tab]close
```
(Scrolling down: a line "Hold G0 then press a letter: what you say is saved there"; further down, docs/13 §6.3's existing Opt+H home…)

- Each row has the key on the left and a mark on the right: **default** (today's notes), **project** (needs a project), **confirm** (opening a session shows a confirm box first), **online** (live class: ones taken offline need Enter in "needs you" after connecting), **cloud** (leaves the Mac).
- **Enter equals pressing Opt+that letter** (the menu only "replays keys", docs/13 §6.2, §10.1), which opens the capture page with the destination already chosen. A destination without a letter reads "(no key) pick it with Opt+G on the capture page"; it is a description row, and Enter does nothing.
- Status on the title row: synced / "offline · using the last sync" / "not synced yet: only N T Q S" (never received a snapshot, or cpad is too old).
- New menu tests: Enter on each destination row has exactly the same effect as pressing Opt+letter directly; rows without a letter do nothing.

### 10.2 Capture page and memo screen

```
Note → Clipboard · #cpa       online ● ↑0
Send Alice the quote before Fri_
                              21/500
[Enter]save [Opt+G]dest [Opt+0-9]project [Tab]menu
```
```
● REC 0:07 → reading-list · #cpa
(level meter)
While holding: T to-do C clip O second… 1-9 project
Let go of G0 to stop
```

The title bar shows the destination before saving. When offline, a live destination is followed by "online", a reminder that this entry needs confirming later.

### 10.3 Pending in "needs you" (listed before sessions)

```
Needs you (3)
▸Copy  Send Alice the quote befo…  Enter
 Fail  reading-list: buy USB-C hub…
 !     cpa/architecture needs you 9 min
[Enter]do [Opt+G]re-route [Opt+D]drop [Tab]menu
```

- Enter: `dest.send go`; Opt+G: pick another destination, then `go` (to-do is not in the list: this version cannot re-route to to-do, §7.4); Opt+D: `drop` (no confirmation: the today's-notes line remains).
- `go` replies `E_BAD_FIELD dest`: hint "Not delivered, kept in today's notes" (the destination was turned off or deleted in the meantime, or there is no project), and the item leaves the list.
- This section is not shown when the host has no `caps.dest`. This section arrives together with `dest.send` and `pending.snapshot` in D5-B / F5.

### 10.4 Device rules

- The snapshot is only for display and key parsing; **decisions are made only in cpad**. Even for destinations that can be pressed on the device, cpad decides again.
- Only §3.1's letters are accepted; for a letter repeated in the snapshot, the first wins; an unknown `kind` is not shown.
- The cache is written to `/cfg/dests.json` (same storage rules as `/cfg/projects.json`), read at boot, and its rev is reported in `revs.dests`.
- **While locked**: destination letters can be pressed while holding G0 to record a memo (docs/13 §7.1's rule is unchanged); the "Go" page, pending previews and the picker are only visible after unlocking.

### 10.5 Hints

| Situation | Hint |
|---|---|
| Typed online → clipboard, `stored` received | Copied to the Mac clipboard |
| Typed offline → clipboard | Saved; after connecting to the Mac, press Enter in "needs you" to copy |
| Archive class | Saved: reading-list (offline: "Saved; goes into reading-list after connecting to the Mac") |
| Cloud | Saved; sent to X (cloud) after connecting to the Mac |
| Speech → clipboard | Saved; once transcribed, press Enter in "needs you" to copy |

## 11. Privacy and security

1. **Everything a destination sends out of the Mac can be listed**: the "leaves the Mac" column of `cpa dest ls`; `cpa audit --leaves-mac`. In this version only one kind of destination leaves the Mac: a `file.*` you turned on whose folder is synced (cloud plugins only arrive in v0.7). `--leaves-mac` only covers these deliveries and opt-in records. **It does not include opening a session**: the `session` destination only queues (`cpa dest ls` shows "local"); when a session is really opened, the body goes out through Claude Code under v0.1's old rule, needs confirmation on the device, and is recorded in `agent.spawned` / `queue.spawned`, not in `cpa audit`.
2. **Everything local by default**: the only destinations on by default are today's notes, to-do, queue, session and clipboard. Destinations that leave the Mac only take effect after typing `y` on the Mac; they cannot be turned on from the device.
3. **Only the entry you explicitly chose is sent**: a default destination can only be a local archive (§5.2 item 5); the destination is never decided from the content; audio, full meeting text and summary bodies are never sent.
4. **The audit never records the body**, and is kept forever with the event log. The body only lives in the existing `item.received` / `rec.result`.
5. **No secrets in the configuration**: strict validation means tokens structurally cannot fit; plugin secrets stay in WhisperWoof.
6. **Injection defence**: the body is only ever handed to executors as data: `pbcopy` reads it from stdin and argv never contains the body; files only get one appended line with control characters replaced by spaces; paths are confined to the `root` you gave, only `.md` / `.txt`, no hidden paths or symlinks, avoiding TCC folders and `$CPA_HOME`.
7. **Clipboard**: copied text is also in today's notes and the event log, and goes into clipboard history tools (WhisperWoof has one itself). The docs and the capture page both warn: don't use it to copy passwords or tokens. `pbcopy` explicitly sets `LANG=en_US.UTF-8`, `LC_ALL=en_US.UTF-8`, `PATH` only `/usr/bin:/bin`, with a 2 s timeout.
8. **Logs**: `RedactFilter` is reused, errors only give error codes; the meaning of `CPA_LOG_CONTENT=1` is unchanged.
9. **Lock screen**: pending previews and the "Go" page are only visible after unlocking (§10.4).

## 12. Daemon implementation: `cpad/dest/`

### 12.1 New files

| File | Content | Pure? |
|---|---|---|
| `dest/__init__.py` | **Must be empty**: when `protocol/specs.py` imports `cpad.dest.specs`, this runs first, and importing `snapshot` here would loop back to `specs` via `codec`, a circular import | — |
| `dest/model.py` | Types, constants, the kind table, `Dest` / `Capture` / `Decision` | Yes |
| `dest/catalog.py` | Built-in defaults, whole-catalogue validation, lookup, immutable add/remove/update | Yes |
| `dest/config.py` | Parsing, dumping, reading and writing `destinations.json` | Parsing and dumping are |
| `dest/policy.py` | `decide()`, `origin_of()` | Yes |
| `dest/capture.py` | Builds a `Capture` from `item.received` / `rec.result` records | Yes |
| `dest/records.py` | Builds `dest.*` events | Yes |
| `dest/pending.py` | `fold_pending()` | Yes |
| `dest/router.py` | Turns decisions into "records to append + deliveries to run + replies"; the memo hook `on_appended` | Yes |
| `dest/paths.py` | Path rules for `file.*` | Parsing is; the `O_NOFOLLOW` open is not |
| `dest/adapters.py` | `ClipAdapter`, `FileAdapter` | No (the only place that does I/O) |
| `dest/specs.py` | The `dests.snapshot` validator and limit constants (D5-B adds 2 more) (**imports only `protocol.validate` and `protocol.limits`**, otherwise `protocol.specs` ↔ `dest` is a circular import) | Yes |
| `dest/snapshot.py` | Data classes for `dests.snapshot` (`DestRow`, `DestsSnapshot`), `content_rev`, row building, fitting into one line (`codec.fit_list`) | Yes |
| `dest/audit.py` | Filtering and formatting audit rows | Yes |
| `dest/cli.py` | `cpa dest …`, `cpa audit` | No |

Each file ≤400 lines, functions ≤50 lines, McCabe ≤10 (ruff C901); all data is `@dataclass(frozen=True, slots=True)`, mappings are `MappingProxyType`.

### 12.2 Public API (implementers follow this; names and signatures are the contract)

```python
# dest/model.py
Kind = Literal["daily", "todo", "queue", "session", "clip", "paste", "file", "plugin"]
Source = Literal["typed", "voice"]
How = Literal["key", "pick", "app"]
Origin = Literal["live", "queued", "confirmed"]
Action = Literal["deliver", "hold", "queue", "downgrade"]
Reason = Literal["archive", "cloud_optin", "live_key", "confirmed", "await_confirm", "await_device",
                 "queue", "never_auto_exec", "unknown", "unavailable", "disabled", "no_optin",
                 "not_explicit", "no_text", "no_project", "bad_text"]   # bad_text: router only (§5.2)
By = Literal["auto", "device", "cli"]

DAILY = "daily"
BUILTIN_IDS: tuple[str, ...] = ("daily", "todo", "queue", "session", "clip", "paste")
FIXED_KEYS: Mapping[str, str]          # {"daily": "n", "todo": "t", "queue": "q", "session": "s"}
BINDABLE_KEYS: frozenset[str]          # frozenset("bcfjkouxy")
RESERVED_KEYS: frozenset[str]          # frozenset("adeghilmprvwz")
DEST_ID = r"^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,15})?$"
CUSTOM_NAME = r"^[a-z0-9][a-z0-9-]{0,15}$"
DESTS_MAX = 12
LABEL_MAX = 24                          # UTF-8 bytes
LIVE_WINDOW_MS = 30_000
HOLD_TTL_S = 86_400
MAX_ATTEMPTS = 3
VOICE_TEXT_MAX = 2000                   # characters

@dataclass(frozen=True, slots=True)
class KindSpec:
    kind: Kind
    live: bool              # changes the Mac's current state: never replayed unattended
    needs_project: bool
    available: bool         # this cpad has an executor (False: paste, plugin)
    custom: bool            # instances are "<kind>.<name>"

KINDS: Mapping[Kind, KindSpec]

@dataclass(frozen=True, slots=True)
class FileOpts:
    root: str
    path: str

@dataclass(frozen=True, slots=True)
class PluginOpts:
    name: str

@dataclass(frozen=True, slots=True)
class Dest:
    id: str
    kind: Kind
    label: str
    slot: int
    key: str | None = None
    enabled: bool = True
    leaves_mac: bool = False
    optin: str | None = None
    file: FileOpts | None = None
    plugin: PluginOpts | None = None

@dataclass(frozen=True, slots=True)
class Capture:
    ref: str                     # outbox item id or recording session id (MonoId)
    source: Source
    dest: str                    # dest id chosen on the device
    how: How = "key"
    project: str | None = None   # None for orphans
    text: str | None = None      # None / "" = nothing to deliver
    stamp: str = ""              # "YYYY-MM-DD HH:MM", "~" prefix when the time is uncertain

@dataclass(frozen=True, slots=True)
class Decision:
    action: Action
    dest: str                    # effective dest id ("daily" on downgrade)
    reason: Reason
    intent: Literal["todo", "spawn"] | None = None
    uncertain: bool = False

@dataclass(frozen=True, slots=True)
class Delivery:                  # here, not in router.py: records / adapters / router all use it (no import cycle)
    key: str                     # f"{capture.ref}:{dest.id}"; adapters are idempotent by it
    capture: Capture
    dest: Dest
    attempt: int = 1
    by: By = "auto"
    re: str | None = None        # dest.send id to answer when it finishes

@dataclass(frozen=True, slots=True)
class DeliveryResult:
    ok: bool
    code: str | None = None      # E_DEST_IO E_DEST_PATH E_DEST_MISSING E_DEST_TIMEOUT
    retryable: bool = False
    ref: str | None = None       # opaque, goes to dest.delivered (block id for files)

def kind_of(dest_id: str) -> Kind | None: ...
def spec(dest: Dest) -> KindSpec: ...                 # KINDS[dest.kind]

# dest/catalog.py
@dataclass(frozen=True, slots=True)
class Catalog:
    dests: tuple[Dest, ...]                           # every builtin present, sorted by slot
    problem: str | None = None                        # destinations.json error; fail_closed() in use

class DestsError(ValueError): ...
def defaults() -> Catalog: ...
def fail_closed() -> Catalog: ...                     # fixed four only; clip / paste off and unbound (§4.3)
def validate(dests: Iterable[Dest]) -> Catalog: ...   # whole-catalog rules (§4.3); raises DestsError
def find(cat: Catalog, dest_id: str) -> Dest | None: ...
def by_key(cat: Catalog, key: str) -> Dest | None: ...
def usable(cat: Catalog) -> tuple[Dest, ...]: ...     # enabled, available, opted in: what the device sees
def state_of(dest: Dest) -> Literal["on", "off", "needs_optin", "unavailable"]: ...
def put(cat: Catalog, dest: Dest) -> Catalog: ...     # add or replace by id, then validate
def remove(cat: Catalog, dest_id: str) -> Catalog: ...  # custom: delete; clip/paste: disable; fixed: DestsError
def bind(cat: Catalog, dest_id: str, key: str | None) -> Catalog: ...  # never steals a key: DestsError
def free_slot(cat: Catalog) -> int: ...              # first free slot >= 7

# dest/config.py
FILE_NAME = "destinations.json"
VERSION = 1
def dests_path(paths: Paths) -> Path: ...             # paths.home / FILE_NAME (no edit to config/settings.py)
def parse_dests(doc: Any) -> Catalog: ...             # strict (§4.3); raises DestsError
def dump_dests(cat: Catalog) -> dict[str, Any]: ...   # parse_dests(dump_dests(c)) == c
def load_dests(path: Path) -> Catalog: ...            # missing file -> defaults(); raises DestsError
def load_catalog(path: Path) -> Catalog: ...          # never raises: fail_closed() with problem set
def save_dests(path: Path, cat: Catalog) -> None: ... # config.files.write_json (atomic, 0600)

# dest/policy.py
@dataclass(frozen=True, slots=True)
class DeviceClock:
    boot: str                                         # hello.boot
    up_ms: int                                        # device up_ms now (last hello/pong + elapsed)

def origin_of(live: bool | None, item_id: str, at_up_ms: int, clock: DeviceClock | None,
              window_ms: int = LIVE_WINDOW_MS) -> Origin: ...
def decide(capture: Capture, dest: Dest | None, online: bool, origin: Origin) -> Decision: ...

# dest/capture.py
def stamp(wall_ms: int | None, received_at: str, tz: tzinfo | None = None) -> str: ...
def capture_from_item(record: Mapping[str, Any], tz: tzinfo | None = None) -> Capture | None: ...
    # item.received, kind note, body.dest present and != "daily"; else None
def capture_from_result(record: Mapping[str, Any], tz: tzinfo | None = None) -> Capture | None: ...
    # rec.result of a memo with dest != "daily"; text = memo_text[:VOICE_TEXT_MAX]

# dest/records.py
DEST_EVENTS: frozenset[str]   # decided delivered failed dropped expired changed (+ v0.4.1 "dest.downgraded")
def digest(text: str) -> tuple[int, str]: ...         # (UTF-8 bytes, sha256[:8])
def dest_decided(cap: Capture, decision: Decision, origin: Origin, at: str,
                 attempt: int | None = None) -> dict[str, Any]: ...   # attempt only when a delivery starts
def dest_delivered(d: Delivery, *, at: str, ref: str | None = None) -> dict[str, Any]: ...
def dest_failed(d: Delivery, *, code: str, retryable: bool, at: str) -> dict[str, Any]: ...
def dest_dropped(ref: str, dest: str, *, by: By, at: str) -> dict[str, Any]: ...
def dest_expired(ref: str, dest: str, *, at: str) -> dict[str, Any]: ...
def dest_changed(op: Literal["add", "rm", "bind"], dest: Dest, *, by: By, at: str) -> dict[str, Any]: ...

# dest/pending.py
@dataclass(frozen=True, slots=True)
class PendingItem:
    capture: Capture
    dest: str                                         # effective dest id
    st: Literal["hold", "running", "failed"]
    since: str                                        # ISO time of the dest.decided
    attempt: int = 0
    code: str | None = None
    retryable: bool = False

@dataclass(frozen=True, slots=True)
class PendingState:
    items: tuple[PendingItem, ...] = ()               # oldest first
    captures: Mapping[str, Capture] = field(default_factory=lambda: MappingProxyType({}))
                                                      # refs with a non-daily dest and no final outcome yet

def fold_pending(events: Iterable[Mapping[str, Any]], start: PendingState | None = None) -> PendingState: ...
def find(state: PendingState, ref: str) -> PendingItem | None: ...
def visible(state: PendingState) -> tuple[PendingItem, ...]: ...   # hold and failed

# dest/router.py   (imports model, catalog, policy, records, pending, events.records.queue_added,
#                   agents.claude_code.argv.check_text, hub.snapshots.truncate_utf8, notes.render.clean)
class SendRequest(Protocol):                          # D5-B's DestSend satisfies it
    id: str; ref: str; op: str; dest: str | None

NO_REROUTE: frozenset[str] = frozenset({"todo"})      # go with dest=todo -> E_BAD_FIELD dest (§7.4)

@dataclass(frozen=True, slots=True)
class Reply:
    code: str | None = None                           # None = ack
    field: str | None = None

@dataclass(frozen=True, slots=True)
class Step:
    records: tuple[Mapping[str, Any], ...] = ()       # to append (one batch, before anything else)
    deliveries: tuple[Delivery, ...] = ()             # to run after the append
    reply: Reply | None = None                        # dest.send answer now; None = later or not a request

def queue_text(text: str | None) -> str | None: ...   # cleaned, <= text_max bytes; None = could never be spawned
def on_capture(cat: Catalog, cap: Capture, *, online: bool, origin: Origin, at: str) -> Step: ...
def route(cap: Capture, ctx: RouteContext) -> Step: ...
def undecided_memos(pend: PendingState) -> tuple[Capture, ...]: ...   # voice captures folded, never decided
def on_appended(cat: Catalog, pend: PendingState, records: Iterable[Mapping[str, Any]], *, online: bool,
                at: str) -> tuple[PendingState, Step]: ...  # fold, then route undecided_memos (§12.5)
def on_send(cat: Catalog, pend: PendingState, req: SendRequest, *, online: bool, at: str) -> Step: ...
def on_result(d: Delivery, result: DeliveryResult, *, at: str) -> Step: ...
def expire(pend: PendingState, *, now: float, at: str) -> Step: ...
def resume(pend: PendingState, cat: Catalog, *, at: str, startup: bool) -> Step: ...
    # startup=True once at app start-up, startup=False at every link-up; no default (§6.2)

# dest/paths.py
class PathRefused(ValueError): ...                    # .reason: "outside_root" "symlink" "denied" "suffix" "hidden" "no_parent"
DENIED_UNDER: tuple[str, ...] = ("Desktop", "Documents", "Downloads", "Library")   # relative to $HOME
SUFFIXES: tuple[str, ...] = (".md", ".txt")
def check_spec(opts: FileOpts) -> None: ...           # lexical rules at parse time; raises PathRefused
def check_place(opts: FileOpts, home: Path, denied: Iterable[Path] = ()) -> None: ...
    # check_spec + deny list for absolute roots too (cpa dest add, before saving; denied = ($CPA_HOME,))
def target_path(opts: FileOpts, home: Path, denied: Iterable[Path] = ()) -> Path: ...
    # delivery time: realpath containment + deny list; raises PathRefused
def append_line(path: Path, line: str, marker: str) -> bool: ...  # O_APPEND|O_CREAT|O_NOFOLLOW, 0600, fsync;
                                                                  # False when marker already ends a line

# dest/adapters.py
@dataclass(frozen=True, slots=True)
class Probe:
    state: Literal["ok", "needs_setup", "missing", "unavailable"]
    reason: str = ""

class Adapter(Protocol):
    def probe(self, dest: Dest) -> Probe: ...         # never touches content
    def deliver(self, d: Delivery) -> DeliveryResult: ...

PBCOPY = "/usr/bin/pbcopy"
class ClipAdapter:
    def __init__(self, run: Callable[..., subprocess.CompletedProcess] = subprocess.run,
                 exe: str = PBCOPY, timeout_s: float = 2.0) -> None: ...
class FileAdapter:
    def __init__(self, home: Path, denied: Iterable[Path] = ()) -> None: ...
def file_line(cap: Capture) -> tuple[str, str]: ...   # (line, marker) — "- {stamp} #proj text ^cpa-<ref>"
def make_adapters(home: Path, denied: Iterable[Path] = ()) -> Mapping[Kind, Adapter]: ...
    # {"clip": ClipAdapter(), "file": FileAdapter(home, denied)}
def probe(dest: Dest, adapters: Mapping[Kind, Adapter]) -> Probe: ...  # unavailable / per-kind rules (§9)

# dest/specs.py  (imports only cpad.protocol.validate and cpad.protocol.limits)
SLOT_MAX = 12
DESTS_MAX = 12
LABEL_MAX = 24                                        # UTF-8 bytes
def dest_specs() -> dict[str, Check]: ...             # {"dests.snapshot"}; D5-B adds "pending.snapshot", "dest.send"

# dest/snapshot.py
@dataclass(frozen=True, slots=True)
class DestRow:
    id: str
    slot: int
    label: str
    kind: str
    key: str | None = None
    live: bool | None = None                          # only True or None on the wire
    cloud: bool | None = None
    proj: bool | None = None

@dataclass(frozen=True, slots=True)
class DestsSnapshot:
    T: ClassVar[str] = "dests.snapshot"
    rev: int
    dests: tuple[DestRow, ...]
    id: str = ""

def content_rev(rows: Sequence[Any]) -> int: ...      # zlib.crc32 of canonical JSON, 0 -> 1
def dest_row(dest: Dest) -> DestRow: ...
def dest_rows(cat: Catalog) -> tuple[DestRow, ...]: ...
def dests_snapshot(cat: Catalog) -> DestsSnapshot: ...      # fitted with codec.fit_list; rev = content_rev
def load_snapshot(path: Path) -> DestsSnapshot: ...         # load_catalog + dests_snapshot

# D5-B, not in this branch: dest/specs.py PENDING_MAX = 8, PREVIEW_MAX = 48 (UTF-8 bytes), and in a new module
@dataclass(frozen=True, slots=True)
class PendingRow:
    ref: str
    dest: str
    st: str
    src: str
    text: str

@dataclass(frozen=True, slots=True)
class PendingSnapshot:
    T: ClassVar[str] = "pending.snapshot"
    rev: int
    n: int
    items: tuple[PendingRow, ...]
    id: str = ""

@dataclass(frozen=True, slots=True)
class DestSend:
    T: ClassVar[str] = "dest.send"
    id: str
    ref: str
    op: Literal["go", "drop"]
    dest: str | None = None

def pending_rows(state: PendingState) -> tuple[tuple[PendingRow, ...], int]: ...   # (rows <= 8, total)
def pending_snapshot(state: PendingState) -> PendingSnapshot: ...
def parse_dest_send(msg: Mapping[str, Any]) -> DestSend: ... # decoded + validated (e.g. messages.Unhandled.raw)
def device_revs(revs: Mapping[str, Any]) -> tuple[int | None, int | None]: ...   # (dests, pending); None = old fw

# dest/audit.py
@dataclass(frozen=True, slots=True)
class AuditRow:
    at: str
    ev: str
    item: str | None
    dest: str | None
    detail: str
    by: str | None = None
    bytes: int | None = None
    sha8: str | None = None
    leaves_mac: bool = False

def parse_since(text: str, now: float) -> str: ...    # "7d" "24h" "30m" or ISO -> ISO lower bound
def audit_rows(events: Iterable[Mapping[str, Any]], *, since: str | None = None,
               leaves_mac: bool = False, dest: str | None = None) -> tuple[AuditRow, ...]: ...
def format_rows(rows: Iterable[AuditRow]) -> list[str]: ...

# dest/cli.py
def add_parsers(sub: Any) -> None: ...                # registers `cpa dest {ls,add,rm,bind,test}` and `cpa audit`
def dest_ls(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_add(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_rm(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_bind(paths: Paths, args: argparse.Namespace) -> int: ...
def dest_test(paths: Paths, args: argparse.Namespace) -> int: ...
def audit(paths: Paths, args: argparse.Namespace) -> int: ...
```

- `DestsError` subclasses `ValueError`, so the existing `except (..., ValueError, OSError)` in `cli/main.py` catches it directly, prints `error: …` and exits with code 2.
- `ClipAdapter.deliver`: `run([exe], input=text.encode("utf-8"), env={"LANG": "en_US.UTF-8", "LC_ALL": "en_US.UTF-8", "PATH": "/usr/bin:/bin"}, capture_output=True, timeout=timeout_s, check=False)`; `FileNotFoundError` → `E_DEST_MISSING`; `TimeoutExpired` → `E_DEST_TIMEOUT`; non-zero return code → `E_DEST_IO`. Failures in the live class are always `retryable=False` (§5.4 item 2).
- `FileAdapter.deliver`: `target_path` → `file_line` → `append_line`; `PathRefused` → `E_DEST_PATH` (not retryable); `OSError` → `E_DEST_IO` (retryable). `ref` is the block ID.
- `on_capture` only produces deliveries for `clip` and `file` (later `plugin`, `paste`); `daily` and `todo` only record `dest.decided`; when queuing, `records` gets one more `events.records.queue_added(qid=None, origin="dest", from_item=cap.ref, text=queue_text(cap.text), …)`, with `qid` assigned by `assign_qids` at append time as before. `queue_text` guarantees `check_text(text) is None`, so a queued item can always open a session later.

### 12.3 What is reused (import only, no changes)

| Existing | Used for |
|---|---|
| `notes.render.clean`, `block_id` | Body cleaning and the idempotency marker in `file_line` |
| The approach of `notes.writer.NotesWriter._append` (end-of-line marker regex, `O_APPEND`, fsync) | `paths.append_line` follows it, plus `O_NOFOLLOW` (`_append` is private and mkdirs in the notes folder, which doesn't suit writing into your vault) |
| `config.files.write_json` / `read_json`, `FILE_MODE` | Reading and writing `destinations.json` |
| `config.settings.Paths`, `cpa_home`, `secure_home` | CLI |
| `events.log.EventLog`, `events.records.iso`, `queue_added` | Appending `dest.changed`, reading for audit, queue records |
| `protocol.validate` combinators, `protocol.limits.LIMITS` | `dest/specs.py` |
| `protocol.codec.fit_list`, `protocol.messages.to_wire` | Fitting a snapshot into one line, encoding |
| `hub.snapshots.label` / `truncate_utf8` | Names and previews in snapshots; truncating queue bodies |
| `agents.claude_code.argv.check_text` | Whether a queue body can open a session (`router.queue_text`) |
| `logging.redact`'s sha8 rule | `records.digest` uses the same algorithm |
| `queue.fold`'s folding style | `pending.fold_pending` |
| `cli.main`'s `fn(paths, args, deps)` convention | `dest/cli.add_parsers` |

### 12.4 Changes to existing files (only these 5)

| File | Change |
|---|---|
| `daemon/src/cpad/cli/main.py` | `from cpad.dest import cli as dest_cli`; one line `dest_cli.add_parsers(sub)` in `build_parser()`. `dest` and `audit` can be added to the command list in the module docstring while there |
| `daemon/src/cpad/protocol/specs.py` | `from cpad.dest.specs import dest_specs`; one line `**dest_specs(),` in `SPECS` |
| `protocol/README.md` | A `dests.snapshot` row in the reply table; item 24 at the end (fields, `rev`, worst-case line length, merge order; it was item 17 when this draft was written) |
| `protocol/tools/test_schemas.py` | Add `dests.snapshot` to the expected set of types |
| `daemon/tests/unit/test_host_lines_never_start_with_word.py` | Add a `DestsSnapshot` to the host message samples |

`config/settings.py` is **not changed**: the path is computed by `dest/config.dests_path(paths)`. `limits.json`, `common.schema.json` and `outbox.item.schema.json` are not changed (their descriptive updates are done with the integration after v0.4.1 merges, see §12.5).

### 12.5 Integration (after v0.4.1 merges, a separate small PR, D5-I)

1. `protocol/messages.py`: `NoteBody` gains `how`, `live`; `HostHello.caps` gains `dest`; `DeviceHello` / `Pong` keep the raw `revs` (or gain two optional fields `dests`, `pending`). `events/records._plain_body` carries `dest`, `how`, `live`.
2. `hub/state.py`: `HubState` gains `dests: Catalog`, `pending: PendingState`, `dest_published: (rev_dests, rev_pending)`, `clock: DeviceClock | None`.
3. `hub/reduce.py`:
   - `OutboxItem(note)`: in place of v0.4.1's `dest.downgraded` branch, `capture_from_item` → `origin_of` → `router.on_capture`; after `Append((received, *step.records))`, `Send(stored)` and `WriteNote` as before, and finally one new effect `RunDelivery` per `Delivery`.
   - `Appended`: `pending, step = router.on_appended(state.dests, state.pending, records, online=…, at=…)`; in the effects, `Append(step.records)` **first**, then one `RunDelivery` per `Delivery`. Memos are decided **only** from the folded state: only those still in `pending.captures` after folding (never decided) with `source` `voice` are decided (`undecided_memos`); the `rec.result` in this batch is **never** decided directly—at startup this batch is the whole `events.jsonl`, and doing so would queue every old memo again, audit it again, and bring dropped or expired clipboard entries back into "needs you". After `step.records` is appended it comes back through the next `Appended` to be folded; the state returned by `on_appended` does not include them. To-do memos for which v0.4.1 wrote a checkbox but no `dest.*` record get one `dest.decided` (`deliver/archive`, no delivery) on first startup.
   - `LinkReady` / `Reconcile`: push `dests_snapshot` / `pending_snapshot` (the latter D5-B) only when the device reports `revs.dests` / `revs.pending`; on `LinkReady` run `router.resume(…, startup=False)` once more: running deliveries are left alone.
   - New device request `dest.send` (D5-B) → `parse_dest_send` → `router.on_send`. Re-routing to `todo` replies `E_BAD_FIELD dest` until the hub has a "write a checkbox line for an entry already in today's notes" effect (then `router` adds an item to `Step` for a notes line the hub writes first, replying `ack` only after it is written); when `go` is downgraded, it still appends `dest.decided` and replies `E_BAD_FIELD dest`.
   - New input `DeliveryFinished(delivery, result)` → `router.on_result` → append records, reply `ack` / `error` as needed, push `pending.snapshot` again (D5-B).
4. `hub/runner.py`: `RunDelivery` runs with `asyncio.to_thread(adapter.deliver, d)`, one at a time in order, and the result comes back to reduce as `DeliveryFinished` (like `RunSpawn`).
5. `app.py`: at startup, first `load_catalog` into `HubState`, then dispatch the existing single `Appended(history)`—this is the only fold of history; **do not** additionally `fold_pending(log.load())`—then `router.resume(…, startup=True)`; reload `destinations.json` by mtime (like `_reload_projects`); `router.expire` every minute in housekeeping.
6. Descriptive protocol updates: `revs` in `common.schema.json` documents `dests`, `pending`; `outbox.item` documents `how`, `live`; `fileItem` documents `how`; `hello` documents `caps.dest`; each with valid and invalid vectors.

## 13. Tests (always tests first: RED → GREEN → REFACTOR)

| Test file (new) | Assertions written first |
|---|---|
| `tests/unit/test_dest_policy.py` | All 96 combinations of §5.3 checked cell by cell (the table is the test oracle); the priority order of §5.2's seven prechecks; §5.4's four invariants (hypothesis: for any input `decide()` never yields "open a session"; the live class is `deliver` only when `online` and `confirmed`, or typed `live`); `origin_of` edges: `live` missing, `false`, a different boot, 29.9 s / 30.1 s, `up_ms` wrap (negative difference) |
| `tests/unit/test_dest_config.py` | Round trip `parse(dump(c)) == c`; missing file = defaults; `version: 2` rejected; one extra field at top level and per entry both rejected (specifically `token`, `api_key`); duplicate `id` / `slot` / `key`; fixed letters can't change, fixed destinations can't be turned off; reserved letters rejected; `label` over 24 B or containing `"` `\` or control characters rejected; `file.path` absolute, `..`, hidden segment, `.sh` extension, under `~/Documents` rejected; `leaves_mac` without `optin` → `needs_optin`; `put` / `remove` / `bind` don't change the original object; `bind` doesn't steal letters; with a bad file (including `version: 2`, an extra field) `load_catalog` returns `fail_closed()` plus `problem`, **no live destination is usable or holds a letter** |
| `tests/unit/test_dest_router.py` | Typed online `clip` → 1 `dest.decided` (with `attempt`) + 1 delivery; offline `clip` → `hold`, no delivery; `queue` / `session` → `queue.added` (`origin:"dest"`, speech `uncertain`); a 2000-character memo with line breaks into the queue, `check_text(queue.added.text) is None` (hypothesis: any body becomes a queued item that can open a session, or falls back with `bad_text`); a downgrade has records only, no delivery; `dest.send go` → `confirmed` delivery, reply deferred; re-route to a turned-off destination or `todo` → `E_BAD_FIELD dest`, nothing recorded; downgrade after `go` (destination deleted, off, no project) → records `dest.decided` and replies `E_BAD_FIELD dest`; `drop` → `dest.dropped` + `ack`; a `ref` not in pending → `E_UNKNOWN_ITEM`; `go` again while `running` → `E_BUSY`; `expire` only touches those over 24 h; `resume`: `startup` has no default, `file` re-delivers, `clip` becomes `E_INTERRUPTED`, retryable failures at most 3 times, running ones untouched on connect; memo hook `on_appended`: a new memo is decided once, history containing `rec.result` + `dest.decided` replayed again (every restart) is not decided again, dropped or expired ones don't come back, an undecided memo left by a crash is decided once at startup |
| `tests/unit/test_dest_pending.py` | Entry and exit for each event; `captures` cleared after a final result; incremental property `fold(all) == fold(second half, fold(first half))`; folding a batch of events onto the state they themselves produced changes nothing (idempotent, `attempt` not incremented again) |
| `tests/unit/test_dest_adapters.py` | `ClipAdapter`: the fake `run` receives argv of only `[PBCOPY]`, body only in `input`, env with a UTF-8 locale, error codes for timeout and missing command, Chinese unchanged; `FileAdapter` (temp folder as `$HOME`): path outside root, symlinks (parent and last component), denied folders, extension, hidden segment, parent missing, new file 0600, the same `key` delivered twice writes only one line, a newline added first when an existing file doesn't end with one; `probe` never writes files |
| `tests/unit/test_dest_snapshot.py`, `protocol/tools/test_dests_snapshot.py` | Snapshot rows sorted by `slot`, only `usable`; a bad `destinations.json` gives only the fixed four; `rev` same for same content, changes when content changes, never 0; 12-entry worst case ≤2048 B; `dest_specs()` constants match the numbers in the schema file (read the JSON and compare); vector files give the declared result through `codec.decode_line` (covered automatically by the existing `test_codec_vectors`). D5-B adds `test_dest_wire.py`: previews truncated by bytes without splitting a character, `parse_dest_send`, the constants of the other 2 schemas |
| `tests/unit/test_dest_audit.py` | Only `dest.*` listed (including v0.4.1's `dest.downgraded`); the forms of `--since`; `--leaves-mac` and `--dest` filters; the output never contains the body (build an `item.received` with a body and assert the output doesn't contain it) |
| `tests/integration/test_dest_cli.py` | Run with `main(argv)`, `CPA_HOME` and `HOME` pointed at temp folders: output and exit codes of `ls` / `add` / `rm` / `bind` / `test`; `--root` given as absolute `$HOME/Documents`, `$HOME/Library/...`, `$CPA_HOME` always refused, nothing written; `add file.x` without `--yes` waits for input, and anything but `y` leaves the file unchanged; with `--yes` it writes `optin` and appends `dest.changed`; `destinations.json` mode 0600; `audit` works without the daemon running |
| `protocol/tools/test_dest_worst_lines.py` | §7.6's worst-case `dests.snapshot` line (booleans `false`, `key` `null`): schema-valid, exactly 1843 B, ≤2048 B; 2131 B, over the limit, when `label` is all `"`. D5-B adds the other two |

The coverage target stays `--cov-fail-under=80`; `dest/` itself aims for 90%+ (everything except `cli.py` is pure functions).

## 14. Work packages and order

| ID | Content | Depends on | Where |
|---|---|---|---|
| **D5-A** | All new files and tests in `cpad/dest/`; **1** schema (`dests.snapshot`), 1 vector file, `test_dest_worst_lines.py`, `test_dests_snapshot.py`; the 5 changes in §12.4 | None | This branch `feat/destinations` |
| **F5-0** | Firmware codec knows `dests.snapshot` (`MsgType`, name table, decoder table; field validation only). **Register only this one**: registering 3 would still leave `schema count == kMsgTypeCount + 1` red | D5-A's schema | Firmware branch; **must land on main before or in the same batch as D5-A** (§7.8) |
| **D5-B** | `pending.snapshot`, `dest.send`: 2 schemas, 2 vector files, 2 validators in `dest/specs.py`, `PendingSnapshot` / `pending_rows` / `pending_snapshot`, `DestSend` / `parse_dest_send`, `device_revs`, two README lines, §7.6's two worst-case lines (measured with `rev` 4294967295), `test_dest_wire.py` | D5-A | Later branch; **lands on main in the same batch as F5-0b** |
| **F5-0b** | Firmware codec knows `pending.snapshot`, `dest.send` | D5-B's schemas | Firmware branch; same batch as D5-B |
| D5-I | §12.5's wiring (the `dest.send` part needs D5-B) | v0.4.1 merged, D5-A, D5-B | After v0.4.1 |
| F5 | `lib/core/dests` (snapshot parsing, cache, letter rules, `resolve_dest`); global and capture-page Opt+letter, G0+letter; the Opt+G picker; the "Save to" section of the "Go" page; pending in "needs you" (re-route list without to-do); `dest.send`; `note.live` / `how` | F5-0, F5-0b, v0.4.0/v0.4.1's menu and "needs you" | Firmware |
| H5 | HIL: record 3 entries offline (to-do, clipboard, `file.sb`) → plug into the Mac: to-do and file deliver automatically, the clipboard one only goes into pending, and after Enter the clipboard holds UTF-8 Chinese; Opt+C online copies at once; after `cpa dest rm file.sb` the one recorded offline falls back to today's notes | All of the above | Real device 👤 |

Completion check for this branch (D5-A) (all run from the worktree root):

```bash
cd daemon && uv run ruff check . && uv run ruff format --check . && uv run pytest --cov=cpad --cov-fail-under=80 -q
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q
uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py
python3 tools/repo/check_sizes.py --warn 400 --fail 800 protocol tools daemon/src daemon/tests
uvx lizard==1.24.1 -l python -L 50 -w daemon/src
```

## 15. Risks and things to verify

| Risk | Response |
|---|---|
| Every new schema turns firmware CI red | §7.8: D5-A's 1 schema with F5-0, D5-B's 2 with F5-0b; D5-A doesn't touch the firmware |
| Before D5-B, offline clipboard entries and failed deliveries can't reach the device | They are still filed (the today's-notes line, `dest.decided` / `dest.failed`); as soon as D5-B merges, `fold_pending` computes them from `events.jsonl` and pushes them to the device |
| v0.4.1 makes `dest` an enum | §1.3 item 1; confirm with the v0.4.1 owner before starting |
| Chinese through `pbcopy` under a LaunchAgent [todo] | Explicit UTF-8 locale; H5 tests with Chinese |
| Can a LaunchAgent write `~/Notes/...` [todo] | `~/Notes` is not on the TCC list [inf]; if it can't write, it returns `E_DEST_IO`, the entry is already filed, and it goes into pending |
| The notes vault gets synced to the cloud | `file.*` counts as "leaving the Mac" by default and needs `y`; auditable |
| Judging "just pressed" for typed copy online | Device `live` mark + same boot + 30 s window, all three required; old firmware is always `queued` (the safe side) |
| Too many pending | The snapshot only sends the oldest 8 plus the total; 24-hour expiry |
| Two Macs (before v0.8) | Each Mac pushes its own catalogue, and `rev` is a content hash so they never mix; offline entries are delivered by whichever Mac is plugged in next (as docs/13 §13 item 7 describes today) |

## 16. Needs your decision

If you don't reply, the defaults are used.

1. **Bindable letters**: `b c f j k o u x y`, with only C = clipboard by default. **Default: agree**.
2. **Global Opt+letter = take a note that goes there** (like Opt+N/T/Q/S), so every destination on the menu's "Go" page can be activated with Enter. **Default: agree**.
3. **Opt+C while typing online copies at once** (#24's "run at once when online"); offline and spoken entries need Enter in "needs you". **Default: agree**.
4. **Pending expires after 24 hours** (only the delivery is dropped; the today's-notes line remains). **Default: agree**.
5. **Append to file counts as "leaving the Mac" by default**, unless you pass `--local` to `cpa dest add`. **Default: agree**. Also please tell me which file in which vault the Obsidian inbox should write to (§18 item 7); without a reply it is not created yet.
6. **The today's-notes line is not marked with its destination**; the destination is in `cpa audit`. **Default: agree**.

## Appendix: differences settled here compared with the research draft and docs/13

| Research draft / docs/13 | Here | Why |
|---|---|---|
| ids `agent.spawn`, `ww.plugin.<id>` | `session`, `plugin.<name>`; the part before the id's dot is the kind | The kind is visible at a glance and can never change later |
| Entries carry `drev`, `prof` | They don't | The id carries its kind, so renaming never sends to the wrong place; context is v0.8 |
| Every outbound send confirmed | Once turned on, delivered automatically, every delivery audited | docs/13 §9.2's settled rule |
| `pending.snapshot` preview 80 B | 48 B | The screen can't show more; even the worst escaping case fits in one line |
| A new type `dest.result`, 4 new error codes | `ack` / `error` with existing error codes | One less type, and the firmware needs no new error codes |
| Snapshot `rev` as a counter | Content hash | The device caches snapshots, and a counter would collide after a daemon restart |
| "→ destination" at the end of the today's-notes line | Not marked | `notes/render.py` unchanged; lines without `dest` byte-for-byte the same as v0.3 |
| `file.append` path rules | Plus: only `.md` / `.txt`, no hidden segments, all of `~/Library` refused | Closes code execution like "append a line to `~/.zshrc`" |

## Appendix: evidence index

- #24 (requirements and settled rules); #19 (host identity, "Cheap now").
- docs/13 §2 principle 2, §5.2 (reserved letters), §6.2 (the menu only replays keys), §7.1 (lock screen), §9, §10.2 (in-flight channel), §11, §12.2 (no new error codes), §13, §18 items 6 and 7.
- Firmware: `lib/input/keymap.cpp:43-100` (existing bindings); `lib/protocol/messages.h:48` (`kMsgTypeCount`); `test/core/test_codec_vectors/test_main.cpp:56, 76` (assertions on schema and vector counts); `.github/workflows/ci.yml` (`pio test -e native`).
- daemon: `notes/render.py:18, 22` (`clean`, `block_id`); `notes/writer.py:55` (`_append`); `hub/snapshots.py:34, 38, 129` (`truncate_utf8`, `label`, `publish`); `protocol/codec.py:118, 131` (`serialize`, `fit_list`); `protocol/specs.py:77, 314` (`request()`, `SPECS`); `protocol/validate.py` (`obj()` allows extra fields); `protocol/messages.py` (`_pick` drops unknown fields, `Unhandled`); `events/records.py:21, 43`; `queue/fold.py`; `cli/main.py:148, 162, 206`; `config/files.py`; `recordings/records.py:66` (`rec.result` carries `memo_text`); `app.py:326, 345` (reload by mtime).
- Protocol: `protocol/README.md` (versioning rules, receiver decision order); `schema/v0/common.schema.json` (`revs`, `errorCode`); `tools/schemas.py:113` (`x-uniqueBy` supports only one key); `tools/test_worst_lines.py` (how worst-case lines are written).
- Line lengths: the numbers in §7.6 are calculated with `json.dumps(..., ensure_ascii=False, separators=(",", ":"))` plus a newline.
