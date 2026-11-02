**English** · [中文](13-assistant-v2-plan.zh-CN.md)

# 13 · Assistant v2 plan: home, always-available menu, lock screen, recording history, capture destinations

> Role: final version by the product lead (Lead). Date 2026-10-21. Merges four research reports (information architecture and interaction, capture destinations, similar products, firmware app framework), one draft and one round of adversarial review. Only docs, code, issues and the git working tree were read; **no device was connected**.
> Basis: 04 PRD §4, §8; 05 architecture §5.4–§5.13, §6; 09; 11 M3 design; 12; gates/whisperwoof.md, granola-pipeline.md, launcher.md, m3-device-gates.md; releases/v0.3.0.md; the `release/v0.3.1` branch working tree (v0.3.1 silent recording, not committed yet); firmware `lib/input`, `lib/shell`, `lib/module_api`, `modules/*`; daemon `device/`, `hub/`, `recordings/`, `notes/`, `cli/`; `protocol/schema/v0`, `protocol/recording/v1`; issues #10 #13 #16 #17 #19 #23 #24.
> Marks: **[src]** docs or code read · **[meas]** existing measurements · **[calc]** calculated · **[inf]** inferred · **[asm]** design assumption, can change · **[todo]** to verify with a gate or the real device · **👤** needs you.
> Screen sketches are drawn for 240×135: one line is about 40 ASCII characters or 20 Chinese characters [src 05 §5.12]. Key combos are written `Opt+X`, matching today's hint bar. `▸` marks the selected item. The sketches below show the English meaning of the device's Chinese UI text.

## 0. One-page summary

**Goal**: turn the Cardputer from "a few features" into "an assistant": jot down whatever comes to mind; find recordings, things waiting on you and projects later; control the lock screen with one hand; find a shortcut even when you forget it.

**First, the "mute" you raised today**: it has already been decided that recording is completely silent (recording is silent, for personal use); this is done on the `release/v0.3.1` branch but not released yet [src CHANGELOG.md working tree; docs/11 line 8]. After v0.3.1 the device makes **no sound at all**, so "not wanting everyone in a meeting to hear it" is already solved. v0.4 adds no mute key or volume levels: there is no sound to mute, so it would be an empty switch. The menu's "Control" page will say "Sound: off". The meeting confirmation stays, with its text changed to say "telling people and getting consent is your responsibility" (§7.4).

**Three steps next**:

1. **Finish v0.3.x**: release v0.3.1 (silent); ship **mic gain** on its own first, not waiting for v0.4. The peak you measured today was only about 6.5% FS, and in a meeting people are farther away, so it is even less (§7.5, needs about 20 minutes of your time 👤); run end-to-end with the real WhisperWoof.
2. **v0.4.0 "whole-device navigation", firmware only**:
   - **Home launcher** (Opt+H): the letter on each tile is its shortcut. The project board moves to **Opt+P**; Opt+1–9 stay as they are.
   - **Tab menu, always available**, from any page. The menu has three pages: this page's keys, go to, control. Select a row and press Enter: the effect is **exactly the same** as pressing that key. The far right of every page's hint bar always says "Tab menu".
   - **Opt+L lock screen**: to unlock, press Opt+L, then Enter within 3 seconds. While locked, the screen is off even when plugged into the Mac, and random presses in a pocket do nothing.
   - **● always visible while recording**: the same when locked, with the menu open, or with a toast. With no prompt tones any more, the screen is the only signal.
3. **v0.4.1 "recording history and to-dos"**, adding protocol and Mac side:
   - **Recordings app (Opt+V)**: browse the full history while connected to the Mac, filtered by all / ★ / memo / meeting. You can add ★, file into a project, or hide from the list.
   - **To-do destination**: Opt+T types a to-do; while holding G0 and speaking, press T to save it as a to-do, or N as a note.

