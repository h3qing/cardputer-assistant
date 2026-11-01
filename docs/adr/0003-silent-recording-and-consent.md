**English** · [中文](0003-silent-recording-and-consent.zh-CN.md)

# 0003 · Recording is silent; the meeting confirm box and `consent:"ack"` stay; ● is always visible while recording

- Status: Accepted
- Date: 2026-10-21
- Related: [11 §2, §3.2, §4.2, §9](../11-m3-design.md), [13 §7.2–§7.4, §18 item 1](../13-assistant-v2-plan.md), [04 §8.6, §8.8](../04-prd.md), `lib/shell/rec_overlay.cpp`, `lib/shell/power.h`, `lib/shell/lock_screen.cpp`, issue #17

## Background

- **Old rule** (v0.3.0, the ruling table in 11 §2, §4.2, §9): recording cue sounds **could not be turned off**. A memo beeped at start ("嘀") and at save ("嘟"). A meeting played a ~360 ms three-tone cue at start and at end, clearly different from a memo. The reason was that people in the room could hear it (📚 granola-pipeline.md §6.6: in California, recording a private conversation needs the consent of all parties).
- **The user's decision** (2026-10-21): turn off the recording cue sounds; recording is silent, for personal use. From v0.3.1 recording is silent throughout (see the note at the top of 11 and the "From 2026-10-21" notes throughout it).
- With the sound gone, the screen is the only indicator on the device. Other people only know if the user tells them in person.

## Options

| Option | Pros | Cons | Evidence |
|---|---|---|---|
| A Keep the cue sounds, cannot be turned off (v0.3.0) | Others can hear it | The user explicitly does not want it; the device is for personal use | 11 §4.2 |
| B Silent throughout; keep and record the meeting confirm box; the screen shows ● the whole time while recording | Matches the user's decision; there is still one deliberate confirmation before a meeting, and it is recorded | Others cannot notice; telling them depends entirely on the user | Shipped in v0.3.1 |
| C Add a sound setting (memo beep off / soft; meetings never sound in any setting) | An option for later | Needs a stored setting and a mute key; the user did not ask for it | Alternative in 13 §7.3 and §18 item 1 |

## Decision rules

- Single-user device, for personal use: the user's decision takes priority.
- A meeting recording must have one key-press confirmation before it starts, and it must be written into the recording's metadata.
- While recording, the screen must always show that it is recording: the lock card, the Tab menu, pop-up hints and backlight dimming must never hide ● (`test_rec_visible` checks every combination).

## Decision

Choose **B**.

1. **Silent** (v0.3.1): nothing sounds for memo start and saved, meeting start and saved, too short or cancelled, refused or failed. v0.4 adds no mute key and no volume levels. The "Controls" page of the menu shows a read-only line "Sound off (recording is silent)" ("声音 关（录音不出声）").
2. **The meeting confirm box stays, with the v0.3.1 wording unchanged** (`render_meeting_confirm`):

   ```
   开始会议录音？
   请先告知在场的人
   Enter 开始    ` 取消
   ```

   In English: "Start meeting recording? / Please tell the people present first / Enter start    ` cancel".

   Any key press within 400 ms of the box opening is ignored. 13 §7.4 proposed a new wording that states the responsibility lies with the user. The user has not agreed to it yet, so v0.4.0 does not use it. Changing the wording later needs a new ADR that supersedes this one.
3. **Recording the confirmation**: for a meeting started by pressing Enter in the confirm box, the `.M` of segment 0 and the session's `manifest.json` get `consent:"ack"` (11 §4.4, §7.3). It only means "the user confirmed they told the people present". It does not mean anyone else consented. Memos have no confirm box and no such field.
4. **The ● rule** (v0.4.0, 13 §7.2): while RecPhase is Cue, Recording, EndConfirm or Finishing, the backlight dims no further than "dimmed", and ● and the timer are drawn, the same on the lock card, with the Tab menu open, and with a pop-up hint. A meeting on the lock card shows only one line top-left: "●42:10 ★2". A memo on the lock card shows "● 准备" (ready) before the microphone opens, then "● REC 0:12".

## Consequences

- Benefits: matches how the user uses the device; there is still one deliberate confirmation before a meeting, with `consent:"ack"` to check afterwards; the screen always tells the user (and anyone who can see the screen) that the device is recording.
- Costs and risks: people present hear no cue at all, and an accidental recording in a pocket goes unnoticed. Mitigations: on the lock card, recording needs "tap G0 once, then hold while the card is lit" (13 §7.1), and memos under 1 s are discarded automatically (11 §4.2). Telling people and getting consent is the user's responsibility. The README says so, and says it is not legal advice.
- Way back: if the user wants sound later, add "Memo cue: off / soft" as in the alternative in 13 §18 item 1, with meetings silent in every setting. If the device is to be used by someone else, or is to record other people, re-evaluate option A.
- Docs and code that must change with this: 11 (the in-place notes of 2026-10-21; §4.1 and §9 point here); "Recording and consent" in the README; 04 §8.6, §8.8; tests `test_rec_overlay` (confirm box wording and the 400 ms guard), `test_rec_visible` (the ● rule), `test_lock_power` (G0 on the lock card).
