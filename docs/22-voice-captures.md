**English** · [中文](22-voice-captures.zh-CN.md)

# 22 · Voice captures: hold G0 in the editor to speak, one capture carries several voice segments

> Status: **design draft v2, waiting for your call** (§15, 3 items) · Date: 2026-10-22 · Role: architecture (voice captures) · Based on `integrate/v041` (`e4c2c68`). v2 was revised after review: all 23 comments held up and all were adopted, and the scope was cut by about half (see "How review comments were handled" at the end).
> Requirement (2026-10-22): "Queue for AI" is useful, but its editor only accepts typing, which is impractical on the small keyboard. In the text box you should also be able to hold G0 and speak. If the device cannot transcribe, it can just show that a voice input is there, and one editor should take several voice inputs; overall there should be a structure that keeps track of the different voice memos. The details are left to the designer.
> Basis: docs/04 §8.4; docs/13 §7.1, §9, §13; docs/14 (`feat/destinations` `b5ad361`, not merged) §2, §3, §5.3, §5.4; docs/15 §1, §2; docs/17 §6.1, §6.3, M10; docs/18 §2 item 6, §3.4; docs/19 §6.6, §6.9, §10; docs/20 U4, U6, U15, U16, U17, U25, U26, D4, D5, D6, D8; ADR 0004; recording contract v1 (`protocol/recording/v1/README.md` and schemas); `protocol/README.md`; `protocol/limits.json`; `schema/v0/{outbox.item,common,hello,pong,queue.snapshot,rec.act}.schema.json`; firmware `lib/shell/{compose,shell,runtime_outbox,confirm}.*`, `lib/rec/{rec_core,rec_meta,rec_msgs}.h`, `lib/core/app_state.h`, `lib/input/keymap.cpp`, `modules/board/board_logic.cpp`; branches in progress (read-only, uncommitted): `shell.cpp`, `compose.cpp`, `keymap.cpp` on `feat/ux-decisions`, `g0_gesture.h`, `shell_g0.cpp`, `shell.cpp`, `rec_core.h`, `keymap.*` on `fix/memo-hold`; daemon `hub/reduce.py`, `agents/claude_code/argv.py`, `recordings/{route,results,contract,history}.py`, `config/settings.py`, `service/launchd.py`; WhisperWoof PRs #149–#152 (all still open); local whisper.cpp and sandbox measurements (§4.5, §4.8).
> Marks: **[src]** code or docs read · **[meas]** measured on this machine · **[calc]** calculated from measurements or scripts · **[asm]** design assumption, can change · **[todo]** to verify with tests or the real device · **👤** needs you.
> Scope: design only. No device connected, no serial port opened, no code changed. Screen sketches are drawn for 240×135, one line about 40 ASCII characters or 20 Chinese characters, colours and parts from docs/19's Mando theme. The sketches show the English meaning of the device's Chinese UI text.
> Evidence paths (temporary files, not in the repo): `$VB` = `<scratch>/voicecap-bench` (timing scripts, synthetic audio, results); `$VC` = `<scratch>/vcap-calc` (line length `lines.py`, `lines2.py`, font `efont.py`, sandbox `conn.py`, `sbx/profile.sb`, language `lang/`). **None of your recordings were used**: timing used macOS synthetic speech (`say -v Tingting`, `say -v Samantha`); recognition accuracy needs a retest with your real speech (H-VC5).

## 0. One-page summary