**Later versions**:
- v0.5: complete destinations (#24, which key goes where is configured by you on the Mac; copy voice to the clipboard after transcription; save into Obsidian); recordings 2.0 (playback on the device, real delete, open on the Mac).
- v0.6: option-style replies (#16), to-do app, coding agent.
- v0.7: Mac app (WhisperWoof) and thinking partner.
- v0.8: context profiles (#19).

**9 things that need your call** (§18) all have defaults; if you do not reply, the defaults apply.

## 1. Where each of your four points of feedback lands

| # | What you said (paraphrased) | How it lands | Later |
|---|---|---|---|
| 1 | Wants a UI, with features as separate apps and shortcuts straight to them; past recordings should be deletable, starrable and taggable | v0.4.0: home launcher, one Opt+letter per app (§4, §5). v0.4.1: the Recordings app can add ★, file into a project (used as a tag), hide from the list (§8) | v0.5: real delete (to the trash), playback on the device, offline browsing, free tags |
| 2 | While connected to the computer, enter things in different ways and file them in different places; this has not been explained yet | §9.1 first explains the 6 paths that exist today. v0.4.1 adds a "to-do" destination, by typing or by voice | v0.5: bindings configured on the Mac (#24), clipboard, Obsidian, "send to" after the fact; v0.7: paste at the cursor, WhisperWoof, cloud to-dos |
| 3 | Coding agent status is only one part, the other features are too thin; needs overall navigation, more like a complete assistant | The project board is just one tile on the home page. Home shows every feature, plus the most urgent "waiting on you" item (§5.6) | v0.6: to-do app; v0.7: thinking partner |
| 4 | Wants an always-available menu that reminds you of forgotten shortcuts; lock screen and mute matter a lot; when recording a meeting, does not want everyone to hear a loud prompt tone | Prompt tones: all removed in v0.3.1. v0.4.0: Tab menu (§6), Opt+L lock screen (§7.1), ● always visible while recording (§7.2) | — |

In feedback 4, the voice transcription said "lock screen press screen"; I read it as "lock the screen, turn the screen off" [asm]. Locking turns the screen off by itself, also when plugged into the Mac (§7.1).

## 2. Starting point: what the device is after v0.3.1

- **Already there**:
  - v0.1: G0 quick notes, project board, waiting on you, queue, start session;
  - v0.2: SD card, Launcher, single-key back;
  - v0.3: hold G0 to record a memo, Opt+R to start a meeting after confirmation, add ★ during a meeting, transfer automatically to `~/CardputerNotes/recordings/` when plugged into the Mac, WhisperWoof does transcription and cleanup (PRs #149–#152 waiting for you to merge) [src releases/v0.3.0.md].
- **v0.3.1 (working tree, not committed)**:
  - all recording prompt tones are removed, and the speaker is never turned on anywhere in the firmware [src audio_io.h, audio_task.h working tree];
  - the meeting confirmation, the 400 ms debounce and `consent:"ack"` stay as they are;
  - the screen is the only signal [src docs/11 line 8].
- **Decided (v0.3.1)**: mic gain `kMicGain {9, 0xC4, 2}` (PGA +27 dB, ADC volume +2.5 dB, no more software gain), calibrated by HIL `mic_cal`; your normal handheld speech should peak at about -12 dBFS (gates/m3-device-gates.md §5.4) [src lib/core/mic_gain.h]; v0.3.0 was `{0, 0xBF, 16}`, and your own test peaked at about 6.5% FS [meas].
- **Effect on the plan**: the draft's "three prompt tone levels, Opt+M mute, settings file, `cue` field in the recording manifest, two-level volume gate" are all removed. Opt+M stays "System", so **only Opt+H changes meaning**.

## 3. Design principles

1. **Only one key table**. Every binding is in `kKeymap` [src keymap.cpp:43-100], and the menu is generated from this table. Tests ensure every action has a one-line description, so the keys written in the menu match the real keys.
2. **One letter stands for one thing**. For example T is always about to-dos: global Opt+T is "write a to-do", Opt+T on the compose page is "change to to-do", and T while holding G0 is "save this speech as a to-do". There is precedent: Opt+1–9 globally opens a project, and on the compose page changes the target project [src keymap.cpp:48-52, 72-76].
3. **Common features take at most two presses**: Opt+letter, one; press the letter on the home page, one; Tab then the letter, two. Rare actions only live in the menu and take no key combo.
4. **The menu and home page add no separate execution path**. A menu row or a home tile is "pressing that key for you", so the effect is always the same as pressing the key directly (§6.2).
5. **Capture is always available**. G0 works on every page, including the menu. On the lock screen it takes one more step (§7.1). During a meeting G0 always adds ★.
6. **● is always visible while recording**, whether locked, in the menu or with a toast (§7.2).
7. **Actions that change the Mac's current state happen only while connected to the Mac, at the moment you press the key, and are never done later** (an extension of PRD principle 2).
8. **File first, then deliver**; **the destination is decided only by the key, never by the content**.
9. **Only single keys and two-key combos** [src 04 §8.1]. **The protocol only gets additions, never changes**; `v` stays 0 [src 05 §6.9]. **Recording contract v1 does not change by a single word in v0.4** (§8.4).

## 4. App model

### 4.1 Apps

| App | How to enter | What it covers | Version |
|---|---|---|---|
| **Home** | Opt+H | Launcher grid, plus the most urgent "waiting on you" item | New in v0.4.0 |
| **Projects** | Opt+P; Opt+1–9 go straight to a project page | Today's project board, moved as is | Key changes in v0.4.0 (was Opt+H) |
| **Waiting on you** | Opt+I | Sessions waiting for your decision. Later also: pending destinations (v0.5), to-dos proposed by AI (v0.6), answers to questions (v0.7) | Exists |
| **System** | Opt+M | Today's system page | Unchanged |
| **Recordings** | Opt+V | History of memos and meetings: list, detail, ★, file into a project, hide | New in v0.4.1 |
| To-dos | Press T on home (from v0.6) | Two tabs, "mine" and "for AI" | v0.6 |
| Ask | Opt+A (reserved) | Thinking partner; answers go into "waiting on you" | v0.7 |

### 4.2 Overlays

Overlays sit on top of the current page and are not apps: write one (short G0 press, or Opt+N/Q/S; v0.4.1 adds Opt+T), voice memo (hold G0), meeting page (Opt+R), **menu (Tab)**, **lock screen (Opt+L)**, confirmations.

### 4.3 What was merged

- **Memos, meetings and recording history become one "Recordings" app**. All three are the same thing: a recording session, only with a different `mode` [src 11 §4.4]. Starting a recording still uses the global gestures; you do not have to enter the app first.
- **The queue is not a separate app**: it is already in the project page; the v0.6 to-do app will gather it.
- **Profiles (#19) take no global key combo**: switching goes into the "Control" page, with a confirmation (v0.8).

## 5. Navigation and keys

### 5.1 Three ways to go anywhere

| Way | Example | Presses |
|---|---|---|
| Opt+letter | Opt+P opens Projects | One combo |
| Press the letter on the home page | Press P on home | One (from v0.4.1 except R and L, which need Opt: docs/20 D1) |
| Tab to open the menu, then a letter, or pick with the arrow keys | Tab → P; or Tab → `;` `.` → Enter | At least two; **no key to remember** |

### 5.2 Global keys (with no overlay)

| Key | Does | Mnemonic | Change |
|---|---|---|---|
| G0 short press | Write one (typing), to today's note | — | Unchanged |
| Hold G0 | Record a voice memo; while holding, 1–9 / 0 file into a project | — | Unchanged; **v0.4.1 adds T to-do, N note** |
| **Tab** | Open the menu | — | New in v0.4.0 |
| `` ` `` / Backspace / Fn+`` ` `` | Back | — | Unchanged |
| **Opt+H** | **Home (launcher)** | Home | **Meaning changes in v0.4.0**: was the project board |
| **Opt+P** | Project board | Projects | New in v0.4.0 |
| Opt+1–9 | Open the Nth project page | — | Unchanged |
| Opt+I | Waiting on you | Inbox | Unchanged |
| Opt+M | System | — | Unchanged |
| Opt+N / Q / S | Write a note / queue for AI / start a session | Note / Queue / Spawn | Unchanged |
| Opt+R | Meeting recording (confirmation first) | Record | Unchanged; confirmation text changes (§7.4) |
| **Opt+L** | Lock screen | Lock | New in v0.4.0 |
| Opt+D | Handle the selected item in a list: done or dismiss | Done | Unchanged; in v0.4.1 Recordings it is "hide" (needs confirmation) |
| **Opt+V** | Recordings | Voice | New in v0.4.1 (reserved in 11 §18) |
| **Opt+T** | Write a to-do | Todo | New in v0.4.1 |

- **Reserved but not bound yet**: A (ask, v0.7), C (clipboard, v0.5), G (send to, v0.5), E (edit title, v0.5), W (WhisperWoof, v0.7).
- **Z is not used**: tests use Opt+Z as the example of "unbound" [src test_shell_dispatch_order/test_main.cpp:139].
- **On the compose page** (v0.4.1): Opt+N/T/Q/S change the kind or destination, Opt+0–9 change the project; the compose page for replying to a session cannot change the destination.
- Every new binding must pass the ghost-key check in `test_keymap_static`. "Hold G0 then press a letter" produces no ghost keys, because G0 is not in the keyboard matrix [src granola §4.2].

### 5.3 Whether Tab and Opt+L work on each layer

| Key | Home and apps | Compose page | Confirmations (incl. meeting confirmation) | Meeting page | Memo HUD (holding G0) | Lock screen |
|---|---|---|---|---|---|---|
| Tab menu | ✓ | ✓ (only "This page" and "Control") | ✓ (same) | ✓ (same) | — | — |
| Opt+L lock | ✓ | ✓ (draft kept in memory) | — (finish the confirmation first); from v0.4.1 the two meeting confirmations ✓ (the start one cancels and locks, the end one keeps recording and locks: docs/20 D3) | ✓ (recording does not stop) | — | Unlock step one |

### 5.4 Key dispatch order (updates 05 §5.6)

Today `Runtime::key_event` goes through `power_key` first, then `Shell::on_key`; with the screen off, the first key only wakes the screen and is swallowed, and with an overlay even a long G0 press is swallowed [src runtime.cpp:155-162; power.h]. The new order:

| Step | Where | Does |
|---|---|---|
| 1 | **Runtime, before `power_key`** | **Lock gate**: while locked, only the unlock key, Enter within the 3-second window and G0 get through (rules in §7.1); all other keys are swallowed and do not light the screen |
| 2 | Runtime | `power_key`: unchanged |
| 3 | Shell | **Menu**: while the menu is open it eats every key, except G0 (§6.1); from v0.4.1, the key combos written in the menu are also exceptions, executed after closing the menu (docs/20 D9) |
| 4 | Shell | **System keys**: Tab, Opt+L, handled per the table in §5.3 |
| 5 | Shell | Recording overlays (memo HUD, meeting confirmation, meeting page, end confirmation): unchanged |
| 6 | Shell | Compose page, confirmations: unchanged |
| 7 | Shell | Global keys: unchanged |
| 8 | Shell | The foreground app; a back key the app does not handle goes up one level |
| 9 | Shell | **Key replay**: when an app or the menu asks "press a key for me" (§10.1), it goes after this step and runs again from step 3. Only one level: a replayed key cannot replay again |

Steps 5–8 are today's order [src shell.cpp:89-98]; steps 1, 3, 4, 9 are new.

### 5.5 Back and the navigation stack

- The navigation stack is still at most 4 levels; the rules do not change [src nav.h].
- **Root page**: the home page if the home module is registered, otherwise the project board, i.e. v0.3's behavior [src shell.cpp:43-44]. The Rig used by existing tests does not register home, so the v0.1–v0.3 navigation tests still pass.
- Back on an app's first level returns to the root page; Opt+H clears the stack back to the root page (what `Action::Home` does today).
- The draft's "single-instance navigation stack" is not built: none of your four points need it.

### 5.6 Status bar, hint bar, home

- **Status bar**: unchanged (link, ↑N, !N, ●timer, recN, time, battery) [src status_bar.cpp].
- **Hint bar**: each page writes at most the 3 most common keys, **with "Tab menu" always at the far right**; that is the reminder that is always there. Each app's hints must leave about 42 px [calc]. v0.5 (docs/19 §6.3) is an exception: drawn as keycaps, the home page's hints have no room for "Tab menu", so on the home page this reminder is the 9th tile `Tab menu`.

Home (v0.4.0):
```
● Online ↑0 !2             13:42 83%
Home · Tue 10/21
▸P Projects 3  I Waiting !2  R Meeting
 N Write one   Q Queue AI    S Session
 M System      L Lock        Tab Menu
!  cpa/architecture waiting 9 min
Hold G0 to speak · Opt+letter jumps
Letter jumps ;.,/move Enter open Tab menu
```

Home (v0.4.1, with V and T added):
```
▸P Projects 3  I Waiting !2  V Recs rec2
 N Write one   T To-do       R Meeting
 Q Queue AI    S Session     M System
 L Lock                      Tab Menu
```

- Press 1–9 on home to open the Nth project page.
- The numbers on the tiles all come from AppState [src app_state.h]: the project tile "running N" (跑N), the waiting tile "!N", the recordings tile "recN" (录N, segments on the device waiting to transfer).
- The draft's "show the new keys on the first 3 boots after upgrading" is not built: the fixed "Tab menu" in the hint bar is enough, and it saves a counter that would need storing.

## 6. Always-available menu (Tab)

### 6.1 How to use it

- Press Tab to open the menu; Tab or `` ` `` closes it. `,` `/` switch pages; it opens on "This page" by default.
- Pick a row with `;` `.` and press Enter: **the menu closes, then the key written on that row is pressed for you**. So you can finish an action even when you forget the key, and the effect is always the same as pressing it directly. Each row shows the key on the left, so next time you can press it directly.
- A row that names a group of keys (such as `1-9`) is only a description; Enter does nothing, and the row ends with "press 1-9". v0.5 (docs/19 §6.5): the key column is drawn as keycaps, and since the keycap already says `1-9`, the row end no longer repeats it; description-only rows use a different keycap colour.
- On the "Go to" and "Control" pages, pressing a letter directly equals Opt+that letter.
- When the menu is opened on an overlay (compose page, confirmation, meeting page), it only has the "This page" and "Control" pages; you cannot jump elsewhere from the menu, and drafts and the recording in progress are not affected.
- **G0 in the menu**: during a meeting, G0 closes the menu and adds ★; otherwise G0 first closes the menu, then starts writing one or recording a memo as usual.
- **While recording, the menu keeps the status bar line**, so ●timer stays visible (§7.2).

### 6.2 Where the menu content comes from

- Each action (`Action`) has a one-line Chinese description, collected in one table (`action_help`).
- Each row carries the key it replays (`HelpRow{ev, keys, label}`, §10.1).
- **"This page"**: overlay rows come straight from that layer's bindings in `kKeymap` (`Binding.trigger` is the key to replay); an app's own rows (e.g. "Space = add ★" in the recordings list) come from the app's `help()`; a final row of system keys.
- **"Go to"**: from the global layer's bindings, with consecutive numbers merged into one row (e.g. Opt+1-9).
- **"Control"**: lock screen, sound (read-only, "off"), battery, SD card, recordings waiting to transfer, ↑N, link state, cpad version.
- Tests ensure four things: every action has a description; each binding in the global layer appears exactly once on the "Go to" or "Control" page; every row fits in 240 px; **on "This page" of the recordings list, the compose page and the meeting page, pressing Enter on each row has exactly the same effect as pressing that key directly**.

### 6.3 Sketches

Menu · This page (opened on the meeting page, recording):
```
●REC 42:10 ★2               13:42 83%
Menu · Meeting      [This page] Control
▸Space    Add ★
 G0       Add ★
 1-9  0   File to project / none (1-9)
 Opt+R    End meeting…
 Opt+L    Lock (keeps recording)
;.pick Enter do ,/page Tab close
```

Menu · Go to:
```
Menu · Go to     This page [Go to] Control
▸Opt+H   Home
 Opt+P   Projects (3 running)
 Opt+I   Waiting on you (!2)
 Opt+1-9 Nth project page (Opt+1-9)
 Opt+M   System
 G0      Write one → today's note
 Hold G0 Speak (voice memo)
;.pick Enter do ,/page letter Tab close
```
(Scrolling down there are also Opt+N/Q/S, Opt+R, Opt+L.)

Menu · Control:
```
Menu · Control   This page  Go to [Control]
▸Opt+L  Lock (unlock: Opt+L then Enter)
 Sound   Off (recording is silent)
 Battery 83% · 4.02V · not charging
 SD      412G free · waiting rec2 · ↑0
 Mac     Online · cpad 0.4.0
;.pick Enter do ,/page letter Tab close
```

### 6.4 Why Tab

| Option | Pros | Problems | Verdict |
|---|---|---|---|
| **Tab** | Single key; today Tab produces no event [src test_chord_detector/test_main.cpp:74]; the device can only input visible ASCII, so Tab does not clash with input | The input layer needs a new `Nav::Menu` event, and the test above changes one line | **Adopted** |
| Opt+/ | On the same key as "?" | A two-key combo, one more key than Tab | Not used |
| Hold Opt for 0.4 s to pop up (which-key style) | Does not bother experienced users | Needs inferring "only a modifier is held" from the key set, a new kind of input event [src chord_detector.h rule 1], and may clash with fast combos | Can be added later |

Pressing a letter after the menu opens is **choosing among visible options**, not a hidden key prefix, so it is not the "Leader key" that 05 §5.7 talks about.

## 7. System control: lock screen, ● always visible, sound, meeting confirmation, microphone

### 7.1 Lock screen (Opt+L)

After Opt+L the screen goes off immediately and the keyboard is locked. Drafts on the compose page stay in memory; if a meeting is in progress, recording does not stop. **The screen goes off even when plugged into the Mac**: locking takes priority over the "always on while powered" rule [src power.h].

| Pressed while locked | No meeting | In a meeting |
|---|---|---|
| Opt+L | Show the lock card and open a 3-second window; Enter within the window unlocks and returns to the page before locking | Same |
| G0 short press | Show the lock card for 3 seconds | Add ★ |
| Hold G0, screen black | **Only show the lock card, no recording** (no recording for this whole hold until release) | Add ★ |
| Hold G0, lock card showing | Record a voice memo, then back to the lock screen (§18 item 3) | Add ★ |
| Any other key | Does nothing and does not light the screen | Same |

- **Why "tap G0 first, then hold"**: with no prompt tone, pressing G0 in a pocket and recording a stretch would go completely unnoticed, and it might record other people talking. With two steps, a long press in a pocket only lights the screen; when you mean to speak, it is just one extra tap.
- **Why unlocking takes two steps**: pressing Opt and L together in a pocket and then Enter within 3 seconds is very unlikely; and it needs no new "hold a combo" input event, since the input layer only produces events on key down [src chord_detector.h].
- **Where it is implemented**: the lock gate is in `Runtime::key_event`, before `power_key`; `PowerState` gets `locked`. For a swallowed long G0 press, its release is swallowed too.
- **The lock card only shows counts**: time, date, battery, "waiting 2 · to send rec2 · ↑0"; no project names, session names or any body text.
- **The lock screen is not a security lock**; it only prevents accidental presses and keeps others from seeing content. It has no PIN, and data on the SD card is plaintext anyway [src 04 S11]. The README will say so clearly.
- "Lock automatically after the screen has been off for a few minutes" can be added later, but needs a stored setting; not in v0.4.

```
                 Locked
                 13:42
         Tue 10/21 · bat 83%
     Waiting 2 · to send rec2 · ↑0

   Unlock: Opt+L, then Enter within 3 s
   Speak: tap G0, then hold G0
```

### 7.2 ● always visible while recording

**Rule**: whenever RecPhase is Cue, Recording, EndConfirm or Finishing, the backlight is at least "dim", and `● REC` and the timer are drawn. Whether locked, with the menu open or a toast showing, and regardless of the prompt tone setting (there are no prompt tones at all now).

- This continues v0.3's rule: "the screen does not turn off while recording, `● REC` is always visible" [src docs/11 R11, B4]. With prompt tones removed in v0.3.1, the screen is the only signal, so this rule matters even more.
- **Meeting while locked**: the backlight goes to its lowest, and only a red "●42:10 ★2" shows in the top left; nothing else.
- **Memo while locked**: holding G0 while the lock card is showing; during recording the simplest ● HUD shows; after release and saving it goes back to the black locked screen.
- **Tests**: every combination of {memo, meeting} × {locked, menu open, toast} must draw ●, with the backlight no lower than "dim".

### 7.3 Sound

- From v0.3.1 the device makes no sound at all (recording start, save, cancel and failure are all silent) [src CHANGELOG.md working tree]. v0.4 adds no sound, so no mute key, volume levels or settings file either.
- The menu's "Control" page has a read-only row "Sound: off (recording is silent)". When you look for "mute", you can see it is already off.
- #17 ("holding G0 makes no sound; wants a prompt tone, with a mute switch") was raised on 10-20 and is superseded by today's decision. Suggest the Lead closes #17 with the reason noted.
- If you later want **a memo "beep" only you can hear**, add a setting "memo prompt tone: off / soft" then, with a mute key other than Opt+M. **Meetings never make a sound under any setting** (the alternative in §18 item 1).

### 7.4 Meeting confirmation and consent

- **Unchanged**: Opt+R shows the confirmation first; key presses within 400 ms are ignored; `consent:"ack"` is written to `.M` and `manifest.json` [src docs/11 G6]; memos only record yourself and show no confirmation.
- **Text change**: v0.3.1's confirmation only says "please tell the people present first" [src rec_overlay.cpp:71-73]. With no prompt tone, others can only know if you tell them, so the text must say the responsibility is yours. At most 20 Chinese characters per line:

```
开始会议录音？（不出声）
加州等地录私下谈话要所有人同意。
请亲自告知每个人并取得同意，
这由你负责。
屏幕会一直显示 ● 和计时。
Enter 我已告知，开始     ` 取消
```

In English: "Start meeting recording? (silent) / In California and elsewhere, recording private conversations needs everyone's consent. / Tell each person yourself and get consent; / this is your responsibility. / The screen will keep showing ● and the timer. / Enter: I have told them, start · `: cancel".

- The text does not change while the confirmation is open, so there is no "text just changed and Enter was pressed" problem. The draft's design of switching levels with Opt+M and changing the text on the spot is removed.
- The README says: "Tell people and get consent before recording them; this is not legal advice."
- Recording is silent, for personal use. If you only record yourself, use a memo (hold G0); meeting recording records others by nature, so the confirmation stays as it is.
- The GPIO21 LED has not been verified [src 11 §2]; nothing relies on it.

### 7.5 Mic gain (shipped on its own before v0.4)

- **Today**: the 7.4-second memo and 57-second meeting you recorded both peak at only about 6.5% FS [meas]. Gain is still M5Unified's default: magnification 16, PGA 0 dB [src audio_io.h:25].
- **The hard part**: in a meeting people are 1–2 m from the microphone, and speech is 10–16 dB lower than at 30 cm [inf]. On the other hand, with PGA at +18 dB, a quiet room's noise floor already reaches 0.0030 RMS, right at WhisperWoof's 0.003 chunk threshold [meas m3-device-gates.md lines 34, 206]. Tuning one gain for 30 cm only would pass, but meetings might still be too quiet.
- **Done criteria** (both must hold):
  1. Normal speech at 30 cm peaks at 20–50% FS;
  2. Speech at 1.5 m: the RMS of a 100 ms window is at least 3 times the chosen `CARDPUTER_SILENCE_RMS` [src m3-device-gates lines 35, 223]; and the real WhisperWoof transcribes a reference meeting recording played from 1.5 m away (through the Mac speakers) without dropping any sentence as silence.
- When one gain cannot serve both, **memos and meetings may use different gains**: carried by `StartSpec`, higher for meetings.
- Needs you 👤: record a memo at normal volume to confirm, then help move the device 1.5 m away for one playback, about 5 minutes. The gain sweep is run automatically by the Lead: the Mac speakers play reference audio, one segment is recorded per gain step, pulled and analyzed. No test recordings of other people (recording is silent, for personal use).
- **How it ships**: this item depends on nothing in v0.4. Once v0.3.1's silencing change is committed, it goes into v0.3.1 if in time, otherwise v0.3.2 (work package V3-G).

## 8. Recordings app (Opt+V, v0.4.1)

### 8.1 Where history lives, and how it is sorted

- **History lives on the Mac**. Once the Mac has verified a received segment, the device deletes it [src 11 §3.3]. The daemon's `sessions.rows()` can already list every session, including ones still on the device [src sessions.py:68]; the result event has the title [src results.py:70].
- **Asked from the Mac page by page**: the device sends `rec.list`, the Mac replies `rec.items`, 6 per page. The device keeps only one page in memory, and the list does not go into AppState.
- **Sorting**: by the wall-clock time the recording started, newest first; without wall-clock time, by archive time; ties broken by session ID. **Never sort by session ID alone**: MonoIds only follow creation order within one epoch, and after NVS is wiped a new random epoch is chosen [src mono_id.h:2-4; launcher.md R4]. Then today's meeting could land on the last page of history. `cpa rec ls` also changes to sort by time.
- **The paging cursor is generated by cpad**; the device does not parse it and sends it back as is (`cursor`, at most 32 B). If the sort order changes later, the protocol does not have to.

### 8.2 What it can do

- **Tabs**: all / ★ / memo / meeting, switched with `,` `/`; reaching the last row loads the next page automatically.
- **Each row shows**: ★ (if starred), date and time, title (without a title, the start of the memo, or "meeting 42m"), duration, state.

  | Symbol | Meaning |
  |---|---|
  | `↑` | Still on the device |
  | `…` | Transferring or processing |
  | `✓3` | Cleaned up, with 3 to-dos |
  | `✓` | Cleaned up |
  | `✗` | Not transcribed |

- **Keys**:
  - Enter: see the detail; Enter on the detail page does nothing;
  - **Space: add / remove ★** (on the meeting page Space already adds ★ [src keymap.cpp:93]);
  - 1–9 / 0: file into a project / no project; this is v0.4.1's "tagging" (§18 item 5);
  - Opt+D: hide from the list, with confirmation (§8.4).
- **"Open on the Mac" is not in v0.4.1**. In the draft, Enter on the detail page ran `open summary.md` on the Mac. During meetings the Mac's screen is often shared or projected, and two Enters would put the minutes in front of everyone. It comes in v0.5, on a different key and with confirmation first. In v0.4.1, find recordings on the Mac with `cpa rec ls`.
- **Offline**: the page shows "Connect to the Mac to see past recordings", and the number of segments on the device waiting to transfer.
- **cpad too old**: if the Mac's hello has no `caps.recs`, the page shows "cpad on the Mac needs an upgrade" and sends no request.
- **Changes to ★ and project are only recorded in cpad's event log** (`rec.edited`), **`manifest.json` is not changed** (§8.4). `cpa rec ls` shows ★, project and whether hidden, and still shows no title or body [src sessions.py:4].

### 8.3 Sketches

List:
```
● Online ↑0 !2 rec1         13:42 83%
Recs [All] ★ Memo Meeting   to send 1
▸★ 10/21 14:02 Client sync… 42m ✓3
   10/21 14:05 Buy milk…     0:12 ✓
   10/21 11:47 Meeting #cpa  18m …
   10/20 21:30 Weekend plan… 1:04 ✗
   10/20 09:12 Weekly        55m ✓
Enter detail Space★ Opt+D hide Tab menu
```

Detail:
```
★ Client sync: pricing & delivery #cpa
Meeting · 42:10 · 10/21 14:02
Cleaned up · ★ marks 2 · to-dos 3
Transcript, summary and audio are on
the Mac; find the folder: cpa rec ls.
Space★ Opt+D hide `back     Tab menu
```

Offline:
```
Recs [All] ★ Memo Meeting   ○ Offline
Connect to the Mac to see past recs.
On device to send: rec2, sent on plug-in.
`back                         Tab menu
```

### 8.4 v0.4.1 "delete" means "hide from the list"

**Why not really delete, or move to the trash**: recording contract v1 says `manifest.json` is written once and never changed afterwards; neither side changes or deletes the other's files; WhisperWoof processes folders that "have a valid manifest but no result.json yet" [src protocol/recording/v1/README.md single-writer table and hand-off rule 1; docs/11 §6.4, §7.2]. The WhisperWoof PRs #149–#152 you are about to merge are written to these rules. The draft planned to rewrite the manifest when adding ★ and to move the whole folder away on delete; both violate the contract: adding ★ could make WhisperWoof reprocess the folder or collide with its reads; a moved folder still contains files WhisperWoof wrote.

**What v0.4.1 does**:

| Recording state | Opt+D and confirm | How to get it back |
|---|---|---|
| Any state | Records a `rec.hidden` event. The device list and `cpa rec ls` no longer show it; not a single file on the Mac is touched | Visible with `cpa rec ls --hidden`; restore with `cpa rec unhide <session>` |

```
Hide from the list?
"Client sync: pricing & delivery"
Files on the Mac are not touched;
bring it back with cpa rec unhide.
Enter hide                    ` cancel
```

- Key presses within 400 ms of the confirmation opening are ignored, like the existing confirmations [src 04 §8.1].
- **Be clear**: hiding is **not** deleting. Audio, transcript, minutes, the line in the daily note, and the title in `events.jsonl` all stay on the Mac. SECURITY.md will say so.
- **Real delete in v0.5**: first add a rule to the contract, "a session folder may disappear at any time; when WhisperWoof hits ENOENT it treats it as a cancel and never recreates the folder", with test vectors. Once WhisperWoof supports this rule, v0.5's delete moves the folder into `~/CardputerNotes/.trash/` and clears it after 30 days. The easiest way is to add the rule before merging #149–#152 (§18 item 4).

### 8.5 Left for v0.5

- Playback on the device: first pass gate G-P1 (10 minutes of streaming playback with 0 underruns; first sound within 150 ms; free heap at least 140 KB while playing). Playback reuses the 24 KB ring buffer used for recording. From v0.3.1 the speaker is not used at all, and playback is its first use.
- Real delete (§8.4); open on the Mac (different key, confirmation first); keep transferred recordings on the device (`files.keep`); offline browsing; deleting recordings not yet transferred (double confirmation); free tags (picked from a tag list configured on the Mac); picking a title from AI candidates; `cpa rec retry` to reprocess a recording.

## 9. Capture destinations: while connected to the Mac, different ways go to different places

### 9.1 The 6 paths that exist today

| How | Where it goes | Offline |
|---|---|---|
| G0 short press, type, Enter | A line with `#project` added to the day's daily note `~/CardputerNotes/YYYY-MM-DD.md` | Yes, synced after plugging in [src 04 US-01, US-12] |
| Opt+Q, type | This project's queue (on the device, the "Queue" tab of the project page; on the Mac, `cpa queue ls`) | Yes |
| Opt+S, type, confirm | Starts a Claude Code background session on the Mac (plan mode) | Cannot start directly; becomes a queued item automatically [src 04 US-09] |
| Select a session in "Waiting on you", Enter, type, confirm | Replies to that session; saved as a queued item when conditions are not met | Saved as a queued item |
| Hold G0 and speak | A recording folder `recordings/<date>/…`, plus a line in the daily note; what WhisperWoof judges to be a to-do is written as a checkbox | Recorded on the card first, transferred automatically after plugging in [src 11 §6.7] |
| Opt+R meeting | `summary.md` (with to-dos) in the session folder, plus a line in the daily note | Same |

### 9.2 Model: each capture = content + destination

- **The key you press decides where it goes**. This is the same idea as WhisperWoof's "the hotkey is the destination"; G0 is like the device's Fn key [src WhisperWoof README:104, CLAUDE.md:21].
- Destinations fall into five classes, and the rules are per class:

| Class | Examples | Captured offline, after plugging into the Mac | While connected to the Mac |
|---|---|---|---|
| Archive | Today's note, to-do checkbox, project queue, Obsidian (v0.5) | Delivered automatically | Delivered on the spot |
| UI | Mac clipboard (v0.5), paste at the cursor (v0.7) | **Never done later**; only filed, goes into "pending" | Only executed at the moment you press the key |
| Exec | Start a session, reply to a session | Becomes a queued item per v0.1's rules | Sends an online request, then shows a confirmation (unchanged) |
| Outbound | Todoist, TickTick, Notion (v0.7) | Delivered automatically only if you enabled it on the Mac; every delivery audited | Same |
| Ask | Ask (v0.7) | Answered after connecting to the Mac; the answer goes into "waiting on you" | Same |

- **An unknown destination always falls back to today's note**, never to paste or exec. WhisperWoof falls back to "paste at the cursor" for an unknown hotkey [src WhisperWoof dictation-route.ts:14]; device items may sync hours later, and falling back that way would type the text into whatever program happens to be in front at sync time.
- **The default destination can only be in the "archive" class and must not leave the Mac**. "UI" and "outbound" classes must be explicitly chosen by your key press.
- **The destination is never decided by the content**. If someone in a meeting says "send it to Slack", nothing happens.

### 9.3 v0.4.1: to-dos

| Letter | Destination | Typing (Opt+letter, or change on the compose page) | Speaking (hold G0 then the letter) | Offline |
|---|---|---|---|---|
| N | Today's note | ✓ (default) | ✓ (default) | Yes |
| T | To-do: a `- [ ]` checkbox in today's note, which can be ticked directly in Obsidian | ✓ | ✓ (your key press wins, whatever WhisperWoof classifies) | Yes |
| Q | Project queue | ✓ (unchanged) | v0.5 | Yes |
| S | Start a session | ✓ (unchanged) | v0.5 (becomes a queued item first) | Becomes a queued item |

- **The destination for speech goes the same way as "press a number while holding to file into a project"**: recorded in segment 0's `.M` and handed to cpad via `file.items`. When cpad gets this speech's transcription result, it writes the checkbox according to `dest`. Neither the recording contract nor `manifest.json` changes.
- **The destination is always visible**: the title bar of the compose page and the memo screen shows "→ destination · #project" before saving. The toast after saving says where it went, e.g. "Saved: to-do #cpa".
- **cpad too old**: if the latest hello has no `caps.todo`, the toast after saving says "Saved; cpad on the Mac is old and will save it as a plain note". An old cpad ignores the `dest` field and saves it as a plain note [src file_messages.py:115, unknown fields are always ignored], so the text is not lost.
- **The clipboard moves to v0.5**. The device can only type ASCII [src 05 §6.7], and you dictate in Chinese and find typing tiring, so typing into the clipboard is the least useful for you. v0.5's approach: while holding G0 press C; after speaking the item goes into "pending", and once transcribed it is copied only when you press Enter on the device.

Compose page (destination is to-do):
```
Write → To-do · #cpa         ↑3 ○ Offline
call the plumber about the leak_

                              31/500
Enter save Opt+T to-do Opt+0-9 Tab menu
```

Holding G0 to record a memo (destination is to-do):
```
● REC 0:07 → To-do · #cpa
(level meter)
While holding: T to-do  N note  1-9 proj
Release G0 to finish
```

### 9.4 v0.5: configure "which key goes where" on the Mac (#24)

- First configured with the `cpa dest` command and `$CPA_HOME/destinations.json`, later in WhisperWoof's settings UI. Synced to the device when plugged into the Mac (`dests.snapshot`); the device caches it so it works offline too. The menu's "Go to" page shows the current bindings.
- **Clipboard** (voice, see above). Text copied out is also written into today's note and the event log, and goes into clipboard history tools. So the help text and the compose page title bar both warn "copied content is also archived"; do not use it to copy passwords or tokens.
- **An inbox file in Obsidian or any folder** (`file.append`), off by default; you specify which file it writes to (§18 item 7).
- **Opt+G send to**: re-deliver a note or recording somewhere else after the fact.
- "Waiting on you" gets "pending destinations"; `cpa audit` lists every delivery, recording only length and hash, never the body.

### 9.5 v0.7: paste at the cursor, WhisperWoof, cloud to-dos

- **Paste at the Mac's cursor, done by WhisperWoof**: it already has the Accessibility permission, and restores the previous clipboard content after pasting [src WhisperWoof clipboard.js:580-756]. Before pasting, confirm with Enter on the device; the confirmation shows the name of the foreground app; **at the moment of pasting WhisperWoof checks the foreground app again and gives up if it changed**; it never presses Return automatically after pasting.
- **WhisperWoof notes and projects**: WhisperWoof adds a "Cardputer text inbox".
- **Todoist / TickTick / Notion**: sent through WhisperWoof's MCP plugins, each enabled separately by you on the Mac, keeping its own "allow" dialog [src whisperwoof.md §5]. cpad never stores third-party API keys.

> **2026-10-21 revision (#34; see [docs/15](15-local-data-access.md), [ADR 0004](adr/0004-local-data-access-mcp.md))**:
> - **No WhisperWoof-specific integration.** In the three items above, "done by WhisperWoof", "WhisperWoof adds a text inbox" and "through WhisperWoof's MCP plugins" all become "any client can do it":
>   - cpad writes pending destinations and content as files and events (archive layout v1, docs/15 §2);
>   - other programs take them through `cpa mcp` (a local stdio MCP server, read-only by default, docs/15 §3) or by reading the files directly;
>   - WhisperWoof is just one of the clients.
> - **When these destinations are designed in v0.7, rewrite this section per docs/15.**
> - **The safety rules do not change and apply to any implementation**:
>   - confirm on the device before pasting;
>   - check the foreground app again at the moment of pasting;
>   - never press Return automatically;
>   - cpad does not store third-party API keys.

> **2026-10-22 revision (docs/25 R1-D, #61)**:
> - "Done by WhisperWoof" → **any executor**: destination = a generic kind + one preset per app (docs/25 §2); there is no app name anywhere in cpad.
> - **Prefilled links are opened by cpad** (ctypes calling `LSOpenFromURLSpec`, handed only to the app identified when it was added); the body never goes into any process's arguments; `app.<name>` only writes the clipboard and brings the app forward, never presses ⌘V, let alone Return.
> - **A click on the Mac counts as confirmation only for draft-only destinations** (docs/25 D14: `cpa filing send`, `cpa dest send`, `origin:"mac"`); destinations that might send on their own (`fl` contains `k`) still only accept Enter on the device after scrolling through the full text, and get `E_NEEDS_DEVICE` when sent from the Mac.

### 9.6 What is not done

- **The device is not turned into a USB keyboard**: HID and today's CDC serial share one USB PHY and would need TinyUSB, and flashing could then no longer auto-reset [src 02 line 41; 01-mac-setup:17, 219]; key codes are affected by the input method and cannot type Chinese; the device would become a key injector that can type into any foreground program. If you really want a keyboard, switch to the factory firmware's Keyboard app via Launcher (#12).
- cpad does not send key events itself (that would mean granting the Accessibility permission to a general-purpose Python interpreter).
- No automatic copy or paste on sync; no automatic session start; nothing sent out to a destination an LLM guessed from the content; "send a message for you" destinations (e.g. Slack) are not in the plan.

## 10. Firmware app framework

### 10.1 What v0.4.0 adds (additions only, all with default implementations)

```cpp
// module.h
struct HelpRow { KeyEvent ev; const char* keys; const char* label; };  // ev = 菜单按 Enter 时重放的键
class Module {
  // 原有：id / begin / onEnter / onKey / render / hint / contextSlot
  virtual uint8_t help(const HelpRow** rows) const { *rows = nullptr; return 0; }
};
class ModuleContext {
  // 原有：state / compose / confirm / request / open / toast / nowMs / sys
  virtual bool press(const KeyEvent& ev) { return false; }  // 替我按一个键：排到第 9 步重放
};
// chord_detector.h：Nav::Menu（Tab）
// keymap.h：Layer::System（Tab、Opt+L）、Layer::Lock；Action::Menu、Lock、Unlock、Projects
// app_state.h：struct UiFlags { bool menu; bool locked; };  放在 AppState.ui（2 B）
```

(Code comments: `ev` is the key replayed when Enter is pressed in the menu; "原有" = existing members; `press` = "press a key for me", replayed at step 9; `UiFlags` lives in `AppState.ui` (2 B).)

- **Home tiles and menu rows both execute by "replaying a key"**: pressing P on home is `ctx.press(Opt+P)`; Enter on a menu row replays that row's `ev`. There is no separate "execute an action" interface, so there can be no inconsistency like "the menu can do it but the key cannot". The draft's `ModuleContext::act(Action)` is dropped.
- Replay goes one level only: a replayed key cannot trigger another replay.
- The menu itself is not a module but a shell overlay: `MenuState` lives in Shell, and the content and drawing come from the pure-function library `lib/help`.

### 10.2 What v0.4.1 adds

```cpp
enum class RequestKind : uint8_t { QueueDone, InboxDismiss, RecList, RecAct };  // 新增后两个
struct ModuleResult { RequestKind kind; bool ok; ErrCode code; const RecPage* page; };
class Module { virtual void onResult(const ModuleResult& r) {} };   // 自己发出的请求的结果
// domain.h：Dest{Daily, Todo}、NoteBody.dest、RecFilter、RecOp、RecRow、RecPage、RecCursor
// SysInfo：host_caps{recs, todo}（来自 Mac 的 hello）
```

(Code comments: the last two `RequestKind`s are new; `onResult` delivers the result of a request the module itself sent; `host_caps` comes from the Mac's hello.)

- `agent.*` can still only be sent from `confirm()` [src module.h]. None of the new request kinds can execute an agent.
- **In-flight requests**: `rec.list`, `rec.act` and the existing `queue.done`, `inbox.dismiss` share the same "at most one in flight" channel, with a 10-second timeout [src runtime.h:50]. When `request()` returns false (channel busy), the app says "Mac is busy, try again later", handled separately from "request failed".
- **The result goes only to the app that sent the request**; if it is no longer in the foreground, the result is dropped. A timeout counts as a failure (`ok=false`).

### 10.3 Rules and budget

- **Registered at compile time, with a hand-written list** (self-registering objects in a static library get dropped by the linker, and the initialization order is undefined) [src 05 §5.5].
- **How an app is written**: no heap, no blocking; no Arduino, M5 or esp headers; state is a value type advanced by pure functions and tested in the native env.
- **Modules are cheap**: each of the three existing modules takes 2.6–4.3 KB of flash and only 8–12 B of RAM for the object [meas firmware.map].
- **Budget** [src 05 §5.13; v0.3.0]: a single app object at most 1 KB (a Recordings app page of 6 rows is about 700 B [calc]); AppState at most 4 KB (now 3,488 B [meas firmware research]); free heap at least 150 KB, largest contiguous block at least 64 KB; at least 4 KB of loop stack left.

### 10.4 Later versions

| Version | Content |
|---|---|
| v0.5 | Extract shared components: list, tabs, picker, generic confirmation. Add `AppManifest` (name, shortcut, capability bits, memory cap) and `test_app_registry`, checking shortcut clashes and that menu descriptions are complete |
| v0.6 | `check_apps.py` (forbidden includes, heap use, hint bar width, flash budget); `new_app.py` template; CONTRIBUTING describes "how to add an app" |
| From v0.7 | **Host-driven UI** (`ui.list` / `ui.pick`): Mac-side plugins send lists or choices, and the device draws them with shared components, so new features can grow without changing firmware. Actions that do things still need confirmation on the device |

**No dynamic loading**: there is no PSRAM, free heap is only about 190 KB [meas v0.3.0]; code on the SD card is not signed; to swap the feature set, Launcher can already switch the whole firmware [src launcher.md].

## 11. Mac side (cpad)

### 11.1 v0.4.1

- **hello**: the hello the Mac sends to the device gets two new items, both optional: `caps.recs`, `caps.todo`, telling the device what this cpad can do; `host_id`, `host_label`, reserved per #19; `host_id` is generated randomly on first run and stored in `$CPA_HOME` with mode 0600. The device only shows `host_label` on the "Control" page and makes no decisions with it before v0.8.
- **Notes**: a note with `dest:"todo"` is written as `- [ ] HH:MM #project body ^cpa-<id>`, the same format as the checkboxes from recordings [src recordings/route.py:72]. An unknown `dest` is written as a plain line, with a `dest.downgraded{item, dest}` event (no body). Without `dest`, the line written is byte-for-byte the same as v0.3.
- **Memo destinations**: when `route.py` gets a memo whose `dest` is todo, it always writes a `- [ ] ` line, whatever WhisperWoof classified.
- **Recording history**:
  - pure function `history.page(...)`: sorts, filters and pages per the rules in §8.1, and generates the cursor;
  - events `rec.edited{rec, starred?, project?}`, `rec.hidden{rec}`, `rec.unhidden{rec}`;
  - `Recordings.on_device_request(msg) -> reply`; `app.py` hands `rec.list`, `rec.act` to it;
  - CLI: `cpa rec ls` shows ★ and project, and with `--hidden` lists hidden ones too; new `cpa rec unhide <session>`.
- **Changing the project does not rewrite existing notes**: daily notes are append-only [src notes/writer.py], and the original line's `#project` stays as it was.

### 11.2 v0.5: the formal destination adapter interface

```python
DestClass = Literal["archive", "ui", "exec", "outbound", "ask"]

@dataclass(frozen=True, slots=True)
class DestSpec:
    id: str            # "daily" "todo" "clip" "file.sb-inbox" "ww.note" …
    label: str         # 设备上显示，≤24 B
    cls: DestClass
    key: str | None    # 绑定的字母
    leaves_mac: bool   # 云端，或者会被同步出这台 Mac 的文件夹

class Destination(Protocol):
    spec: DestSpec
    async def probe(self) -> DestStatus: ...                     # 不碰任何内容
    async def deliver(self, d: Delivery) -> DeliveryResult: ...  # 按幂等键 f"{dev}:{item}:{dest}" 幂等
```

(Code comments: `label` is shown on the device, ≤24 B; `key` is the bound letter; `leaves_mac` = cloud, or a folder that gets synced off this Mac; `probe` touches no content; `deliver` is idempotent on the key `f"{dev}:{item}:{dest}"`.)

- A pure function `decide()` decides deliver, hold, downgrade or turn into a queued item from four conditions: destination class, online or not, whether it is a key just pressed, and whether this destination is enabled. Tested with a truth table.
- `clipboard`: the body goes to `pbcopy` via stdin, with `LANG=en_US.UTF-8`, a 2-second timeout; the body never appears in command line arguments or logs. Whether it works inside a LaunchAgent must be measured [todo].
- `file.append`: the path must be inside the root you configured; symlinks refused; `~/Documents`, `~/Desktop` and iCloud Drive refused.
- Three ways for contributors to add a destination: configure a `file.append` (no code); write a Python adapter that passes the shared contract tests; from v0.7, write a WhisperWoof MCP plugin.

### 11.3 v0.7: WhisperWoof as a destination provider

cpad is still the only router for Cardputer captures: it owns the serial port, and `events.jsonl` is the authoritative data. WhisperWoof provides pasting, encrypted notes and projects, cloud plugins and those plugins' keys, and reports which destinations it can provide through cpad.sock v1. PRs to send WhisperWoof: a text inbox; plugin dispatch; turning off "recognize intent from speech and act" for content imported from Cardputer; a key binding page in the settings UI.

> **2026-10-21 revision (#34; see [docs/15](15-local-data-access.md) §4.6, [ADR 0004](adr/0004-local-data-access-mcp.md))**:
> - **The "WhisperWoof as a destination provider" plan is void.**
> - **No new `cpad.sock v1` for WhisperWoof**: `cpad.sock` is only for the `cpa` command line.
> - **There are only two standard external interfaces**:
>   - `cpa mcp serve`: stdio, read-only by default, write tools enabled one by one, audited;
>   - the archive files.
> - **WhisperWoof is like any other program**: a client of recording contract v1 and `cpa mcp`.
> - **What does not change**: cpad is still the only router for Cardputer captures and still owns the serial port; `events.jsonl` is still the authoritative data.
> - **The WhisperWoof PRs listed above**:
>   - "turn off 'recognize intent from speech and act' for content imported from Cardputer" is still to be done. It is a safety requirement, not an integration;
>   - the others are re-evaluated in v0.7 on the basis that "WhisperWoof is a client".

## 12. Protocol additions

**All additions only; `v` stays 0.** Old versions on both ends ignore unknown fields [src file_messages.py:115]; for an unknown request type, cpad replies `E_UNKNOWN_TYPE` [src codec.py:8].

### 12.1 v0.4.0

| Change | Direction | Content | Compatibility |
|---|---|---|---|
| `debug.state` gets optional field `ui{menu, locked}` | Device → Mac | While the menu is open, `overlay` still reports the layer below it | **No new `overlay` values**: an old cpad only knows none / compose / confirm [src specs.py:210], and a new value would make it reply `E_BAD_FIELD`. Only HIL uses this field |

### 12.2 v0.4.1

| Change | Direction | Content | Compatibility |
|---|---|---|---|
| host `hello` gets `caps.recs?`, `caps.todo?`, `host_id?`, `host_label?` | Mac → device | `host_id` format `^[a-z0-9-]{8,32}$`; `host_label` at most 24 B | Old firmware ignores |
| `note` in `outbox.item` gets `dest?` | Device → Mac | v0.4.1 only sends `"todo"`; no field = today's note | Old cpad ignores and saves a plain note |
| Each entry in `file.items` gets `dest?` | Device → Mac | Only memos carry it | Old cpad ignores |
| `rec.list` → `rec.items` | Device request, Mac reply | Request: `filter` (all / starred / memo / meeting), `max` (1–6), `cursor?`. Reply: `items` (at most 6), `next?` (a cursor when there is a next page) | Old cpad replies `E_UNKNOWN_TYPE`; the device checks `caps.recs` first and does not send without it |
| `rec.act` | Online request from the device | `rec`, `op` (star / unstar / tag / hide), `project?` (for tag; null means no project). The Mac replies `ack` or `error` | Same |

```jsonl
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0005","kind":"note","at":{"up_ms":98000,"wall_ms":1792440045000},"body":{"text":"call the plumber","project":"cpa","dest":"todo"}}
{"v":0,"t":"rec.list","id":"d23","filter":"all","max":6}
{"v":0,"t":"rec.items","id":"h24","re":"d23","next":"t1792440000000-a1","items":[{"rec":"3QZK1M7-00009-0001","mode":"meeting","wall_ms":1792440000000,"dur_ms":2530000,"project":"cpa","starred":true,"marks":2,"st":"done","title":"客户同步：定价和交付","todo_n":3}]}
{"v":0,"t":"rec.list","id":"d25","filter":"all","max":6,"cursor":"t1792440000000-a1"}
{"v":0,"t":"rec.act","id":"d27","rec":"3QZK1M7-00009-0001","op":"hide"}
{"v":0,"t":"ack","id":"h28","re":"d27"}
```

- `cursor` / `next`: opaque strings, `^[A-Za-z0-9_-]{1,32}$`, sent back by the device as is.
- `st` reuses the values of `sessions.state_of` (on_device, pulling, archived, processing, blocked, done, transcript, failed, stale) [src sessions.py:20].
- **No new error codes**: session not found → `E_UNKNOWN_ITEM`; bad field → `E_BAD_FIELD`; internal Mac error → `E_INTERNAL`. `hide` works on a session in any state, so `E_BUSY` is not needed.
- **Mixed versions**: with a new cpad and old firmware, the old firmware never sends the new messages, and cpad sends nothing new on its own (extra hello fields are ignored by old firmware). With an old cpad and new firmware, the device checks `caps` and says up front "cpad needs an upgrade"; everything else works as usual.
- **Line length**: the protocol caps each line at 2048 B. `rec.items` with 6 rows, each title 48 B of Chinese and every optional field filled, is about 1.5 KB [calc], guarded by `test_worst_lines`.
- **Recording contract v1 does not change**: no `user{}` block, no `cue`, `manifest.json` still written once. The `recs.snapshot` push planned in 11 §18 is no longer used.

## 13. Privacy and consent

1. **Meeting recording**: the confirmation always stays, and its text says the responsibility to tell people is yours (§7.4); `consent:"ack"` is kept on record; ● and the timer are always on screen, even when locked (§7.2); the README says "this is not legal advice".
2. **Memos while locked**: you must tap G0 first before holding it to record (§7.1). So pressing G0 in a pocket does not silently record other people's conversations.
3. **Lock screen**: the lock card only shows counts. Locking is not encryption; content on the SD card and in `~/CardputerNotes` is plaintext [src 11 §9].
4. **Recording titles**: shown on the device when unlocked; the lock card and home page show no body text. cpad's logs and `cpa rec ls` still do not show titles [src sessions.py:4].
5. **Hiding is not deleting** (§8.4); SECURITY.md says so.
6. **Content that leaves the Mac**: v0.4 adds no path off the Mac. From v0.5, folders synced with Obsidian Sync or git carry content off the Mac; from v0.7 there are cloud plugins too. All are enabled per destination and off by default; every delivery is audited, without the body.
7. **Several Macs**: before v0.8, offline items sync to the next Mac plugged in [inf]. I assume you use only one Mac for now [asm]; see §18 item 8.
8. **Injection defense**: the destination is decided only by the key; the body is passed to adapters only as data (stdin, a line appended to a file, a plugin's `{text}` parameter), never spliced into a command line or template.
9. **Logs**: keep using `RedactFilter`; error messages carry only error codes [src daemon logging/redact.py].

## 14. Where coding agents and context profiles fit

**Coding agents**: in the whole assistant they take two apps (Projects P, Waiting on you I) and two destinations (queue Q, start session S). The home tiles show "running N" and "!N", and the most urgent "waiting on you" item also shows on home. v0.6 goes deeper: #16 option-style replies; Claude Code hooks for a more accurate "waiting on you" state; meeting to-dos confirmed one by one before going into the project queue. Approval tiers and exposing the queue to agents as MCP go into the backlog first.

**Context profiles (#19)**:
- In v0.8, because they build on v0.5's "destination catalog". v0.5's destination catalog is already "each Mac pushes its own", so one Mac does not see another Mac's destinations.
- v0.4.1 does the "do it while we're there" step #19 asked for, reserving `host_id` / `host_label` in hello (§11.1); the device only displays them and makes no decisions.
- The real isolation is done in v0.8: each capture is tagged with a profile, and the device uploads items only to the Mac that serves that profile.
- Switching profiles takes no global key combo (the Opt+P in #19's sketch is already taken by Projects); it goes into the "Control" page with a confirmation; the status bar keeps one character for the current profile.

## 15. New roadmap

| Version | Goal | Main content | Done criteria (summary) |
|---|---|---|---|
| Finish v0.3.x | Complete v0.3 | Release v0.3.1 (silent); mic gain (V3-G); you merge WhisperWoof PRs #149–#152 (ideally after adding the ENOENT rule); run end-to-end with the real WhisperWoof; #22 rerun the long test; #21 add counters; #14 not coming online after plugging in; #17 closed with a note | Both the 30 cm and 1.5 m gain criteria met; one memo and one two-person meeting get transcripts and minutes in the real WhisperWoof, with no sentence dropped as silence |
| **v0.4.0 whole-device navigation (next version)** | Like a complete assistant | Home launcher; Tab menu; Opt+L lock; ● always visible while recording; new meeting confirmation text; `debug.state.ui` | HIL nav, menu, lock scenarios pass; v0.1–v0.3.1 regressions pass (with and without card); lock and power tests pass; you carry the locked device for a day with no accidental actions and no accidental recordings |
| v0.4.1 steady release (inserted 2026-10-22, docs/20 D10) | Easy and steady on a small screen and keyboard | Mando theme (docs/19); packages A and B of the docs/20 interaction review (D1–D10 at defaults); fixes from v0.4.0's long real-device test | docs/20 regression tests and scripts pass; real-device gates UX-H1, H2 pass; HIL regressions pass |
| v0.4.2 recording history and to-dos (formerly v0.4.1) | Find and manage recordings later; speech becomes to-dos directly | Recordings app (list, filter, detail, ★, file into project, hide); Opt+T and T/N while holding G0; caps and host_id in hello; `cpa rec ls`, `cpa rec unhide` | HIL history, dest scenarios pass; paging across two epochs stays in order; `manifest.json` byte-for-byte unchanged before and after ★ and hide; with an old cpad the device asks for an upgrade |
| v0.5.0 complete destinations and recordings 2.0 | Different ways go to different places; recordings can be heard away from the Mac | #24 bindings configured on the Mac; copy voice to the clipboard after transcription; Obsidian; Opt+G send to; pending list; audit; contract ENOENT rule and real delete; open on the Mac (with confirmation); playback on the device (G-P1); keep transferred recordings; offline browsing; free tags; shared components and AppManifest | Offline items are delivered per binding after plugging in, and UI-class destinations are never done later; G-P1 passes; WhisperWoof does not recreate a folder after delete |
| v0.6.0 select, don't type + coding agent | Decisions on the device by selection only | #16 option-style replies; meeting to-dos confirmed one by one; to-do app; hooks; check_apps and new_app | Replying to sessions, confirming to-dos and ticking off to-dos need no typing |
| v0.7.0 Mac app + thinking partner | WhisperWoof becomes the Cardputer's Mac app | cpad.sock v1; a Cardputer panel in WhisperWoof; paste at the cursor; WhisperWoof notes; cloud to-dos; voice replies; ask; host-driven UI | Device, recordings and to-dos visible in WhisperWoof, and key bindings can be changed there |
| v0.8.0 context profiles | Strict isolation between Macs | Host identity; uploads filtered by profile; each profile has its own projects and destinations | HIL with two fake hosts: items from one profile never reach the other Mac |

The full scope and done criteria of each version are in the structured roadmap.

> **2026-10-22 revision (docs/20 D10, you agreed to the defaults)**:
> - The interaction review (docs/20) fixes and the Mando theme (docs/19) ship together first as **v0.4.1 "steady release"**.
> - The original v0.4.1 "recording history and to-dos" moves to **v0.4.2**, with the same scope and done criteria: the device UI for the Recordings app (Opt+V) and to-dos (Opt+T, T/N while holding G0). Its protocol and Mac side already shipped with v0.4.0 (docs/releases/v0.4.0.md), so v0.4.2 only changes firmware.
> - Wherever else this doc says "v0.4.1" (the Recordings app, to-dos and protocol additions in §8–§12), it means these features; read it as v0.4.2.

> **2026-10-21 revision (#34; see [docs/15](15-local-data-access.md) §7.3, [ADR 0004](adr/0004-local-data-access-mcp.md))**:
> - **The v0.7 row becomes "standard Mac-side interfaces + thinking partner"**:
>   - `cpad.sock v1` and "a Cardputer panel in WhisperWoof" are no longer goals;
>   - the done criterion becomes: any MCP client can see the device, recordings and to-dos (Claude Code, Codex, Cursor, WhisperWoof too).
> - **docs/15's four phases land in these versions**:
>
>   | Phase | Content | Version |
>   |---|---|---|
>   | A | Read-only `cpa mcp` and `cpa export` | In the v0.5.0 milestone per #34. Daemon only, does not wait for firmware; can ship early with any daemon version once done |
>   | B | Write tools | After v0.4.1's to-do destination |
>   | C | "Send to device" | Stays in v0.7 |
>   | D | Filter by profile | Together with v0.8 |

## 16. What changed from the old roadmap

| Old plan | New plan | Why |
|---|---|---|
| v0.4 Coding-agent interaction (#16, hooks, approval tiers, richer inbox) | #16 and hooks move to v0.6; approval tiers and MCP exposure go to the backlog | You said coding agents are only one part; first complete the assistant's skeleton |
| v0.5 Mac app + thinking partner (#10) | Moves to v0.7 | Depends on the WhisperWoof PRs' progress; the device side goes first, and the two do not block each other |
| v0.6 context profiles (#19) | Moves to v0.8; v0.4.1 reserves `host_id` first | Builds on the destination catalog; the reservation is #19's own "do it while we're there" |
| (none) | New v0.4.0 whole-device navigation, v0.4.1 recording history and to-dos, v0.5 destinations and recordings 2.0 | From your four points of feedback today |
| 11 §18: v0.3.x does `recs.snapshot` and the recordings list | Merged into v0.4.1, changed to page-by-page requests | Page-by-page requests are not limited by history length |
| 11 §9, 12: meeting prompt tones cannot be turned off | v0.3.1 already made everything silent (your decision); v0.4.0 changes the confirmation text and adds the "● always visible" rule | Feedback 4 |
| Draft: three prompt tone levels, Opt+M mute, settings file, `cue` field | All removed | After v0.3.1 there is no sound to adjust |
| 04 §8.2: Opt+H is the project board | Opt+H home, Opt+P project board; Opt+M is still System | Home must hold every feature; Opt+M need not give way to mute, so the change shrinks to one key |
| Mic gain (part of #23, originally planned into v0.4) | Ships on its own in v0.3.x, with a meeting-distance criterion | The level you measured today is too low, lower still in meetings; it should not wait for v0.4 |
| 05 §5.7: "no Leader key" | Still none; the Tab menu is a visible choice | — |

Suggest the Lead on GitHub: rename the v0.4.0 milestone "whole-device navigation", and create v0.4.1 "recording history and to-dos"; move #16 to v0.6.0; #10 to v0.7.0; #19 to v0.8.0; put #24 in v0.5.0 (v0.4.1 does to-dos first); close #17 noting "superseded by the v0.3.1 decision".

## 17. Work package overview

The full scope and acceptance tests are in the structured output; only content and dependencies are listed here.

| ID | Version | Content | Depends on |
|---|---|---|---|
| V3-G | v0.3.x | Mic gain: the 30 cm and 1.5 m criteria, with separate gains for memos and meetings if needed | v0.3.1's silencing change committed first |
| V40-F0 | v0.4.0 | Firmware shared interfaces: headers only, merged first | None |
| V40-M | v0.4.0 | Menu library: action description table, key names, menu content, key replay (pure functions) | V40-F0 |
| V40-L | v0.4.0 | Home launcher module | V40-F0 |
| V40-P | v0.4.0 | Protocol: `debug.state.ui` | V40-F0 |
| V40-F | v0.4.0 | Firmware body: Tab, key table, dispatch order, key replay, menu wiring, lock and power, ● rule, confirmation text | V40-F0, V40-M, V40-P |
| V40-H | v0.4.0 | Integration and real-device acceptance | All the above |
| V41-F0 | v0.4.1 | Firmware shared interfaces: recording requests, result callback, destination types | V40-H (v0.4.0's headers frozen until release) |
| V41-P | v0.4.1 | Protocol: caps and host_id in hello, dest in note and file.items, `rec.*`. Schema and daemon codecs can be done in parallel with v0.4.0 | V41-F0 (only the firmware codec waits for it) |
| V41-R | v0.4.1 | Recordings app module | V41-F0 |
| V41-F | v0.4.1 | Firmware wiring: Opt+V, Opt+T, T/N while holding G0, routing of recording requests, V and T tiles on home | V41-F0, V41-P |
| V41-D1 | v0.4.1 | cpad: caps and host_id in hello, to-do notes, `rec.*` hand-off | V41-P |
| V41-D2 | v0.4.1 | cpad: recording history, ★, project, hide, destinations in `route.py`, CLI | V41-P |
| V41-H | v0.4.1 | Integration and real-device acceptance | All the above |
| V4-DOC | Both | Key table, dispatch order, README, SECURITY, CONTRIBUTING, release notes | None (release notes finalized after each H) |

```
V3-G（独立，v0.3.x）
V40-F0 ──► V40-M、V40-L、V40-P ──► V40-F ──► V40-H（发 v0.4.0）
V41-F0 ──► V41-P ──► V41-F、V41-D1、V41-D2
V41-F0 ──► V41-R
全部 ──► V41-H（发 v0.4.1）
V4-DOC 并行，最后定稿
```

(V3-G is independent, v0.3.x; V40-H ships v0.4.0; "全部" = all, and V41-H ships v0.4.1; V4-DOC runs in parallel and is finalized last.)

**Two conventions**:

- **Header freeze**: once a version's F0 is merged, the headers it changed are not changed again in that version. If one really must change, open a small header-only PR, merged by the Lead.
- **Changing v0.1–v0.3 tests is limited to two cases**: line 74 of `test_chord_detector` asserting "Tab produces no event" (now Tab produces `Nav::Menu`); tests asserting Opt+H opens the project board, or that Opt+P / Opt+L are unbound. Each change is listed in the commit message, and no other test changes by a single line. The existing Rig does not register home, and the root page is still the project board, so navigation tests are not affected (§5.5).

**Manual gates** (👤 schedule with you in advance): V3-G about 5 minutes (one memo, plus moving the device 1.5 m away once); V40-H carry the locked device for a day, then read through the confirmation text once (about 10 minutes); V41-H use the Recordings app once (about 10 minutes).

## 18. Your call

If you do not reply, the defaults apply.

1. **Sound**: v0.3.1 is already silent throughout. v0.4 adds no mute key or volume levels; the "Control" page says "Sound: off"; the meeting confirmation switches to the text in §7.4; #17 closed with a note. **Default: agree**. Alternative: later add a memo "beep" only you hear (setting "off / soft"), with meetings silent under any setting.
2. **Key layout**: v0.4.0 makes Opt+H home, moves the project board to Opt+P, adds the Tab menu and Opt+L lock; v0.4.1 adds Opt+V recordings and Opt+T to-do; Opt+M stays System. **Default: agree**.
3. **G0 while locked**: **Default**: tap G0 to light the screen first, then hold G0 within 3 seconds to record a memo; during a meeting G0 is always ★. Alternatives: holding G0 records directly; or G0 cannot record while locked.
4. **Deleting recordings**: **Default**: v0.4.1's delete is only "hide from the list"; files are not touched and can be restored; before merging WhisperWoof PRs #149–#152, first add the rule "a disappeared folder (ENOENT) counts as a cancel", and do real delete in v0.5 (to the trash, cleared after 30 days). Alternative: do not add the rule, and push real delete further out.
5. **Tags**: **Default**: v0.4.1 uses "file into a project" as the tag. If you want free tags, v0.5 makes them a pick from a tag list configured on the Mac, with no typing on the device.
6. **v0.4.1 destinations**: to-dos are written as checkboxes in today's note, by typing or speaking; the clipboard moves to v0.5, as "speak, wait for transcription, press Enter on the device to copy". **Default: do it this way**. Also please tell me which to-do app you actually use (Todoist / TickTick / neither), so v0.7 connects to that.
7. **Obsidian inbox (for v0.5)**: which file in which vault? Is it acceptable for this text to leave this Mac via sync? **Default: off for now**, until you specify.
8. **Several Macs**: will you plug this device into more than one Mac now? **Default: only one**; v0.4.1 reserves `host_id` in hello per #19, and v0.8 does the isolation. If you will use two, move "sync only to the paired Mac" up to v0.5.
9. **Roadmap order and manual gate timing**: **Default**: v0.3.x → v0.4.0 → v0.4.1 → v0.5 → v0.6 → v0.7 → v0.8; mic gain (about 20 minutes, plus one trial meeting recording) this week. Alternative: move #16 option-style replies up to v0.5.

## 19. Risks

| Risk | Response |
|---|---|
| With no prompt tone, accidental recording in a pocket goes unnoticed | While locked, "tap first, then hold"; memos under 1 second are dropped automatically [src 11 §4.2]; the backlight stays on while recording, and ● is always there |
| Opt+H changes meaning, breaking muscle memory | Only this one key changes; the Tab menu can be checked any time; you are the only user |
| The lock gate and power rules interact (screen-off swallows keys, always on while powered) | The gate goes before `power_key`; power tests cover "locked + powered → screen off", "locked + 50 random keys → still off", "Opt+L from a black screen → lights once, opens the 3-second window"; HIL's random key test excludes Opt+L and Enter so the device does not unlock itself |
| Menu replay differs from pressing the key directly | The menu only replays keys, with no separate execution interface; tests compare effects row by row |
| History order breaks after an NVS wipe | Sort by wall-clock time, cursor generated by cpad; a test across two epochs |
| Hiding mistaken for deleting | The confirmation and SECURITY.md say "files are not touched"; v0.5 does real delete |
| Old cpad with new firmware | Check `caps` in hello: the Recordings app asks for an upgrade, to-dos say "will be saved as a plain note"; HIL has a dedicated flow |
| One shared in-flight channel: loading the Recordings page, then adding ★ right away, collides | A separate "Mac is busy" message when the channel is busy; loading a page is fast; a 10-second timeout counts as failure |
| With higher gain, the noise floor hits WhisperWoof's threshold | Verify both criteria together; separate gains for memos and meetings if needed; record `CARDPUTER_SILENCE_RMS` and hand it to you to change on the WhisperWoof side |
| Newly used glyphs (▸ ✓ ✗ ↑ …) are not in the efontCN_12 font [todo] | Device tests draw every new page and assert `missing_glyphs == 0`; missing ones are replaced with ASCII. Precedent: ⚠ was not in the font and was replaced with ※ [src status_bar.cpp:71] |
| v0.3.1's silencing change is not committed yet | v0.4.0 assumes it; if you change your mind, handle it per the alternative in §18 item 1; the rest of v0.4 is unaffected |
| Time waiting for you on manual gates | §17 lists the durations; please schedule ahead |

## 20. How review comments were handled

| Review comment | Handling |
|---|---|
| Blocker 1: rewriting the manifest and moving folders violates the recording contract | **Adopted**. The manifest is not changed; ★ and project are only cpad events; delete becomes "hide"; the contract does not change by a word in v0.4; the ENOENT rule goes to v0.5, with the suggestion to add it before merging the WhisperWoof PRs (§8.4) |
| Blocker 2: rows on the menu's "This page" cannot be executed | **Adopted**. `HelpRow` carries a `KeyEvent`, and the menu only does "key replay"; `act()` dropped; rows for a group of keys are descriptions only; row-by-row comparison tests (§6.2, §10.1) |
| Blocker 3: the lock gate after `power_key`, always on while powered | **Adopted**. The gate moves into Runtime, before `power_key`; `PowerState.locked`; locking takes priority over always-on while powered; power tests added; HIL random keys exclude the unlock keys (§5.4, §7.1) |
| Blocker 4: ● is not always visible; the debounce is not reset after the confirmation text changes | **Adopted**. New rule "● always visible while recording" with combination tests (§7.2). The confirmation text no longer changes while open (level switching removed), so the problem is gone |
| Blocker 5: sorting by MonoId breaks | **Adopted**. Sort by wall-clock time, opaque cursor, cross-epoch tests (§8.1) |
| Blocker 6: two Enters open the minutes on the Mac | **Adopted**. Removed from v0.4.1; v0.5 uses a different key with confirmation (§8.2) |
| Blocker 7: gain only considers 30 cm; deferred to v0.4 | **Adopted**. Added the 1.5 m criterion and a trial meeting with the real WhisperWoof; separate gains per mode allowed; ships alone in v0.3.x (§7.5) |
| 7 cut suggestions | **All adopted**: gain ships alone; v0.4 split into v0.4.0 (firmware) and v0.4.1 (protocol plus daemon); typed copy to the clipboard dropped (the voice version goes to v0.5); "open on the Mac" dropped; the manifest's `user{}` dropped; new-key hints on the first 3 boots dropped (so v0.4 no longer needs a settings file); no single-instance navigation stack |
| Risk: a new `debug.state.overlay` value would be rejected by an old cpad | Use a new optional field `ui{}` instead; `overlay` values unchanged (§12.1) |
| Risk: #19 asks to reserve host identity | Adopted; v0.4.1 reserves `host_id`; "several Macs" is also listed as decision 8 |
| Risk: `test_exit_keys` also presses Opt+M | Opt+M does not change meaning, so this test is unaffected |
| Risk: menu library tests depend on the new key table | V40-M's tests iterate `kKeymap` and do not name any binding that only V40-F adds |
| Risk: shared in-flight channel | A separate "busy" message (§10.2) |
| Other risks | Switching levels mid-meeting, two inconsistent descriptions of volume: levels removed, no longer exist. With an old cpad it still said "saved to to-dos": now checks `caps.todo` (§9.3). The foreground app changes before pasting in v0.7: check again at the moment of pasting (§9.5). "Tagging" built only as filing into a project: listed as decision 5. Two §4.4s: renumbered. Manual gate timing: listed in §17 and decision 9 |
| **New finding outside the review** | The `release/v0.3.1` working tree shows it has already been decided that recording is silent throughout. The draft's three prompt tone levels, Opt+M mute, settings file, `cue` field and two-level volume gate are all removed; Opt+M stays "System"; V3-G no longer touches the already deleted `rec_tones.*` |

## Appendix: evidence index

- **v0.3.1 working tree**: `CHANGELOG.md` (the [v0.3.1] entry); `docs/11-m3-design.md` line 8 (the decision); `firmware/app/lib/hal/audio_io.h` (no longer turns on the speaker); `firmware/app/lib/core/mic_gain.h` (`kMicGain` at line 30); `rec_tones.*` deleted.
- **Input**: `firmware/app/lib/input/keymap.cpp:43-100`; `chord_detector.{h,cpp}`; `test/core/test_chord_detector/test_main.cpp:74` (Tab produces no event).
- **Shell**: `lib/shell/runtime.cpp:155-162` (`key_event` goes through `power_key` first); `power.h` (screen-off swallows keys, always on while powered, dim while recording); `shell.cpp:43-44` (the root page is board), `shell.cpp:89-121` (dispatch order and global actions); `nav.h`; `rec_overlay.cpp:55-73` (memo prompts and meeting confirmation text); `runtime.h:50` (10-second in-flight request timeout); `test/ui/test_exit_keys/test_main.cpp:95-118`.
- **Module API**: `lib/module_api/module.h` (`RequestKind`, `SysInfo.host_app`).
- **Protocol**: `protocol/schema/v0/hello.schema.json` (host's `app`, `caps`); `fileItem` in `common.schema.json`; `daemon/src/cpad/protocol/specs.py:210` (overlay values); `file_messages.py:115` (unknown fields always ignored); `codec.py:8` (unknown types get `E_UNKNOWN_TYPE`); `firmware/app/lib/core/mono_id.h:2-4` (epoch).
- **Recording contract**: `protocol/recording/v1/README.md` (single-writer table; hand-off rule 1); `docs/11-m3-design.md` §6.4, §7.2.
- **daemon**: `recordings/sessions.py:4, 20, 68`; `results.py:70`; `route.py:72`; `cli/rec.py`; `notes/writer.py`; `device/link.py:88` (`host_hello`).
- **Gates**: `docs/gates/m3-device-gates.md` lines 34, 35, 206, 223 (noise floor at PGA +18 dB; `CARDPUTER_SILENCE_RMS`); `whisperwoof.md:16, 61, 318` (WhisperWoof's silence threshold).
- **WhisperWoof** (h3qing/WhisperWoof): README:104 ("Hotkey = destination"); `core/router/dictation-route.ts:14`; `helpers/clipboard.js:580-756`.
- **Issues**: #16, #17 (wants a prompt tone, with a mute switch), #19 ("Cheap now": reserve host identity at the next protocol change), #23, #24 (key = destination, bindings configured on the Mac).
- **Similar products** (from research, some only seen as search snippets): Flipper Zero (lock, Archive, delete confirmation); Pebble (Quick Launch, Quiet Time); which-key.nvim; Raycast; Plaud; Granola; Apple Voice Memos ("Recently Deleted" kept 30 days).
