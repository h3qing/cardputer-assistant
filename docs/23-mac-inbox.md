**English** · [中文](23-mac-inbox.zh-CN.md)

# 23 · To file on the Mac: unassigned memos in one list, filed or handed to AI in one press

> Status: **design draft, waiting for your decision** (§8) · Date: 2026-10-22 (revised the same day after four reviews: user, privacy and security, contract and architecture, feasibility; disagreements between reviews and the trade-offs are in §9) · Role: architecture (Mac To file) · Based on origin/main `aaa9747` (v0.4.0 plus #55; the daemon already has v0.4.1's recording edit events) · For #54; related #52, #53, #19, #34, #13, #46, #24.
> Requirements:
> - M1: unassigned memos form one list; each shows its text, which device and when; it can be filtered; each can be filed or handed to AI to kick off a workflow in one press; filing with 1–9 while recording is best, but it is hard to remember what each of 1–9 is.
> - M2: the software may behave differently on different devices and networks; each recording should note the network at the time and the name of the connected computer, because there are several Macs and their things must not get mixed.
> - M3: in the editor, hold G0 to add several voice clips (#52, docs/22): there needs to be a structure that can hold different voice memos.
> Basis: docs/15 §1, §2, §3.4–§3.12, §4.6; docs/17 §6.3; docs/18 §1.3, §2, §4, §7, §15; docs/20 D4, D10; docs/21 §5.3–§5.4 (`wt-settings`); docs/22 §2, §3.7, §4.7, §5, §7, §8 (`wt-voicecap`); docs/14 §1.2, §2.3, §5.2 (`feat/destinations`, not merged); ADR 0002, 0004; recording contract v1; `protocol/README.md` item 20; the daemon's `recordings/`, `archive/`, `mcp/`, `cli/`, `hub/reduce.py`, `queue/`, `events/`, `control/`; firmware `decode_host.cpp`; WhisperWoof PRs #149–#152 (all still open); the local recordings directory, event counts and process start time, read-only; local network commands measured (without location permission).
> Tags: **[src]** code or docs read · **[meas]** measured on this Mac · **[calc]** estimated · **[inf]** inferred · **[asm]** design assumption, can change · **[todo]** to verify with a test · **👤** needs you.
> Scope: design only. No device attached, no serial port opened, no code changed; in `~/CardputerNotes` only manifests and directory listings were read.
> Path abbreviations: `cpad/` = `daemon/src/cpad/`; `dst/` = `daemon/src/cpad/dest/` on `feat/destinations` (`b5ad361`); `WW/` = the WhisperWoof workspace `feat/cardputer-w4-summaries` (`c6b3060`, head of PR #152, includes #149 and #151, #150 already merged).

## 0. One-page summary

**Two things for you to do first:**
1. **Merge WhisperWoof #149–#152** so the list has text in it. So far none of the recordings has been transcribed; until docs/22 VC-1 (cpad transcribes by itself), transcription relies on WhisperWoof.
2. **Restart cpad once** (`cpa service install` stops and then starts the LaunchAgent [src cpad/service/launchd.py:79-80]). The running cpad was started before v0.4.1 was merged, so `cpa rec ls` reports recordings that have a project as having none (§1.3).

**Design:**
1. **What you want**: after saying something unassigned, the Mac has a "To file" list; each item shows its text, which device and when, and can be filtered by date, text and project; file it to a project in one press, or hand it to AI. **Pressing 1–9 while recording is still the fastest path**; #53 makes the recording page show who 1–9 are, and To file on the Mac is the safety net.
2. **Today**: most recordings on this Mac have no project (memos and meetings alike), and none are transcribed; there is no "filed" state (§1.3).
3. **Model** (§2): To file = memos and meetings with neither a valid project nor a valid destination; items whose project key belongs to another Mac form their own group, "other Macs' projects". Five states: new, filed, handed to AI, seen, hidden. **All state is recorded in `events.jsonl`, one record per action**; reuse `rec.edited`, `rec.hidden`, `queue.added`, `queue.done`, add a few optional fields (`done`, `by`, `voice`, `bytes`, `sha8`) and the manifest's `env`; no new files, nothing goes into WhisperWoof.
4. **Standard interface** (§3): `cpa filing ls|show|file|hand|done|hide|reopen|triage`; the JSON output is a contract with schema and vectors, `protocol/filing/v1/`. MCP only gains two read-only tools, `list_unfiled` and `get_filing_item`; **no MCP write tools** (D5).
5. **Hand to AI** (§3.4): `hand` only queues; cpad itself takes the body from the transcript, cleans it, truncates to 500 B, and it never goes into argv; meetings cannot be handed (they contain other people's words). **Opening a session is still only Enter on the device board**, always in plan mode; opening straight from the Mac is the alternative and needs its own ADR (D7). 👤
6. **Environment stamp** (§4, M2): P1 stamps only two things: **which Mac pulled it** (a name you give, empty by default, one click on "Home / Work") and **whether it was connected to this Mac while recording** (`here` only when it can be proven). **For recordings made in a pocket (most of them) we only know which Mac pulled them and when; where they were recorded has to wait for choosing a profile on the device in v0.8.** The network fingerprint is designed (§4.3) and only built if you carry "one laptop between places" (D8). 👤
7. **The stamp can only detect mixing, not prevent it**: today any cpad pulls everything off the device. Real separation comes with docs/18 v0.8 (§4.5, §8.2 item 1). 👤
8. **UI** (§5): a new Cardputer page in WhisperWoof: filters on the left (date, search, multi-select projects), list in the middle, details on the right; `1`–`9` files and jumps to the next item, Space plays, `x` marks seen, `q` hands to AI, ⌘Z undoes repeatedly; each action shows a 5-second toast "Filed to 3 acmeshop · ⌘Z to undo". It only talks through `cpa`'s JSON and open files. PR 5/5 splits into 5a read-only and 5b actions, merged by you.
9. **Without WhisperWoof**: `cpa filing triage` goes through items one by one in the terminal, same keys; no web page, no port (§3.6).
10. **Phases** (§7): P1 touches only cpad (about 1,800 lines of code and 2,000 lines of tests; the network fingerprint adds +400 / +400), released on its own, without waiting for firmware or VC-1; P2 is the WhisperWoof page (about +2,300); P3 is the device-side stamp and #53.
11. **18 decisions** all have defaults (§8.1); **only 2 need your call** (§8.2).

## 1. What you want, and how far today is from it

### 1.1 Requirements

- Unassigned memos form one list (§2.1).
- Each row has text, device and time (§3.2, §5.1).
- Filter by date, text, project, state and type; device and connection are offered only when they have two or more values (§3.1, §5.1).
- File in one press; hand to AI; after docs/14 merges, send to any configured destination (§2.2, §3.4).
- Pressing 1–9 while recording stays the main path; the device must show what 1–9 are (§1.2, §7 (#53)).
- The software behaves differently by environment: by computer, docs/18 binds each Mac to a profile (automatic); by network, only a hint, no automatic switching (§4.5, §8.2 item 1).
- Every recording gets a Mac-side stamp: which Mac pulled it and whether it was connected to that Mac while recording; for pocket recordings that is all we know (§4.2).
- There are several Macs and their things must not mix: other Macs' projects form their own group and cannot be filed with one press; real separation comes with docs/18 (§2.1, §4.5, §8.2).
- To file leaves room for voice captures (§2.5).

### 1.2 Goal: count key presses by your standard

1. **One press per item**: when the page opens, the first "new" item is already selected and focus is on the list; after an action it jumps to the next "new" item automatically.
2. **No typing at all**: filing picks from 1–9; dates are buttons; naming the computer is a button; the search box can be dictated into with WhisperWoof's own dictation key.
3. **Filing while recording stays the fastest path**: #53 shows the names of 1–9 on the recording page; To file on the Mac only catches what slipped through.
4. **Every press shows its result**: a 5-second toast says exactly where it went; ⌘Z steps back several times in a row.
5. **Stable**: nothing is deleted; one action writes one record, so nothing can stop "half filed".
6. **Everything stays on this Mac**, with no app-specific integrations (#34, ADR 0004).

| What | On the device (while recording) | Mac page | Terminal `cpa filing triage` |
|---|---|---|---|
| File to project 3 | Press `3` while recording: 1 press | `3`: 1 press, jumps to the next | `3`: 1 press |
| Listen first, then file (none have text yet today) | — | Space, `3`: 2 presses | `p`, `3`: 2 presses |
| Seen, no project needed | — | `x`: 1 press | `x`: 1 press |
| Another Mac's project, file to this Mac's 3 | — | `3`, Enter: 2 presses | `3`, `y`: 2 presses |
| Queue for AI | (#52 editor voice) | `q`, `3`: 2 presses (To file rows have no project to begin with) | `q`, `3` |
| Hand to AI, then open a session | Select on the board, Enter | `s`, `3`: 2 presses, then Enter on the device board (D7) | `s`, `3`, same |
| Find last week's memo for a project | — | "All", "Last week", project: 3 presses | `cpa filing ls --filing all --from … --project …` |
| Undo | — | ⌘Z, can repeat | `u` |
| Hide (the device recording app and AI no longer see it either) | Hide in the recording app | Button in details | `cpa filing hide ID` |

On rows with no text, `q` and `s` are greyed out and say "Not transcribed yet: file it or wait for the transcript"; pressing them does nothing, rather than acting first and then failing.

### 1.3 How it looks today

- **On this Mac** [meas]: the sessions in `~/CardputerNotes/recordings/` include memos and meetings, most without a project, none with a `dest`. Each directory holds only `manifest.json` and `seg-000.wav`, no transcript files at all. `events.jsonl` has `rec.archived` and `rec.stale`, but no `rec.result` or `rec.edited`. So today's To file would have **no text on any row**.
- **There is no "filed"**, only scattered signals:

| Signal | Where it comes from | Who honours it |
|---|---|---|
| `manifest.project` | The digit pressed while recording, written once at archive time; only looks at segment 0 [src cpad/recordings/manifest.py:73-79] | Almost every reader |
| `rec.edited{project}` | A project changed later [src cpad/recordings/records.py:96-98] | Only `cpa rec ls/show` and the device's `rec.list` [src cpad/recordings/sessions.py:81-89] |
| Segment 0 `dest` | The destination key at recording time [src protocol/README.md item 20] | Today's cpad only honours `todo` (a checkbox in the notes); everything else becomes an ordinary note line [src cpad/notes/render.py:19] |
| `rec.hidden` | Hidden on the device | `rec.list`, `cpa rec ls`, every MCP read |

- **`cpa rec ls --json` reports every row as `project:null`; that is a version mismatch, not a reading bug** [meas]:
  - The running cpad was started before v0.4.1 (`129c101`) was merged; `$CPA_HOME` also lacks the `host_id` that only v0.4.1 creates.
  - The old cpad's `{"cmd":"rec"}` rows have no `project` key; when the new `cpa` applies edits on top it uses `row.get("project")` and gets `None` [src cpad/recordings/sessions.py:81-83]. Some manifests do have a project, and so does `rec.archived`.
  - Neither side can see the mismatch: `__version__` has always been `"0.1.0"` [src cpad/__init__.py:3; daemon/pyproject.toml:3]. P1-A fixes it: control replies gain an integer `api`, and `cpa status` says "restart cpad" when cpad is older than `cpa`; `_edited` falls back to the project in `rec.archived` when the row has no `project`.
- **Real gaps this Mac cannot hit today**: MCP's `list_recordings` / `get_recording`, todos, captures, export and the tags in today's notes all look only at the manifest, not `rec.edited` [src cpad/archive/recordings.py:269-291; cpad/recordings/results.py:85-91]. With 0 `rec.edited` today it does not show yet; P1-A makes every reader use the same "effective project".
- **Other gaps**: MCP's project filter cannot express "no project" [src cpad/mcp/schemas.py:48, 137-140]; `cpa rec` never shows text by design [src cpad/cli/rec.py:1-2]; the only write paths are `cpa rec unhide` and `queue_add`, which can only queue a `todo` [src cpad/cli/rec.py:142-169; cpad/mcp/backend.py:155-166]; `device.seen` has only `dev`, serial and firmware, no computer or network [src cpad/events/records.py:97-98].
- **Naming**: "inbox" is already taken three times: the device's Inbox list in cpad (`cpad/inbox/need.py`, `inbox.dismiss` on the wire, schema in `protocol/schema/v0/inbox.dismiss.schema.json`) [src cpad/inbox/need.py:1-7; cpad/protocol/specs.py:212]; WhisperWoof's own transcription inbox (`core/cardputer/inbox-*.js`); and the target of `send_to_device` in docs/15 phase C. So every external name follows the code package `cpad/filing/`: command `cpa filing`, contract `protocol/filing/v1/`, MCP `list_unfiled`; the UI calls it "To file" (待归档).

## 2. Model

### 2.1 What counts as To file

A recording session R is in To file if and only if:
1. It is archived (has `manifest.json`). Segments not yet pulled from the device are not listed; the top of the list only says "N more segments on the device".
2. **It has no effective project**, or its effective project is not a key registered on this Mac.
   - Effective project = the project of the last `rec.edited{project}` in the log or of a queue item made by `hand` (§2.2); with neither, a memo takes the manifest's project, and **a meeting takes the project or slot of its last landed segment** (read from `rec.seg.stored`, without waiting for #46 to fix the manifest's "segment 0 only" [src cpad/recordings/manifest.py:73-79]). (2026-10-22, merged main) #50 has fixed it: `manifest.json` now also takes the last segment's project or slot [src cpad/recordings/manifest.py:77-84], consistent with the fold; the fold still reads `rec.seg.stored`.
   - When the device's 1–9 cache came from another Mac, the manifest still records a key this Mac does not know. Such rows go into **a separate group, "other Macs' projects"**, carry `asked` (the key pressed while recording), and the UI says "Pressed acme while recording (not a project on this Mac)".
3. **It has no effective destination**. Effective destination = the `dest` of the last `dest.decided`, not `daily`, and not ended by `dest.dropped`, `dest.expired` or a final `dest.failed`; for `queue` / `session` destinations there must also really be a queue item with `from_item=R`. Before docs/14 merges: only segment 0 with `dest:"todo"` counts; any other `dest`, or one with `dest.downgraded`, counts as no destination (today's cpad treats them as ordinary note lines [src cpad/notes/render.py:19]). So a zero-typing memo made by holding G0 then pressing Q without picking a project (docs/22 §3.6) shows up here instead of falling between two stools.
   (2026-10-22, docs/25 R1-D) An item on hold (`dest.decided` with `action:"hold"`: waiting for confirmation, waiting for the device) does not count as an effective destination: such a recording stays in To file and also appears in "Waiting to send" (`cpa dest pending`). Sending from the Mac (`cpa filing send`) does not change the filing state: it went to another app, and the project still needs filing.
4. Its state is "new" (§2.2).
5. The manifest has no `cap` (docs/22 §7: a recording with `cap` is one clip of a capture, known from the moment it lands). Captures are handled per §2.5.

- **Rows without text are listed too**, with a clear state: pulling, waiting for transcription, transcribing, transcriber can't run, transcript only, has summary, transcription failed, timed out untranscribed (`rec.stale`, 24 h). They can be filed without text: press Space to listen, then a digit.
- There is also a filter "**No text came out**": regardless of state, it lists everything that failed or timed out, so a recording does not quietly end up without text. A "Re-transcribe" button only shows once cpad supports re-transcription (docs/22 VC's `cpa rec retry`).
- **Typed notes are not included**: they are already in today's notes, with searchable text [asm].

### 2.2 States

| State | Meaning | How it gets there | Recorded as (one per action) | Leaves To file? |
|---|---|---|---|---|
| New | Not decided yet | Archived with no project and no destination | — | Stays |
| Filed | Filed to project P | Digit pressed while recording; changed in the device recording app; `cpa filing file`; digit on the Mac page | Existing `rec.edited{project}`, plus optional `by:"cli"` (device edits carry no `by`: absent means the device) | Leaves |
| Handed to AI | A queue item was made from it | `cpa filing hand` | Existing `queue.added` with `origin:"inbox"`, `from_item:R`, `project`, `intent`, plus `voice:true`, `bytes`, `sha8` (§3.4) | Leaves; queued, opened and done are shown under "Handed to AI" |
| Seen | Seen, no project needed | `cpa filing done`; `x` on the Mac page | `rec.edited{done:true, by}` (new field) | Leaves; **still searchable everywhere** |
| Hidden | No longer listed by the device recording app, `cpa rec ls` or MCP | `cpa filing hide`; button in details on the Mac page | Existing `rec.hidden`, plus optional `by` | Leaves; **not a single file is deleted**; refused while an open queue item exists (`E_ALREADY`) |

- **Back to "new"**: `reopen ID` removes the one cause of the current state: `rec.edited{done:false}`, `rec.unhidden`, or withdrawing the queue item made by `hand`: `queue.done{qid, by:"undo"}` (returns `E_ALREADY` if the queue item has already opened a session). Change the project with `file`; go back to no project with `file --none`.
- (2026-10-22, implemented) **`file` is also refused while an open queue item exists** (`E_ALREADY`, `field:"queue"`): the queue item keeps its project; `reopen` to withdraw it first, then file. Otherwise, after "hand to AI, then file elsewhere", repeated undo cannot get back to the start, and the row's project no longer matches the project the queue item runs in.
- (2026-10-22, implemented) **Withdrawing while the device is opening it**: `queue.spawned` is only recorded after the open finishes (up to about 20 s), so a withdrawal may land in between. The fold still counts a queue item that later records `queue.spawned` as handed to AI (`open:false`); it does not go back to "new" and cannot be delivered again.
- (2026-10-22, merged main) **`cpa queue reopen` (docs/20 U12, `queue.reopened`) also reopens handed queue items**: the fold counts it as an open `hand` again, even if it had been withdrawn, until it is withdrawn once more. So after reopening, `file`, `hide` and another `hand` still return `E_ALREADY`, and `cpa filing reopen` can withdraw it [src cpad/filing/fold.py:122-124, 143-147; tests/unit/test_filing_fold.py:111-131]. Not decided yet: if the recording was filed to another project after the queue item closed and then it is reopened, the row shows the later project while the queue item runs the original one; should reopening count as re-choosing the queue item's project?
- **Withdrawing a queue item uses `queue.done` with `by:"undo"`**, not a new event: the old cpad's fold already closes the item on `queue.done` [src cpad/queue/fold.py:11, 50]; the new fold stops counting it as "handed to AI" when it sees `by:"undo"`.
- **State comes from a pure fold**, written like `queue/fold.py`; order of checks: hidden → Hidden; a queue item with `from_item=R` that was not withdrawn → Handed to AI; effective project registered or an effective destination → Filed; `done` → Seen; otherwise → New (in the "other Macs' projects" group when the effective project is another Mac's key).
- **Old cpad does not break** [src]: `rec.edited{done}` passes `CHECKS` and changes nothing [src cpad/recordings/index.py:45-69]; `QueueItem` drops unknown fields [src cpad/queue/fold.py:36-40]. But old cpad does not know the plan rule for `origin:"inbox"`, so when `hand` finds cpad running with too old an `api`, it returns `E_UNAVAILABLE` and does not queue.

### 2.3 Where state is recorded

| Option | Pros | Problems |
|---|---|---|
| **A `events.jsonl` (recommended)** | Already has a file lock and fsync [src cpad/events/log.py:87-100]; while cpad runs, appends from other processes are picked up by housekeeping within 3 s [src cpad/app.py:354-361], which is exactly how `cpa rec unhide` works [src cpad/cli/rec.py:142-153]; queue items already live only here [src docs/15 §2.1]; replay is idempotent | Not an open file: `cpa export` turns it into `data/filing.jsonl` |
| B Another `filing.json` in the recording directory | Other programs read it directly | The recording contract gets another writer and the single-writer table changes; the same state stored twice drifts; voice captures are not in the recording directory |
| C A separate index file | Fast to read | A second source of truth; atomic writes and recovery need separate work |

- **One record per action**: `append` writes several lines with one `os.write` and then fsyncs [src cpad/events/log.py:99-100]; a crash may leave only the first few lines, and readers only skip a torn last line [src cpad/events/log.py:58-67]. So each action writes exactly one record: `file` writes only `rec.edited`, `hand` writes only `queue.added` ("filed to P" is inferred from it by the fold), and filing a capture writes only one `item.edited`. A torn tail means the action did not happen; doing it again converges.
- **Export**: `cpa export` adds `data/filing.jsonl` (one row per line, the same shape as the row in §3.2, `hidden` included, since hiding is not deleting); with the network fingerprint, `data/networks.json` is added as well. The counts in `CPA-EXPORT.json` and the export README follow; layout stays 1 (§6.2).
- **Versioning**: events only gain fields. The external contract is the shape of `cpa filing … --json` (§3.2), with the rules of recording contract v1: additive only, no `additionalProperties:false`, bump to v2 only when meaning changes [src protocol/recording/v1/README.md "forward compatibility"].
- **Never put into WhisperWoof**: it keeps no state and asks `cpa` every time; its design also explicitly keeps recordings out of its own database [src WW/docs/design/cardputer-inbox.md:248-253].

### 2.4 What happens to today's notes after filing

Today's notes are append-only and never rewritten [src cpad/notes/writer.py:36-67]; each line is drawn from one event, so it can be replayed at startup [src cpad/recordings/route.py:1-6, 69-82; cpad/app.py:122].
- **Filed before the result arrives**: the line written later simply uses the effective project. This needs a change to `results.result_record`, which today copies the context from `rec.archived` [src cpad/recordings/results.py:85-91].
- **Filed after the result arrives**: `rec.edited` carries only `rec` and `project` and cannot draw a line [src cpad/recordings/records.py:96-98]. So:
  - **only cpad writes this line**; the CLI never touches notes (`NotesWriter` has no lock, and two writers would collide [src cpad/notes/writer.py:55-67]);
  - when housekeeping picks up a `rec.edited{project}` or a queue item from `hand`, and this recording already has a `rec.result`, cpad appends a self-contained `rec.filed{rec, project, dir, mode, started_ms, uncertain, title, n}`, which `route.rec_entry` draws as `- ~HH:MM #project ↪ 归档：<title or "语音"> ^cpa-<id>-f<n>-<key>`, deduplicated by block id, old lines untouched (the `-u` suffix is the precedent [src cpad/recordings/route.py:62-66]); (2026-10-22, implemented) `n` is which `rec.filed` this is for the recording: with only `-f-<key>`, filing to cpa, changing to blog, then undoing back to cpa would find the third line's block id already present, and the notes would stay at blog;
  - today housekeeping does not write notes [src cpad/app.py:354-361]; P1-A adds it; at startup backfill replays `rec.filed` and adds one for any filing in the log that has no `rec.filed` yet.
- A capture's filing line uses the item's block id: `^cpa-<item id>-f<n>-<key>`.
- (2026-10-22, implemented) The memo's own todo line (`- [ ] … ^cpa-<id>`) keeps the `#tag` from recording time, and the later "↪ 归档" (filed) line is not a todo, so MCP `list_todos` also uses the effective project for todo lines whose block id is a recording (ids of typed notes are never recording ids, so those keep their own tags).
- Seen and hidden: nothing is written to the notes.

### 2.5 Voice captures (#52)

- In docs/22, each voice clip is a memo with `cap`, and a capture refers to them by id (`body.voice`, at most 4 clips); `vcap.assembled` is recorded when it is assembled [src docs/22 §2.1, §5.6].
- **A recording with `cap` never gets its own row** (from the manifest's `cap`, present on landing, no need to wait for the fold). A capture is one row, "Capture · 2 clips": the typed text, plus each clip's transcript or "not transcribed yet", each playable.
  - Clips deleted with Opt+D in the editor are **never listed**;
  - clips still waiting for their item are not listed; the top of the list counts them: "2 more voice clips waiting for their item";
  - clips with `vcap.orphan` (claimed by nobody, docs/22 §2.4) get their own row as ordinary memos.
- Captures of type queue, open session or reply already have a project and count as filed (shown under "Handed to AI"). Only "Note" captures whose destination is `daily` and that have no project go into To file.
- **Filing a capture = one new event `item.edited{ref, project, by}`**, read by the vcap fold: the capture's project is in `item.received.body.project`, and writing `rec.edited` on its clips cannot change it. The clips themselves get no edits.
- A clip belongs to only one capture, first come first served [src docs/22 §5.7], so it cannot be filed twice. Until VC-2's `vcap` fold is merged there are no captures, and this section does nothing.

## 3. Standard interface: `cpa filing` and MCP

### 3.1 Commands

| Command | What it does | What it writes |
|---|---|---|
| `cpa filing` = `cpa filing ls [--json]` | By default lists only "new", excluding other Macs' projects, newest first | — |
| Filters | `--from/--to YYYY-MM-DD`, `--device ID`, `--link here\|unknown`, `--mode memo\|meeting`, `--project KEY` (repeatable) `\|--none`, `--filing new\|filed\|handed\|done\|hidden\|all`, `--tx …` (the enum in §3.2), `--text WORD` or `--text -` (read from stdin; literal substring, case-insensitive, same rules as MCP `search` [src cpad/archive/search.py:1-9]), `--orphans` (include other Macs' projects), `--limit`, `--cursor`; `--net ID` with the network fingerprint; `--prof` from v0.8 | — |
| `cpa filing show ID [--json]` | Full text, sentence-by-sentence transcript (with mm:ss), todos from the summary, audio file names, firmware version, the queue item's body | — |
| `cpa filing file ID KEY` / `--none` | File to a project / back to no project; the "other Macs' projects" group needs `--other` (the UI and triage ask first, then add it) | `rec.edited{project, by:"cli"}` |
| `cpa filing hand ID [KEY] [--spawn]` | Hand to AI (§3.4) | `queue.added` |
| `cpa filing done ID` / `hide ID` / `reopen ID` | Seen / hidden / back to "new" | `rec.edited{done}` / `rec.hidden` / the reverse |
| `cpa filing triage` | Go through items one by one in the terminal (§3.6) | As above |
| `cpa project ls --json` | `{"v":1, "projects":[{key, name, slot, ok}]}`, with no local paths (today it only prints text, including paths [src cpad/cli/commands.py:29-33]) | — |
| `cpa host name -- NAME` / `--clear` [`--json`] | Name this Mac (§4.2); (2026-10-22, implemented) `--json` prints the contract's `host` document `{v, host}` (for read, set and clear); WhisperWoof's naming banner uses it to know whether a name was given | `host_label` in `config.json` |
| `cpa net …` | Only with the network fingerprint (§4.3) | `$CPA_HOME/networks.json` |
| `cpa filing send ID DEST` | After docs/14 D5-A merges: send to any enabled destination (§3.4). (2026-10-22, docs/25 R1-D implemented) Only accepts `clip`, `file.*`, `url.*`, `app.*`; `--json` prints `sent` or `error` from contract `protocol/dest-cli/v1` | Through docs/14's router: `dest.decided{origin:"mac", how:"pick"}`, then `dest.handed` / `delivered` / `failed` |

An ID is a recording session id or a capture's item id; both are MonoIds, case-insensitive.

`cpa filing` computes only from `events.jsonl` and files; it only asks cpad "how many segments are still on the device" (`counts.on_device` is `null` when cpad is not running). So even when cpad and `cpa` versions mismatch (§1.3), the list is not wrong.

(2026-10-22, implemented)
- That question waits only 0.5 s: if cpad is stuck the list still comes out with `on_device` `null`, never hitting WhisperWoof's 5 s timeout (`hand` waits 2 s to check cpad's version).
- Two conveniences for people (command line only, not in the contract, not in MCP): with only `--project`, list with `--filing all` ("new" rows never have a project); `--from` and `--to` also accept `today` and `yesterday`.
- With `--json`, arguments argparse does not know (for example a future `--net` or `--prof`) also print the error document `E_BAD_ARG`, `field:"argv"`, exit code 2; an older `cpa` prints nothing on stdout in that case, and WhisperWoof treats it as "update cpa".

### 3.2 Contract `filing/v1`: one row of `ls --json`

```json
{"v":1, "counts":{"new":3, "other_mac":0, "clips_waiting":0, "on_device":0}, "next_cursor":null, "rows":[{
  "id":"…-0002Q-0002", "kind":"memo", "filing":"new", "why":["unfiled"], "asked":null,
  "at":{"wall_ms":1792639380000, "tz_min":480, "uncertain":false}, "dur_ms":24000,
  "device":"5c0e9a7b3d21",
  "env":{"host":"公司", "link":"unknown"},
  "prof":null, "project":null, "starred":false,
  "tx":"waiting", "title":null, "text":null, "intent":null,
  "clips":null, "queue":null}]}
```

- `filing`: `new`, `filed`, `handed`, `done`, `hidden`. The field is not called `state`: in MCP's `list_recordings`, `state:"done"` means "transcribed" [src docs/15 §3.4].
- `why`: `unfiled`, `orphan` (the project belongs to another Mac), `no_text` (failed or timed out). `asked`: in orphan rows, the key pressed while recording; `null` in other rows.
- `tx`: reuses the recording `state` enum from docs/15 §3.4 (`pulling`, `waiting`, `processing`, `blocked`, `transcript`, `done`, `failed`), plus `stale`.
- `device`: the first 6 bytes (12 hex digits) of `HMAC-SHA256(id_key, "dev|"+dev)`. **`dev` is not given**: it is the device's factory MAC address [src firmware/app/lib/hal/hal_platform.cpp:39-40], which docs/15 deliberately keeps from MCP [src docs/15 §3.4 "not provided"]. `id_key` is `$CPA_HOME/id_key`, 32 random bytes, 0600, never exported or logged, like `host_id` [src cpad/config/host.py:1-5]. One per Mac, so ids from two Macs cannot be linked.
- **`dir` is not given**: docs/15 principle 6 is "give ids, not paths"; WhisperWoof scans directories itself (§5.3).
- `text`: `memo.text`, or else the first few sentences of the transcript; at most 280 characters after `display_safe()` (§3.3). With `--no-text`, `title` and `text` are not given.
- `env`: `null` for old recordings; with the network fingerprint it also has `net:{id, kind, when, label}`, where `label` is looked up from `networks.json` when read, not stored (§4.3).
- `clips` only on capture rows; `queue` is `{qid, intent, open}` after handing to AI.
- **Extensible enums** (`kind`, `why`, `tx`, `link`; later `capture`, `other`, `net.kind`) are marked with `x-knownValues` in the schema, following the precedent of `marks[].k` [src protocol/recording/v1/README.md:55]: a reader that meets an unknown value shows that item as "unknown" and does not reject the whole row.
- **Contract directory**: `protocol/filing/v1/{README.md, schema/{ls,show,result,error,projects,host}.schema.json, vectors/{valid,invalid}/, VECTORS.sha256}`, plus `networks.schema.json` with the network fingerprint; checker `protocol/filing/tools/check_filing.py`, with a `REQUIRED_INVALID` list. The schemas use only keywords WhisperWoof's validator supports (no `oneOf`, `else`, `not`, `format`, `patternProperties`, `exclusiveMinimum`, `additionalProperties`), or it throws at startup [src WW/src/whisperwoof/core/cardputer/contract-schema.js:16-37, 76-81]. (2026-10-22, implemented) The checker follows its approach with an allow-list (`KEYWORDS`, plus `x-` annotations), so things like `default` and `$comment` are blocked too; the `host` document is the output of `cpa host name --json`; lengths are counted in code points, and the vectors include characters outside the Basic Multilingual Plane.
- **Where the text goes**: `text` only reaches your terminal, the app page and (through `list_unfiled`) the MCP clients you enable. In events: `rec.*` never carries transcript sentences [src cpad/recordings/records.py:17]; `rec.result` carries the title and `memo_text` (today's notes need them) [src cpad/recordings/records.py:76-81]; `hand` copies ≤500 B into `queue.added.text`, the same as a queue item typed on the device (§3.4).

### 3.3 Rules for write commands

1. **argv holds only ids, project keys and enums**, each validated first: MonoId; `^[a-z0-9][a-z0-9-]{0,11}$` and registered [src cpad/projects/registry.py:1-6] (the one exception: undo restoring a key this recording itself carried, i.e. the other Mac's key in the manifest; (2026-10-22, implemented) also a key set on it earlier that this Mac has since deleted); enums. Bodies never go into argv. WhisperWoof's search words go through stdin (`--text -`). Names (computer names, network names) can only be fixed button values or what you type yourself in the terminal, always after `--`, and anything starting with `-` is rejected.
2. **One record per action**, returning only after fsync (§2.3).
3. **Idempotent**: if it is already in that state, nothing is written and it returns `changed:false`; `hand` again while an open queue item exists returns `E_ALREADY`.
4. **Uniform output** (schemas in `protocol/filing/v1/`): success `{"ok":true, "v":1, "id", "filing", "changed", "undo":{op, id, args}|null}`; failure `{"ok":false, "v":1, "error":"E_…", "field"?}`, with fixed error codes (`E_BAD_ARG`, `E_UNKNOWN_ID`, `E_UNKNOWN_PROJECT`, `E_NO_TEXT`, `E_ALREADY`, `E_UNAVAILABLE`), exit codes 0 / 1 / 2, input never echoed.
5. Writes work even when cpad is not running.
6. **Undo**: `undo` is a neutral object. `cpa` prints it as argv; WhisperWoof does not copy that argv but rebuilds it from its own verb table and validates the id again. Undo for `file` is `{op:"file", args:{project:<previous value or null>}}`; undo for `done`, `hide` and `hand` is `{op:"reopen"}`; a `hand` whose queue item already opened a session has no undo (`undo:null`). An undo is itself just an ordinary event, so there is no "undo failed halfway".
7. **`display_safe()`**: lives in `cpad/filing/text.py`; every output meant for people (terminal, `title` / `text` in JSON, triage prompts) goes through it: it strips C0, DEL, C1 (U+0080–009F, including U+009B), bidi controls (U+202A–202E, U+2066–2069), U+2028 / U+2029; (2026-10-22, implemented) and lone surrogate code points (U+D800–DFFF: half an emoji cut off by a transcriber, legal in JSON, but it breaks any UTF-8 output, so a single transcript could make the whole list, the export and `hand` fail). Today's `NO_CONTROL` only covers C0 and DEL [src cpad/protocol/validate.py:19]; these characters can come from LLM titles, `summary.md` or third-party transcribers (docs/22 §4.2).

### 3.4 Hand to AI

- `hand ID KEY`: without KEY it uses the effective project; with no project, or another Mac's project, it returns `E_UNKNOWN_PROJECT` (the Mac page then asks you for a digit).
- **Only memos and captures**: meetings return `E_BAD_ARG` (`field:"id"`). Meetings contain other people's words; docs/17 §6.3 requires that what enters the queue is the ≤96 B you see, prefixed "会议·" (Meeting ·); docs/22 §8 item 1 says meeting transcripts are never stitched into a queue body. Meeting todos go through docs/17 §6, confirmed one by one.
- **cpad takes the body itself**: `memo.text`, or else the stitched sentences of `transcript.json`; cleaned with `queue_text()` (control characters become spaces, truncated to 500 B without cutting a character in half); rejected if empty after truncation or starting with `-` [src dst/router.py:100-106; docs/22 §5.3]. With no text it returns `E_NO_TEXT`. **No suffix is added**: the same function and the same body rules as docs/14 and docs/22. If docs/14 merges first, use its function directly; if P1-C comes first, put it in `cpad/filing/text.py` and have docs/14 reference it when it merges.
- **Record**: one `queue.added{origin:"inbox", from_item:ID, project, intent:"todo"|"spawn", voice:true, bytes, sha8}`. `bytes` and `sha8` are as in docs/22 §5.6: the body's UTF-8 byte count and the first 8 hex digits of its sha256. `voice:true` is the field from docs/22 §5.4 item 2; not `uncertain`, which means "may have been opened already" (docs/22 §5.4 item 6).
- **Board and opening a session**: P1-C adds a rule to the `AgentSpawn` branch of `hub/reduce.py`: queue items with `origin == "inbox"` are always `plan`, regardless of the project's configured mode [src cpad/hub/reduce.py:172]; the board summary starts with "语音·" (Voice ·) (docs/22 §3.7; summaries are built in `queue_rows` [src cpad/hub/snapshots.py:101-106]). It keys on `origin` because `QueueItem` drops `voice` today [src cpad/queue/fold.py:36-40] and docs/22 has not merged yet; once it merges both rules switch to `voice`, and items from `hand` still qualify.
- **`--spawn` (D7 default)**: only queues it as `intent=spawn`; to open it you select it on the device board and press Enter in the confirm box, the existing path in ADR 0002 [src docs/adr/0002 item 3; docs/22 §5.4 items 1–2]. After `s` on the Mac page the toast says "Queued q12 · press Enter on the device board to open (plan)", and details show the queue body read back by qid (≤500 B), which is exactly what you open on the device.
- **Opening straight from the Mac (D7 alternative, 👤)**: today any process running as you can connect to `cpad.sock` (0600, separated only by Unix user [src cpad/control/server.py]) and can run `cpa …`; Codex runs commands in a pseudo-terminal, so a TTY proves no one is there either. So "the client says it showed a dialog" (`--yes`) cannot count as confirmation. If the alternative is chosen, it works like this:
  - confirmation belongs to cpad: on receiving `{"cmd":"inbox_start","qid","sha8"}`, cpad itself shows a dialog with fixed wording via `/usr/bin/osascript` (argument array), containing the body read back by qid and passed through `display_safe()`; if sha8 does not match it returns `E_BUSY`; it only accepts items with `origin:"inbox"`, not yet opened, queued within 10 minutes;
  - it cannot reuse the device's reducer branch: that would send the device an `agent.result` it does not know, and the control client times out after 5 s while opening a session takes 20 s [src cpad/hub/reduce.py:90-102, 159-174; cpad/control/client.py:15; cpad/agents/claude_code/adapter.py:34]. It needs a new `inputs.MacSpawn(qid, project, re="mac-<n>")`, sharing a pure function `_spawn_checks()` with the device, passing `mode="plan"` explicitly; completion is only appended, not sent to the device; the control handler first runs `events.poll()` and then dispatches, replying at once with `{"ok":true,"accepted":qid}`, and the caller waits for `queue.spawned` or `agent.spawn_failed`; it records `agent.spawned{via:"mac", parent}`;
  - it needs ADR 0005, amending ADR 0002 item 3, docs/15 §3.6, docs/22 §5.4 items 1–2 and the sentence at the top of `hub/reduce.py` "only online agent.* requests are executed"; and it must state that confirmation on the Mac only guards against slips, not against local programs; an agent with a shell can run `claude` directly anyway;
  - about 250 more lines of code and 350 of tests.
- **"Workflows" do not include cloud plugins**: WhisperWoof's Todoist, Notion, Slack and other plugins [src WW/src/whisperwoof/bridge/plugin-bridge.js:16-20] do not appear here. (2026-10-22, docs/25) Changed to: **any destination you have enabled and that is marked cloud** (`url.*`, `app.*`, syncing `file.*`) can be sent to with `cpa filing send`; each one was confirmed by you when added and is visible in `cpa audit --leaves-mac`; there are still no app-specific plugins.
- **Sending to other destinations (P1-G, waits for docs/14 D5-A)**: M1's "kick off a workflow", #24's "key = destination". `cpa filing send ID DEST` hands over `Capture{ref:ID, source:"voice", dest, how:"pick", project, text}`, judged by `dest/router._decided` [src dst/router.py:117-126], so `dest.decided` lands in `cpa audit` and `bad_text` still downgrades. The CLI can send to any enabled destination (`todo`, `file.*` with `optin` on, `queue`, `session`; `session` also only queues). Then `hand` is just `send ID queue|session`, leaving a single path. docs/14 §1.2 "re-routing an arbitrary old note is out of scope" needs a dated note: unfiled recordings can be re-routed with `cpa filing send`.
- (2026-10-22, docs/25 R1-D implemented) `cpa filing send ID DEST` and "Waiting to send"'s `cpa dest send REF [DEST]` are the same path:
  - it hands over `Capture{ref:ID, source:"voice", how:"pick", project:effective project, text:memo.text}`, **never the stitched transcript**; `origin:"mac"` (docs/25 §5.2); it runs `decide()` first, and if refused writes nothing and returns a fixed error code (`E_NEEDS_DEVICE`, `E_NO_TEXT`, `E_UNKNOWN_DEST`, `E_DEST_OFF`, `E_OTHER_PROFILE`, `E_BUSY` and others, see `protocol/dest-cli/v1`).
  - Meetings can only go to local files (`file.*` with `leaves_mac:false`); anything else returns `E_BAD_ARG` (`field:"id"`), as with `hand`.
  - **Different from the original plan above**: `daily`, `todo`, `queue` and `session` are not accepted (`E_BAD_ARG`, `field:"dest"`). The line in today's notes is already written; `todo` would need rewriting that line in the notes, which cpad cannot do yet (docs/14 `NO_REROUTE`); handing to AI is still `cpa filing hand ID [KEY] [--spawn]`, with its rules (plan, `origin:"inbox"`, 500 B) unchanged.
  - At most once: under the lock it first writes the `dest.decided` that starts delivery (returns `E_BUSY` if the same item is still being sent), then hands over, then records the result; failures stay in "Waiting to send" and are only retried when you deliver again.
- MCP has no tool that can open a session, and never will [src docs/15 §3.6 "writes that will never exist"].

### 3.5 MCP

| Tool | Default | Notes |
|---|---|---|
| `list_unfiled` | On (like the other read-only tools) | Same function as `cpa filing ls --json`, with **exactly the same filters**; `filing` can only be `new\|filed\|handed\|done`, and "all" **never includes hidden**; other Macs' projects only with `orphans:true`; `outputSchema` `$ref`s the contract's row schema directly ((2026-10-22, implemented) changed to an inline copy: MCP clients compile `outputSchema` themselves and cannot follow a `$ref` into `protocol/`; `tests/contract/test_filing_contract.py` pins the fields as identical, see docs/15 §3.4); text is marked untrusted data per docs/15 §3.7 |
| `get_filing_item{id}` | On | Returns the `show` schema; hidden items return "no such item" |

- **MCP rows have less than the CLI**: no `env.net` (place plus time is a movement trail), no `fw`, no `dev`, no `dir`. `env.host` is given: it is only the name you chose (D10), empty by default.
- **No write tools** (D5). #54 mentioned "optional MCP write tools", but nobody needs them: WhisperWoof writes through the CLI, and local agents can run `cpa filing file` too. If they are ever really wanted, first: a dated amendment to docs/15 §3.6 and ADR 0004 (filing and "seen" are append-only, undoable annotations); no "hide"; `hand` only with `intent:"todo"`, writing `origin:"mcp"`; the UI shows `by:"mcp"` as "changed by AI" and can filter on it; tool names `file_item`, `set_filing_state`, `hand_item`.
- **Fix along the way**: `list_recordings` / `get_recording` switch to the effective project and add `env.host`, `env.link` to rows; `list_queue` and `list_captures` skip items whose source (`from_item`) is hidden, completing docs/15 §3.4's "hidden recordings are not returned".

### 3.6 Without WhisperWoof: the terminal is enough

`cpa filing triage` shows one item per screen:

```
3/7   10-20 09:15  0:24  录音时：没连电脑  取回：公司（这台）
  （还没转写：q、s 先不能用）
  1-9 归档  0 退回  x 看过了  q 交给AI  s 交给AI（到设备上开）  p 播放  u 撤销  n 跳过  Esc 退出
  1 acmeshop  2 cardputer
→ 3 acmeshop（u 撤销）
```

(The screen reads: item 3 of 7, date, length 0:24, "while recording: not connected to a computer", "pulled by: Work (this Mac)"; "(not transcribed yet: q and s unavailable)"; keys: 1-9 file, 0 back, x seen, q hand to AI, s hand to AI (open on the device), p play, u undo, n skip, Esc quit; the project list; the result line "→ 3 acmeshop (u to undo)".)

- After each key a result line such as "→ 3 acmeshop（u 撤销）" (→ 3 acmeshop (u to undo)) is printed below, then it moves to the next item.
- Another Mac's project: a digit first asks "Pressed acme while recording (not a project on this Mac). File to 3 acmeshop? y/n". (2026-10-22, implemented) `0` asks "Drop acme and treat it as no project? y/n", with the result "→ no longer acme".
- (2026-10-22, implemented) Keys typed before the next item appears are discarded: pressing `2` twice files only the item on screen, not the next one you have not seen (keys between `q`/`s` and its digit, and between a digit and `y`, are not discarded). With a Chinese input method on, full-width `２`, `ｘ`, `？` count as the matching keys; other Chinese characters prompt "switch to English, then press". `u` says which state it went back to ("↩ undone: back to To file"). Memos whose transcription failed or timed out say "q and s unavailable; press p to listen, then file". A playback failure is reported.
- `p` plays `seg-*.wav` with `/usr/bin/afplay` (argument array). Hiding is not here; use `cpa filing hide ID`.
- No web page, no port, consistent with docs/15 §3.12 "no localhost HTTP for now".
- (2026-10-22, docs/25 R1-D, D4) **"Waiting to send" comes first**: the items from `cpa dest pending` (held ones first, then ones pre-selected by the project's usual destination) come before To file, one per screen: "Waiting to send 1/2  → Grok  #acmeshop  suggested". ↩ sends to the pre-selected destination; `g` plus a letter sends to that letter's destination (see the letters with `cpa dest ls`, e.g. `x` is Grok); `d` drops it (the line in today's notes stays); `n` skips; Esc quits. Something sent cannot be taken back (it is already in another app), so there is no `u` here. Memos in To file can also be sent with `g` plus a letter; meetings cannot.

## 4. Environment stamp (M2)

### 4.1 Who can know what

| Information | Device | This cpad | Used as |
|---|---|---|---|
| Which computer it was connected to while recording | Only knows the link is Ready; keeps nothing about the Mac after the handshake, even less after a restart [src integrate/v041 firmware/app/lib/link/link_session.cpp:74-86] | Can prove "connected to me": recorded within the same boot and the same continuous connection [src cpad/recordings/service.py:75-90; wall.py:38-45] | `link` |
| What this computer is called | — | `host_label`, not set by default today [src cpad/config/settings.py:65] | `host` |
| Network name (SSID) | No WiFi: turning it on costs 31.7–34.3 KB of heap, below the 150 KB floor [src docs/21 §5.3] | On macOS 26, `networksetup`, `ipconfig`, CoreWLAN and `ioreg` all read `<redacted>`; location permission is required [meas] | Not used |
| Network fingerprint | — | Gateway MAC (three sources agree: `arp`, RA, DHCPv6), gateway IP and subnet, interface type: no permissions, about 35 ms, no packets sent [meas] | `net.id`, only if §4.3 is built |
| Pull time | — | The manifest's `written_at` | Display |

**Recorded in a pocket** (not connected to a computer, most memos): the device knows nothing, and the Mac only knows which Mac pulled it and when. Where it was recorded has to wait for the profile you choose on the device in v0.8 (§4.4).

### 4.2 The Mac-side stamp (P1-D1)

```json
"env": {"host":"公司", "link":"here"}
```

- **`host`**: the `host_label` of the Mac that pulled this recording, at that time; `null` if not set.
  - **Not set by default** (D10): macOS computer names are often "Someone's MacBook Pro", with your real name, and would end up in every manifest, export and MCP reply.
  - Naming: the first time WhisperWoof opens the Cardputer page, a bar at the top says "Name this Mac: Home / Work" (two buttons; one click is `cpa host name -- 公司`); in the terminal, `cpa host name -- NAME`. The name goes through `host.valid_label` (1–24 B, no control characters, encodable as UTF-8) [src cpad/config/host.py:52-54], and anything starting with `-` is rejected; (2026-10-22, implemented) C1, U+2028/9 and bidi controls are rejected too: `display_safe()` would strip them from every row, so the stored name and the displayed one would differ; an invalid name makes the device reject the whole host hello [src firmware/app/lib/protocol/decode_host.cpp:17-18], so it is blocked before writing.
  - The device's Control page already shows `host_label`; once named, the device also shows "connected: Work" — that is "the name of the connected computer" from M2.
  - The manifest stores the name at that time: the manifest is written only once, as a historical record of "which Mac pulled it then". Renaming later does not change old recordings.
- **`link:"here"` only when it can be proven**: the segment's boot number equals the current connection's boot number, and `link_since.up_ms ≤ segment 0's up_ms ≤ the up_ms of the latest pong`. A dropped link always means a new hello (ping 5 s, timeout 15 s), which resets `link_since`. A cpad restart or Mac sleep can only add `unknown`, **never a false `here`**.
- **When it is computed, without slowing landing**: when segment 0 lands, a pure function computes `env` from the in-memory link state and the config; any exception gives `env:null`; no command is run; it is recorded in `rec.seg.stored`, and at archive time copied into the manifest (a new optional top-level field; the manifest is still written only once) and `rec.archived`. `_store` runs before the fsync that `file.del` waits for [src cpad/recordings/service.py:111-140], so not a single step here may be slow.
- **No nested `v`**: the manifest's own `v` governs it.
- **Contract**: recording contract v1 gains an optional `env`, one valid vector, and a recomputed `VECTORS.sha256`. The schema only uses keywords WhisperWoof's validator supports (§3.2), and cpad adds a test that pins the set of keywords the recording schema uses. WhisperWoof ignores properties it does not know [src WW/src/whisperwoof/core/cardputer/contract-schema.js:187-195]; the vectors it copies are synced by a PR (P2-c, merged by you). `env` and docs/18 D5's `prof` are two separate keys.

### 4.3 Network fingerprint (P1-D2, only if §8.2 item 1 is answered "one laptop")

With several Macs, "which Mac pulled it" already separates the places, so a network fingerprint adds no information; with one laptop carried between places, it is the input to docs/18 P8-B1 "suggest switching profile by network". So the design lives here, and P1 does not build it. When it is built:

- **Only `net:{id, kind, when}` is stored**, never names. Names live only in `$CPA_HOME/networks.json` (0600, `{"v":1,"names":{id:name}}`) and are looked up when reading and exporting. That way `cpa net forget` really forgets: place names do not stay behind in write-once manifests and events.
- **Fingerprint**: `id` = the first 6 bytes of `HMAC-SHA256(id_key, "v1|net|"+type+"|"+gateway MAC+"|"+gateway IPv4/prefix length)`.
  - Why not a plain salted hash: with the salt right next to it, about 2,000 router vendor prefixes × 2^24 ≈ 3.4e10 possibilities can be brute-forced on one GPU in seconds [calc].
  - One `id_key` per Mac, so ids from different places cannot be linked; `cpa net reset` makes a new key (all network names become invalid, and the device's short id changes too).
  - IPv6-only uses the source link-layer address from the RA; with a VPN, use the first non-tunnel interface; VRRP/HSRP virtual MACs repeat across different company networks, so the subnet is included.
- **The raw gateway MAC, IP and subnet stay only in memory**, never written to archives, events, logs, exports or MCP. A router's LAN MAC is often right next to its Wi-Fi BSSID, and BSSIDs are in public location databases, so it is one step from a location.
- **Sampling**: on its own timer with `create_subprocess_exec`, absolute paths, argument arrays, 2 s timeout: `/sbin/route -n get default`, `/usr/sbin/arp -n <gateway>`, `/usr/sbin/ipconfig getsummary <interface>`, `/usr/sbin/scutil --nwi`. Once at connect time, then every 60 s while connected. On landing, only the latest in-memory sample ≤60 s old is used. Not `system_profiler` (16 s, triggers a scan) or `wdutil` (needs sudo) [meas].
- **Parser allow-list**: take only `InterfaceType`, `Router`, the IPv4 address and prefix, and drop the rest of the output at once (when the app has location permission, `ipconfig getsummary` prints the SSID and BSSID). On a parse error or timeout, log only a fixed reason code plus `type(exc).__name__`, never the line that was read.
- **`when`**: with `here`, take the latest sample before recording started, `when:"record"`; otherwise take the moment of landing, `when:"pull"`, and the UI says "when pulled, at …". If nothing can be read, `null`; no guessing.
- **Filters only match recording time**: `--net` and the UI's "network while recording" group only match `when:"record"`; the network at pull time is shown as text only. Otherwise a memo from one place pulled at another would appear under the second place.
- **Naming** (once per network, automatic after that): `cpa net` prints "now: Wi-Fi · Home" or "unnamed"; `cpa net name --id X -- NAME` (1–16 characters, no control or bidi characters, anything starting with `-` rejected); `cpa net forget --id X`; `cpa net off|on`; `cpa net reset`. WhisperWoof only has fixed buttons "Home / Work / Out"; free-form names are only given in the terminal.
- `secure_home`'s file table gains `id_key` and `networks.json` [src cpad/config/settings.py:157-166]. MCP never gets `net`.

### 4.4 The device-side stamp (P3, with docs/18 v0.8)

- **`prof`** (docs/18 D5): the profile on the device when recording starts, chosen by you on the device; this is the honest "context at recording time".
- **`lk`**: segment 0 gets one extra bit, "link was Ready when recording started", about 7 B (`,"lk":1`); the 512 B worst case of `.M` still has about 11 B left [calc]. The Mac reads it like this: `lk=0` → not connected to a computer; `lk=1` and provably this Mac → `here`; `lk=1` but not provable → `other` (another computer, unknown which); field absent → `unknown`.
- **Not done**: carrying a summary of "the previous Mac" in `.M` or on the wire, which violates docs/18 §4.2 (the device never echoes `host_id` or reveals other Macs); WiFi scanning on the device, which needs G-WIFI and about 32 KB of heap, and cannot happen before recording without delaying every start by 1.6–4.7 s [src docs/21 §5.4].

### 4.5 How filters and context profiles use it

- **By computer**: automatic. docs/18 v0.8 binds each Mac to a profile; the device only hands profile P's items to the Macs bound to P, with one 1–9 per profile.
- **By network**: only a hint, no automatic switching. The network fingerprint (if built) is only a suggestion you confirm on the device (docs/18 principle 1); SSIDs and gateways can be spoofed.
- **The stamp is only an annotation**: it never decides the profile, never routes, never changes the project.
- **The stamp detects, it cannot prevent**: today any cpad pulls everything off the device [src docs/18 §1.1]; the device cache's 3 may be a different project on another Mac [src cpad/recordings/manifest.py:73-79]. So before v0.8:
  - other Macs' projects form their own group, need a second confirmation after the digit, and MCP does not give them by default (§2.1, §3.5);
  - no `--host` or "computer" filter: each Mac's `~/CardputerNotes` only holds what it pulled itself, so `host` only ever has one value. Add it when the archives of several Macs are merged.
- From v0.8: To file gains `--prof`; queue items from `hand` carry the recording's `prof` (docs/18 principle 4: derived things follow their source).

## 5. UI: WhisperWoof's Cardputer page

### 5.1 Placement and layout

- In the sidebar's main group, after Meetings, add **Cardputer**, with lucide's Inbox icon and the To file count beside it [src WW/src/components/ControlPanelSidebar.tsx:73-97]. Not inside Meetings: Meetings are notes in the database, and the design doc keeps recordings out of WhisperWoof's database [src WW/docs/design/cardputer-inbox.md:248-253].
- **Three columns**, following the Notes page [src WW/src/whisperwoof/ui/notes/VoiceNotesView.tsx:265-420] and DESIGN.md §6 (glass only for navigation, solid cards for content, one caramel accent):
  - **Filter column** (glass):
    - State: To file (N, default), Other Macs' projects (N, only when present), Handed to AI, Seen, No text came out, All;
    - Date: Today / This week / Last week / All (buttons, no typing);
    - Search box: like the Notes page's search box [src WW/src/whisperwoof/ui/notes/VoiceNotesView.tsx:294-300], dictatable with WhisperWoof's own dictation key; `/` enters the search box, Esc returns to the list;
    - Projects 1–9, multi-select, rows can be dragged onto them; the numbering is **the same 1–9 this Mac gives the device**, from `cpa project ls --json`;
    - Memo / meeting; device, whether connected to this Mac while recording, network while recording (only shown when there are two or more values).
  - **List**: an opening sentence "7 to file, 3 today" (DESIGN §11); separated by day; each row shows the title or first sentence ("Memo · 0:24 · not transcribed yet" when there is no text), relative time, length, small tags, state (icon plus text plus colour, never colour alone, DESIGN §7), a "todo" tag, ★. Rows from another Mac's project add "Pressed acme while recording (not a project on this Mac)".
  - **Details**: title; "Recorded at 08:10 (not connected to a computer) · pulled by Work at 13:00", with "~" when the time is uncertain; project picker; play; sentence-by-sentence transcript (`[mm:ss]`); `summary.md` (links shown as plain text, images not rendered); after handing to AI, the queue body read back by qid; buttons "File to…" (the only primary button), "Hand to AI", "Seen", "Hide (device and AI won't see it)", "Show in Finder".
  - **Empty state**: Mando's head and "All filed".

### 5.2 Keys

Single keys only work when focus is on the list; typing in the search box never files anything.

| Key | What it does |
|---|---|
| ↑ ↓ / `j` `k` | Move |
| `1`–`9` | File to the project with that number and jump to the next; the "other Macs' projects" group asks first, Enter confirms |
| `0` | Back to To file |
| Space | Play / stop |
| Enter | Open details |
| `q` | Queue for AI; with no project, press a digit next |
| `s` | Hand to AI, queued as "open a session"; with no project, press a digit next; then press Enter on the device board |
| `x` | Seen |
| `/` | Enter the search box |
| ⌘Z | Undo; can repeat, stepping back several actions since the page was opened, and re-selects the restored row |

- **When the page opens** the first "new" item is already selected and focus is on the list.
- **Every action gives feedback**: a 5-second toast "Filed to 3 acmeshop · ⌘Z to undo" ("Queued q12 · press Enter on the device board to open"); a wrong digit is visible at a glance.
- **Rows without text**: `q` and `s` are greyed out, with "Not transcribed yet: file it or wait for the transcript".
- After a successful action, selection jumps to the next "new" item; on failure it says why right there (e.g. "cpad is too old, restart it first") and does not jump.

### 5.3 Where the data comes from

- **State and text only come from `cpa`**: run `cpa filing ls --json` when the page opens, when `fs.watch` on `notes_dir/recordings` fires (2 s debounce, **never watching `$CPA_HOME`**), every 30 s, and when the window gains focus; details run `cpa filing show ID --json`. `cpa rec ls --json` measured 0.11 s [meas].
- **Audio is read from open files**: the main process finds the directory by id with its own scan (`O_NOFOLLOW`, must be inside the root, no symlinks followed [src WW/src/whisperwoof/core/cardputer/session-files.js:17-89]), reads `seg-*.wav` (≤20 MB) and hands it to the page to play; it never uses a path from `cpa` (the contract has no paths anyway).
- **Running `cpa`**: `execFile(cpa, argv, {shell:false, timeout:5000, maxBuffer:4 MiB, env:buildCpaEnv()})`.
  - `buildCpaEnv()` is a real allow-list: `HOME`, `USER`, `LOGNAME`, `LANG`, `LC_*`, `TMPDIR`, `CPA_HOME`, with `PATH` fixed to `/usr/bin:/bin:/usr/sbin:/sbin`. The existing `buildSidecarEnv` is a deny-list, which passes `PYTHONPATH`, `PYTHONHOME`, `DYLD_LIBRARY_PATH` and `DYLD_FRAMEWORK_PATH` through to the Python-based `cpa` [src WW/src/whisperwoof/bridge/sidecar-env-pure.js:35-46].
  - `cpa`'s location is found once in main (`~/.local/bin`, `/opt/homebrew/bin`, `/usr/local/bin`, or picked by you in a native dialog), resolved with realpath, required to be owned by the current user or root and not writable by group or others, and shown in settings.
  - Arguments are validated first; output shape is validated against `protocol/filing/v1` in a pure module; stdout is never logged; search words go through stdin.
  - Undo: main rebuilds argv from the `undo` object with its own verb table and validates the id, never copying it.
- **The page only sends ids, project keys and enums** to main, never paths or text. New IPC uses the `cardputer-tab-*` prefix (the existing `whisperwoof-cardputer-*` is pinned by a test that allows only three calls [src WW/src/whisperwoof/core/cardputer/inbox-shell.test.ts:308-319]), **plus a similar pinning test**: list every `cardputer-tab-*` call and assert none carries a path or text.
- **Without `cpa` installed**: only a plain "Recordings" list (not called To file, no count in the sidebar), with "Install cpa to file recordings" at the top. The filing logic is not reimplemented in WhisperWoof; that would bring back §1.3's "different readers, different answers".
- **Transcription**: in P1 WhisperWoof transcribes (the "Transcribe Cardputer recordings" switch on); after VC-1 cpad transcribes (switch off, docs/22 §4.7); To file works with both.
- The line in WhisperWoof's design doc "later open a socket to cpad" [src WW/docs/design/cardputer-inbox.md:252-253] changes to "through the `cpa` command line", consistent with docs/15 §4.6 and #34.

### 5.4 PR 5/5

- Based on #152; about 26 files, +2,200 lines (1,900–2,600) [calc: compared with the Notes page's #124 at 17 files +813, and #152 at +2,679].
  - **5a read-only**: list, details, filters (date, search, multi-select), playback, Show in Finder, `buildCpaEnv`, the IPC pinning test, plus the fixes below, about +1,400; needs P1's `cpa filing ls/show --json`.
  - **5b actions**: keys, toasts, multi-step undo, confirmation for other Macs' projects, the naming buttons, about +800; needs P1's write commands.
- **Fix three things from #151 along the way**:
  - The "Open" folder button on the settings page does nothing: `openExternal("file://…")` is refused by a handler that only allows http, https and mailto, without any error [src WW/src/whisperwoof/ui/settings/CardputerSection.tsx:77-84; WW/src/helpers/ipcHandlers.js:1810-1817]. Switch to `shell.openPath` in main; **this IPC takes no arguments**; main uses its own stored root, and `lstat` must show a real directory not ending in `.app` (`openPath` can launch app bundles).
  - A backlog produces one system notification per segment: opening the inbox with a backlog of N segments pops N notifications [src WW/src/whisperwoof/core/cardputer/inbox-runner.js:168-173]. Change to one per round, "3 recordings ready", which opens the Cardputer page when clicked.
  - Text shown in native dialogs and on the page goes through the same `display_safe` rules (§3.3).
- You merge; agents only open PRs.

## 6. Security and privacy

### 6.1 Rules

| Rule | How it holds | Basis |
|---|---|---|
| Local only | cpad, `cpa` and WhisperWoof are all on this Mac; audio and text are not uploaded; "workflows" exclude cloud plugins (2026-10-22, docs/25: leaves the Mac only through a cloud-marked destination you chose for this item and confirmed when enabling it, `cpa audit --leaves-mac`) | #34; ADR 0004 |
| No ports | WhisperWoof calls `cpa` with `execFile`; MCP uses stdio; the terminal version opens no web page | docs/15 §3.1, §3.12 |
| Argument arrays | `cpa`, `afplay`, `route`, `arp`, `ipconfig`, `scutil` and `claude` never go through a shell; bodies always after `--` | [src cpad/agents/claude_code/argv.py:47-48] |
| Bodies never in argv | Commands only take ids, keys and enums; names after `--`, anything starting with `-` rejected; search words through stdin; cpad takes, cleans and truncates bodies itself | §3.3, §3.4 |
| Path checks | Only MonoIds; no paths in the contract; each side finds directories by its own scan; realpath must be inside the root, `O_NOFOLLOW`, size-limited | [src cpad/recordings/results.py:94-149; WW session-files.js] |
| Different Macs don't mix | `env` is only an annotation; other Macs' projects form their own group, need a second confirmation, and MCP does not give them by default; isolation comes from docs/18 | §2.1, §4.5; docs/18 §2 |
| The device's MAC is never given out | Rows carry a short id computed with `id_key`; `dev`, `fw` and `dir` never reach MCP | docs/15 §3.4 |
| Computer names carry no real name | `host_label` is empty by default and only uses a name you choose | §4.2 |
| No location-level data on disk (with the fingerprint) | Gateway MAC, IP and subnet only feed an HMAC in memory; parser allow-list; errors log only reason codes; no SSID, no location permission; names only in `networks.json`; MCP never gets `net` | §4.3 |
| Where text goes | `rec.*` carries no transcript sentences; `rec.result` carries the title and `memo_text`; `hand` copies ≤500 B into `queue.added.text` (with `bytes`, `sha8`), like queue items from the device; audit only records lengths; WhisperWoof only logs types and error codes | §3.2; docs/15 §3.9 |
| Hidden means hidden | Hiding is refused while an open queue item exists; `list_queue` and `list_captures` skip items whose source is hidden; `list_unfiled` never returns hidden items | §2.2, §3.5; docs/15 §3.4 |
| AI clients can only read | To file has only two read-only MCP tools; no write tools; never able to open a session | docs/15 §3.6 |
| Sessions are confirmed only on the device | `hand --spawn` only queues; `origin:"inbox"` is always plan, with the summary prefixed "语音·" (Voice ·); ADR 0002 unchanged | ADR 0002; docs/22 §5.4 |
| Agents with a shell | They can do anything `cpa` can (file, hand to AI) but cannot open a session; the boundary is their own shell approval | §3.4 |
| Transcripts are untrusted data | `queue_text` cleans them; `display_safe()` strips C0, C1, bidi controls, U+2028/9; links in `summary.md` are shown as text, images not shown; MCP marks them untrusted | docs/15 §3.7; docs/22 §5.4 |
| Meetings never enter the queue | `hand` refuses meetings; meeting todos go through docs/17 §6 | docs/17 §6.3; docs/22 §8 |
| Nothing is deleted | "Hide" is `rec.hidden`, files untouched; every action can be undone | [src cpad/cli/rec.py:1-8] |
| Single writer | cpad never touches the transcriber's files; To file state is never written into recording directories; only cpad writes today's notes | Recording contract v1; §2.4 |
| `cpa` itself is trusted | WhisperWoof checks `cpa`'s owner and permissions; environment allow-list; `openPath` takes no arguments | §5.3, §5.4 |

### 6.2 Docs to revise along with P1

These docs describe what the code does now, so each revision is merged in the same PR as the code change, not earlier.

| Doc | Revision | With which package |
|---|---|---|
| docs/15 §1 item 3, §4.6; ADR 0004 decision point 3 | The external local contracts go from two to three: schema-backed CLI `--json` output under `protocol/…/v1` is the human-operated channel, needs no enabling, and `rec.*` records carry `by:"cli"` (handing to AI is `origin:"inbox"`, withdrawing is `by:"undo"`); WhisperWoof uses it, not `cpa mcp` | P1-B |
| docs/15 §2.4 | Export adds `data/filing.jsonl` (plus `data/networks.json` with the fingerprint); `CPA-EXPORT.json` counts and the export README; layout stays 1 | P1-B |
| docs/15 §3.4 | New tools `list_unfiled`, `get_filing_item`; `list_recordings` uses the effective project and adds `env.host`, `env.link`; `list_queue` and `list_captures` skip items whose source is hidden | P1-E |
| docs/22 §5.4 item 3, §3.7 | plan and "语音·" (Voice ·) also key on `origin:"inbox"` | P1-C |
| docs/14 §1.2 | Unfiled recordings can be re-routed with `cpa filing send` | P1-G |
| The note at the top of `hub/reduce.py` | Changed only if the D7 alternative is chosen | ADR 0005 |
| WW `docs/design/cardputer-inbox.md:252-253` | socket → `cpa` command line | P2-a |

## 7. Phases, work packages, tests

Always write the failing test first (RED → GREEN → REFACTOR). All of P1 can be tested without a device: `tests/support/rec_shell.py` and `tests/unit/test_reduce.py` exist, and hypothesis is already a dependency.

### 7.1 Work packages

| Package | Content | Where | Code / tests (lines) [calc] | Depends on |
|---|---|---|---|---|
| **P1-A fold and readers** | To file fold (pure function); effective project (including a meeting's last segment and `hand` queue items) and effective destination; every reader uses the effective project; `result_record` uses it; `rec.filed` and its note line (written by housekeeping, replayed by backfill); the `_edited` fallback; `api` in control replies and the `cpa status` hint | New `cpad/filing/{fold,rows,text}.py`; changes `archive/{recordings,todos,events_view,export}.py`, `recordings/{results,route,records,sessions}.py`, `app.py`, `control/server.py`, `cli/main.py` | 500 / 600 | none |
| **P1-B CLI and contract** | `cpa filing` ls, show, file, done, hide, reopen, with filters, error codes, undo objects, `display_safe`; `cpa project ls --json`; `cpa host name`; schemas, vectors and checker in `protocol/filing/v1/`; `filing.jsonl` in the export | New `cpad/cli/filing.py`, `protocol/filing/`; changes `cli/main.py`, `cli/commands.py`, `archive/export.py` | 600 / 550 | P1-A |
| **P1-C hand to AI** | `hand` (one `queue.added`, meetings refused, `api` check); shared `queue_text`; `origin:"inbox"` → plan and "语音·" (Voice ·); folding `queue.done{by:"undo"}`; hide refused while a queue item is open | `cpad/filing/`, `hub/reduce.py`, `hub/snapshots.py`, `queue/fold.py` | 160 / 260 | P1-B |
| **P1-D1 computer and connection** | `link_since`; a pure function computing `env{host, link}`, into `rec.seg.stored` / manifest / `rec.archived`; recording contract gains `env` and a vector; a test pinning schema keywords | `recordings/{service,manifest,records}.py`, `protocol/recording/v1/` | 150 / 200 | none |
| **P1-D2 network fingerprint (see Q1)** | Sampling timer, allow-list parser, HMAC, the network part of `id_key`, `networks.json`, `cpa net`, canary tests | New `cpad/config/netenv.py`, `cpad/cli/net.py`; changes `config/settings.py` | 400 / 400 | P1-D1; §8.2 item 1 answered B |
| **P1-E MCP** | `list_unfiled`, `get_filing_item`; `list_recordings` / `get_recording` use the effective project and add `env.host`, `env.link`; `list_queue` and `list_captures` skip items whose source is hidden; a test that `outputSchema` references the contract (implemented: an inline copy plus a field-identity test, §3.5) | `cpad/mcp/{catalog,tools,schemas,backend}.py` | 180 / 220 | P1-A, P1-B |
| **P1-F terminal** | `cpa filing triage`: result lines, confirmation for other Macs' projects | New `cpad/cli/triage.py` | 200 / 170 | P1-B |
| **P1-G send to destinations** | `cpa filing send ID DEST` through docs/14's router; `hand` folded into it | `cpad/filing/`, `dest/` | 150 / 200 | docs/14 D5-A; P1-C |
| **P2-a** | WhisperWoof 5a read-only | WW | +1,400 | P1-B released; #149–#152 merged |
| **P2-b** | WhisperWoof 5b actions | WW | +800 | P2-a, P1-C |
| **P2-c** | WhisperWoof syncs the recording contract vectors (`env`) and the `filing/v1` vectors, with a `SOURCE` file | WW fixtures | +80 | P1-B, P1-D1 |
| **P3-a** | Device `prof`, `lk` | Firmware `rec_meta`, `file.items`, schema | With docs/18 P8-F0/F2 | docs/18 |
| **P3-b** | #53 recording page shows 1–9; numbered from the same `projects.json` as the Mac page | Firmware recording page | With #53 | `feat/ux-decisions` |

**Totals** [calc]: P1 (A–F, without D2 and G) about 1,790 lines of code and 2,000 of tests; with D2 about 2,190 / 2,400; P1-G counted separately. P2 about +2,280. The D7 alternative adds about 250 / 350.

```
You: merge WW #149–#152 (or docs/22 VC-1) ── only then does the list have text
P1-A ─► P1-B ─► P1-C, P1-E, P1-F
P1-D1 (independent) ─► P2-c; Q1 answered B ─► P1-D2
docs/14 D5-A ─► P1-G
P1-B released + #149–#152 merged ─► P2-a ─► P2-b (also needs P1-C)
docs/18 P8 ─► P3-a; feat/ux-decisions ─► P3-b
```

### 7.2 Tests

- **Fold** (hypothesis property tests): for event streams in any order, the state is unique and independent of how many times it is replayed; after `undo` it returns to the original state (including restoring another Mac's original key); **a torn tail means the action did not happen, and doing it again converges**; segments with `cap` never get their own row; deleted clips are never listed; clips waiting for their item are only counted; `vcap.orphan` clips get their own row; a clip cannot be filed twice.
- **Effective destination**: a zero-typing Q memo with no project counts as To file; one with `dest.downgraded` counts as having no destination; `todo` counts as a destination; `dest.decided{queue}` without a matching queue item counts as no destination.
- **Effective project**: manifest with / without × `rec.edited` with / without × `hand` with / without × key registered / not registered × memo / meeting (a meeting takes its last segment): CLI, MCP, todos, captures, export and today's notes all give the same answer; `cpa rec ls` is still right when an old cpad's rows lack `project`; `cpa status` suggests a restart when cpad is older than `cpa`.
- **Today's notes**: filing after the result writes one `rec.filed` and one line; backfill replay does not duplicate; the CLI never writes notes.
- **CLI**: every subcommand's JSON passes `check_filing.py`; every `invalid/` vector is rejected; invalid ids, keys, enums and names return fixed error codes without echoing; writes work whether cpad is running or not; `display_safe` strips C1, bidi controls, U+2028/9.
- **Hand to AI**: meetings return `E_BAD_ARG`; leading `-`, over-long text, control characters and empty transcripts are either cleaned or rejected; an `origin:"inbox"` item opened on the device is plan even if the project is configured `acceptEdits`, and its summary starts with "语音·" (Voice ·); with cpad's `api` too old it returns `E_UNAVAILABLE` and does not queue; `hide` returns `E_ALREADY` while a queue item is open; after `queue.done{by:"undo"}` it no longer counts as handed to AI, and the old fold closes it too; [todo] a plan session opened from such a queue item cannot call cpa's MCP write tools (not even `queue_add`).
- **MCP**: `list_unfiled`'s output passes the contract's row schema; no `dev`, `fw`, `dir` or `env.net`; never returns hidden items; no other Macs' projects without `orphans:true`; `tools/list` has no tool that can open a session or change filing; `list_queue` and `list_captures` skip items whose source is hidden.
- **Environment stamp**: the here / unknown boundary (a different boot, before connecting, after a new hello); when the `env` function throws, `env:null`, and `rec.seg.stored` is not delayed by a step; computer-name validation (emoji, control characters, over-long, leading `-`); the recording schema's keyword set is pinned.
- **Network fingerprint (when built)**: the parser runs on redacted samples (Wi-Fi, Ethernet, VPN, IPv6-only, no arp entry, output containing SSID/BSSID); parse failures and timeouts log only reason codes; **canary**: fake gateway MAC, IP, SSID and BSSID never appear in manifests, `events.jsonl`, logs, exports or MCP output; `id_key` is not in the export; after `cpa net off`, `net` is `null`; `--net` only matches `when:"record"`.
- **Terminal**: a pseudo-terminal script presses `1`–`9`, `u`, `x`, and `y`/`n` for other Macs' projects, and asserts the events and result lines.
- **WhisperWoof** (vitest, node environment): pure modules (filters, grouping, keys, row building, argv building, undo rebuilding, output validation); a `cpa` client with a fake `execFile` (timeout, non-zero exit, bad JSON); `buildCpaEnv` passes only the allow-list; the `cardputer-tab-*` pinning test; `openPath` refuses `.app`; `renderToStaticMarkup` rendering (toasts, greyed `q`/`s`, the other-Mac sentence); main wiring.
- **End to end**: a temporary `CPA_HOME` and notes directory with 4 fake recordings (one transcribed, one failed, one belonging to a capture, one with another Mac's key); run `cpa filing triage` once, then assert with `cpa filing ls --json`.

## 8. Decisions, and what needs your call

### 8.1 Decisions

With no reply, the defaults are used.

| # | Decision | Default (recommended) | Alternative | Why | See |
|---|---|---|---|---|---|
| D1 | What To file holds | Memos and meetings with neither an effective project nor an effective destination; other Macs' projects in their own group; one row per capture; rows without text listed too; typed notes excluded | Memos only | Items with a destination already have a home; a Q memo without a destination must not fall between two stools | §2.1 |
| D2 | States and the "don't need it" key | New, filed, handed to AI, seen, hidden; one `x` = seen, hide is a button in details | Two keys, `e` handled and `x` don't need | One key fewer to remember (M1 already notes that keys are hard to remember); hiding affects the device and AI, so it should not be a single key | §2.2, §5.2 |
| D3 | Where state lives | `events.jsonl`, one record per action; exported as `data/filing.jsonl` | `filing.json` in the recording directory; a separate index | Lock and fsync already exist; one record cannot be half written | §2.3 |
| D4 | Today's notes after filing | Filed before the result: the new project is used directly; filed after: cpad appends a self-contained `rec.filed`, drawn as the `-f<n>-<key>` line; old lines unchanged | No catch-up | Notes are append-only; each line is drawn from one event and can be replayed; only cpad writes notes | §2.4 |
| D5 | Interface | `cpa filing` + contract `protocol/filing/v1`; MCP only has the read-only `list_unfiled` and `get_filing_item` | Add three MCP write tools, off by default | Nobody needs write tools; docs/15 §3.6 says MCP does not change existing content; WhisperWoof uses the CLI | §3 |
| D6 | Text | Terminal, page and MCP give ≤280 characters (through `display_safe`); `rec.*` carries no transcript sentences; `hand` copies ≤500 B into the queue item | Title only | Without the text you cannot file; queue items follow the same rules as those typed on the device | §3.2 |
| D7 | Where a session is opened after handing to AI | Only queued as `intent=spawn`, Enter on the device board, always plan; ADR 0002 unchanged | Open on the Mac: cpad shows its own dialog, binds the body by sha8, a new `MacSpawn` input, ADR 0005 (+250 / +350 lines) | Confirmation on the Mac cannot stop local programs; only Enter on the device proves a person is there; the alternative only guards against slips | §3.4 👤 |
| D8 | How much environment stamp | P1 only does the computer name and "connected to this Mac while recording"; the network fingerprint waits until §8.2 item 1 is answered "one laptop" | Build the network fingerprint now (+400 / +400 lines) | With several Macs the computer already tells places apart; a fingerprint adds nothing | §4.2, §4.3 👤 |
| D9 | Network fingerprint (when built) | HMAC with a local key (type + gateway MAC + subnet), only the id stored; names only in `networks.json`; parser allow-list; no SSID | Plain salted hash | A salted hash is brute-forced in seconds; place names must be truly forgettable | §4.3 |
| D10 | Computer name | `host_label` empty by default; one click on "Home / Work" or `cpa host name -- NAME` | Use the macOS computer name by default | Computer names often carry real names, which would end up in every manifest, export and MCP reply | §4.2 |
| D11 | Device-side stamp | With docs/18 v0.8: `prof` plus one `lk` bit; no "previous Mac" summary, no WiFi scan on the device | — | The device never reveals other Macs; a WiFi scan lacks heap and also slows down starting a recording | §4.4 |
| D12 | UI | A new Cardputer page in WhisperWoof, date buttons, search, multi-select projects, 5-second toasts with multi-step undo; PR 5/5 split into 5a / 5b; only through `cpa` JSON and open files | Put it in the Meetings page | Recordings do not go into WhisperWoof's database; a wrong press is seen and undone at once | §5 |
| D13 | Without WhisperWoof | `cpa filing triage`, single keys in the terminal | A local web page | No ports (docs/15 §3.12) | §3.6 |
| D14 | Release | P1 ships on its own, Mac only, without waiting for firmware or VC-1; P2 waits for #149–#152 to merge and for P1; P1-G waits for docs/14; P3 goes with v0.8 | Split P1 into two releases | Rows without text can still be listened to and filed; the re-transcribe button shows once cpad supports it | §7 |
| D15 | Other Macs' projects | Own group, showing the key pressed while recording; Enter after the digit; MCP does not give them by default | Treat them as ordinary To file items, filed with one digit | Such an item most likely came from one Mac and was pulled by another, and one digit would mix them | §2.1, §3.5 |
| D16 | Can meetings be handed to AI | No (`E_BAD_ARG`); meeting todos go through docs/17 §6, confirmed one by one | Hand the first 500 B | Meetings contain other people's words; docs/17 and docs/22 both keep them out of the queue | §3.4 |
| D17 | Withdrawing a hand to AI | `queue.done{by:"undo"}` | A new event `queue.cancelled` | Old cpad closes the item too, without the device and other readers having to learn a new event | §2.2 |
| D18 | External names | `cpa filing`, `protocol/filing/v1`, MCP `list_unfiled`; the UI calls it "To file" (待归档) | `cpa inbox` | "inbox" already means the device's Inbox and WhisperWoof's transcription inbox | §1.3 |

### 8.2 What needs your call

1. **Is it A: several Macs (e.g. one at home, one at work) with the device carried between them, or B: one laptop carried between places?** (docs/18 §15 items 1 and 9.)
   - **A (default)**: the device only hands each Mac the recordings that belong to it. That means pulling docs/18's device-side profiles (protocol, firmware and cpad) forward to right after P1. Until then, To file covers it with the "other Macs' projects" group. No network fingerprint; the computer name is enough to tell them apart.
   - **B**: the laptop suggests switching profiles by network (docs/18 P8-B1), and the network fingerprint (P1-D2) is built as its input.
   - A related small question, from M2's requirement that the software behave differently by device and network: **by default, split by computer automatically; by network, only a hint, no automatic switching**.
2. **D7: after handing to AI, can a session be opened straight from the Mac?** **Default: no**; press Enter on the device board (ADR 0002 unchanged, plan mode). The cost is that even when you are at the Mac you pick up the device and press once. Alternative: cpad shows its own confirmation dialog on the Mac (§3.4), which needs ADR 0005; it only guards against slips, not against other programs on this Mac.

## 9. Disagreements between reviews, and trade-offs

The corrections from all four reviews (user, privacy and security, contract and architecture, feasibility) are already written into the sections above. Below are the points where they disagreed.

| Disagreement | Chosen | Why |
|---|---|---|
| D7 default: confirm on the Mac and open (first draft) vs back to confirming on the device (privacy, contract and feasibility all pointed out that `--yes` is the client vouching for itself) | Confirm on the device; drop `--yes` and `--start` | Three reviews agree: any confirmation on the Mac can be answered by a local program |
| How to build the D7 alternative: the client reads back the body and carries sha8 (privacy) vs cpad shows its own dialog (contract) | Both combined: cpad shows the dialog, containing the body read back by qid, bound by sha8 | Confirmation belongs to cpad, not to a client flag; sha8 guarantees the dialog shows what will be opened |
| MCP write tools: keep after amending docs/15 (privacy, contract) vs don't build (user, feasibility) | Don't build; the amendment conditions are written in §3.5 | Nobody needs them; WhisperWoof uses the CLI; saves about 150 / 200 lines |
| Computer name in MCP rows: don't give it (privacy) vs give `host` and `link` (feasibility) | Give `host` and `link`, not `net` | `host_label` is now empty by default and only a name you choose, so the real-name concern is gone; `net` is a movement trail |
| How to label the device: a per-Mac sequence `d1` (privacy) vs an HMAC id (contract) | HMAC id | No lookup table to store; cannot be linked across Macs |
| Withdrawing a hand to AI: a new event `queue.cancelled` (contract) vs `queue.done{by:"undo"}` (feasibility) | `queue.done{by:"undo"}` | Old cpad closes the item as well |
| Default computer name: a truncated, validated macOS computer name (feasibility) vs empty by default (privacy, contract) | Empty by default; feasibility's validation rules apply to the name you choose | A real name would end up in every manifest; an invalid name makes the device reject hello |
| Other Macs' projects: file with one digit, showing the pressed key (user) vs confirm again after the digit (privacy) | Show the pressed key; Enter after the digit | One digit is the easiest way to mix things from different Macs; the extra press only applies to this group |
| `hand` through docs/14's router (contract) vs one record per action (feasibility) | P1 writes one `queue.added`, sharing `queue_text` with the router; after docs/14 merges, `send` uses the router and `hand` folds into it | The router is not merged yet; it writes two records at once, so the fold requires the queue item to really exist |
| Network fingerprint: reviewed as "build now" (privacy, contract, feasibility) vs wait for Q1 (user) | The design, revised per the three reviews, stays in §4.3; P1 does not build it and waits for Q1 | With several Macs it adds no information; with one laptop it is exactly P8-B1's input |
| The "(recording <id>)" suffix at the end of the queue body and the `read_transcript` hint: memos only (privacy) vs the same rule as the router (contract) | Drop the suffix | Meetings can no longer be handed; one `queue_text` shared with docs/14 and docs/22 |
| External name: `cpa inbox` (first draft, #54's title) vs rename (contract) | The `cpa filing` family | "inbox" is already taken three times; the UI still says "To file" |

## Appendix: evidence index

- daemon (origin/main `aaa9747`): `recordings/manifest.py:1-10, 73-79, 93-122`; `recordings/service.py:75-91, 111-140, 170-185`; `recordings/records.py:1-17, 76-81, 92-106`; `recordings/sessions.py:70-89`; `recordings/index.py:45-69`; `recordings/results.py:85-91, 94-149`; `recordings/route.py:1-6, 62-82`; `recordings/wall.py:38-45`; `archive/recordings.py:265-291`; `archive/search.py:1-9`; `cli/rec.py:1-8, 142-169`; `cli/commands.py:29-33, 49-60`; `cli/main.py:156-206`; `mcp/schemas.py:46-50, 136-140`; `mcp/config.py:34-58`; `mcp/limits.py:56-57`; `mcp/backend.py:147-166`; `hub/reduce.py:1-5, 90-102, 124-195`; `queue/fold.py:11-55`; `events/log.py:58-67, 87-104`; `events/records.py:1-5, 49-75, 97-98`; `agents/claude_code/argv.py:1-60`; `agents/claude_code/adapter.py:34`; `projects/registry.py:1-6, 21-23`; `control/server.py:3-4, 54-66`; `control/client.py:15`; `config/host.py:1-5, 52-54`; `config/settings.py:63-65, 157-166`; `protocol/validate.py:19`; `notes/render.py:19`; `notes/writer.py:36-67`; `inbox/need.py:1-7`; `protocol/specs.py:212`; `service/launchd.py:79-80`; `app.py:118-126, 354-361`; `__init__.py:3`.
- Firmware: `app/lib/protocol/decode_host.cpp:10-24`; `app/lib/hal/hal_platform.cpp:39-40`.
- Protocol: `protocol/README.md` item 20; `protocol/recording/v1/README.md` (single-writer table, hand-off rules, forward compatibility, `x-knownValues` at line 55); `protocol/schema/v0/inbox.dismiss.schema.json`.
- Docs: docs/15 §1, §2.1–§2.4, §3.4, §3.6, §3.7, §3.9, §3.12, §4.6; docs/17 §6.2–§6.3; docs/18 §1.3, §2, §4.2, §7, §15; docs/21 §5.3–§5.4 (`wt-settings`); docs/22 §2.1–§2.4, §3.6–§3.7, §4.7, §5.1–§5.9, §7, §8 (`wt-voicecap`); docs/14 §1.2, §5.2, `dst/router.py:1-136` (`feat/destinations` `b5ad361`); ADR 0002 item 3, ADR 0004.
- WhisperWoof (`WW/`, `c6b3060`): `src/components/ControlPanelSidebar.tsx:73-97`; `src/whisperwoof/ui/notes/VoiceNotesView.tsx:265-420` (search box 294-300); `src/whisperwoof/ui/settings/CardputerSection.tsx:77-84`; `src/helpers/ipcHandlers.js:1810-1817`; `src/whisperwoof/core/cardputer/{session-files.js:17-89, inbox-runner.js:168-173, inbox-shell.test.ts:308-319, contract-schema.js:16-37, 76-91, 187-195}`; `src/whisperwoof/bridge/{sidecar-env-pure.js:35-46, plugin-bridge.js:16-20}`; `docs/design/cardputer-inbox.md:248-253`; `DESIGN.md` §6, §7, §11.
- This Mac [meas] (2026-10-22, read-only): `~/CardputerNotes/recordings/*/manifest.json` (modes, projects and `dest`); directory listings (no transcript files); event type counts in `events.jsonl`; `cpa rec ls --json` (0.11 s, every row `project:null`); the running cpad predates v0.4.1, and `$CPA_HOME` has no `host_id`; output of `networksetup`, `ipconfig getsummary`, `system_profiler`, CoreWLAN (JXA), `ioreg`, `route`, `arp` and `scutil` on macOS 26, all values redacted and not saved.