1. **What you want**: when you open the editor of "Queue for AI" (and of write one, start session), you can also hold G0 and speak; if the device cannot transcribe, it first shows "there is a voice segment here"; one editor can take several segments; and there should be a structure that keeps these voice pieces organized.
2. **Model** (§2): **one capture = destination + optional typed text + 0–4 voice segments**. A segment is a memo carrying a capture reference `cap`. The item **states** which segments are attached (`voice`) and which were removed (`dropped`); if a draft is closed without sending an item, the device sends a "tombstone". The Mac never guesses: a segment that no item mentions always gets a note line of its own and never silently disappears.
3. **On the device** (§3): holding G0 in the editor = add a segment to this capture, and the draft gets a line `① 0:07 On the Mac`. The device does not show transcript text (later), only "there is a voice segment here". **Only Enter saves**: a single G0 tap no longer saves, and a double press records a segment hands-free. After releasing you can type or press Enter right away: that key first ends the segment, then goes to the editor. The reply editor, the menu, confirmations and the lock screen still record a separate memo per D4, and the recording page says which kind before you start speaking.
4. **Nothing lost in one press**: with voice present, Backspace and `` ` `` never exit directly; when discarding, Enter = drop the typed text and save the voice as a note, Opt+D = drop the voice too.
5. **Transcription on the Mac** (§4): a new standalone program **`cpa-stt`**, which like WhisperWoof is just a client of the recording contract; it runs one local command that you configure (example config: whisper.cpp large-v3-turbo, automatic language detection, greedy, VAD), wrapped in a tight sandbox (no network, can only write its own temp folder and the Metal cache; measured, no slowdown). **Off by default**; it only turns on when you run `cpa-stt use` once. cpad has no transcription engine code at all.
6. **Measured** [meas]: with automatic language detection, 10 seconds of speech gives text in about 1.7 seconds, 30 seconds in about 2.2 seconds. With forced `-l zh`, a whole English sentence came out as gibberish, so the default is `auto` (§4.8).
7. **Assembly** (§5): once every segment has a result, cpad joins "typed text + each segment's text" into the body and writes a queued item and a daily-note line according to the destination.
8. **Safety** (§5.4, §8): voice is **only queued, never automatically starts a session**. Starting a session from a voice item requires paging through the **full text** on the device and pressing Enter on the last page; cpad checks that the device really fetched the full text; always plan mode; sessions started from voice do not accept device replies. Reply-type queued items cannot be started as new sessions (also plugging this hole that exists today). Voice for queue and start session is at most 40 seconds per capture, and a start-session item whose assembled text exceeds 500 B does not go into the queue.
9. **Protocol additions only, `v` stays 0** (§6): `outbox.item` gets `cap`, `voice`, `dropped`, `drop`; `file.items` and `.M` get `cap`; items in `queue.snapshot` get `voice`; a new message pair `queue.get` / `queue.text` (fetch the full text). No new snapshot, no new caps, no new error codes. The worst line with the new fields is 1429 B (`outbox.item`, limit 2048 B). Recording contract v1 only gets optional fields and a claim file.
10. **Phases** (§11): **VC-1 is Mac only** (cpa-stt, about 3 days), can ship any time, and is useful on its own: none of your current 13 memos has been transcribed. VC-2 (protocol + firmware + cpad, about two weeks) comes after the D4 and memo-hold branches are merged. **3 things need your call** (§15), all with defaults.

## 1. What you want, and how today differs

### 1.1 The requirement, item by item

- The editor must accept speech: when "Queue for AI" is open, the only input today is typing on a very small keyboard (§3.1).
- In the editor, hold G0 to speak (§3.1, §3.2).
- The device cannot transcribe, so it first shows a placeholder for the voice input; transcription happens on the Mac (§3.4, §3.6, §4).
- One editor takes several voice inputs, i.e. one capture carries several segments (§2, §3.4).
- A structure that records the different voice memos: the relation between segments and captures (§2).
- The details are left to the designer: they follow docs/20's small-device principles, and only changes to your habits are brought up for a decision (§15).

### 1.2 How a typed capture goes today [src]

1. The editor is the pure function `compose_key()`: it only takes ASCII 0x20–0x7E [`compose.cpp:14-15`], 1–500 bytes; Opt+N/Q/S change the kind, Opt+0–9 change the project; queue and start session need a project.
2. Enter or a single G0 tap (`keymap.cpp:81-82`) → save. Start session, reply → confirmation → send `agent.spawn` / `agent.reply` online; if it cannot run (offline, session waiting for permission) it is saved as a queued item (`confirm.cpp:42, 65`); write one → note; queue for AI → queued item.
3. Items and recordings use **the same MonoId sequence** (`runtime_outbox.cpp:47`); the first 13 bits of a MonoId are the boot number, and within one boot the string order of ids is the order they were created.
4. cpad: `item.received`, plus `queue.added` for queued ones; write one immediately writes a daily-note line (`hub/reduce.py:125-137`); replies `outbox.ack(stored)`.
5. Selecting a queued item on the board and pressing Enter sends `agent.spawn{qid}` **whatever the `intent`** (`board_logic.cpp:69-74`); cpad uses the queued item's full text as the prompt (`reduce.py` `_spawn_text`). The confirmation only shows a summary of ≤80 B (`queue_text_max`).

### 1.3 How memos go today [src]

1. Holding G0 for 500 ms starts recording, release ends it; under 1 s is discarded; at most 5 minutes. `fix/memo-hold` adds a 600 ms release grace (pressing again within the grace continues the same segment) and "double press = hands-free recording" (`g0_gesture.h`, `kDoubleTapMs = 350`); there is no space lock.
2. When a segment ends, `.M` is written (≤512 B, unknown fields ignored, `rec_meta.h`). The recording session id = segment 0's MonoId, carried by the `Started` event (`rec_msgs.h:51`). The end signal can be lost: if it does not arrive within 10 s of finishing, it goes back to Idle (`rec_core.h` `kFinishGiveUpMs`). On reboot, segments without `.M` are rescued by the boot scan and marked `rcv`.
3. cpad looks at `files{n, rev, live}` in `hello` / `pong`, and when there are new segments pulls them with `file.list` / `file.get`, lands them, verifies, `file.del`. `pong` comes every 5 s.
4. When the last segment lands, `manifest.json` is written; then the transcriber writes `transcript.*` and `result.json`; cpad scans every 3 s, records `rec.result` when it sees `result.json`, records `rec.stale` after 24 h with no result, and both write a daily-note line (`recordings/route.py`, `results.py`).

### 1.4 Gaps

| What is missing | Today | Evidence |
|---|---|---|
| Holding G0 in the editor | v0.4.0 records nothing. `feat/ux-decisions` is changing it per D4 to "record a separate memo, keep the draft" | docs/20 U4; that branch's `shell.cpp:120` |
| Relation between draft and recording | None | — |
| Transcription on the device | Impossible: no PSRAM, about 180 KB of usable heap; the smallest whisper model is 77.7 MB | docs/19 §10; [meas] |
| Transcription on the Mac | No program writes `result.json`: none of the 15 recording folders (13 memos of 1.0–50.7 seconds, 2 meetings) has one. WhisperWoof #149–#152 not merged | `~/CardputerNotes/recordings/*/manifest.json` (only the manifests were read) |
| Seeing the full text before starting a session | The confirmation only has an 80 B summary, while the full 500 B is what gets sent | §1.2 item 5 |
| Reply-type queued items | The board can still start one as a **new** session | §1.2 item 5 |

## 2. Model: one capture = destination + optional typed text + 0–4 segments

### 2.1 Definition

```
Capture {
  ref      条目 id（outbox.item 的 MonoId）
  dest     去向：daily / todo / queue / session —— 你开的编辑器（docs/14 合并后：编辑器当前的去向）
  project  项目（queue、session 必须有）
  typed    你打的字，可以没有
  cap      采集引用：第一段开录前从共用的 MonoId 序列取的 id
  voice    挂上的片段（录音会话 id），按录的顺序，0–4 个
  dropped  拿掉的片段，0–4 个，和 voice 不重叠
}
Clip = 一段 mode=memo 的录音，第 0 段的 .M 带 cap
```

(In English: `ref` is the item id (the MonoId of the outbox.item); `dest` is the destination daily / todo / queue / session, i.e. the editor you opened (after docs/14 is merged: the editor's current destination); `project` is required for queue and session; `typed` is the text you typed, may be empty; `cap` is the capture reference, an id taken from the shared MonoId sequence before the first segment starts; `voice` is the attached segments (recording session ids) in recording order, 0–4; `dropped` is the removed segments, 0–4, not overlapping `voice`. A Clip is a recording with mode=memo whose segment 0 `.M` carries `cap`.)

- **`cap` is fixed before the first segment starts**: the device takes a MonoId as `cap`, puts it in `StartSpec`, and writes it into every segment's `.M`. When the Mac receives a segment, it knows "this is a segment of some capture" without waiting for the item.
- **The draft records the segment at `Started`** (the session id is known by then), in state "recording"; the duration is filled in at the end; **only `Discarded`** (under 1 s, nothing stored) removes it from the draft. If the end signal is lost (giving up after 10 s of finishing), the segment still stays in the draft and still goes into `voice`, and the Mac decides whether it actually arrived (§5.2).
- **Every draft with a `cap` ends with exactly one `outbox.item` that mentions it**: either the item itself (write one, queue) or a tombstone (§2.5). So every segment's outcome is stated by the device, not inferred by the Mac.
- **The order can be checked**: within one boot, `cap` < each segment's session id < item id. Drafts live only in memory and do not survive a reboot, so all three must have the same boot number (§5.7).

### 2.2 Why a segment is "a memo with `cap`", not a new `mode`

| Approach | The old end | Verdict |
|---|---|---|
| `mode:"clip"` | **Rejected whole**: the recording contract lists "unknown `mode`" as a required invalid vector, and WhisperWoof would write `failed/E_INVALID`; when v0.4.1 cpad decodes `file.items`, `mode` is an enum, it replies `E_BAD_FIELD`, and pulling gets stuck [src] | No |
| One recording session per capture, several continued segments | Needs changes to the recording engine, the recovery scan, segment numbering | No |
| **Memo + optional field `cap`** | `.M`, `file.items`, `manifest.json` all ignore unknown fields and treat it as a plain memo [src] | **Adopted** |

The field is not called `clip`: in docs/14, `clip` is already the id of the "clipboard" destination.

### 2.3 What each editor becomes

| Editor | Enter with voice present | What the device sends | After every segment has a result |
|---|---|---|---|
| Write one (Opt+N), to-do (Opt+T) | Saves, no typing needed | `outbox.item{kind:"note", dest?, cap, voice, dropped?}` | One daily-note line: typed text + each segment's full text + audio links (a checkbox for to-dos) |
| Queue for AI (Opt+Q) | Saves | `outbox.item{kind:"queue", intent:"todo", cap, voice, dropped?}` | A queued item whose summary starts with "Voice·"; one daily-note line |
| Start session (Opt+S) | **No confirmation, saved straight as a queued item**: there is no text yet, so nothing can start | `outbox.item{kind:"queue", intent:"spawn", cap, voice, dropped?}` | A voice queued item with `intent=spawn`, started only after paging through the full text on the board (§3.8) |
| Reply | **No voice attached**: holding G0 records a separate memo per D4 (§3.1) | — | — |

- Start session becoming a queued item matches docs/14 §5.4 item 1: "`session` coming from the outbox or memos always becomes a queued item".
- Replies do not attach: a voice reply would become a queued item that can only be recorded and never reaches the session, which would keep waiting (voice replies from docs/17 §17 item 6 go to VC-3).
- Zero typing (without opening the editor, pressing Q / S while holding G0) is not in this version; it comes with docs/14 F5's "hold G0 then press a letter" (VC-3).

### 2.4 A segment's outcome: the Mac only looks at what the device stated

| Outcome | How it is recognized | On the Mac |
|---|---|---|
| **Attached** | In some item's `voice`, and it has a result at assembly time | The text goes into that capture's body |
| **Supplement** | In `voice`, but the result arrived after assembly (§5.2) | A "supplement" line in the daily note: text + audio link; **not queued** |
| **Removed** | In some item's or tombstone's `dropped` | No note, not queued; records `rec.hidden`, not visible in the Recordings app (same as "hide", recoverable with `cpa rec unhide`) |
| **Not attached** (orphan) | Carries `cap`, no item or tombstone mentions it, **and** the device reconnected with a new boot number with the outbox empty (`pong.pending == 0`) | A daily-note line "voice segment (not attached to an item)": text + audio link |
| **Waiting** | None of the above yet | Nothing written; counted in `cpa status` as "segments waiting for an item" |

- **Never infer deletion from "same `cap` but not in `voice`"**.
- **No orphans within the same boot**: a draft left open overnight is fine, and is claimed as soon as the item arrives. After a reboot, items stored in the outbox are sent first (`pending == 0`), so only segments that nobody has mentioned after "new boot number, outbox empty" are truly unwanted.
- Nothing is lost: every segment's audio stays in the recordings folder, handled by the recording contract's retention (WAV 30 days, transcripts forever).

### 2.5 Tombstone: the draft was closed but no item was sent

A draft with a `cap` closes without an item in these cases: when discarding you chose "drop the voice too" (§3.3); or all voice was removed and it ended up going through the confirmation → `agent.spawn` (start a session online); or all voice was removed and the text was deleted, then it exited. Then the device **automatically** sends a tombstone:

```
outbox.item{kind:"note", body:{text:"（语音）", cap, dropped:[…], drop:true}}
```

cpad on receiving it: no note, not queued; segments in `dropped` get `rec.hidden`, and this capture is closed. Tombstones go through the outbox, so they are not lost offline either. An old cpad does not know `drop` and writes a "（语音）" ("(voice)") note line; this only happens with "new firmware + old cpad" and is harmless (§6.5).

## 3. On the device

### 3.1 Holding G0

| Situation | Holding G0 records | The recording page says before you speak | After release |
|---|---|---|---|
| The editor is on top (not locked, no menu or confirmation open); kind is write one, to-do, queue for AI, start session; destination is today's note, to-do, queue, session; fewer than 4 segments; (queue, start session) voice not yet at 40 seconds | **A segment**, attached to this capture | "Voice segment · no. 2", "Add to (For AI · #cardputer)" | The draft gets a line `② 0:07`; too short (<1 s) says "Too short, not added" |
| Same, but already 4 segments | A separate memo (D4) | "Voice memo" | Says "At most 4 segments: this one was saved as a memo" |
| Same, but queue / start session voice already at 40 seconds | A separate memo | "Voice memo" | Says "This capture's voice is at 40 seconds: this one was saved as a memo" |
| The reply editor | A separate memo | "Voice memo" | Says "Replies need typing: this one was saved as a memo" |
| Destination is clipboard, paste, plugin, file (after docs/14 is merged) | A separate memo | "Voice memo" | Says "This destination takes no voice: this one was saved as a memo" |
| Tab menu open over the editor | A separate memo | "Voice memo" | Says "Menu was open: this one was saved as a memo" |
| A confirmation open | A separate memo | "Voice memo" | Says "Saved as a memo; to add to this capture, press ` to go back to the editor first" (D6) |
| Locked (whatever is underneath) | A separate memo | "Voice memo" | Same as today |
| No editor open | A memo (today's behavior) | "Voice memo" | — |

- **One rule**: attach only if the draft is visible and this capture can take it; otherwise it is D4's separate memo, and the recording page says which kind before you speak. Whether it attaches **does not depend on the Mac's version** (§6.5); the meaning of the same gesture does not change with invisible state.
- **Menu**: `dispatch()` on `feat/ux-decisions` hands keys to the menu first (`shell.cpp:117`), and after the menu closes it may replay this hold. So "menu open" must be recorded **before** the menu receives the G0 hold, and the replay records a separate memo; a test pins this.
- **Why replies do not attach**: see §2.3.

### 3.2 Other uses of G0, release grace, other keys while recording a segment

- **A single G0 tap no longer saves** (today `keymap.cpp:82` saves). `fix/memo-hold` makes "double press" hands-free recording; if a single tap in the editor saved immediately, a double press would become "save the draft away, then open a new note" (the second press lands on the global quick note). So in the editor: a single tap only says "Enter to save · hold G0 to speak"; a second press within 350 ms = **record a segment hands-free** (attached or not per §3.1), and one more G0 press ends it. **Enter is the only save key, and G0 means speaking everywhere** (§15 item 1).
- **Release grace** (600 ms): pressing again within the grace continues the same segment. To add a second segment: release, wait for the recording page to close (about 0.6 s), then hold again.
- **Other keys while recording a segment** (docs/20 U17: a press must get a reaction):

| When | Enter | `` ` `` | Letters, digits, Backspace | Tab, Opt+L |
|---|---|---|---|---|
| G0 held | Says "Release G0 first" | Same | Same (digits do not change the project: "project and destination come from the editor") | Per memo-hold: menu over the recording / lock and keep recording |
| Release grace, hands-free, finishing | **First ends this segment**, then saves (with this segment) | First ends this segment, then shows "Discard this capture?" | First ends this segment, then goes to the editor as usual (type, delete) | Same as above |

  - "First ends": grace and hands-free recordings finish immediately; one that is finishing is waited for.
  - Keys are queued in order (up to 8) and handed to the editor once the segment is **settled** (`Saved` / `Discarded` / `Failed`, or giving up after 10 s); from the 9th on it says "Wait for this segment to be saved". So an item saved by "release, press Enter at once" always carries the segment just recorded.
- **D8 (pre-roll, not decided)**: the first ~0.65 seconds are not recorded, the same for every segment; wait until you see ● REC before speaking. If pre-roll is built, segments benefit automatically.

### 3.3 Keys in the editor

| Key | No voice (today) | With voice |
|---|---|---|
| Enter | Save | Save; works without typing too (placeholder body, §6.3) |
| G0 single tap | Save → **changes to** a hint | Hint "Enter to save · hold G0 to speak" |
| Backspace (no text) | Exit (issue #15) | **Does not exit**; hint "Opt+D removes a segment · ` discards" |
| `` ` `` (D5) | Exits directly with no text, asks with text | **Always asks** "Discard this capture?" (below) |
| Opt+D | — | Removes **the last segment**, hint "Removed ②"; with no voice, hint "No voice to remove" |
| Opt+N / T / Q / S | Change kind | Change kind; with over 40 seconds of voice, cannot change to queue or start session (hint "Voice over 40 seconds: write one only") |
| docs/14's Opt+letter | Change destination | Only to today's note, to-do, queue, session; others hint "This destination takes no voice" |

- Opt+D in the edit layer is unbound today; it means the same as "Opt+D = handle" in lists. It is called "remove", not "delete": the audio is still on the Mac, it just does not go into this capture (§2.4).
- However a draft with a `cap` is closed, it sends exactly one item or tombstone (§2.5).

When discarding (with voice):

```
╭──────────────────────────────────────╮
│Discard this capture?  2 segs · 0:19  │
│                                      │
│[Enter] Drop text, save voice as note │
│[Opt+D] Drop the voice too            │
│[`] Go back and keep writing          │
╰──────────────────────────────────────╯
```

- Enter: sends `note{text:"（语音）", cap, voice, dropped?}`; the voice goes into today's note, nothing is lost. The default key is the one that loses the least.
- Opt+D: sends a tombstone, with every segment in `dropped`.
- `` ` `` / Fn+`` ` ``: back to the editor (same as today's discard prompt).

### 3.4 Editor screen (Mando theme)

The compose page from docs/19 §6.9: the title in a neutral capsule at (4,13); the body in a Plain card (4,28,232,92), padding 6, 5 lines; the card's bottom line has warnings on the left and the character count on the right. With segments attached:

```
[head] ↑0                       Wed 09:41 94%
(Queue for AI → cardputer)
╭──────────────────────────────────────╮
│fix wifi page_                        │  typed text (Ink), at least 1 line
│① 0:07  On the Mac                    │  segment: number and duration in a capsule
│② 0:12  Waiting                       │
│                                      │
│                                      │
│Voice 2 segs · 0:19/0:40       14/500 │
╰──────────────────────────────────────╯
Enter save G0 hold speak Opt+D rm Tab menu
```

- **Segment line**: starts with a hand-drawn 7×9 microphone (`tools/theme/gen_icons.py`, the same pipeline as the status bar's dog head, Accent colour; efontCN_12 cannot draw 🎤); circled number (①–④ are in the font [meas]) and duration in a neutral capsule of height 12; then the state (§3.6, Ink3).
- **Lines**: one line per segment, the typed text takes the rest, at least 1 line; 4 segments + 1 line of text is exactly 5 lines. When it does not fit, the typed text shows only the end and the cursor (same as today).
- **Voice only, nothing typed**: the typing line says `(typing optional)` (`（可以不打字）`, Ink3) plus the cursor.
- **Card bottom left**: `Voice N segs · total duration`; for queue and start session it reads `0:19/0:40`, in Warn when less than 10 seconds remain. Start session's warning about a leading `-` takes priority.
- **Hint bar**: with voice, `Enter save · G0 hold to speak · Opt+D remove · Tab menu` (`Enter保存 G0按住说话 Opt+D拿掉 Tab菜单`); in an empty editor, today's `Opt N/Q/S kind` is replaced by `G0 hold to speak` (`G0按住说话`) (changing kind stays in the menu's "This page"). It was not obvious that you can speak in the editor, so the empty editor should teach exactly this. Both must pass the first tier of `hint_fits`; if they do not fit, drop `Tab menu` [todo test].
- **No animation**: segment lines are redrawn only when their state changes (docs/19 "only move when the state changes").

### 3.5 While recording a segment (docs/19 §6.6's memo page, over the editor)

```
[●0:07] [head] ↑0                Wed 09:41 94%
  ▟▙      Voice segment              no. 2
  ▛▜      ● 0:07  REC             0:21 left
  ▐▌      ┃┃·┃┃┃·┃┃┃┃┃·┃┃┃┃·┃┃┃┃┃·┃┃┃
(listening) Add to (For AI · #cardputer)
[G0]release to add  double press=hands-free
```

- It differs from the memo page in only a few places: the label "Voice segment", "no. N", "Save to" becomes "Add to", and the capsule holds the editor's kind and project; there is no `1-9 pick project`.
- **Queue and start session have a 40-second total**: the right side shows "0:21 left"; when it runs out the segment ends by itself, and the end page says "40 seconds reached, segment ends here" (U6: anything that stops by itself must say why). The device passes the remaining allowance to the recording engine as this recording's limit (`StartSpec` gets `max_ms`; the engine already stops by itself at the 5-minute limit today). Write one and to-do use the memo's 5 minutes per segment.

### 3.6 Segment line states (the device only writes what it knows)

| State | How the device knows | What it writes |
|---|---|---|
| Duration unknown | The end signal was lost (giving up after 10 s of finishing) | `Duration unknown · still attached` |
| On the device | The segment is still in the local file index | Online: `Waiting`; offline: `Sent when connected to the Mac` |
| On the Mac | Gone from the index (`file.del` after the Mac pulled it) | `On the Mac` |

- **No waiting needed**: Enter can be pressed any time. After the Mac gets the item it waits for the segments to arrive and be transcribed, then assembles (§5).
- The device **does not show transcript text**: that needs a new Mac→device snapshot, in VC-3. In this version transcript text first appears on the device as the "Voice·…" summary on the board and as the full text before starting a session (§3.8).

### 3.7 After saving

| Situation | Toast |
|---|---|
| Queue for AI, with voice | Saved · queued after transcription |
| Start session, with voice | Queued · start it from the board after transcription (plan) |
| Write one, to-do, with voice | Saved · goes into today's note after transcription |
| Discard, voice saved as a note | Typed text dropped · voice saved as a note |
| Discard, voice dropped too | Dropped (audio is on the Mac, hidden) |

### 3.8 Board and confirmation: page through the full text before starting a session

- **Summary**: a voice queued item's summary is generated by cpad: `语音·` ("Voice·") plus the start of **the voice part** (not the start of the typed text), ≤80 B (§5.3). Items in `queue.snapshot` get an extra `voice:true`.
- **Starting a session needs the full text first**: select a voice item on the board and press Enter; the device sends `queue.get{qid}`, gets back the full text `queue.text` (≤500 B), and shows it paged in the confirmation, 5 lines per page:

```
╭──────────────────────────────────────╮
│Start session · plan · voice     1/3  │
│fix wifi page 把设置页的WiFi部分改成英 │
│文，再跑一遍test shell的单元测试，然后 │
│……                                    │
│                                      │
│[Enter]Next page  [`]Don't start      │
╰──────────────────────────────────────╯
```

  - Enter goes to the next page; **only the last page's** Enter means "start" (it says "Enter start (plan: plan only)"). Every page has the confirmation's 400 ms debounce, so repeated presses cannot skip through.
  - What you see is what is sent: the full text paged as is, nothing removed or abbreviated; a truncated ending carries "…（截断）" ("…(truncated)") (§5.3).
  - Full text not received (offline, timeout, old cpad): it says "Full text not received: cannot start" and does not start.
  - cpad checks too: if the full text of this item was not sent to the device on this connection, `agent.spawn{qid}` is refused (§5.4).
- **Reply-type queued items** (`intent=feedback`, voice or not): pressing Enter on the board only says "Reply items cannot start a new session: handle them on the Mac"; Opt+D handles them as before. cpad refuses too (§5.4).
- "May already have started, check Sessions first" is only for `uncertain` (U25) and has nothing to do with voice.

### 3.9 What changes on the device (VC-2f)

| File | Change |
|---|---|
| `lib/rec/rec_msgs.h`, `rec_core.*`, `rec_meta.*` | `StartSpec` gets `Opt<MonoId> cap` and `max_ms`; segment 0's `.M` writes `cap`; the session ids of `Started` / end events are handed to Shell |
| `lib/shell/compose.*` | `ComposeState` gets `cap` and `clips[4]{id, ms, st}`, `dropped[4]`; `Started` adds a segment, `Discarded` removes one; with voice, `cancel()` and `delete_char()` do not exit; Opt+D removes the last segment; with voice, empty text can be saved; Enter / Opt+D in the discard prompt; kind and destination limits; the tombstone when closing a draft |
| `lib/input/keymap.cpp`, `lib/shell/shell*.cpp` (`shell_g0.cpp`) | Remove "G0 single tap = save" in the edit layer; single-tap hint, double-press hands-free; the decision in §3.1 (including recording "menu open" before replay); keys while recording a segment first end it, queue up to 8, hand over once settled; start session with voice skips the confirmation and saves a queued item directly |
| `lib/shell/shell_render.cpp`, `lib/theme`, `tools/theme/gen_icons.py` | Segment lines, the small microphone, card bottom, hint bar, a few texts on the segment recording page, the discard prompt |
| `lib/shell/confirm.*`, `modules/board/board_logic.cpp` | Voice items fetch the full text first, paged, start only on the last page; reply-type queued items do not start |
| `lib/protocol/*` | Encode `body.cap/voice/dropped/drop`, `file.items[].cap`, `queue.get`; decode `queue.snapshot[].voice`, `queue.text` |
| `lib/core/app_state.h` | Queued items get `voice` (1 B per item) |

### 3.10 Why not transcribe on the device

No PSRAM, about 180 KB of free heap [src docs/19 §10]; the smallest whisper model `ggml-tiny.bin` is 77.7 MB, large-v3-turbo 1.62 GB [meas]. The device only records, stores and displays; this division of labor was set in docs/02.

## 4. On the Mac: the transcriber `cpa-stt`

### 4.1 Who transcribes

| Option | How | Verdict |
|---|---|---|
| **A. Standalone `cpa-stt`** | A separate program with its own LaunchAgent that only reads and writes the recordings folders; like WhisperWoof it is a client of the recording contract; it runs one local command that you configure | **Adopted**. cpad takes on no extra role and touches no transcription engine; however slow transcription is, it cannot hold up the serial port (another process) |
| B. WhisperWoof | After #149–#152 are merged, turn on its Cardputer inbox | Still possible, the contract does not change; only one of the two can be on (§4.10) |
| C. cpad runs the transcription command itself | — | No: engine-specific code would grow inside cpad (ADR 0004), and transcription would share a process with the serial port |
| D. Both on at once | — | Not allowed: the contract is single-writer (§4.10) |

### 4.2 What `cpa-stt` is

- Python standard library only, at the repo's top-level `stt/` (the review suggested `tools/cpa-stt`; the location changed because it gets installed on your Mac, while everything under `tools/` is a development tool). (When VC-1 landed it went into `daemon/`; see §4.13.)
- Its own LaunchAgent `com.cardputer-assistant.cpa-stt`, written like cpad's `service/launchd.py`; its own config `$CPA_HOME/stt/config.json`, logs `$CPA_HOME/stt/log/`.
- Between it and cpad there are **only the files in the recordings folders**: it reads `manifest.json` and writes `status.json`, `transcript.*`, `result.json`; cpad's 3 s scan picks up `result.json` as before. (It also reads cpad's `config.json` and `projects.json`, read-only; see §4.13.)
- It knows nothing about items, queues or destinations. It only knows the `cap` in `manifest.json`, used for "segments someone is waiting for go first".

### 4.3 Command conventions (between `cpa-stt` and the transcription command)

Any local program that follows these can be used (whisper.cpp, sherpa-onnx, your own script):

| Item | Convention |
|---|---|
| Input | One WAV: 16 kHz, 16 bit, mono PCM, 44 B header (i.e. `seg-000.wav`, not copied). Placeholders `{wav}` (exactly once) and `{offset_ms}` (optional, skips the first `lead_ms`). A placeholder must be a whole argv element |
| Output | stdout, UTF-8, one sentence per line; a line may start with `[HH:MM:SS.mmm --> HH:MM:SS.mmm]`, used as the sentence's `t_ms` if present, else 0. Empty output = nothing heard |
| Exit code | 0 success; non-zero, killed by a signal or timeout all count as failure |
| Environment | No shell; `argv[0]` is an existing absolute path; environment variables only `PATH=/usr/bin:/bin`, `HOME`, `LANG=en_US.UTF-8` (when cpa-stt itself has `__CF_USER_TEXT_ENCODING` it is passed on too, so CoreFoundation does not read `~/.CFUserTextEncoding` in the real home folder, #71); the working directory is an empty 0700 temp folder; stdin closed; stdout read up to 256 KiB (VC-1 changed this to grow with duration, §4.13); **stderr dropped, not logged** (it may contain transcript text) |
| Sandbox | Always wrapped in the sandbox of §4.5, **with no option to turn it off** |
| Timeout | Per segment `max(timeout_s, 30 + 2 × duration in seconds)`, `timeout_s` default 120 |
| Concurrency | One segment at a time, one process per segment (model load 0.4 s, in exchange for not keeping 2 GB resident) |

These conventions are `cpa-stt`'s own config format, not a cpad external contract; cpad's external contracts are still only the file layout and MCP (§4.12).

### 4.4 Config: engine parameters are data, not code

```json
{
  "provider": "off",
  "recordings_dir": "~/CardputerNotes/recordings",
  "argv": null,
  "sandbox_mach": ["com.apple.MTLCompilerService", "com.apple.system.opendirectoryd.membership"],
  "meetings": false,
  "timeout_s": 120,
  "drop_lines": []
}
```

- `provider`: `off` (**default**) / `command`. There is no `auto`; it does not go looking for models anywhere.
- The repo ships an **example config** `stt/examples/whisper-cpp.json` (data):

```json
{"provider": "command",
 "argv": ["/opt/homebrew/bin/whisper-cli", "-m", "<MODEL>", "-l", "auto", "-bs", "1", "-bo", "1",
          "-np", "--vad", "-vm", "<VAD_MODEL>", "-f", "{wav}"]}
```

  You replace `<MODEL>`, `<VAD_MODEL>` with paths on your own machine (e.g. the `~/.cache/openwhispr/whisper-models/ggml-large-v3-turbo.bin` already on this Mac) and run `cpa-stt use file`. `cpa-stt` only checks that paths exist and placeholders are valid, then runs a self-test (§4.6). It **does not know** whisper.cpp: switching to another engine is just another config.
- `-l auto`: with forced `-l zh`, English sentences become gibberish (§4.8); `auto` costs about 0.5 s more per segment.
- No `-ot {offset_ms}`: whisper.cpp 1.8.3 applies `-ot` to the speech VAD keeps, not to the file. Together with `--vad`, the first word or two of real memos were cut off, and one became a hallucinated sentence entirely, which after filtering turned into "nothing heard" (§4.13). The 150 ms `lead_ms` is left to VAD and whisper; `{offset_ms}` is for engines that apply the offset to the file.
- No `--prompt`: a prompt can fix individual words, but it also brings in wrong words and pops out verbatim on silence (§4.8, §4.9). If you want one, write it into `argv` yourself, and `cpa-stt` adds it to the hallucination filter. (VC-1 changed this: the example config uses project names as the prompt; see §4.13.)
- `meetings`: default `false`, only memos and segments are transcribed. Meetings are long and contain other people's words, so they go to WhisperWoof's meeting pipeline [asm]. (VC-1 changed the default to `true`; see §4.13.)
- `sandbox_mach`: system services allowed inside the sandbox; the default two were found by measuring whisper-cli with Metal (§4.5). Other engines need other services; add them here.
- `drop_lines`: your own additional hallucination lines.

### 4.5 Sandbox: blocks network and stray writes [meas]

Each run generates a profile like this (`WD` is this run's temp working folder, `C` is `getconf DARWIN_USER_CACHE_DIR`):

```
(version 1)(allow default)(deny network*)
(deny file-write* (require-not (subpath WD)))
(allow file-write* (literal "/dev/null") (literal "/dev/dtracehelper") (subpath C/com.apple.metal))
(deny mach-lookup)
(allow mach-lookup (global-name "com.apple.MTLCompilerService")
                   (global-name "com.apple.system.opendirectoryd.membership"))
```

| Measured (whisper-cli 1.8.3, macOS 26, Apple silicon) | Result |
|---|---|
| 10 s segment, `-l zh` greedy, 3 runs | 1.15 / 1.15 / 1.17 s (1.16 s without sandbox); text identical to the unsandboxed run |
| 30 s segment, same | 1.64–2.20 s (2.18 s without); text identical |
| 10 s / 30 s / 6 s English, `-l auto` | 1.66–1.70 / 2.18–2.75 / 1.65 s |
| Metal cache folder not writable | Recompiles the Metal library every time, **5 seconds more** ("loaded in 4.988 sec") |
| `MTLCompilerService` not allowed | Building the Metal library fails and whisper-cli crashes (exit code 134) |
| `opendirectoryd.membership` not allowed | Also about 5 seconds more every time |
| Connecting to a local port / resolving a domain in the sandbox | `Operation not permitted` / `Could not resolve host` |
| Writing `$HOME` in the sandbox | `Operation not permitted`; writing the working folder works |
| `open -a TextEdit` in the sandbox | App not found (LaunchServices unreachable) |

- **What it blocks**: the command going online itself, going online indirectly through system services (download services, LaunchServices `open`), and writing files outside the working folder and the Metal cache.
- **What it does not block**: reading files. The command can read your files, but cannot send them out or write them anywhere else. Docs and the CLI always say "blocks direct network access and stray writes", never "no network".
- `sandbox-exec` is marked deprecated by Apple, but it is still there on macOS 26 [meas]. **If it is missing, stop**: write `blocked{engine}`, and never run outside the sandbox.
- The two allowed services were measured for whisper-cli; they must be measured again under a LaunchAgent (H-VC6).

### 4.6 Run rules

- **Finding work**: scan the recordings folder at startup and every 2 s after [asm], picking folders that "have a valid `manifest.json`, no `result.json`, `mode=memo` (meetings too with `meetings:true`), and nobody else writing".
- **Order**: segments whose manifest carries `cap` → other memos (newest first) → meetings.
- **Self-test**: at startup, after a config change, and after any failure, first run the command on a built-in 1-second silent WAV.
  - **Self-test fails = an environment problem** (model or binary missing, `sandbox-exec` gone, Metal will not start): only write `status.json` `blocked{reason}`, with `reason` `model_missing` (path missing) or the new `engine` (the command will not run), **no `result.json`**. Once fixed (`cpa-stt retry`, or triggered automatically by a config change), these folders are transcribed as usual, and a broken environment never causes permanent failures.
  - **Self-test passes, only this segment fails = a problem with this file**: retry after 30 s, 2 min, 8 min; if all three fail, write `result.json` `failed{E_STT, retryable:true}`; a bad WAV header writes `failed{E_AUDIO, retryable:false}`. Same backoff as WhisperWoof [src PR #151].
- **Rerun**: `cpa-stt retry` clears `blocked`, deletes the `result.json` of `failed` folders **it wrote itself** (`pipeline.app == "cpa-stt"`), and transcribes again. This is a command you run in the terminal, equivalent to "the user deletes `result.json`" in the contract.
- **Logs**: only session id, duration, timing, exit code; no text.
- **Does not block cpad, nor cpad it**: two processes. cpad's serial traffic (ack, pong) is not affected by transcription; the integration test measures cpad's pong latency during a 60 s transcription (§12.1).

### 4.7 Which files it writes (per the "transcriber" role of recording contract v1)

| File | Content |
|---|---|
| `status.json` | `transcribing` written before starting, with `by:"cpa-stt"` (a new optional field in the contract, §7). After a restart it recognizes its own by `by`, not by remembering the mtime |
| `transcript.json` | `{v:1, session, attempt:1, lead_ms, engine:{stt:<file name of argv[0]>}, sentences:[{t_ms, text}]}` |
| `transcript.md` | Each line `- [MM:SS] text ^tHHMMSS`; from the second sentence in the same second, add `-2` |
| `result.json` | **Written last**: `done`, `level:"transcript"`, two `outputs` with sha256, `pipeline:{app:"cpa-stt"}`, `error:null`; memos and segments also get `memo:{text, intent:"note"}` (full text, cut at 2000 characters) |

- Each file: a temp file starting with `.`, 0600, `O_NOFOLLOW`, fsync, rename; `result.json` is put in place with `link()`, and **if one already exists it gives up**, never overwriting someone else's result.
- `memo.text` lets cpad's `rec.result` carry `memo_text` directly, so assembling a capture only looks at events. `intent` is always `note`: `cpa-stt` has no LLM.

### 4.8 Measured: whisper.cpp large-v3-turbo [meas]

Environment: an Apple silicon Mac, macOS 26; Homebrew `whisper-cpp 1.8.3`, Metal; model in the file cache. Audio: synthetic speech, 16 kHz, 16 bit, mono (same as the device). A new process each time, median of 3 runs.

| Segment | `-l zh` default (beam 5) | `-l zh` greedy | `-l auto` greedy |
|---|---|---|---|
| 3 s | 1.17 s | 1.17 s | — |
| 10 s | 1.17 s | 1.19 s | 1.67 s |
| 20 s | 1.69 s | 1.17 s | — |
| 30 s | 1.69 s | 1.67 s | 2.18 s |
| 45 s | 2.20 s | 2.20 s | — |
| 60 s | 2.70 s | 2.20 s | about 2.7 s [calc] |

- Breakdown: model load 380–470 ms; the encoder about 390 ms per 30-second window; `auto` runs the encoder one more time to detect the language. Peak memory about 1.98 GB.
- Greedy and beam 5 give exactly the same text on segments ≤20 seconds; greedy is faster and drops less at the 30-second seam.
- **Language**:
  - A 6-second English sentence ("Please fix the wifi settings page and then run the test shell unit tests…"): `-l zh` turned it into "нут果需要Amazon後Projut權" (2.68 s); `-l auto` and `-l en` were word-for-word correct (1.65 s / 1.16 s).
  - Mixed Chinese and English ("把 settings page 改成英文，然后跑一下 npm run test，再开一个 pull request"): `auto` and `zh` give the same result ("Settings Page", "NPM Runetest", "Pool Request"); `-l en` outputs traditional characters.
  - So the default is `auto`.
- **Chinese recognition** (synthetic speech, reference only): Chinese is mostly right; English words go wrong more: "AI" → "RE", "cardputer" → "Cartputer".
- **Prompt**: project names alone fix cardputer; a long word list brings in new errors ("rec act" → "WiFi"). The example config has none (§4.4).
- Your current 13 memos total about 159 seconds (median 7.4 seconds) [src manifest]; transcribing all of them takes about 20–25 seconds [calc].
- **Cold start** (model not in the file cache) was not measured; estimated 0.3–1 second more [todo H-VC6].

### 4.9 Two problems to guard against [meas]

**Sentences dropped at the 30-second seam**: on 45- and 60-second segments, default decoding dropped a whole sentence at the 30-second seam (about 17 characters); greedy only dropped the last three characters. Remedies: the example config uses greedy; voice for queue and start session is at most 40 seconds per capture; cutting long write-one segments at silences goes to VC-3 [todo real speech].

**"Hallucinations" on silence and noise**:

| Input | Without VAD | With VAD |
|---|---|---|
| 5 s pink noise | "明镜需要您的支持 欢迎订阅明镜" | Empty |
| 5 s pure silence | "优优独播剧场——YoYo Television Series Exclusive" | Empty |

(These are well-known whisper hallucinations, roughly "Mingjing needs your support, subscribe to Mingjing" and "YoYo exclusive drama theatre".)

Three filters; any one of them stopping it counts as "nothing heard": the example config has `--vad`; `cpa-stt` computes the PCM RMS itself before running the command, and records empty below the threshold (WhisperWoof's calibrated threshold is about int16 27.5 [src PR #151]); known hallucination sentences are removed whole (the two above, "字幕由Amara.org社区提供" and the like, **plus the sentence given as `--prompt` in the config** and `drop_lines`); the list is in the code, with tests. Empty transcript: `sentences` empty, `result.json` still `done`, no `memo`.

### 4.10 Only one transcriber at a time

- The contract says `transcript.*` and `result.json` are written only by the transcriber, and only one at a time. Finding out afterwards is not enough: both sides may already have written `transcript.*`, and the sha256 in `outputs` would no longer match the files.
- **Claim file** (new in the contract, §7): `recordings/.transcriber`, content `{v:1, app, pid, since, beat_at}`. The running transcriber updates `beat_at` every 60 s; any transcriber that sees **someone else's claim updated within 5 minutes** writes not a single file, and sets its own state to "another transcriber is running".
- `cpa-stt` is off by default, so installing VC-1 does not quietly take over folders WhisperWoof will later handle.
- **WhisperWoof needs the same rule** (another repo, 👤): add reading and writing `.transcriber` before #151 is merged. Until then: only one of the two can be on; when `cpa-stt` finds someone else's `status.json` (with a `by` that is not itself) or a `result.json` whose `pipeline.app` is not itself in a folder, it stops on that folder, and warns "another transcriber is also writing recordings/" in `cpa-stt status` and in cpad's `cpa status`.
- **Handover**: `cpa-stt off` deletes its own claim and stops; then turn on WhisperWoof, and vice versa. Folders `cpa-stt` wrote have `result.json`, and per the contract nobody else touches them; to have WhisperWoof redo the summaries, delete those `result.json` files (the contract's "manual rerun").

### 4.11 Command line

| Command | Does |
|---|---|
| `cpa-stt status` | On or off, claim, whether command and model exist, whether the sandbox exists, the last self-test, the `blocked` reason, timings of the last 10 segments, whether there is a second writer. Prints no text |
| `cpa-stt use file` | Reads the config, checks that `argv[0]` and paths in placeholders exist (taking the realpath) and placeholders are valid, writes `$CPA_HOME/stt/config.json`, runs the self-test, installs and starts the LaunchAgent (VC-1 does not install; see §4.13) |
| `cpa-stt off` | Stops, deletes its own claim, uninstalls the LaunchAgent |
| `cpa-stt test FILE.wav` | Runs once on the file you give in the same sandbox, printing timing and text (only in your terminal) |
| `cpa-stt retry` | The rerun in §4.6 |
| `cpa status` (cpad, changed) | Two more lines: transcriber (who holds the claim / not on / the `blocked` reason), "voice captures: N waiting for transcription, M segments waiting for an item" (VC-1 only did the claim line; see §4.13) |
| `cpa vcap redo REF` (cpad, new) | §5.2: when transcription arrives later, reassemble a voice capture that was already downgraded |

Output is in English, consistent with the existing `cpa` subcommands.

### 4.12 Relation to ADR 0004 and docs/15

ADR 0004 decided "no app-specific integrations", and docs/15 §1 item 3 says "no app-specific code. Only two external contracts: the file layout and MCP". ADR 0004's background already says the transcriber is "WhisperWoof today, any program tomorrow". This doc's approach:

- cpad has **no** transcription engine code, and still has only those two external contracts;
- `cpa-stt` is one more client of the recording contract, in the same position as WhisperWoof; it knows no engine, and whisper.cpp only appears in the example config (data);
- no more using Spotlight to find other apps' models, no cloning other apps' caches.

To be written down (DOC package, with VC-1, 👤 item 3):

- **ADR 0004** gets a decision: "This repo may ship standalone recording contract clients (such as `cpa-stt`). They have the same standing as third-party transcribers, exchange data with cpad only through the recording contract, and engine parameters live only in the user's config."
- After **docs/15 §1 item 3**, add: "`cpa-stt`'s command conventions are its own config format, not an external contract."

### 4.13 Implementation notes: where VC-1 differs from the text above (2026-10-22)

- **Location**: `cpa-stt` is in `daemon/src/cpa_stt/`, in the same package as `cpa` with its own entry point (`daemon/pyproject.toml`), not in a top-level `stt/`. Still standard library only, plus a few of cpad's small utility modules in the same package (config file read/write, validators, recording contract reading, projects.json, launchd); it does not import the daemon; cpad does not import it, and cpad has no transcription engine code. The example config is `daemon/src/cpa_stt/examples/whisper-cpp.json`, read as package data (`importlib.resources`, as in #57).
- **Boundary with cpad**: besides the recordings folders, it also reads, **read-only**, two of cpad's files: `$CPA_HOME/config.json` (where the notes folder is) and `projects.json` (project names as the prompt). It writes none of cpad's files and does not connect to `cpad.sock`. The ADR 0004 decision is written to match.
- **Meetings are transcribed by default too** (`meetings: true`): they are all your own recordings. Meeting text is still never treated as instructions (§8 item 1), and `result.json` has no `memo`.
- **Models live in `$CPA_HOME/models`**: the example config has fill-in markers `<MODEL:ggml-large-v3-turbo.bin>`, `<MODEL:ggml-silero-*.bin>`, `<WHICH:whisper-cli>`, `<PROJECTS>`, and `cpa-stt use` fills them in, then checks and self-tests. `cpa-stt setup` can use Spotlight (`mdfind`) to find existing ggml models and create symlinks there: no copying, no downloading, never overwriting real files. Spotlight does not search hidden folders like `~/.cache`, so on this Mac the path has to be given directly: `cpa-stt setup ~/.cache/openwhispr/whisper-models/ggml-large-v3-turbo.bin`. This differs from v2's "no Spotlight for models": it only searches when you run `setup`, only creates links, and engines other than the example are still just a config.
- **VAD only with a model, project names as the prompt**: the example config only adds `--vad` when `ggml-silero-*.bin` is in `$CPA_HOME/models`; project names are joined into `--prompt`. The prompt goes into the hallucination filter per §4.9; your `drop_lines` and the prompt are removed only as whole sentences, while the built-in long hallucination markers (Amara.org, 明镜…) are removed by substring.
- **The LaunchAgent is not installed by default**: `cpa-stt use` only writes the config and runs the self-test (not saved if the self-test fails). `cpa-stt run --once` transcribes what is waiting and exits, `cpa-stt watch` polls every 2 s, and to run at login, `cpa-stt service install`; `cpa-stt off` uninstalls it.
- **Claim**: another `cpa-stt` process also counts as "someone else"; when its pid no longer exists on this machine, it can take over without waiting 5 minutes. `cpa-stt run --once` deletes its own claim when done.
- **Real memo measurements** (2026-10-22, copies of 15 of your memos, 1–80 s, large-v3-turbo + Silero VAD, in the sandbox, compared to `-l zh` without VAD): the earliest example config had `-ot {offset_ms}`, and 8 of 15 lost or misheard the first word or two, and one 3.5 s memo was replaced entirely by whisper with "请订阅我的频道" ("please subscribe to my channel"), which after filtering became "nothing heard". With `-ot` removed, all 15 kept their beginnings, 10 matched the reference word for word, and the rest differed by a few characters; for the 80 s one, the reference itself lost about 25 s in the middle (cut out and transcribed separately, it is real speech), and `cpa-stt` lost nothing. Time per memo: about 1.7 s up to 14 s, 2.2 s for 33 s, 2.7 s for 51 s, 3.7 s for 80 s. The project-name prompt corrected "卡普特" and "购键" to Cardputer and Go key, and also miscorrected a character or two. All 15 `result.json` pass the contract checker and cpad's reader; curl in the sandbox cannot connect to a local port (exit code 7) and cannot resolve domains (exit code 6).
- **Additional measurement** (synthetic speech, in the sandbox): in a 7-second meeting that starts in English and then switches to Chinese, `-l auto` detected Chinese (p = 0.63), and the English sentence did not come out at all. Watch for language switches in meetings; noted in H-VC5.
- For the "real memo measurements" above, the count of 10 word-for-word matches is the correct one (the build report body said 11). The post-review changes below did not change the command or parameters, only filtered a few more traditional-character hallucinations, so these numbers still apply.

**Changes after review (2026-10-22, three reviews)**

- **Claim**: with no claim present, it is created exclusively with `link()`, so of two starting at the same time only one gets it; after taking over an expired claim it reads it back and only continues if it is its own. It also renews every 60 s while idle and while `blocked`, so others never think it stopped. While the transcription command runs, it checks the claim every 0.2 s: if it is no longer its own (or it must stop), it kills the whole process group and writes nothing more; it checks once more before writing `transcript.*` and `result.json`. A `beat_at` in the future is judged by the file's mtime, and one without a timezone offset is treated as unreadable (mtime is used), and cpad's `cpa status` does the same; previously such a claim would block forever.
- **Leftover files and a second writer**: someone else's `status.json`, `transcript.*`, `summary.md` changed within 5 minutes counts as "another transcriber is writing"; older ones are leftovers, and the program holding the claim may overwrite `status.json` and `transcript.*`, never `result.json`. So the handover in §4.10 (including the state `cpa-stt` wrote while `blocked`) does not jam folders. Before writing it also checks that `status.json` is still its own; a `result.json` someone else puts down while it holds the claim also counts as a second writer. All of these are recorded as warnings in `cpa-stt status`.
- **Stopping**: `cpa-stt off`, SIGTERM and Ctrl-C stop before the next folder, and a running command is killed within 0.2 s; a second Ctrl-C exits immediately. `off` first uninstalls the LaunchAgent, then clears a dead process's claim.
- **Recovery**: `cpa-stt retry` leaves a file `$CPA_HOME/stt/retry`, and a running `watch` (or the LaunchAgent) reads it on the next round and clears `blocked`; previously `watch` would overwrite it with its in-memory state. When blocked by `engine`, the self-test runs again at most once a minute, so once the environment is fixed transcription continues without a restart.
- **One bad folder does not hold up the others**: a folder that cannot be read or written (permissions) only records `io_error` (session id and errno, not the path), retried at 30 s / 2 min / 8 min, and finally, if it can write, `failed / E_IO` (retryable); other folders continue. While waiting to retry, `status.json` says `queued` instead of staying at `transcribing`. With meetings off, even meeting folders with a non-conforming manifest are not touched; folders whose segments are symlinks do not get `blocked`.
- **Long meetings**: the stdout cap grows with duration (256 KiB + 160 B per second of audio, about ten times whisper's output); exceeding it fails that attempt (retried, finally `failed / E_STT`), never writing a silently truncated transcript.
- **Hallucinations**: `-l auto` sometimes outputs traditional characters, so the traditional versions are filtered too (謝謝觀看, 感謝觀看, 請訂閱我的頻道, 中文字幕志願者 李宗盛, and whole lines containing 明鏡需要您的支持, 歡迎訂閱明鏡, 點點欄目, 優優獨播劇場, 索兰娅/索蘭婭). When nothing is heard, `transcript.md` says "（没听到）" ("(nothing heard)"), matching the device; terminal output is still English.
- **Logs**: a folder someone else is writing is logged once, not a line every 2 s; logs rotate at 1 MiB (keeping one `.1`); an idle `watch` no longer rewrites `state.json` every round.
- **Command line**: `cpa-stt use` without a file keeps your other settings (`meetings`, `drop_lines`, `timeout_s`, `recordings_dir`), only switches the command and sandbox services, and reminds you "only one transcriber at a time; if WhisperWoof's inbox is on, turn it off first". `status`, when on but no program holds the claim, says "running: no, new recordings wait", and says whether the LaunchAgent is installed. `watch` starts by saying which folder it is watching and how to stop, then prints session id, duration and timing (no text) after each item, and prints again only when the state changes. A missing model says which file and how to relink it.
- **`setup`**: first looks in the known folder `~/.cache/openwhispr/whisper-models` (the one mentioned in §4.4, which Spotlight does not search), then uses Spotlight; what it finds is listed first, and it links only after you answer y (or with `--yes`). Whether found or given by you, the linked file must belong to you (or root), neither it nor its folder may be writable by others, not under `/Volumes`, and it must start with the ggml magic `lmgg`. A link is not a copy: if another app moves or deletes it, recordings stop at `model_missing`; run `setup` again, then `use`.
- **cpad**: with a broken `config.json`, `cpa status` no longer exits with an error, and the transcriber line shows none.

**Raised in review, not done this time**

- cpad's `cpa status` only shows who holds the claim; it does not show the `blocked` reason, and has no "another transcriber is also writing" warning (§4.10, §4.11 say it should). Both are in `cpa-stt status`; for cpad to show them it would have to read `cpa-stt`'s own state file, which conflicts with "cpad does not know the transcriber". The `recordings:` line of `cpa status` still has `blocked=N`.
- `cpa status` looks at cpad's `notes_dir/recordings`, not `cpa-stt`'s `recordings_dir`: when `cpa-stt` watches another folder, cpad's recordings folder really has no transcriber, so none is correct.
- Lowering the transcription command's priority (`taskpolicy -c utility`): priorities differ between running in a terminal and under a LaunchAgent (`ProcessType: Background`); 85 s of synthetic speech measured 3.2 s / 3.6 s (utility) / 5.1 s (`taskpolicy -b`, standing in for background). Whether it competes with WhisperWoof dictation for the GPU has not been measured; put into H-VC6: measure dictation latency first, then decide.
- When the daily note later shows `memo_text` directly for "transcript only" memos, "[全文]" ("[full text]") must link to `transcript.md` (`cpa-stt` does not write `summary.md`). Today the "transcript only" line already links to `transcript.md`.

## 5. On the Mac: assembling the capture (cpad)

### 5.1 Flow

```
设备 outbox.item(queue, cap:C, voice:[R1,R2])
  └─ cpad：item.received（body 带 cap、voice）→ fsync → outbox.ack(stored)
           有 voice 的条目：不马上记 queue.added，也不马上写今日笔记（还没有文字）
设备 file.items（R1、R2 带 cap:C）── 拉过来 ── 归档 ── cpa-stt ── result.json ── rec.result(R1)、rec.result(R2)
  └─ vcap.fold：条目要的片段都有结果了（或确定来不了）→ 拼正文（§5.3）
       ├─ 能排队：queue.added{origin:"voice", voice:true, intent} + vcap.assembled   ← 同一批追加；再写今日笔记一行
       └─ 不能排队：vcap.assembled{action:"down"/"too_long", reason} + 今日笔记一行
```

(In English: the device sends `outbox.item(queue, cap:C, voice:[R1,R2])`; cpad records `item.received` (body with cap and voice) → fsync → `outbox.ack(stored)`; for items with voice it does not record `queue.added` or write the daily note right away (there is no text yet). The device's `file.items` (R1, R2 with cap:C) are pulled → archived → cpa-stt → `result.json` → `rec.result(R1)`, `rec.result(R2)`. `vcap.fold`: once every segment the item needs has a result (or is certain not to come) → assemble the body (§5.3); if it can be queued: `queue.added{origin:"voice", voice:true, intent}` + `vcap.assembled`, appended in the same batch, then a daily-note line; if not: `vcap.assembled{action:"down"/"too_long", reason}` + a daily-note line.)

- Segments and items may arrive in either order. The item usually arrives first; while the next segment is being recorded the device replies `E_BUSY` to `file.*`, so segments are pulled only after you release.
- Items with `cap` but no `voice` (all voice removed, text saved as usual): the typed text is handled by today's rules, `dropped` get `rec.hidden`, and `vcap.closed` is recorded.
- Tombstones (`drop:true`): only `rec.hidden` and `vcap.closed` are recorded, nothing else is written (§2.5).

### 5.2 When it counts as "complete"

A segment's "final result" is one of:

1. `rec.result{done}` (with or without text) or `rec.result{failed}`;
2. `rec.stale`: no result `transcribe_wait_h` (24 h) after archiving, e.g. the transcriber is not on, or `blocked`;
3. **Certain not to come**: the device is connected with `files.n == 0`, `files.live == false`, for two `pong`s in a row (about 10 s), and the segment has not reached the Mac. The device no longer holds any recording, so it will not come.

- **Only item 3 counts as "missing"**, so however long you stay unplugged does not matter: while the device still holds recordings, the Mac keeps waiting.
- Once every segment the item needs has a final result, assemble. If any segment failed, went stale or is missing, append "（另有 1 段没转出来）" ("(1 more segment was not transcribed)") to the body and create it anyway; with no text at all, downgrade to a daily-note line (`reason:"no_text"`).
- **Late segments**: if a segment in `voice` only gets a result after assembly (e.g. both the end signal and the index were lost and the next boot's recovery scan found it; or the transcriber was only fixed after `rec.stale`), write a "supplement" note line (§5.5), **not queued**: the thing you queued back then was already handled without this segment.
- **After the transcriber is fixed**: captures already downgraded by `rec.stale` are not requeued by themselves (queuing something automatically a day later would be alarming); the "supplement" line includes `cpa vcap redo <ref>`, which you can run if you want it queued.

### 5.3 How the body is assembled

1. Typed text first (when `text` is not the placeholder `（语音）`, §6.3);
2. Each segment's text follows in `voice` order, with one space between segments; `memo_text` is used, otherwise the sentences of `transcript.json` are read (when an external transcriber only reaches `level:"transcript"`);
3. Cleaned per docs/14 §2.3's `queue_text()`: control characters become spaces, leading and trailing whitespace removed;
4. **Length** (`text_max` 500 B, about 35–40 seconds of speech):
   - destination today's note, to-do (write one): no limit, the note has the full text;
   - `intent=spawn`: **over 500 B is not queued**, downgraded to a daily-note line with `reason:"too_long"`. What gets cut off might be exactly a qualifier like "…but don't delete Y yet";
   - `intent=todo`: cut to 500 B (including the trailing "…（截断）"), never splitting a character. The full-text page before starting a session shows this ending (§3.8);
5. If the result is empty after cutting, or starts with `-` (which `check_text()` rejects) → downgrade to today's note, `reason:"bad_text"`.
6. **Summary**: `语音·` + the start of the voice part, cut to 80 B per `hub/snapshots.label`. The typed text is usually a few words, visible on the full-text page.

The 40-second allowance on the device (§3.5) makes `too_long` rare: Chinese dictation runs about 4 characters, 12 B, per second, so 40 seconds is about 480 B [calc]. Only very fast speech exceeds it.

### 5.4 Rules for queueing and starting sessions

1. **Voice is only queued, never executed**: for things coming from `voice`, cpad only writes `queue.added`, and **never calls** `agent.spawn` / `agent.reply`. Sessions only start the old way: select on the board, press Enter on the confirmation. Offline captures never start a session (docs/14 §5.4 item 1).
2. **Full text first, then start**: `queue.get{qid}` replies `queue.text`, and cpad remembers in memory "the full text of this item was sent to the device on this connection". If `agent.spawn{qid}` points to a voice item whose full text was not sent on this connection → `E_BAD_FIELD`, not started. A new device connection must fetch again.
3. **Always plan**: a session started from a voice item uses `--permission-mode plan` whatever mode the project is configured with (same as docs/17 §6.3's rule for meeting to-dos). Change `SpawnRequest` in `hub/reduce.py`: use `plan` when the queued item's `voice` is true.
4. **Sessions started from voice do not accept device replies**: `queue.spawned` records that it came from a voice item; `agent.reply` pointing to such a session → `E_NOT_REPLYABLE` (the device says "cannot reply"). Reason: `resume_argv` carries no permission arguments (`argv.py:70`), so replying "ok" might approve the plan, with an unclear execution mode (H-VC8 tests this). It can be handled on the Mac as usual.
5. **Reply-type queued items never start a new session** (voice or not): `agent.spawn{qid}` pointing to an `intent=feedback` item → `E_BAD_FIELD`; the device board does not send it either (§3.8). Today the confirmation saves replies to a "waiting" session as this kind of item (`confirm.cpp:42`), yet the board can start it as a new session; this rule plugs that.
6. **The destination only depends on the key you pressed**: the editor's kind and destination. Saying "start a session" or "send it to Slack" in the transcript does nothing (docs/13 §9.2, docs/14 §5.4 item 3).
7. **Transcripts are untrusted data**: cleaning, length rules, downgrade on a leading `-` (§5.3); the body always goes after `--` in argv; session names are still generated by `session_name()`.
8. **No `uncertain`**: that is U25's "may already have started". docs/14 and docs/17 follow (§13.1).

> **2026-10-26 addendum (docs/25 D23, §7.6)**: voice segments and voice memos **never start an agent session directly**, and are never sent directly to an agent: they first go into the Mac app's "Inbox", from where you hand them by hand to some agent or send them to some app. The "queue, start session" voice above only ever becomes a queued item; starting a session still needs paging through the full text on the device board and pressing Enter, and only for projects with a linked code folder (a project can now be just a group, docs/25 D24).

### 5.5 Daily note

One line per voice capture, with the item's block ID (`^cpa-<item id>`). Each segment's text follows `route.py`'s rules: over 280 characters is cut to the first sentence, with `[全文](…/transcript.md)` ("[full text]") added.

The line formats below are the literal Chinese text cpad writes:

| Case | What the line looks like |
|---|---|
| Write one | `- 14:02 #cardputer fix wifi page 语音：①把设置页的WiFi部分改成英文。[音频](recordings/2026-10-22/1402-…/seg-000.wav) ②再跑一遍单元测试。[音频](…) ^cpa-…` |
| To-do | Same, starting with `- [ ] ` (v0.4.1's checkbox) |
| Queue for AI | `- 14:02 #cardputer 给 AI（q12）：fix wifi page 语音：①… [音频](…) ②… [音频](…) ^cpa-…` |
| Start session | Same, with the prefix `开 session（排队 q13）` ("start session (queued q13)") |
| Too long, not queued | `- 14:02 #cardputer 语音（太长，没进队列，要开就重说短一点）：全文 [音频](…) ^cpa-…` ("voice (too long, not queued; say it shorter to start it)") |
| No text transcribed | `- 14:02 #cardputer 语音（没转出文字，没进队列）：[音频 1](…) [音频 2](…) ^cpa-…` ("voice (no text, not queued)") |
| Supplement | `- 14:20 #cardputer 补充（^cpa-<条目 id> 的第 2 段，没进队列；cpa vcap redo <条目 id>）：文字 [音频](…) ^cpa-<会话 id>` ("supplement (segment 2 of ^cpa-<item id>, not queued; …)") |
| Not attached | `- 14:02 #cardputer 语音片段（没挂上条目）：文字 [音频](…) ^cpa-<会话 id>` ("voice segment (not attached to an item)") |

**No duplicate lines**:

- `hub/reduce.py` today writes a line immediately for `kind:"note"` (`fx.WriteNote`): not when the item carries `voice`; the assembled line replaces it.
- `recordings/route.py` writes a line for both `rec.result` **and** `rec.stale` ("语音（未转写）", "voice (not transcribed)"): segments with `cap` write neither; the capture line, "supplement" or "not attached" replaces them. Removed segments get no line at all.
- Typed queued items still do not write to today's note (unchanged); voice ones do, because segments no longer have their own line, and without it the speech would only be in the recordings folder.

### 5.6 Events (`events.jsonl`; new events carry no body)

| Event | Fields | When |
|---|---|---|
| `item.received` | Existing fields; `body` adds `cap`, `voice`, `dropped`, `drop` | An item or tombstone arrives (the body is here anyway) |
| `rec.result`, `rec.stale` | Existing fields, plus `cap` (copied from the manifest) | A segment gets a result |
| `vcap.assembled` | `ref, cap, dest, clips, got, missing, action(queue/note/down/too_long), reason?, bytes, sha8` | At the moment of assembly, in the same batch as `queue.added` |
| `vcap.closed` | `ref, cap, dropped, why(typed/tombstone)` | Items without `voice`, tombstones |
| `vcap.late` | `rec, ref` | A "supplement" line was written |
| `vcap.orphan` | `rec, cap` | A "not attached" line was written |
| `vcap.redo` | `ref` | `cpa vcap redo` |
| `rec.hidden` | Existing event, plus `why:"dropped"` | A segment was removed |
| `queue.added` | Existing fields, `origin:"voice"`, plus `voice:true` | Same as `vcap.assembled` |

`bytes`, `sha8` are as in docs/14 §8.1: the body's UTF-8 byte count and the first 8 characters of its sha256.

### 5.7 Reference checks (on failure, treat the segment as nonexistent and log)

- `voice` and `dropped` are each distinct MonoIds, at most 4 each, not overlapping; either one requires `cap`; `drop:true` only on `kind:"note"`, with no `voice` and a non-empty `dropped`;
- the segment was recorded by the same device (`dev`), `mode=memo`, and its `cap` equals the item's. **Meetings cannot be segments**: meetings contain other people's words (docs/17 §6.3), and a meeting with `cap` is treated as having no `cap`;
- same boot number, and `cap` < segment session id < item id (the same MonoId sequence);
- a segment belongs to only one item; the first one to arrive wins;
- from v0.8, in the same context profile as the item (docs/18 §3.4). The device does not switch profiles while an overlay is up or while recording (docs/18 §2 item 6).

### 5.8 Restart and idempotence

- `vcap.fold` is a pure function, written like `queue/fold.py`: it folds "captures waiting" and "segments waiting for an item" from `item.received`, `rec.result`, `rec.stale`, `hello` (boot number), `pong` (`pending`, `files`) and `vcap.*`. **Items with `vcap.assembled` and segments with `vcap.late` / `vcap.orphan` are not processed again** (except the one time after `vcap.redo`), so replaying the whole log at every startup never queues or writes twice.
- "Certain not to come" (§5.2 item 3) only looks at `pong`s received **in this run**, never inferred from the log.
- A crash between "assembled" and "appended": at the next startup the fold still has it waiting, and assembles it then (the same as docs/14 §6.1 for memos).

### 5.9 Hooking into docs/14's routing (v0.5)

Once docs/14 is merged, vcap no longer writes `queue.added` itself, but hands over `Capture{ref, source:"voice", dest, how:"key", project, text}`, `origin:"queued"`, for `decide()` to judge. **Segments only appear in captures whose destination is today's note, to-do, queue or session**: the device does not attach on other destinations (§3.1, §3.3), and when cpad receives `voice` attached to another destination it handles it as "downgrade to today's note" (`dest.downgraded`), never handing it to the clipboard, paste, plugins or files. So docs/14 §5.3's "plugins deliver voice items even offline" never touches voice captures, and "audio and text do not leave the Mac" holds. VC-2 **does not need to wait for docs/14**.

## 6. Protocol additions (additions only, `v` stays 0, no new error codes)

### 6.1 Overview

| Change | Direction | Content | The old end |
|---|---|---|---|
| `note.body`, `queue.body` of `outbox.item` get `cap?`, `voice?`, `dropped?`; `note.body` gets `drop?` | D→H | `cap`: MonoId; `voice`, `dropped`: 1–4 distinct MonoIds, not overlapping; `voice` or `dropped` requires `cap`; with `drop:true` only `dropped` | Old cpad ignores, saves a plain note / queued item (§6.3) |
| `file.items[]` gets `cap?` | D→H | MonoId, only on segment 0 of a segment recording; never together with `dest` | Old cpad ignores, treats as a plain memo |
| `.M` gets `cap` | Device-internal | Same | Old firmware's recovery scan ignores unknown fields |
| Items in `queue.snapshot` get `voice?` | H→D | Boolean: this item came from voice, the full text must be viewed before starting | Old firmware ignores |
| **`queue.get`** (new) | D→H request | `{qid}`; replies `queue.text` or `error{E_UNKNOWN_ITEM}`; same in-flight limit class as `queue.done` (1) | Old cpad replies `E_UNKNOWN_TYPE` |
| **`queue.text`** (new) | H→D reply | `{re, qid, text}`, `text` is `userText` (1–500 B) | — |
| `limits.json` | — | `voice_max:4`, `voice_queue_ms_max:40000`, `voice_text:"（语音）"`, registered in `schemas.LIMIT_POINTERS` | — |

No `hello.caps`, no snapshot: whether the device attaches segments does not depend on the Mac's version (§6.5). The protocol README gets a new item 23.

### 6.2 Examples

```jsonl
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-000A","kind":"queue","at":{"up_ms":98000,"wall_ms":1792440045000},"body":{"text":"fix wifi page","project":"cardputer","intent":"spawn","cap":"3QZK1M7-0000C-0006","voice":["3QZK1M7-0000C-0007","3QZK1M7-0000C-0009"],"dropped":["3QZK1M7-0000C-0008"]}}
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0010","kind":"note","at":{"up_ms":131000},"body":{"text":"（语音）","cap":"3QZK1M7-0000C-000C","dropped":["3QZK1M7-0000C-000D"],"drop":true}}
{"v":0,"t":"queue.get","id":"d40","qid":"q13"}
{"v":0,"t":"queue.text","id":"h41","re":"d40","qid":"q13","text":"fix wifi page 把设置页的WiFi部分改成英文，再跑一遍test shell的单元测试。"}
```

### 6.3 What `text` holds when you only spoke and typed nothing

`text` is required (`userText` at least 1 byte). Making it optional would make an old cpad reply `E_BAD_FIELD`, and the item would go into the device's `bad/`. So with nothing typed the device writes the placeholder **`（语音）`** ("(voice)", 12 B, full-width parentheses): the device keyboard can only type ASCII, so you **cannot possibly type** these characters, and a new cpad that sees it knows "nothing was typed". Tombstones use it too.

### 6.4 Line length [calc `$VC/lines.py`, `$VC/lines2.py`, per `wire.canonical_line`'s rules]

| Line | Construction | Bytes |
|---|---|---|
| `outbox.item` queue, worst case | 500 B ASCII body, 12-character project, `sid`, `uncertain`, `cap`, 4 `voice`, 4 `dropped` | 929 (712 without the new fields) |
| Same, body of 500 `"` | Each escaped to 2 bytes | **1429** |
| Tombstone | 4 `dropped` | 321 |
| `file.items` with 4 entries, every field at maximum and with `cap` | — | 2096 (over 2048) → per `file_fit_page` the device puts 3 per page, `more:true`: 1600. Same precedent as v0.4.1's long `dest` |
| `queue.get` | Longest id and qid | 63 |
| `queue.text` | 500 B ASCII / 166 Chinese characters / 500 `"` | 593 / 591 / 1093 |
| `queue.snapshot` | 12 items, each 80 `"`, with `voice` | 3291 (3135 today): already over today, paged by `codec.fit_list` as before |

The limit is always 2048 B (`line_max`). `test_worst_lines.py` adds these lines.

### 6.5 Mixed versions

| Combination | Result |
|---|---|
| New firmware + old cpad | Segments still attach (the device does not look at the Mac's version). The old cpad treats segments as plain memos (one line each), saves the item as "（语音）" or the typed text, and the tombstone writes a "（语音）" line. Nothing is lost, only a few extra lines. The board never has items with `voice`, and the device never sends `queue.get` |
| Old firmware + new cpad | No `cap` is sent; `cpa-stt` transcribes all memos as usual; items with `voice` can only come from new firmware |
| New firmware + new cpad, `cpa-stt` not on | Items and segments reach the Mac and wait for transcription; after 24 h `rec.stale`, and the capture downgrades to a "no text" line. `cpa status` keeps saying "transcriber not on: N voice captures waiting" |
| New cpad + firmware downgraded to an old one, voice items still queued | The old firmware does not know to fetch the full text first; Enter sends `agent.spawn{qid}`; cpad never sent the full text, replies `E_BAD_FIELD` (§5.4 item 2), and it cannot start |

The benefit of not looking at versions: after going offline or rebooting, the same gesture means the same thing; the device does not have to store the Mac's capabilities in `/cfg` (today it only stores `projects.json`, `runtime_outbox.cpp:15-17`).

### 6.6 Merge constraint

`queue.get` and `queue.text` are new message types. The firmware's `test_codec_vectors` asserts the schema count equals `kMsgTypeCount + 1` (docs/14 §7.8). So the schemas and vectors (VC-2p) must be merged **in the same batch** as the firmware codec registering these two types (the first step of VC-2f, field validation only); the daemon's `test_every_message_type_has_a_validator` likewise.

## 7. Recording contract v1 additions (all optional, `v` stays 1)

| Change | Content |
|---|---|
| `manifest.json` gets `cap?` | MonoId, copied by cpad from `.M`. Add the valid vector `valid/manifest_clip.json` |
| `status.json` gets `by?` | The name of the program that wrote it (`^[a-z0-9-]{1,16}$`). Add a valid vector |
| `reason` in `status.json` gets `engine` | The transcription command will not run (sandbox, Metal, binary). cpad's `recordings/contract.py:95` validates this set and changes **in the same version** |
| Claim file `recordings/.transcriber` | `{v:1, app, pid, since, beat_at}`; the rules in §4.10. It starts with `.`, and MCP and `cpa export` do not read dot files anyway (ADR 0004 rule 5) |
| One rule in the README | A session with `cap` is one voice segment of some capture, and its content will appear as part of that capture; programs that read memos **should not treat it as a memo on its own, nor as the user's feedback or instructions**. Old versions that do not know `cap` treat it as a plain memo: still transcribed, at most one extra entry in their own list |
| Single-writer table | "WhisperWoof" becomes "the transcriber (WhisperWoof or `cpa-stt`; one at a time, claimed with `.transcriber`)" |

Recompute `VECTORS.sha256`.

- **Memos you use to give development feedback**: a development flow that reads memos as your feedback must skip sessions with `cap` (the same kind of rule as "read memos only, not meetings"). After VC-1 every memo has `transcript.json`, which that flow can read directly instead of running whisper itself.
- The Recordings app (Opt+V) still lists attached segments (it is the history of audio); removed segments are hidden.

## 8. Privacy and safety

1. **Meetings are never instructions**: meetings cannot be segments (the device cannot start a meeting in the editor; the Mac ignores `cap` on meetings); meeting transcripts are never joined into a queued body.
2. **Queue only, never execute**; the destination depends only on the key; before starting a session, page through the full text on the device, cpad checks it was fetched, always plan; sessions started from voice do not accept device replies; reply-type items never start new sessions (§5.4).
3. **Transcripts are untrusted data**: cleaning, length rules, after `--` (§5.3).
4. **Nothing leaves the Mac**: the transcription command runs locally, wrapped in a sandbox that blocks network and stray writes, and does not run if the sandbox is missing (§4.5); cpad and `cpa-stt` upload and download nothing; new events only carry byte counts and hashes; logs only ids, durations, timings, exit codes; the command's stderr is dropped. Segments only go to the four destinations today's note, to-do, queue, session (§5.9).
5. **No draft visible, no attaching**: holding G0 while locked, in the menu or on a confirmation records a separate memo, and the recording page says so before you speak (§3.1).
6. **Recording other people**: segments are memos, with the same rules (docs/13 §13): no prompt tone, ● and the timer always visible.
7. **Single writer**: only one transcriber at a time; the claim file; `result.json` never overwrites someone else's (§4.10).
8. **The command itself**: no shell, absolute paths, environment allowlist, timeout, output cap, empty working folder (§4.3).
9. **Two Macs** (before v0.8): segments and items might be collected by different Macs, and then neither side can assemble; the segments become "not attached" lines on each side, and nothing is lost. I still assume you use only one Mac (docs/13 §13 item 7) [asm].

## 9. Latency: from releasing G0 to the voice item showing on the board [calc]

Online, Enter pressed, one segment of d seconds:

| Step | Time | Basis |
|---|---|---|
| cpad sees the new segment | 0–5 s (waits for the next `pong`) | `ping_interval_ms` 5000 |
| Pull | ≈ 0.062 d: 10 s → 0.6 s, 40 s → 2.5 s | v0.4.0 measured 513 KB/s |
| `cpa-stt` notices | 0–2 s | §4.6 |
| Transcription (`auto`, sandboxed) | 10 s → 1.7 s, 30 s → 2.2 s, 40 s → about 2.5 s | §4.5, §4.8 |
| cpad picks up `result.json` | 0–3 s | `results.py` scan interval |
| Assemble, queue, push `queue.snapshot` | <0.1 s | — |
| **Total** | **10 s: about 2.4–12.4 s; 40 s: about 5–15 s** | |

- The device's segment line turns into "On the Mac" after it is pulled: 0–5 s plus pull time.
- When recording several segments in a row, earlier segments are pulled only after you release (`file.*` replies `E_BUSY` while recording).
- Offline: after plugging in, pull first, then transcribe; 4 segments of 10 seconds take about 4 × (0.6 + 1.7) ≈ 9 s, plus waiting for `pong` and both scans.
- Going faster (the device sending `pong` on its own when a segment lands, cpad not waiting for the 3 s scan) goes to VC-3 [asm].

## 10. Budget

| Item | Added | Limit / now | Basis |
|---|---|---|---|
| AppState | Queued item `voice` 12 × 1 B | 4 KB; now about 3.5 KB | docs/13 §10.3, docs/19 §10 |
| Draft (Shell) | `cap` + 4 segments {id, duration, state} + 4 `dropped` ≈ 0.2 KB; 8 queued keys ≈ 0.1 KB | — | [calc] |
| Full-text page (Runtime) | `queue.text` 501 B + paging state | Static RAM 96 KB; now about 66.3 KB | [calc] |
| Receive buffer | Longest new line 1093 B (`queue.text`) | 8192 B | §6.4 |
| flash | Code about 4 KB [est]; microphone icon 7×9 = 63 B | Image warning line 1,835,008 B | docs/19 §10 |
| SD card | 32 KB/s: a 10-second segment is 320 KB; worst capture (write one) 4 × 5 minutes = 38.4 MB; queue and start session at most 40 seconds = 1.3 MB | — | `limits.json` |
| loop | Segment lines redrawn only when their state changes | ≤50 ms per step | [todo `test_runtime_budget`] |
| Mac memory | whisper peak about 1.98 GB, one process at a time, exits when done | — | [meas] |
| Mac compute | A 10-second segment about 1.7 s; 50 segments a day about 1.5 minutes | — | [calc] |
| Mac disk | A few KB of `transcript.*` per segment; `events.jsonl` about 250 B more per capture | — | [calc] |

## 11. Phases and work packages

Failing tests always come first (RED → GREEN → REFACTOR). Sizes: S ≤1 day, M 2–4 days, L about 5 days [est].

| Package | Content | Where | Depends on | Size |
|---|---|---|---|---|
| **VC-0** (other branches, not this doc) | D4: holding G0 under an overlay records a separate memo; release grace, double-press hands-free | `feat/ux-decisions`, `fix/memo-hold` | — | — |
| **VC-1 cpa-stt** (Mac only) | All of §4: config and validation, claim file, scanning and ordering, command runner (sandbox, environment, timeout, output cap), self-test and `blocked`, stdout parsing, RMS threshold and hallucinations, contract file writing, rerun, LaunchAgent, CLI; example config | New `stt/` (pure functions: `config`, `argv`, `profile`, `parse`, `gate`, `order`, `claim`, `contract_out`; I/O: `runner`, `service`, `cli`) | None, **can ship any time** | M (about 3 days) |
| **VC-1c contract** | All of §7; cpad `contract.py` knows `engine`; cpad's `cpa status` shows the transcriber; the two sentences for ADR 0004 and docs/15 | `protocol/recording/v1`, daemon, docs | Same batch as VC-1 | S |
| **VC-2p protocol** | §6's schemas, vectors (valid / invalid), `limits.json`, README item 23, `test_worst_lines` | `protocol/` | Merged in the same batch as VC-2f's codec registration (§6.6) | S (1 day) |
| **VC-2f firmware** | All of §3.9 | Firmware | **After both D4 and memo-hold are merged**: both branches change G0 dispatch in `shell.cpp` and `rec_gesture` (memo-hold makes it return `bool`), and this package adds its decision in the same place | L (about 5 days) |
| **VC-2d cpad** | `cpad/vcap/` (`fold.py`, `assemble.py` pure functions); `item.received`, `rec.result`, `rec.stale` with the new fields; remove duplicate note lines; `queue.added` / `QueueItem` with `voice`; summary; `queue.get` / `queue.text`; full-text check and plan for starting sessions; refuse replies to voice sessions, refuse starting reply-type items; `rec.hidden`; tombstones; supplement and not attached; `cpa vcap redo`; `cpa status` | daemon `protocol/{specs,messages}.py`, `events/records.py`, `queue/fold.py`, `hub/{reduce,snapshots}.py`, `recordings/{manifest,route}.py`, `cli/main.py` | VC-2p; VC-1 is not a hard dependency (captures wait when nobody transcribes) | M (about 4 days) |
| **VC-2h real device** | The gates in §12.2 | — | All the above | S (about 40 minutes with you) |
| **WW** (another repo, 👤) | WhisperWoof knows `.transcriber` | WhisperWoof #151 | VC-1c | S |
| **VC-3** (later) | Segment text and a "too long" hint on the device (a new Mac→device snapshot); zero-typing G0+Q/S (with docs/14 F5); MCP fields; voice replies (docs/17 §17 item 6); cutting long segments at silences; "segment·" titles in the Recordings app; `cpa vcap ls`; faster discovery | — | — | — |

- **Releases**: VC-1 goes with the next cpad version. VC-2 goes with the next "firmware + protocol" version after the v0.4.1 steady release set by D10 (v0.4.2 or v0.5) 👤.
- **Order**: VC-1 + VC-1c → VC-2p (RED) → VC-2f's codec registration + VC-2p merged together → the rest of VC-2f and VC-2d in parallel → real-device gates.
- Total: VC-1 about 3.5 days; VC-2 about 10 working days, within about two weeks with firmware and cpad in parallel.

## 12. Tests and real-device gates

### 12.1 Tests (written first, RED)

| Layer | What is tested |
|---|---|
| Firmware native | Holding G0 in the editor: attaches; `cap` taken only for the first segment; `Started` adds a segment, `Discarded` removes one, a lost end signal still attaches; the 5th segment, reaching 40 seconds, the reply editor, menu open (including the hold replayed after the menu closes), confirmation, locked are all separate memos; a single G0 tap does not save, only hints; double press = hands-free segment; pressing again within the grace is not a new segment; during grace, hands-free and finishing, Enter / `` ` `` / letters first end the segment then take effect, queue 8, the 9th hints; with voice, Backspace and `` ` `` do not exit; Opt+D removes the last segment; the discard prompt's Enter (save as note) and Opt+D (tombstone); every way of closing a draft with `cap` sends exactly one item or tombstone; kind and destination limits; start session with voice skips the confirmation; board: voice items fetch the full text first, paged, start only on the last page, 400 ms debounce per page, no start if not fetched; reply-type items do not start; the three segment line states; first tier of `hint_fits`; `test_text_width` measures the new texts; `.M` with `cap` still ≤512 B at worst; codec vectors |
| Protocol | Valid: notes and queued items with `cap`, 1–4 `voice`, `dropped`; tombstones; `queue.get`, `queue.text`; `queue.snapshot` with `voice`. Invalid: 5 `voice`, duplicates, `voice` and `dropped` overlapping, not a MonoId, `voice` without `cap`, `drop` with `voice`, `drop` on a queued item, a 501 B `queue.text` → `E_BAD_FIELD`; new lines in `test_worst_lines`; old cpad compatibility (decode `file.items` and `outbox.item` with the new fields using v0.4.1's `specs.py`) |
| Recording contract | Valid vectors for `manifest.cap`, `status.by`, `reason:"engine"`; `check_recording` passes; `VECTORS.sha256` |
| `cpa-stt` unit | Config validation (`argv[0]` absolute and existing, `{wav}` exactly once, placeholders as whole elements); sandbox profile text (working folder, Metal cache, two services); stdout parsing (with / without timestamps, empty, oversized); RMS threshold; hallucinations including the prompt from the config; contract files pass `check_recording.validate`; no overwrite when `result.json` exists; claim: someone else's fresh claim → write nothing, an expired one → take over; someone else's `status.json` (different `by`) → skip; recognizes its own `status.json` after restart; self-test fails → `blocked`, no `result.json`; a single segment fails → `failed` after backoff; `retry`; timeout kills the process; stderr not logged; segments first |
| `cpa-stt` integration | A fake command (a small script printing fixed text) runs through; the fake command in the sandbox fails to connect to a local port and to write `$HOME`; without `sandbox-exec`, `blocked{engine}` and no run outside the sandbox; a smoke test with real whisper-cli skipped by default (only runs with `CPA_TEST_WHISPER=1`) |
| cpad unit | `vcap.fold`: item first / segment first; `dropped` and tombstones (no line, `rec.hidden`); start session by typing after all voice was removed (tombstone); segments with a lost end signal: keep waiting while offline, missing only after two `pong`s with `files.n == 0`; late segments write "supplement", not queued; a draft open 25 h in the same boot is not an orphan; orphan only after a new boot number + `pending == 0`; `too_long` (spawn downgraded, todo cut with the marker); `bad_text`; `rec.stale` downgrade and `cpa vcap redo`; meetings as segments refused; one segment with two references; wrong order refused; replaying the whole log does not duplicate (hypothesis: any transcript text either yields a startable queued item or downgrades); no duplicate lines (notes with `voice` not written immediately; `rec.result`, `rec.stale` with `cap` write no line); summary is "Voice·" plus the start of the voice; `queue.get` / `queue.text`; `agent.spawn` without fetching the full text refused, with it uses plan; replies to voice sessions refused; starting reply-type items refused |
| cpad integration | A fake transcriber (a small script writing contract files) runs through "item + 2 segments → queued item + daily-note line + snapshot"; cpad's pong latency unchanged during a 60 s fake transcription |
| Simulator | `firmware/sim/scripts/ux/voice_capture.cpas` (two segments in the editor, Opt+D, Enter), `voice_capture_offline.cpas`, `voice_capture_discard.cpas` (both discard choices), `voice_spawn_confirm.cpas` (start after paging); `tools/sim/cpad_sim.py` adds `queue.get` and a fake transcriber |

### 12.2 Real-device gates (👤 schedule with you, about 40 minutes)

| Gate | Method | Pass |
|---|---|---|
| H-VC1 online, 3 segments | In the queue-for-AI editor, hold G0 and speak three segments of 5–15 seconds each, Enter | Segment lines turn to "On the Mac"; within 15 seconds "Voice·…" appears on the board, and today's note has a line |
| H-VC2 offline | Record 2 segments + Enter unplugged, then plug in | Both transcribed, queued item appears; while offline, segment lines say "Sent when connected to the Mac" |
| H-VC3 reboot with a draft open | With 1 segment attached, long-press power to reboot, then reconnect | Today's note gets a "voice segment (not attached to an item)" line |
| H-VC3b reboot mid-recording | Long-press power halfway through a segment | The segment rescued by the recovery scan (`rcv`, no `cap`) becomes a plain memo with its own "语音：…" line; noted in the release notes |
| H-VC4 grace, double press, press right after release | Press again within 300 ms of releasing; in the editor double-press to speak, then press G0; press Enter right after releasing; type right after releasing | All one segment; the double press did not save the draft away; the item saved with Enter carries the segment just recorded; not a single typed character lost |
| H-VC5 your real speech | 👤 run `cpa-stt test` in the terminal on a few of your own memos; record a few more: Chinese only, English only, mixed, with code names (`test_shell`, `npm run test`) | You judge whether the text is right; English does not turn into gibberish |
| H-VC6 LaunchAgent | `cpa-stt` transcribes a segment under a LaunchAgent with the sandbox, recording cold-start time; watch cpad's pong at the same time | Metal works, no extra 5 seconds; the sandbox does not block reading the model and WAV; pong unaffected |
| H-VC7 cases that do not attach | Hold G0 and speak while locked, with the Tab menu open, and in the reply editor | The recording page says "Voice memo" before you speak; all are separate memos, the draft unchanged |
| H-VC8 how plan mode ends | Start a session from a voice item and wait for its plan | Record whether it stops at `waiting` or `idle`; device replies refused with "cannot reply"; the result feeds VC-3's voice reply design |

## 13. Relation to other docs and branches in progress

### 13.1 Places to change accordingly (DOC, with each package)

| Doc | Says now | Changes to |
|---|---|---|
| docs/04 §8.4, docs/20 D4 | In the edit layer, a single G0 tap = save; holding G0 in the editor records a separate memo | A single G0 tap only hints, double press is hands-free; when the editor is on top and this capture can take it, the segment attaches to the draft (§3.1) |
| docs/14 §2.1 table, `Q~` / `S~` in §5.3, §13 tests | Queued items from voice have `uncertain=true` | `voice=true`; `uncertain` only for U25; voice only goes to four destinations (§5.9) |
| docs/14 §6.3 | Memos with only a transcript and no cleanup have no `memo_text` | After VC-1, memos written by `cpa-stt` have `memo_text`; decide by whether `memo_text` exists, not by `level` |
| docs/17 §6.1 | Meeting to-dos go into the queue "always with `uncertain=true`" | Same as above: `voice` plus a "会议·" ("Meeting·") summary; `queue.get` can also be used to page the full text before starting |
| Recording contract v1 README | The single writer is WhisperWoof; no `cap` | §7 |
| ADR 0004, docs/15 §1 | — | The two sentences in §4.12 |
| Protocol README | — | Item 23: voice captures (this doc's §6) |

### 13.2 Branches in progress

- **`feat/ux-decisions` (D4, D5, D6)**: the D4 line (`shell.cpp:120`) is this doc's starting point: this doc adds the "attach or not" decision before it, and the menu-open case must be decided before the menu receives the hold (§3.1). D5's direct exit on `` ` `` changes to asking first when there is voice (§3.3). D6's "` on a confirmation goes back to the editor" is used directly.
- **`fix/memo-hold`**: release grace and double-press hands-free are the same for every segment; its double press is the reason "a single G0 tap in the editor no longer saves" (§3.2). It changes `rec_gesture` (returns `bool`) and G0 dispatch, so VC-2f comes after it.
- **`docs/settings-design` (docs/21)**: the transcription language is `cpa-stt`'s config (default `auto`); the language you speak and the UI language are two different things. With an English UI, the segment line texts (`待传`, `已传 Mac`: "Waiting", "On the Mac") follow the UI translation.
- **docs/18 (v0.8)**: once `item.received` carries `prof`, §5.7's profile check uses it directly.

## 14. Risks and things to verify

| Risk | How to verify / mitigate |
|---|---|
| **Recognition of real speech not measured**: all numbers come from synthetic speech | 👤 H-VC5; latency mostly depends on duration, so it is broadly reliable |
| `cpa-stt` is off by default, you forget to turn it on, and voice captures keep waiting | `cpa status` keeps reminding; after 24 h they downgrade to note lines; the first step of the release notes is `cpa-stt use` |
| `sandbox-exec` gets removed some day | When missing, `blocked{engine}`, never run outside the sandbox; `cpa status` says so |
| The two allowed system services were only measured with whisper-cli 1.8.3 | The self-test exposes it (crash or slowness); `sandbox_mach` can add more; H-VC6 |
| WhisperWoof does not know the claim file yet | Only one of the two on; `cpa-stt` off by default; on finding a second writer it stops and warns (§4.10) |
| Speaking very fast, 40 seconds assembles to over 500 B | Start-session ones are not queued, and today's note says "too long"; no hint on the device in this version (VC-3) |
| whisper outputs characters outside the font, drawn as □ in the device's summary and full-text page | `auto` outputs simplified Chinese (measured); check in H-VC5; add traditional-to-simplified in VC-3 if needed |
| Sentences dropped at the 30-second seam | Greedy; queue and start session at most 40 seconds; cutting long write-one segments in VC-3 |
| Hallucinations not caught | Three filters (§4.9); `drop_lines` extended as they come up |
| Reboot with a draft open | Segments become "not attached" lines, nothing lost (H-VC3); stored together once docs/20 U16's draft persistence is done |
| Reboot mid-recording | The rescued segment has no `cap` and becomes a plain memo (H-VC3b); to keep the link, `cap` must be written at recording start somewhere the recovery scan can read, in VC-3 |
| Where a voice-started session stops after its plan | H-VC8; in this version the device can never reply to it |
| A single G0 tap no longer saves, changing your habit | A single tap hints "Enter to save"; §15 item 1 |
| New firmware + old cpad gives extra "（语音）" lines | Release both sides in the same version; only extra lines, nothing lost (§6.5) |
| Transcription uses the GPU and competes with your dictation on the Mac | One segment at a time, 1–3 seconds each; lower QoS if needed [todo] |

## 15. Your call

All have defaults; without a reply, the defaults apply. Other parameters (at most 4 segments per capture, at most 40 seconds for queue and start session, 500 B body, the two discard choices) follow the design and are not decided separately.

1. **Holding G0 in the editor = add a segment to this capture** (write one, to-do, queue for AI, start session; replies do not attach), **a single G0 tap no longer saves, only Enter saves**, and a double press records a segment hands-free. **Default: agree**. Alternative: a single G0 tap waits 350 ms for a second press, then saves (adds a "second press or not" state; not recommended).
2. **Voice is only queued**: in the start-session editor with voice, Enter only saves a queued item; starting a session from a voice item requires paging through the full text on the device and pressing Enter on the last page, always plan; sessions started from voice cannot be replied to from the device. **Default: agree**.
3. **Transcription by the standalone `cpa-stt`, off by default**: it only turns on when you run `cpa-stt use` once (with the whisper.cpp example config: large-v3-turbo, automatic language detection, greedy, VAD, tight sandbox); ADR 0004 and docs/15 each get one sentence (§4.12). **Default: agree**. Alternative: do not install `cpa-stt`, and wait for WhisperWoof (#149–#152 merged, and knowing `.transcriber`).

## Appendix: how review comments were handled

The review raised 9 high, 9 medium and 5 low comments, plus a "scope cuts" section. Each was checked against the code and the three working trees in progress (`cancel()` / `delete_char()` in `compose.cpp`, `keymap.cpp:82`, `g0_gesture.h`, `shell_g0.cpp`, `dispatch()` on `feat/ux-decisions`, `rec_key_event`, `kFinishGiveUpMs` in `rec_core.h`, `Started` in `rec_msgs.h`, `runtime_outbox.cpp:15-17`, `LinkInfo` in `app_state.h`, `board_logic.cpp:69-74`, `WriteNote` and `_spawn_text` in `reduce.py`, `resume_argv` in `argv.py`, `rec.stale` in `route.py`, `queue_text_max` in `limits.json`, `files.n` in `common.schema.json`, docs/14 §5.3); all held up and all were adopted. Two more things were measured: the tight sandbox (item 16) and the effect of forced Chinese on English (item 18).

| Review comment | Handling |
|---|---|
| **1** Removed segments are guessed: discarding, starting a session online, and a lost end signal all go wrong | **Adopted**. Items get `dropped`; a draft with `cap` that closes without an item automatically sends a tombstone `note{cap, dropped, drop:true}`; a segment nobody mentions is always "not attached", never "deleted"; the draft adds segments at `Started` and removes them only at `Discarded`; the discard prompt gets Opt+D "drop the voice too" (§2.1, §2.4, §2.5, §3.3, §5.1) |
| **2** Once the 24 h "missing" timer passes, late segments become invisible | **Adopted**. The 24 h timer is removed; "missing" only holds when the device is connected, `files.n == 0`, not recording, for two `pong`s in a row; late segments write a "supplement" line, not queued (§5.2, §5.5) |
| **3** With voice and no typing, one key throws the draft away | **Adopted**. Voice counts as "not empty": `cancel()` always asks first, `delete_char()` does not exit and hints "Opt+D removes a segment · ` discards"; native tests (§3.3, §12.1) |
| **4** The doc describes a space lock, but memo-hold built a double press; a double press in the editor would save the draft away | **Adopted**. §3.2 rewritten per memo-hold's actual double press; "G0 single tap = save" removed from the edit layer, Enter is the only save key, double press = hands-free segment; brought up for decision (§15 item 1); native tests and H-VC4 |
| **5** Keys pressed during grace and finishing are swallowed with no reaction | **Adopted**. During grace, hands-free and finishing, keys other than G0 first end the segment, are queued in order (8), and handed to the editor once it settles; with G0 held, "Release G0 first" (§3.2) |
| **6** plan is not a hard gate: the confirmation only shows 80 B; how a plan session ends is unverified | **Adopted**. Voice items fetch the full text before starting, paged, only the last page can start, 400 ms debounce per page; cpad checks the full text was sent on this connection; the summary uses the start of the voice part; new gate H-VC8; sessions started from voice do not accept device replies; removed "pressing Enter here means it was seen on the device" (§3.8, §5.3, §5.4) |
| **7** Truncating dictated instructions | **Adopted**. `intent=spawn` over 500 B is not queued and downgrades as `too_long`; `intent=todo` is cut with "…（截断）"; voice for queue and start session at most 40 seconds per capture, write one still 5 minutes. A "too long" hint on the device needs a new Mac→device message, in VC-3; in this version the 40-second allowance makes it rare, and today's note says so (§3.5, §5.3, §14) |
| **8** Voice replies are a dead end, and might even start a new session | **Adopted**. The reply editor attaches no voice in this version; holding G0 records a separate memo with the hint "Replies need typing"; cpad refuses `agent.spawn{qid}` pointing to reply-type items, and the device board does not send it either: voice or not, the hole that exists today is plugged too (§2.3, §3.1, §3.8, §5.4 item 5) |
| **9** docs/14's destinations ignored | **Adopted**. Only today's note, to-do, queue, session attach segments; other destinations record a separate memo with a hint; with voice, the destination cannot be changed to another; cpad downgrades `voice` attached to other destinations to today's note (§3.1, §3.3, §5.9) |
| **10** "Capabilities stored in `/cfg`" is wrong | **Adopted**, choosing "always attach": `caps.vcap` removed, attaching does not depend on the Mac's version; an old cpad only produces a few extra "（语音）" lines and separate memo lines (§6.5) |
| **11** Duplicate daily-note lines | **Adopted**. Notes with `voice` are not `WriteNote`d immediately; `rec.result` and `rec.stale` with `cap` write no line (§5.5) |
| **12** Rebooting mid-recording disconnects the segment from the draft | **Adopted** (documented, not fixed in this version): the rescued segment has no `cap` and becomes a plain memo; new gate H-VC3b, noted in the release notes; writing `cap` at recording start goes to VC-3 (§12.2, §14) |
| **13** Recognizing its own `status.json` by mtime fails after a restart | **Adopted**. `status.json` writes `by:"cpa-stt"`, and the contract gets this optional field (§4.7, §7) |
| **14** Environment problems become permanent failures | **Adopted**. Self-test first: environment problems only write `blocked{model_missing / engine}`, no `result.json`, and transcription continues once fixed; only single-file problems write `failed`; `cpa-stt retry`, `cpa vcap redo` (§4.6, §4.11, §5.2) |
| **15** Racing WhisperWoof to write; `auto` silently picks whisper.cpp | **Adopted**. Default `off`, no `auto`; the contract gets the claim file `.transcriber`, which both sides must honour (WhisperWoof's side listed as the 👤 WW package); how to hand over is written down (§4.4, §4.10) |
| **16** "No-network sandbox" overstates it | **Adopted**, and the tight profile was measured: can only write the working folder and the Metal cache, only two system services allowed; connecting to ports, resolving domains, writing `$HOME`, `open` all fail; timing unchanged (not allowing the Metal cache costs 5 seconds, not allowing the compiler service crashes; both recorded). Wording changed to "blocks direct network access and stray writes"; without `sandbox-exec`, `blocked`, with no option to turn the sandbox off (§4.5) |
| **17** Conflicts with "no app-specific code" | **Adopted**, choosing the review's recommendation: transcription goes into the standalone `cpa-stt`, in the same position as WhisperWoof; it only knows the generic command conventions, and whisper.cpp only appears in the example config; Spotlight model search and cloning other apps' caches removed; the two sentence changes to ADR 0004 and docs/15 are spelled out in §4.12 and brought up for decision (§15 item 3) |
| **18** Mixed Chinese and English | **Adopted**, and measured: forced `-l zh` turned an English sentence into gibberish, `auto` was word-for-word correct and the same as `zh` on mixed speech, so the default is `auto` (+0.5 s); H-VC5 adds English only, mixed, and code names; the prompt from the config goes into the hallucination filter (§4.4, §4.8, §4.9) |
| **19** Opt+D can only remove the last segment, cannot be undone, and audio and transcript remain | **Adopted**. Called "remove", not "delete", with the hint "Removed ②"; removed segments get `rec.hidden`, are not visible in the Recordings app, and can be brought back with `cpa rec unhide`; deleting unpulled audio on the device is not done (today there is no path for deleting recordings on the device) (§2.4, §3.3) |
| **20** A draft open over 24 h makes its segments orphans | **Adopted**. No orphans within the same boot; only after a new boot number and an empty outbox (§2.4) |
| **21** Tab menu open over the editor | **Adopted**. With the menu open, a separate memo is recorded (no draft visible means no attaching, same as locked), decided before the menu receives the hold, with a test (§3.1) |
| **22** Merge order | **Adopted**. VC-2f depends on both the D4 and memo-hold branches (§11, §13.2) |
| **23** Transcription must not hold up the serial port | **Adopted**. Transcription is in another process (`cpa-stt`), and cpad is not affected; the integration test measures pong latency during a 60 s transcription, and H-VC6 checks again on the real device (§4.6, §12) |
| **Scope** VC-2z zero-typing G0+Q/S | **Cut**, done with docs/14 F5 (VC-3) |
| **Scope** `vcap.snapshot`, `revs.vcap`, `caps.vcap`, "transcribing voice" on the board | **Deferred to VC-3**; in this version segment lines only show the duration and "Waiting / On the Mac" (§3.6) |
| **Scope** MCP fields, "segment·" titles in the Recordings app, `cpa vcap ls`, `cpa transcriber setup`, `hint:"projects"` | **Cut or deferred** (VC-3). Decisions reduced from 6 to 3 |

## Appendix: evidence index

- Firmware: `lib/shell/compose.cpp:14-15` (ASCII), `44-65` (`submit`, `cancel`, `delete_char`), `76-84` (discard prompt); `lib/input/keymap.cpp:80-82` (edit layer Enter, G0 single tap = save); `lib/shell/runtime_outbox.cpp:15-17` (`/cfg` only stores `projects.json`), `47` (shared MonoId); `lib/core/app_state.h:15-22` (`LinkInfo` capabilities only in memory); `lib/rec/rec_msgs.h:21-28` (`StartSpec`), `51-62` (`Started` carries the session id, `Discarded`); `lib/rec/rec_meta.h` (`.M` ≤512 B, unknown fields ignored, `rcv`); `lib/rec/rec_core.h` (`kFinishGiveUpMs`); `lib/shell/confirm.cpp:42, 65` (saved as a reply-type queued item when the session is waiting); `modules/board/board_logic.cpp:69-74` (the board starts a session for any queued item); `lib/protocol/protocol_limits.h:31` (`kQueueTextMax = 80`).
- Branches in progress (read-only, uncommitted): on `fix/memo-hold`, `lib/input/g0_gesture.h` (`kDoubleTapMs = 350`, `HandsFree`), `lib/shell/shell_g0.cpp` (`g0_down`, `hands_free`), `lib/shell/shell.cpp` (`rec_key_event` only accepts digits during grace, `RecLatch`), `lib/rec/rec_core.h` (`kMemoGraceMs = 600`), `lib/input/keymap.*` (`G0Double → RecHandsFree`, no space lock); on `feat/ux-decisions`, `lib/shell/shell.cpp:115-124` (`dispatch()`: menu before the D4 line), `lib/input/keymap.cpp` (D5: `` ` `` = exit), `lib/shell/compose.cpp`.
- daemon: `hub/reduce.py:125-137` (`outbox.item`; write one immediately `WriteNote`s), `149-157` (`_spawn_text` uses the full text), `160-174` (`agent.spawn`; the `SpawnRequest` at `172` uses the project's mode), `177-188` (`agent.reply`); `agents/claude_code/argv.py:66-71` (`spawn_argv` carries `--permission-mode`, `resume_argv` does not); `recordings/route.py:59-71` (memo lines; `rec.stale` also writes a line); `recordings/results.py` (3 s scan, `transcribe_wait_h`); `recordings/contract.py:95-100` (the `reason` set of `status.json`); `recordings/history.py:124-132` (hiding is only hiding); `config/settings.py:63` (`transcribe_wait_h = 24`); `service/launchd.py:19` (how the LaunchAgent is written).
- Protocol: `limits.json`; `schema/v0/outbox.item.schema.json`, `common.schema.json` (`userText`, `files{n, rev, live}`, `intent`), `pong.schema.json` (`pending`, `revs`), `queue.snapshot.schema.json` (≤12 items, `text` ≤80 B), `rec.act.schema.json`; `tools/test_worst_lines.py`, `tools/wire.py` (`canonical_line`); `recording/v1/README.md` (single-writer table, `blocked{reason}` in `status.json`, hand-off rules, forward compatibility).
- Docs: docs/02; docs/04 §8.4; docs/13 §7.1, §9.2, §10.3, §13; docs/14 (`b5ad361`) §2.1–§2.3, §3, §5.3, §5.4, §6.1, §6.3, §7.8, §8.1, F5; docs/15 §1, §2; docs/17 §6.1, §6.3, §17 item 6, M10; docs/18 §2 item 6, §3.4; docs/19 §6.6, §6.9, §10; docs/20 U4, U6, U15, U16, U17, U25, U26, D4–D8, D10; ADR 0004 (background "WhisperWoof today, any program tomorrow", rule 5).
- External: WhisperWoof PRs #149–#152 (still OPEN on 2026-10-22), #151 body (inbox off by default, silence threshold 0.00084, backoff 30 s / 2 min / 8 min, `result.json` placed with link).
- Local [meas]: `whisper-cpp 1.8.3` (Homebrew); `ggml-large-v3-turbo.bin` 1,624,555,275 B; `ggml-tiny.bin` 77,691,713 B; Apple silicon Mac, macOS 26; `$VB/out/bench.json`, `bench-greedy.json`, `*-vad.txt`, `noise05.txt`, `silence05.txt` (v1's timing and hallucinations); `$VC/sbx/profile.sb` (§4.5's sandbox profile) and `$VC/sbx/wd/` (timing and output of each profile: network-only deny 1.19 s; write deny without the Metal cache 6.66 s, "loaded in 4.988 sec"; all system services denied, exit code 134, "Unable to reach MTLCompilerService"; only the compiler service allowed 7.0 s; final profile 10 seconds 1.15–1.17 s, 30 seconds 1.64–2.20 s, word-for-word the same as unsandboxed); sandbox probes (`$VC/conn.py` port connection `Operation not permitted`, `curl` resolution failure, `touch $HOME/…` failure, `open -a TextEdit` failure); `$VC/lang/` (`say -v Samantha` English, `say -v Tingting` mixed, output and timing for `-l zh/auto/en`); `$VC/lines2.py` (§6.4); `$VC/efont.py` (efontCN_12: ①–④, …, · present, 🎤 absent); `~/CardputerNotes/recordings/*/manifest.json` (only manifests read: 15 sessions, 13 memos of 1.0–50.7 seconds, 2 meetings, no `result.json`).
