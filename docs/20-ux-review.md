**English** · [中文](20-ux-review.zh-CN.md)

# 20 · Interaction review: convenience and stability on a small screen and a small keyboard

> Role: interaction review (summarised by the Lead). Date 2026-10-22. The subject is v0.4.0 (origin/main `4ae63d7`), checked against the Mando theme in progress (`wt-theme`, based on `740b7f0` plus uncommitted changes). Five angles: first use, carrying it in a pocket every day, recovering from wrong presses, stability, and information on screen. Every finding went through a "refute and verify" round: someone else re-ran it, looked for counter-examples, and re-graded it. Only the simulator, the code and fuzz tests were used; **no real device was attached**.
> Requirement: review the whole interaction design from the user's point of view; this is a device with a small screen and a small keyboard, so convenience and stability matter most.
> Tags as in docs/13: **[src]** code or docs read · **[meas]** measured in the simulator or by fuzzing · **[calc]** estimated · **[inf]** inferred · **[todo]** needs the real device · **👤** needs you.
> Evidence paths: `$UX` = `<scratch>/scratch-ux`, `$TH` = `<theme worktree>` (the theme worktree, read-only). These are temporary review files and not in the repo; when this review became docs/20, the regression scripts listed in §4.4 were to move into `firmware/sim/scripts/ux/`, and the rest of the evidence is only for the record.

## 0. One-page summary

**In one sentence**: v0.4.0 has a stable foundation; the problem is that "when you press the wrong thing, you don't know".

- **What is stable** [meas]:
  - Fuzzing ran about 8.7 hours of simulated time and 32,000 random presses, with 31 restarts, 156 cable pulls and 117 cpad restarts along the way.
  - Result: no crashes, no out-of-bounds access; with projects present, not one saved capture was lost; 345 recordings (about 94 MB) were pulled, all checksums correct.
  - From any state, at most 5 presses get back to Home (§4).
- **What is missing**: the device has no sound and no vibration; the screen is the only feedback. Yet quite a few places currently **show nothing at all**, so you cannot tell when you pressed the wrong thing, when nothing was recorded, or when something was recorded that should not have been.

**The problems fall into four groups**:

1. **A misfire can turn into "recording other people"** (recording is silent, for personal use):
   - The 400 ms guard on the meeting confirm box **silently** swallows an Enter pressed too fast. The box does not go away by itself and ignores Opt+L. After that, any stray Enter in the pocket starts a meeting recording (U1).
   - There is no auto-lock. If you forget Opt+L and pocket it, the black screen still takes notes, records memos, even starts meetings (U2).
   - The locked "tap, then hold" can be bypassed with two long presses (U3).
   - Typing on Home, hitting r and then Enter also starts a meeting (U5).
2. **Speaking, the main path, silently fails in several states**:
   - Holding G0 while the editor is open records nothing. All 297 attempts in fuzzing missed (U4).
   - A hold in the lock card's last 0.5 seconds is swallowed, and the card goes dark while you hold (U3).
   - After recording while locked, you cannot see "saved / too short / can't record" (U6).
   - About the first 0.65 seconds are not recorded (U8).
   - Reaching the 5-minute limit gives no warning, and what you say after that is lost (U15).
3. **The screen does not say what happened**:
   - The locked black screen does not react to ordinary keys at all, which looks like a crash (U11).
   - When a meeting stops automatically because of low battery, you cannot see why while locked. 17% is still shown in white, and recording stops at exactly 17% (U6).
   - The Inbox says "[Enter]reply", but most sessions cannot be reached at all. Only after typing does it tell you "replies come in v0.4", and you are already on v0.4.0 (U7).
4. **Picking the wrong item, losing what you just typed**:
   - After the list refreshes, the cursor stays on the same position, so Opt+D dismisses or completes a different item (U12).
   - Pressing `, labelled "Cancel", on a confirm box throws away what you just typed (U9).
   - In the editor, ` types a backtick instead of going back (U10).

**Presses (now → after the fix)**:

| Action | Now | After |
|---|---|---|
| Spot a typo on the confirm box and go back to fix it | Retype everything (a measured 72-character task takes 75 presses) | ` once |
| Leave the editor | Fn+` (hard to remember) | ` (D5) |
| Start and end a meeting | Opt+R, wait at least 0.4 s, then Enter; the screen does not show how long to wait | Still two steps; too fast shows "press again" |
| Type a note from the black screen (on battery) | G0, one press | Opt+L, Enter, G0, three presses (the cost of D2); speaking a memo is still "tap, then hold" |
| Queue for AI from Home | Q, text, Enter (refused), Opt+1, Enter | When refused, the hint spells out "1 acmeshop · 2 cardputer" |

**The fixes come in two packages** (§5):

- **Package A: logic and keys only, no drawing**. The theme did not touch these files (keymap, shell.cpp, compose, confirm, lock, power, rec_core, toast…), so it can ship first. Suggested as **v0.4.1 "steady"**.
- **Package B: things drawn on screen**, such as the result line on the lock card, the memo countdown, status bar width, per-row hints. These are done with the Mando theme to avoid changing them twice. A few theme changes make problems more visible, e.g. saying "All done" even when offline, the recording page dropping the battery level, and a napping Mando on the lock card looking like "all is well"; listed in §5.3.

**The 10 things for you to decide** (§3) all have defaults, used if you do not reply (2026-10-22: all defaults were accepted, see §5.6). The three biggest changes: lock as soon as the screen turns off on battery (D2), R and L on Home need Opt (D1), and ` in the editor becomes the back key (D5).

**The 10 to fix first**: U1 meeting box, U2 auto-lock, U3 locked memo gesture, U4 holding G0 under overlays, U5 single letters on Home, U6 recording results invisible while locked, U7 Inbox replies, U8 start of a memo, U9 confirm box loses text, U10 back key.

## 1. How the review was done and graded

- **Five angles, each writing its own findings**: first use; carrying it every day (lock, wake, memos, meetings, seeing who is waiting); errors and recovery; stability (long fuzzing, soak tests with a real cpad); screen information design. Every finding needs evidence: a simulator script plus screenshots, measured key presses, lines of code, or a fuzz log.
- **One more verification round**: someone else re-ran the script, looked for counter-examples, checked whether v0.4.0 or the theme already handled it, and re-graded it. About 73 findings remained after verification. Several angles hit the same problems, merged into the 25 items below (U1–U25), plus one fault-injection finding (S1, §4.3).
- **Severity**: **blocking** = loses saved data, hangs, needs a restart; **important** = common friction, or a likely wrong press; **minor** = everything else.
- **None is blocking**. A few were first reported as blocking: the meeting box swallowing Enter, meetings stopping automatically while locked, the confirm box losing text, Opt+D picking the wrong queue item. After verification they were all downgraded to important: saved data is intact, nothing hangs, and no restart is needed. The only "needs a restart" state comes from fault injection (§4.3) and has not been seen on a real device.
- **Frequency**: daily / weekly / occasional / rare. Estimated from your usage: mostly speaking, rarely typing on the device, carried locked in a pocket. During verification, many "daily" items were downgraded to "occasional".
- **Fix size**: small = under a day; medium = one to three days; large = more than three days or needs the real device.

## 2. Issue list (sorted by severity × frequency)

Status column (2026-10-22, branch `feat/ux-decisions`, shipped in v0.4.1): D1–D10 were all set to the recommended defaults, and all ten were done that way. **Done** = finished, with tests (unit tests plus regression scripts in `firmware/sim/scripts/ux/`); **device gate** = UX-H1–H5 in §5.5, which need the device and have not run yet; **not done** = the default you chose is not to do it. Details in §5.6.

| ID | Issue | Severity | Frequency | Size | Package | Decision | Status |
|---|---|---|---|---|---|---|---|
| U1 | Meeting confirm and end boxes: an Enter that is too fast is silently swallowed, the box stays, Opt+L ignored | Important | Weekly | Small | A1 B2 | D3 | Done (D3; B2's "press Enter again") |
| U2 | No auto-lock: pocketed unlocked, the black screen still takes notes, records memos, starts meetings | Important | Daily (when you forget to lock) | Small | A2 | D2 | Done (D2; device gate H1) |
| U3 | Three holes in the locked memo gesture: two long presses bypass it, the last 0.5 s is swallowed, the card goes dark while holding | Important | Daily | Small | A2 B1 | — | Done (A2, B1; device gate H1) |
| U4 | Holding G0 with the editor or a confirm box open neither records nor says anything | Important | Daily | Small | A3 | D4 | Done (D4) |
| U5 | Single letters on Home: r plus Enter starts a meeting, l locks, n/q/s swallow the first letter | Important | Occasional | Small | A4 B4 | D1 | Done (D1, B4) |
| U6 | Recording results invisible while locked; an automatic meeting stop leaves no trace; 17% still white | Important | Daily / rare | Small | A9 B1 B2 B3 | D7 | Done (A9, D7, B1–B3) |
| U7 | Inbox "[Enter]reply" mostly cannot be delivered; the text is outdated and truncated | Important | Daily | Small | A8 B5 | — | Done (A8, B5) |
| U8 | About the first 0.65 s of a memo is not recorded, with no "you can speak now" cue | Important/minor | Daily | Small / large | B2 B4 A11 | D8 | Done: "● Wait" (● 准备) shows on press (D8, B2, B4); pre-roll (A11) not done per D8 |
| U9 | Pressing ` on the open-session and reply confirm boxes throws away what you just typed | Important | Weekly | Small | A5 | D6 | Done (D6) |
| U10 | In the editor ` types a backtick and only Fn+` exits; four kinds of pages, four ways back | Important | Weekly | Medium | A5 | D5 | Done (D5) |
| U11 | Too little information on the locked screen: Inbox not visible, an accidental lock looks like a crash | Important | Daily (locked, plugged into the Mac) | Small | A2 B1 | D7 | Done (D7, B1) |
| U12 | List cursor tracks position, not item; after a refresh Enter / Opt+D land on another item | Important | Occasional | Medium | A6 B5 | — | Done (A6, B5) |
| U13 | Slots without a project: queuing reports a false "Save failed (E_STORAGE)", digit keys open an empty page | Important | Occasional | Small | A7 | — | Done |
| U14 | Long session names: the confirm pages of two sessions look identical, key prefix repeated | Important | Occasional | Small | A10 B5 | — | Done (A10, B5) |
| U15 | No countdown to the 5-minute memo limit; talking on while holding G0 is wasted | Important | Rare | Small | A9 B2 | — | Done (A9, B2's countdown) |
| U16 | After the SD card is pulled and reinserted, every save fails until a restart, and the notice only says E_STORAGE | Important | Rare | Medium | A9 B3 | — | Done (A9, B3; device gate H3 not run) |
| U17 | Ignored keys give no feedback (editor, meeting page, menu, Fn+arrows, note rows) | Minor | Daily | Small | A8 B6 | D9 | Done (A8, D9, B6) |
| U18 | Discarding a draft has no guard, and the Tab menu defaults to "Discard draft" | Minor | Occasional | Small | A5 | — | Done |
| U19 | Offline or unknown data shown as if live (the theme says "All done") | Minor | Daily | Small | B4 B5 | — | Done (B4, B5; lock card see §5.6) |
| U20 | Toasts queue up: the result of your own action takes 2.5–7.5 s to appear | Minor | Occasional | Small | A8 | — | Done |
| U21 | Status bar: symbols unexplained, one count with three names, overlapping, ※k stays yellow until restart | Minor | Daily | Small | A8 B3 | — | Done (A8, B3) |
| U22 | Inconsistent wording and formats: several names for one action, "1h", tty/bg, words broken mid-word, the most urgent item is the oldest | Minor | Daily | Small | A8 B6 | — | Done (A8, B4, B6) |
| U23 | Meeting page: typing adds ★, digits change the project, ` opens the end box | Minor | Occasional | Small | A1 A8 | D3 | Done (D3, A8) |
| U24 | No dimming before the screen turns off; it just goes black | Minor | Occasional | Small | A2 | — | Done |
| U25 | An open-session request with an unknown result looks like an ordinary queue item and gets opened again | Minor | Rare | Medium | A10 B5 | — | Done (A10, B5) |
| U26 | A memo held on G0 suddenly cuts off; holding the whole time is tiring (gh #51) | Important | Daily | Small | — | — | Done (`fix/memo-hold`, merged into v0.4.1, see §5.7): G0 release debounce, 0.6 s grace, double tap for hands-free; issues from two review rounds fixed too; double-tap count on the real device still to run |
| U27 | While speaking you cannot see which project each of 1–9 is, so you have to remember (gh #53) | Important | Daily | Small | — | — | Done (`feat/memo-legend`, see §2.4): the memo and meeting pages always show a 1–9 / 0 legend with the selected one highlighted; a look on the real device still to do |

### 2.1 Important and common

#### U1 Meeting confirm and end boxes: an Enter that is too fast is silently swallowed, the box stays, Opt+L ignored

**Important · weekly · small · A1 + B2 · D3**

- **What you hit**: starting a meeting with the habitual "Opt+R, Enter, Opt+L, pocket" goes wrong when your hands are quick:
  - the Enter lands within 400 ms of the box appearing and is dropped, with nothing on screen to say so;
  - Opt+L does nothing on the confirm box;
  - the box never goes away; it is still there 10 minutes later, and the screen only dims, never turns off;
  - after that, any stray Enter in the pocket starts the meeting recording, and consent is recorded as "confirmed".

  Ending works the same way: Opt+R, Enter too fast, the end box stays, recording continues all the way to the 3-hour limit.
- **Evidence** [meas]:
  - Start box: `$UX/daily/t02_meeting.cpas` (part A) → `$UX/daily/out/t02/001-a-fast-enter.png`, `002-a-dark.png`; `$UX/daily/t07b_confirm_open.cpas` → `$UX/daily/out/t07b/001-c-optl.png` (not locked), `002-c-2min.png`, `003-c-pocket-rec.png` (one Enter and it is ● REC); `$UX/refute_t02/r2_habit.cpas` → `$UX/refute_t02/out_r2/` ("Opt+R, Enter, Opt+L" in one go: not recording, not locked, 5 minutes later one Enter starts the meeting).
  - End box: `$UX/daily/t02b_end_fast.cpas` → `$UX/daily/out/t02b/001-end-fast.png`, `002-end-10min.png` (● REC 10:08 after 10 minutes); `$UX/refute_t02b/sweep_*.cpas` sweeps from 100 ms to 450 ms, and the boundary is exactly 400 ms; `$UX/refute_t02b/lock_on_box.cpas` → `out/lock_on_box/` (Opt+L has no effect, the box is still there after 70 seconds).
  - ` on the meeting page opens the end box: `$UX/s07_back_key.cpas`, `$UX/s11_end_box_lock.cpas` → `$UX/out/s07/contact.png`, `$UX/out/s11/contact.png`.
- **Code** [src]:
  - `rec_core.cpp:81,93-94`: keys during the guard are simply `keep`, with no effect.
  - `rec_core.cpp:181-183`: `rec_tick` only runs in the recording and end-box phases, so the start box never times out.
  - `rec_core.h:97`: `rec_live` counts the start box as recording too. So `power.cpp:33-34` only dims and never turns the screen off, and `runtime_files.cpp:83` makes the Mac get `E_BUSY live` when pulling recordings.
  - `keymap.cpp:84-88`: the confirm layer has no Opt+L; this is a deliberate design in docs/13 §5.3 ("deal with the confirm box first").
  - `keymap.cpp:105`: on the meeting page ` means end.
- **Fix** (A1, small):
  1. An Enter pressed during the guard must not vanish silently: either flash "Too fast: press Enter again", or only draw "[Enter]start / stop" after 400 ms (this part is B2).
  2. If nobody touches the start box for 15 seconds, cancel it automatically; if nobody touches the end box for 10 seconds, go back to the meeting page and keep recording.
  3. On the start box, pressing a letter, digit or any key other than Enter and Tab cancels (which also fixes half of U5).
  4. Opt+L: on the start box it is "Cancel and lock"; on the end box it is "Keep recording, lock", the same as Opt+L on the meeting page.
  5. The start box does not count as "recording": the backlight turns off as usual, and the Mac pulls recordings as usual.
  6. ` on the meeting page no longer opens the end box; it only says "In a meeting: Opt+R ends it".
- **Old tests to change**: the part of `test_rec_core` that asserts "the start box does not time out" (if any), and `test_keymap_static` (Opt+L added to the confirm layer).

#### U2 No auto-lock: pocketed unlocked, the black screen still takes notes, records memos, starts meetings

**Important · daily (when you forget to lock) · small · A2 · D2**

- **What you hit**: every time you unlock to type or check the board, you have to press Opt+L again before pocketing it. If you forget, on battery the screen goes black after 60 seconds, but that black screen is not locked:
  - the first key only lights the screen, and from the second key on keys work as usual;
  - G0 is never swallowed: a tap opens the editor, a hold records a memo straight away;
  - r, r, Enter starts a meeting.

  A locked black screen and an unlocked black screen look exactly the same.
- **Evidence** [meas]:
  - `$UX/daily/t07_pocket_unlocked.cpas` → `$UX/daily/out/t07/003-g0-wakes-compose.png`, `004-pocket-note-saved.png` ("asdf jkl" plus Enter saved as a note), `007-pocket-memo.png` (holding G0 on the black screen goes straight to ● REC).
  - `$UX/verify/v2_meeting.cpas` → `$UX/verify/v2/001-r-r-enter.png`: r, r, Enter, about 1 second apart, starts a meeting recording.
  - Pocket fuzzing (`$UX/daily/gen_pocket_fuzz.py`, 200 presses per run): unlocked, 6 of 8 runs left 5–13 junk notes, and the other 2 hit L and locked themselves (`$UX/daily/out/fuzz_unlocked_montage.png`); locked, 0 of 8 runs changed anything (`fuzz_locked_montage.png`).
  - `$UX/s04_pocket.cpas` → `$UX/out/s04/contact.png`: a kept draft had gibberish appended and was saved.
- **Code** [src]: `power.cpp:8-10,14-22`: only the first key after the screen turns off is swallowed, and G0 never is. docs/13 §7.1 line 279 says "auto-lock a few minutes after the screen turns off could come later; not in v0.4".
- **Fix** (A2, small): per the D2 default, on battery lock as soon as the backlight turns off (60 seconds), reusing the existing `lock_engage()`. Once locked, "tap, then hold" for memos still works. No auto-lock while plugged into the Mac.

#### U3 Three holes in the locked memo gesture

**Important · daily · small · A2 + B1 · no decision needed** (this fix matches what docs/13:776 decided: "tap G0 to light the screen, then hold within 3 seconds to record")

- **(a) Two long presses bypass "tap, then hold"**: the first long press on the black screen is swallowed, but it lights the card for 3 seconds, so a second long press within those 3 seconds starts recording. Sitting down or shifting can press the side key twice, landing right in that window.
  - Evidence [meas]: `$UX/s17_locked_two_presses.cpas` → `$UX/out/s17/contact.png`. `$UX/refute_s17/` sweeps various intervals; `f_settle20s` silently recorded up to 0:19. Lock-screen fuzzing (`$UX/lockfuzz.py` → `$UX/out/lockfuzz_run.txt`): walking, 157 presses, 0 recordings; sitting, 49 presses, 11 recordings; mixed, 51 presses, 7 recordings; the device stayed locked the whole time.
  - Code [src]: `lock.cpp:26-33`; the unit test `test_lock_power:109-122` treats this behaviour as the spec.
- **(b) A hold in the card's last 0.5 seconds is swallowed**: "is the card lit?" is checked at the moment the long press is recognised, i.e. 500 ms after pressing, not at the moment of pressing. So the card is clearly lit, but the hold does not record. The most natural way to trigger it: out of habit, hold from the black screen → only the card lights → let go and read the card → hold again as the card says, and that one is swallowed too.
  - Evidence [meas]: `$UX/stab/scripts/st03_lock_card_race.cpas` → `$UX/stab/out/st03/contact.png`; `$UX/verify-st03/race.cpas` (part C is exactly the flow above). Fuzzing: holds started with under 500 ms of card time left missed 4 out of 4; otherwise 37 of 38 recorded (`$UX/stab/out/g0_hold_aggregate.txt`).
  - Code [src]: `lock.cpp:29` uses `lock_lit(now)`; `chord_detector.h:29` has `kG0LongMs = 500`.
- **(c) The card goes dark while you hold, without saying "not recording"**: on the black screen the hold is swallowed, the card lights for only 3 seconds and is dark by 3.6 seconds, with G0 still held. You think you are recording, but not a word is captured.
  - Evidence [meas]: `$UX/verify/v11_locked_hold_timing.cpas` → `$UX/verify/v11/contact.png` (the backlight is off at 3.6 and 7.6 seconds of holding, and afterwards the card says "R0" (录0)).
  - Code [src]: `lock_tick` in `lock.cpp:65-70` ignores `eat_up`; `lock.h:24` has `kLockCardMs = 3000`.
- **Fix**:
  - A2 (small): only a **short** G0 press (released within 500 ms) can "arm" it; whether the card is lit is judged at the moment G0 is pressed; the card stays lit while G0 is held.
  - B1 (small): for a swallowed hold, the card text becomes large "Not recording: let go, tap G0, then hold".
  - Also change the table in docs/13 §7.1 and that assertion in `test_lock_power`, and note it in the commit message.

#### U4 Holding G0 with the editor or a confirm box open neither records nor says anything

**Important · daily · small · A3 · D4**

- **What you hit**: holding G0 normally records a memo, but in the following cases nothing happens: no ●, no hint. This device makes no sound, so you only find out it did not record after you finish speaking.
  - You want a memo but let go of G0 a little early (under 500 ms), which opens the editor; holding again does nothing;
  - on battery, tapping G0 to wake the screen goes straight into the editor; holding again does nothing;
  - the "tap, then hold" the lock card teaches does not work when unlocked.
- **Evidence** [meas]:
  - Fuzzing summary (20 seeds, 26.5 simulated hours), `$UX/stab/out/g0_hold_aggregate.txt`, whether holding G0 recorded:

    | Open while holding | Recorded / holds |
    |---|---|
    | Ordinary page | 476 / 479 |
    | Menu open on an ordinary page | 56 / 56 |
    | Editor | 0 / 297 |
    | Menu open over the editor | 0 / 36 |
    | Confirm box | 0 / 34 |

  - `$UX/stab/scripts/st02_g0_hold_overlays.cpas` → `$UX/stab/out/st02/contact.png`.
  - `$UX/verify/g0_hold_editor_battery.cpas` → `$UX/verify/g0bat/contact.png`, all three everyday paths reproduced.
  - `$UX/refute_s16g0/a.cpas` → `$UX/refute_s16g0/out_a/contact.png`: with the same draft, "tap, then hold" while locked **does** record, and the draft is kept. So the fix already exists.
- **Code** [src]:
  - `shell.cpp:117-119`: the editor takes every key before the global keys.
  - `keymap.cpp:70-83`: the edit layer only binds a short G0 press (= save), not a long one.
  - `shell.cpp:190`: long presses are ignored during the recording confirm phase.
  - `shell_menu.cpp:146`: the menu replays G0 to the editor, where it is dropped again; this contradicts v0.4.0.md:28, "pressing G0 in the menu… records a memo".
- **Fix** (A3, small): per the D4 default, as long as nothing is recording, holding G0 under any overlay starts a memo, leaving the draft or confirm box underneath, the same as on the lock screen. Holding G0 on the meeting start or end box says "Answer the box first".
- **Old tests to change**: `test_shell_dispatch_order:151-172`, `test_recording_gestures_ignored_under_compose_and_confirm`. Both were set deliberately in v0.3 and now conflict with docs/13 principle 5, "G0 works on every page".

#### U5 Single letters on Home: r plus Enter starts a meeting, l locks, n/q/s swallow the first letter

**Important · occasional · small · A4 + B4 · D1**

- **What you hit**: the letters on the Home tiles are shortcuts (P I R N Q S M L and 1–9). Text can end up on Home without you noticing; listed here are the three **most likely** paths, not "using Home as a text box":
  - one Backspace too many in an empty editor closes it and returns to Home;
  - after saving with Enter, you go back to Home and keep typing the next one;
  - aiming for `;` (up) but hitting the neighbouring l.

  Consequences:
  - typing "remind me…": the first r opens the meeting confirm box, it swallows the following letters, the final Enter is already past 400 ms, and a meeting recording starts;
  - typing "lunch…": the first letter locks the screen;
  - typing "note to self": what gets saved is "ote to self".
- **Evidence** [meas]:
  - `$UX/scripts/fm02_type_on_home.cpas` → `$UX/fm02_type_on_home/002-typed.png`, `003-enter.png`, `004-after.png`.
  - `$UX/v_s10/b_overshoot.cpas` → `$UX/v_s10/out_b_overshoot/contact.png`: one Backspace too many returns to Home, then typing "rent due friday" plus Enter starts a meeting.
  - `$UX/refute-home-type/b.cpas` → `$UX/refute-home-type/out_b/contact.png`: after saving "buy milk", typing "send the invoice" opens a new session with "end the invoice".
  - `$UX/fuzz/phrases.py` → `$UX/fuzz/phr_montage.png`: 12 of 14 everyday phrases had unintended effects.
  - Random 12 keys (`$UX/fuzz/fuzz.py 300 12 20261022`): 4 meetings and 19 locks in 300 runs.
- **Code** [src]: `home_logic.cpp:73-75`; `home_logic.h:26-33`; `shell.cpp:184-188` (the start box ignores letters); `compose.cpp:53-56` (Backspace in an empty editor closes it).
- **Fix**:
  - A1: a letter on the start box cancels it (see U1).
  - A4 (small): per the D1 default, R and L on Home need Opt; the other tiles stay single keys. The first time a non-tile letter is pressed on Home, show once "Letters are shortcuts: Note with G0".
  - B4: the tiles say "Opt+R meeting" and "Opt+L lock".
  - Alternative (medium, D1): a non-tile letter on Home starts a Note directly with that letter ("type to note").

#### U6 Recording results invisible while locked; an automatic meeting stop leaves no trace; 17% still white

**Important · daily (memos while locked) / rare (automatic stop) · small · A9 + B1 B2 B3 · D7**

- **What you hit**:
  - Recording a memo while locked, the screen goes fully black the moment you let go. Notices like "saved", "too short, discarded", "needs an SD card", "battery low" all expire while you cannot see them. With low battery, the card is still lit saying "Talk: tap G0, then hold G0"; you do as told, and only after speaking learn that nothing was recorded.
  - Holding a meeting while locked:
    - when the battery drops to 17%, or the 3-hour limit or a full card is reached, the meeting stops by itself;
    - after it stops, the dim ● line disappears and the screen goes fully black, with the reason left nowhere;
    - after unlocking you see only a green "Recording saved", and only if you unlock within 2.5 seconds.
  - The battery level is a straight line from 3300–4150 mV, and **17% is exactly where recording stops**, yet it only turns red below 10%. So a meeting started at 18% gives no warning at all.
- **Evidence** [meas]:
  - Memo: `$UX/daily/t01_memo.cpas` → `$UX/daily/out/t01/008-b-released.png` (black as soon as you let go), `010-c-short.png` (discarded as too short, also black); `$UX/verify/v_locked_memo.cpas`, `$UX/verify/v_nosd.cpas` → `$UX/verify/out/memo/`, `$UX/verify/out/nosd/`.
  - Automatic stop: `$UX/refute_lockstop/bat_stop.cpas`, `cap_stop.cpas` → `$UX/refute_lockstop/out/bat/`, `out/cap/`; `$UX/s05_meeting_lowbat.cpas` → `$UX/out/s05/contact.png`.
  - Battery: `$UX/out/s11_battery_stop/003-meeting-3450-5s.png` (17% in white); `$UX/refute_batstop/out/002-meeting-3560-warn-zone.png` (no warning at 30% either); the theme's recording page drops the battery level entirely (`$UX/out/theme_s11_battery_stop/003-meeting-3450-5s.png`).
- **Code** [src]:
  - `shell.cpp:177`; `shell_render.cpp:89`: toasts are not drawn while locked.
  - `toast.cpp:41-44`: toasts keep counting down and expire while locked.
  - `power.cpp:56-58`: when recording ends and the device is locked, the backlight turns off.
  - `rec_core.cpp:184,186-201`; `runtime_rec.cpp:188`: the `auto_stop` flag is not checked.
  - `rec_overlay.cpp:108`: a stop mid-way uses the refusal-to-start text "can't record".
  - `status_bar.cpp:12-14,62`; `rec_core.h:33-34`.
  - The 3.55 V warning required by docs/11 §4.3 was never built.
- **Fix**:
  - A9 (small):
    - remember "the result or stop reason of the last recording" until you have lit the screen and seen it once;
    - when recording ends while locked (manually or automatically), light the card for 2 seconds;
    - use different text for an automatic stop and a refusal to start: "Meeting stopped: battery low · saved 42:10" / "3 h limit reached" / "SD full";
    - add the 3.55 V warning line.
  - B1, B2, B3 (small):
    - draw a result line on the card (✓ Saved 0:12 / ✗ Too short / ✗ Needs an SD card / ✗ Battery low). Only state and numbers, no content, so the lock screen privacy rule is not broken.
    - Battery turns yellow below 30% and red below 17%.
    - The meeting page keeps "Bat N%".
    - Below 30%, pressing Opt+R adds a line "Battery: about N min" to the confirm box. How long it can really record needs real-device power measurements [todo G-A7].

#### U7 Inbox "[Enter]reply" mostly cannot be delivered; the text is outdated and truncated

**Important · daily · small · A8 + B5 · no decision needed**

- **What you hit**: the Inbox's main action is "reply to a session waiting for you", and every row's hint bar says "[Enter]reply". However:
  - for sessions opened in a terminal (tty), a reply can only be saved to the Mac queue;
  - the same for sessions waiting for permission or asking a question;
  - cpad has replies off by default (`experimental_bg_reply = False`), so even idle background sessions cannot be reached.

  Only after typing on the small keyboard and pressing Enter do you see "interactive sessions can be replied to from v0.4, saving…" (交互式 session 要到 v0.4 才能回复，将存…): the v0.4 it mentions is the version you are using, and the sentence is cut in half by the screen edge. Queued replies are not delivered automatically on the Mac either.
- **Evidence** [meas]:
  - `$UX/scripts/fm05_reply.cpas` → `$UX/verify_fm05/` (001: a row says "waiting 9m tty" but the hint is [Enter]reply; 003: confirm box; 004: toast).
  - `$UX/verify-inbox/`: v1, a waiting background session, the confirm box says "Session busy" while the row says "waiting"; v2, with the default config, says "Replies off"; v4, a terminal session.
  - Width: the text is about 270 px, the screen only 240 px (`$UX/refute_s05_tty/out/003-confirm.png`, `004-toast.png`).
- **Code** [src]: `confirm.cpp:31,47` (the unit test `test_confirm_execute_vs_queue:170` locks in the outdated text); `inbox_module.cpp:15,86`; `board_module.cpp:27,263`; `inbox_logic.cpp:64-70` (the detail row never shows what the session is asking); daemon `settings.py:62`, `hub/reduce.py:3-5`; docs/17 §1.1, §4.3–4.4.
- **Fix**:
  - A8 (small):
    - shorten the confirm box text (no more than 20 Chinese characters) and drop "v0.4";
    - for the "waiting" state, stop saying "busy";
    - make the queue path explicit: "Queued on the Mac (not sent)" (存到 Mac 队列（不会发给 session）);
    - add a test that every confirm title and toast is no wider than 240 px.
  - B5 (small): per-row hint bar. Rows that can be reached (background, idle, cpad replies on) say "[Enter]reply", others "[Enter]note"; the editor title says so too.
  - Long term: docs/17's v0.6 (show what the session is asking, reply by choosing options).

#### U8 About the first 0.65 seconds of a memo is not recorded, with no "you can speak now" cue

**Important (daily angle) / minor (first-use angle) · daily · small + large · B2 B4 + A11 · D8**

- **What you hit**: holding G0 only counts as a "hold" after a full 500 ms, and for those first 500 ms the screen does not change at all; recording starts about 25 ms after that, and WhisperWoof also skips the first 150 ms of the file. So if you start speaking as you press, the first ~0.65 seconds are lost.
  - The Home hint says "hold G0 and talk", which tells you to speak as soon as you press.
  - The line "Speak once you see ● REC" only flashes for 134–150 ms, far too short to read.
  - A short memo also has to be held for about 1.5 seconds or more, or it is discarded as "too short".
- **Evidence**:
  - [meas] `$UX/memo_preroll/memo_start.cpas` → `$UX/memo_preroll/out/contact.png`: holding for 100, 300 and 450 ms, the picture does not change.
  - [meas] `$UX/refute_memo_start/short_hold.cpas`: a 1.4-second press is discarded, 1.7 seconds is saved.
  - Real-device figure: 525 ms from pressing G0 to the first sample (docs/07:768, v0.3.2.md:65).
  - [src] `chord_detector.h:29`; `rec_overlay.cpp:57-58`; `home_module.cpp:83`; `rec_engine.cpp:151`; `rec_meta.h:24`.
  - Pre-roll was originally dropped because the beep and the microphone share I2S pins (docs/11 §0.2). Since v0.3.1 there is no beep, so that reason is gone.
- **Fix**:
  - B2, B4 (small): show "● Wait" (● 准备) as soon as G0 is pressed; the theme's large breathing ● fits nicely; change the hint to "Hold G0, speak once you see ● REC".
  - A11 (large, D8): pre-roll. As soon as G0 is pressed, start the microphone into a ring buffer:
    - when the press reaches 500 ms and becomes a hold, write the buffer at the start of the memo;
    - on a short press, drop the buffer;
    - this period does not count as "recording" and does not interrupt the Mac pulling recordings;
    - not on the locked black screen and not during meetings.

    That would cut the loss from about 650 ms to about 150 ms (the mic start-up transient) [todo real device].

#### U9 Pressing ` on the open-session and reply confirm boxes throws away what you just typed

**Important · weekly · small · A5 · D6**

- **What you hit**: the confirm box is where you **first** see the whole text quoted back, so that is where typos are usually spotted. Pressing `, labelled "Cancel", goes back to the previous page and the text is gone. Pressing Opt+S again opens an empty editor (0/500).
- **Evidence** [meas]:
  - `$UX/s01_spawn_cancel.cpas` → `$UX/out/s01/contact.png`: for a 72-character task, recovering takes 75 presses.
  - `$UX/refute_s01/reply_cancel.cpas` → `$UX/refute_s01/out/contact.png`: same for replies.
  - `$UX/refute/confirm_cancel_spawn.cpas` → `$UX/refute/out/004-after-cancel.png`, `005-reopen.png`.
- **Code** [src]: `shell.cpp:254-258` (cancel closes the overlay outright), `shell.cpp:330-335` (reopening clears it). Compare: `compose.cpp:54-59`, the editor itself asks before dropping text; `shell.cpp:290-291`, a failed save goes back to the editor with the text kept.
- **Fix** (A5, small): per the D6 default, on the open-session and reply confirm boxes, ` and Fn+` both go back to the editor with text and target kept; the hint becomes "[`]edit" (` 返回修改) (that wording is B6). Confirm boxes for queue items on the board have no draft and still just close. Cost: discarding the whole thing goes from 1 press to 3 (`, Fn+`, Enter).

#### U10 In the editor ` types a backtick and only Fn+` exits; four kinds of pages, four ways back

**Important · weekly · medium · A5 · D5**

- **What you hit**:
  - on list pages, ` and Backspace both go back;
  - in the editor, ` types a backtick, and only Fn+` exits (#15 already noted that Fn+` as the exit key is hard to remember; #15 only fixed lists and the empty editor);
  - on the meeting page, ` means "Stop recording?";
  - on the discard prompt, "[`]back" means go back to editing.

  After opening the editor by mistake, pressing ` and then Enter out of habit saves a note containing only "`".
- **Evidence** [meas]:
  - `$UX/scripts/fm16_back_loop.cpas` → `$UX/fm16/001-b1.png` to `004-b4.png` (Fn+` toggles the discard prompt back and forth, and ` gets typed into the note).
  - `$UX/scripts/refute_back2.cpas` → `$UX/refute_back2/004-g0-grave-enter.png` (a junk note, "Saved · to sync 1").
  - Fuzzing: 107 of 300 runs ended stuck in the editor, unable to get out with "Fn+` twice, ` twice, Opt+H" (`$UX/fuzz/other_groups.png`). This only shows that this way out always fails in the editor; it says nothing about everyday frequency.
- **Code** [src]: `keymap.cpp:67-68,73,105`; `type_char` in `compose.cpp`; `board_module.cpp:262` (the board overview hint does not mention the back key); docs/devlog/2026-10-19-20.md:42.
- **Fix** (A5, medium, D5):
  - ` in the editor exits: straight away when empty, asking "Discard?" when there is text;
  - the backtick character is typed with Fn+` instead;
  - on the discard prompt, ` still goes back to editing (back on a dialog cancels the dialog);
  - the board overview hint gains "[`]back" (B5).
  - Optional (medium, not done by default): exiting with text does not lose the draft but "parks" it, to be restored the next time G0 or Opt+N opens the editor. This would also fix U17's "a draft holds the whole device hostage".

#### U11 Too little information on the locked screen: Inbox not visible, an accidental lock looks like a crash

**Important · daily (locked, plugged into the Mac) · small · A2 + B1 · D7**

- **(a) Inbox not visible**: while locked and plugged into the Mac, a session starts waiting for you and the screen stays fully black. The device has no sound or vibration, and the Mac shows no notification, so the message never reaches you; 10 minutes later it is still black. The most common case: carried locked in a pocket all day, then plugged into the Mac back at the desk. You cannot even tell that it connected.
  - Evidence [meas]: `$UX/scripts/s20_locked_needs_you.cpas` → `$UX/out/v040_s20/002-locked-need-1s.png`, `003-locked-need-10m.png` (0 lit pixels); `$UX/scripts/s20b_pocket_then_plug.cpas` → `$UX/out/refute_s20b/shots.json`.
  - Code [src]: `lock_screen.cpp:91-95`; `power.cpp:55-60`; the daemon has no macOS notifications at all.
- **(b) An accidental lock looks like a crash**: a single l on Home locks the screen, and l sits right next to `;` (up) and `,` (left). The screen goes black the moment it locks, without saying "Locked". After that, Enter, Space, `, Tab and Opt+H do nothing, even when plugged into the Mac. But your usual experience is "any key lights a black screen" (true when unlocked), so it is easy to think it has crashed and reach for the power switch.
  - Evidence [meas]: `$UX/scripts/fm03_hello_lock.cpas` → `$UX/fm03_hello_lock/001-hello.png`, `002-keys.png`, `004-g0.png` (only G0 lights the card); `$UX/refute_lockdead.cpas` → `$UX/refute_lockdead/contact.png`; `$UX/refute-lock-l/b_bat.cpas` → `$UX/refute-lock-l/out_b/` (the same black screen lights on a key when unlocked, not when locked).
  - Code [src]: `lock.cpp:37-41` (`card=false` on locking), `lock.cpp:61`.
  - Three of the four verifiers graded it minor and one important; fixed together with (a).
- **Fix**:
  - A2, B1 (small, D7):
    - light the card for 2 seconds at the moment of locking ("Locked · to unlock: Opt+L, then Enter");
    - while plugged into the Mac, light the card for 3 seconds whenever the Inbox count goes up, showing only the number;
    - L on Home needs Opt (D1).
  - Optional, not done by default per D7: Enter or Space while locked lights the card for 1 second, at most once every 30 seconds.

### 2.2 Important, but less common

#### U12 List cursor tracks position, not item; after a refresh Enter / Opt+D land on another item

**Important · occasional · medium · A6 + B5**

- **What you hit**: the cursor remembers "which row", not "which item". Every refresh from the Mac re-sorts the list:
  - a session starts waiting;
  - idle and busy switch back and forth;
  - a new queue item arrives at the top.

  The row under the cursor is then a different item. Opt+D has no confirmation, and the toast does not say which item ("Dismissed; it will come back when its state changes" / "Sent"). The result:
  - a session you dismissed by mistake disappears from the Inbox;
  - a queue item marked done by mistake cannot be undone on the device, and the Mac has no `cpa queue reopen` either.

  With the Tab menu open, the menu covers the jump, so it is even harder to notice.
- **Evidence** [meas]:
  - Queue: `$UX/s02_queue_cursor.cpas` → `$UX/out/s02/contact.png`; `$UX/s02_serve_trace.py` → `$UX/out/s02_serve/trace.txt` (q18 was selected, `queue.done q19` was sent); `$UX/refute_s02/new_item.cpas`, `menu.cpas` → `$UX/refute_s02/out_new/`, `out_menu/`.
  - Session: `$UX/s03_inbox_cursor.cpas` → `$UX/out/s03/contact.png`; `$UX/s03_refute_serve.py` → `$UX/out/s03_refute/sheet.png` (`inbox.dismiss cccc0003` was sent, which is host-sim).
- **Code** [src]: `inbox_logic.h:11-13` and `board_logic.h:18-22` store an index; `board_module.cpp:145,163-164`; daemon `hub/snapshots.py:57-59,102`; `queue/fold.py:50-51`.
- **Fix** (A6, medium):
  - Keep the selected sid or qid in the UI state, and find its position by id again on every key press and every frame. This needs no change to `module.h`.
  - If that item is gone, nothing is selected: Enter and Opt+D do nothing until you move the cursor.
  - The Opt+D toast says which item: "Dismissed: payment callback retry" / "Done: ask lawyer…".
  - Add `cpa queue reopen <qid>` on the Mac.

#### U13 Slots without a project: queuing reports a false "Save failed (E_STORAGE)", digit keys open an empty page

**Important · occasional (first use, wrong digit, gaps between slots) · small · A7**

- **What you hit**:
  - queuing or opening a session with an empty slot selected (Opt+5 when you only have 2 projects), Enter shows a red "Save failed (E_STORAGE)", which sounds like a broken SD card;
  - the draft stays in the editor, and tapping G0 in the editor means "save", so every note you then try to take with a G0 tap is sucked into that draft and fails with it;
  - with no projects at all, following the hint "Pick a project first (Opt+1..9)" and pressing Opt+1 also gives E_STORAGE;
  - pressing 5 on Home opens an empty page titled "5 ? ·";
  - Enter on the board's "Other" row does nothing.
- **Evidence** [meas]:
  - `$UX/stab/scripts/st01_queue_unknown_slot.cpas` → `$UX/stab/out/st01/contact.png`.
  - `$UX/refute-st01/`: r01, tapping G0 in the editor gives E_STORAGE again; r02, opening a session; r04, with zero projects, following the hint also fails.
  - Soak test with no registered projects, `$UX/stab/out/soak-smoke/result.json`: 6 of 13 never reached the Mac; `$UX/stab/out/soak-smoke/shots/021-0020-flow_nav.png`: one queue draft swallowed the next 6.
  - `$UX/scripts/fm12_slots.cpas` → `$UX/fm12/001-five.png`; `$UX/refute_slot5/` (Opt+Q from that empty page is also E_STORAGE); `$UX/refute_fm12/007-other-enter.png`.
  - `$UX/daily/t04_note_queue.cpas` → `$UX/daily/out/t04/007-q-saved.png`: already saved, yet the stale "Pick a project first" is still on screen.
- **Code** [src]: `compose.cpp:32,41,123`; `shell.cpp:158,237-251,271,288,307`; `board_module.cpp:203`; `board_logic.cpp:40`.
- **Fix** (A7, small):
  - treat an empty slot like slot 0 (NeedProject), and have the hint list the existing projects: "Pick a project first: 1 acmeshop · 2 cardputer";
  - with no projects at all, say "No projects: cpa project add on Mac", and mention that Opt+N saves it as a Note instead;
  - a digit for an empty slot shows "No project 5 (only 2)" in place, without opening an empty page;
  - Enter on the "Other" row says "Other projects: see the Mac";
  - never report "no project" as E_STORAGE.

#### U14 Long session names: the confirm pages of two sessions look identical, key prefix repeated

**Important · occasional · small · A10 + B5**

- **What you hit**:
  - two sessions in the same project, whose names start the same, wait at the same time, and their confirm pages are **pixel-identical** ("Send now → cardputer-as/fix-flaky-refun…"). That is exactly the screen meant to prevent sending to the wrong one, yet you cannot tell who it goes to, and a wrong send cannot be taken back.
  - Sessions opened from the device are already named "key/slug", and the device adds the key again when showing them, giving "cardputer-as/cardputer-as/…".
  - Tasks dictated in Chinese get names like "task-3".
  - The detail row of a background session says "claude attach a1b2c3", not the name.
- **Evidence** [meas]:
  - `$UX/daily/t10b_confirm.cpas` → `$UX/daily/out/t10b/002-confirm-1.png` and `004-confirm-2.png`; a PIL comparison finds no difference.
  - `$UX/verify/v10b_confirm_daemon_names.cpas` → `$UX/verify/out/v10b/`.
  - `$UX/scripts/refute_s10_spawned.cpas` → `$UX/out/refute_s10_spawned/001-inbox-spawned.png`.
- **Code** [src]: `inbox_module.cpp:31-34`; `inbox_logic.cpp:65-67`; `shell_render.cpp:37-47,161,167`; `home_module.cpp:105`; `runtime_link.cpp:141`; daemon `agents/claude_code/argv.py:30-38`.
- **Fix** (small):
  - A10: if the name already starts with "key/", do not add it again; the daemon names Chinese tasks from the first 8 characters of the task text instead of task-N.
  - B5: the confirm page keeps the end of the name, plus a short id or how long it has waited ("…webhook-timeout · d4e5 · 5m"); the Inbox detail row shows the full name, over two lines if needed.

#### U15 No countdown to the 5-minute memo limit; talking on while holding G0 is wasted

**Important · rare · small · A9 + B2**

- **What you hit**: up to 4:55 there is no sign on screen that the limit is near. At 5:00, even with G0 still held and you still talking, the screen jumps straight back to Home, flashing "Recording saved" once, and nothing you say after that is recorded. The original design had a beep at 5:00; when sound was removed nothing replaced it.
- **Evidence** [meas]: `$UX/scripts/s12_memo_cap.cpas` → `$UX/out/s12_memo_cap/002-memo-4m55.png`, `003-memo-5m01.png`; same with the theme: `$UX/out/theme_s12_memo_cap/`. See also `$UX/daily/t13_misc.cpas` → `$UX/daily/out/t13/003-b-cap.png`.
- **Code** [src]: `protocol_limits.h:75`; `rec_overlay.cpp:54-68`; `rec_core.cpp:184`; docs/11 §3.1 step 4.
- **Fix** (small):
  - B2: from 4:30 show "0:30 left" in the warning colour, with the level bar turning yellow.
  - A9: after the limit, the recording screen stays on "5 min reached: saved, let go of G0" until you let go.
  - Add a hint: "Over 5 minutes? Use Opt+R for a meeting".

#### U16 After the SD card is pulled and reinserted, every save fails until a restart, and the notice only says E_STORAGE

**Important · rare · medium · A9 + B3**

- **What you hit**:
  - if the card is pulled while running and put back (say, to read it on the Mac), and any read or write hits the no-card state meanwhile, storage locks up until a restart. After that:
    - every save only shows "Save failed (E_STORAGE)", exactly as with a full card;
    - "needs a restart" is only written on the system page;
    - and a restart clears the draft in memory.
  - When booting without a card, Home still shows "R meeting" and "hold G0 and talk"; you only find out after one failure. After inserting the card a restart is also needed before it is recognised, but nothing says so.
- **Evidence**:
  - Pull and reinsert: code only [src], since the simulator does not simulate pulling the card. See `sd_card.h:5,52-53`, `from_errno` in `sd_card.cpp`, `sd_store.h:10-13`, `shell.cpp:307`, `system_logic.cpp:88`, `runtime_rec.cpp:45`; docs/09 §3.8 states this is deliberate.
  - No card [meas]: `$UX/scripts/fm07_nosd.cpas` → `$UX/fm07_nosd/001-home.png`, `002-memo.png`, `006-control.png` (the Control page says "SD free --"), `005-system.png` (the warning is on row 9).
- **Fix**:
  - A9 (small): when storage locks up, the notice becomes "SD card error: reseat it, restart"; "Needs an SD card" gains "(insert, restart)"; when recording is impossible, mark the R tile and the G0 hint on Home.
  - B3 (small): status bar gains ※SD; the Control page's SD row says "no card (can't record)"; the system page puts the warning on the first row.
  - A9 (medium): save the draft in internal cpafs so it survives a restart.
  - Device gate [todo]: pull and reinsert the card.

### 2.3 Minor

#### U17 Ignored keys give no feedback

**Minor · daily · small · A8 + B6 · D9**

The keyboard has no diodes, so missed keys are common. When a key does nothing, you cannot tell "not allowed" from "not registered", so you press it again, or press `, which on the meeting page happens to mean "Stop recording?". Specifically:

| Where | Ignored keys | Evidence |
|---|---|---|
| Editor with a draft | Opt+H / I / P / M all silently do nothing; the draft holds the whole device hostage | `$UX/s14_draft_blocks.cpas` → `$UX/out/s14/contact.png`; `$UX/refute_s14/out/contact.png`; `compose.h:7` |
| Meeting page | Opt+N / I / H / P / M | `$UX/refute-s07m/s07m.cpas` → `$UX/refute-s07m/out/contact.png`; `shell.cpp:116,200` |
| Tab menu open | Opt+L and Opt+I, listed on the menu, are eaten | `$UX/refute14/r14.cpas` → `$UX/refute14/out/contact.png`; `shortcut()` in `menu_view.cpp` |
| Lists, Home, menus | Fn+`;` `.` `,` `/` (arrows printed on the keycaps) | `$UX/scripts/refute_fm15.cpas` → `$UX/refute_fm15/contact.png`; `chord_detector.cpp:35-38`, `keymap.cpp:35` |
| Note rows in menus | The rows "Hold G0" and "Opt+1-9" can be selected, but Enter does nothing | `$UX/refute-fm09-menu/menu_shown_only.cpas` → `$UX/refute-fm09-menu/out/`; `menu_view.cpp:50-54` |

**Fix**:
- A8 (small):
  - One rule: a refused key gets a one-line hint, and the same hint is not repeated within 3 seconds. For example "In a meeting: G0 adds ★, Opt+R ends", "Enter saves or ` exits first".
  - Bind Fn+arrows once at the list layer so they work in all four places. `nav_keys` must return the Fn key set for `test_keymap_static` to pass.
- B6 (small):
  - With a menu open, key combinations bound on the layer below take effect directly: close the menu first, then run it, the same as pressing Enter in the menu (D9; changes docs/13 §5.4 step 3).
  - Enter on a note row says "Only a note: hold G0".
  - In the theme, a selected note row must still look like a note.

#### U18 Discarding a draft has no guard, and the Tab menu defaults to "Discard draft"

**Minor · occasional · small · A5**

- **What you hit**:
  - Pressing Enter within 80 ms of Fn+` throws the draft away. Every other confirm box has a 400 ms guard, and PRD §8.1 requires one on all confirm boxes.
  - Pressing Tab on the discard prompt selects "[Enter]discard draft" by default.
  - Pressing Fn by mistake when aiming for `~` (Shift is right next to it): the letters typed next are ignored, and the Enter meant to save becomes a discard.
- **Evidence** [meas]: `$UX/refute_discard/discard.cpas` → `$UX/refute_discard/out/001-a-esc-enter-fast.png`; compare `002-b-confirm-fast-enter.png`; `$UX/s15_menu_default_row.cpas` → `$UX/refute_s15menu/contact.png`; `$UX/refute_s04b/out/009-d-gone.png`.
- **Code** [src]: `compose.cpp:71-79`; `shell_menu.cpp:34-38`; `test_compose_limits_and_dash_guard:154-157`.
- **Fix** (A5, small):
  - add a 400 ms guard to the discard prompt;
  - put "[`]back to editing" on the first row of the menu;
  - optional: typing on the discard prompt goes back to the editor and types that character.

#### U19 Offline or unknown data shown as if live

**Minor · daily · small · B4 + B5**

- **What you hit**:
  - With cpad not running, Home says "No session needs you". The truth is "unknown", not "zero".
  - After restarting while offline, Home and the Inbox say "Inbox (0)" and the status bar "!0", while the board says "run--".
  - 40 minutes after unplugging, "waiting 49m" keeps counting up, and the "[Opt+D]hide" hint is still there.
  - The theme goes further: offline, the greeting is "Hello, all done", and Mando's face is the same as online.
- **Evidence** [meas]: `$UX/refute_fm_nolink/nolink.cpas` → `$UX/refute_fm_nolink/out/contact.png`; `$UX/scripts/s15_offline_boot.cpas` → `$UX/out/s15/002-offline-boot.png`, `004-offline-inbox.png`; `$UX/s12_offline_stale.cpas` → `$UX/refute-s12/out/contact.png`; theme: `$TH/firmware/sim/out/theme_tour/001-home-empty.png`, `$UX/out/theme_s15_offline_boot/002-offline-boot.png`.
- **Code** [src]: `home_module.cpp:93-108`; `inbox_module.cpp:68-86`; `home_logic.cpp:94`; `reducers.cpp:84-87`; `board_module.cpp:105-120` (only the board shows "Updated N min ago"); `status_bar.cpp:85` says "no reply", `confirm.cpp:22,28` says "offline".
- **Fix** (small): when the link is not ready:
  - Home's most urgent item and the Inbox say "Offline · as of N ago", and ages stop growing;
  - if no data has arrived since boot, say "No Mac yet", not 0;
  - when cpad does not answer, hint "cpad isn't running on the Mac (cpa status)";
  - the Inbox does not show Opt+D;
  - use one word for "no reply" and "offline";
  - the theme does not say "All done" when offline, and Mando gets an offline look.

#### U20 Toasts queue up: the result of your own action takes 2.5–7.5 s to appear

**Minor · occasional · small · A8**

- **What you hit**: toasts are a first-in-first-out queue, 2.5 seconds each, at most 3 queued. With "X needs you" from the Mac ahead of it, "Saved" appears 2.5–7.5 seconds late. If saving fails, the error is just as late.
- **Evidence** [meas]: `$UX/refute-st04/b_async_waiting.cpas` → `$UX/refute-st04/out/b_async_waiting/contact.png` ("Saved" only after about 3 seconds); `$UX/s06_toasts.cpas` → `$UX/out/s06/contact.png` (5 seconds late with 3 queued). Also, "a green Saved covering a red failure" only happens at machine typing speed (`$UX/refute-st04/a_human_speed.cpas`).
- **Code** [src]: `toast.h:11-12`; `toast.cpp:31-44`; `runtime_link.cpp:142`.
- **Fix** (A8, small):
  - results of your own actions jump to the front of the queue and push out queued "needs you" toasts (the status bar's !N already carries that information);
  - error toasts show for 4–5 seconds;
  - toasts whose condition no longer holds (such as "Pick a project first" after the target changed) are removed.

#### U21 Status bar: symbols unexplained, one count with three names, overlapping, ※k stays yellow until restart

**Minor · daily · small · A8 + B3**

- **What you hit**:
  - **↑N, !N, R N (录N), ※kN and ◇/◎/○ are explained nowhere on the device**.
  - Notes waiting to be sent have three names: ↑N, "to sync N" and "outbox pending N".
  - R N counts 5-minute segments, so a 1-hour meeting shows "R12" (录12).
  - Holding a key in the pocket for more than 10 seconds produces a yellow "※k1" that stays until restart, and there is nothing you can do about it.
  - In long meetings the left and right groups overlap. In the theme, ※k completely covers "R N".
  - After recordings are transferred there is no "✓ synced"; the number just disappears.
- **Evidence** [meas]: `$UX/fm13/001-ghost.png`, `002-later.png`, `003-control.png`; `$UX/refute_glyphs/`; `$UX/out/s18_x3/002-meeting-125m.png` ("录3※k1"); `$UX/out/theme_s18_status_overlap/002-meeting-125m.png`; `$UX/refute-s18/out_r1/002-after-pocket-press.png`; `$UX/refute_recsync/rec_count.cpas` → `$UX/refute_recsync/out/contact.png`.
- **Code** [src]: `status_bar.cpp:12-14,36-38,44,65-68,112-120`; `menu_model.cpp:191-199,225-231`; `system_logic.cpp:99,138,142`; `kb_watch.cpp:14`.
- **Fix**:
  - A8 (small): add a "Status bar" block to the Control page, in whole sentences:
    - "↑1: 1 note waiting to go to the Mac"
    - "R2: 2 recordings waiting to transfer"
    - "※k1: keys pressed too fast, 1 dropped"

    One name per count.
  - B3 (small):
    - lay out the left group by the width the right group leaves; when it does not fit, drop ↑0, !0 and R N first;
    - show ※k for 10 seconds only;
    - label "R N" as "N segments" or show minutes instead;
    - on the lock card and the Control page, when the counts are 0 and the Mac is connected, show "✓ synced".

#### U22 Inconsistent wording and formats

**Minor · daily · small · A8 + B6 · the order of Home's most urgent item is under "done by default" (end of §3)**

| Problem | Example | Evidence |
|---|---|---|
| Several names for one action | 记一条 / 记笔记 / 笔记 (note); 回复 / 反馈 (reply / feedback); 开session / 新session / 开新 session (new session); 排队给AI / 排队 / 队列 / 队N (AI queue); "未归属" (unassigned) used to mean today's notes | `action_help.cpp:13,19`; `home_logic.h:27-30`; `compose.cpp:113-116`; `shell_render.cpp:158`; `$UX/out/refute_glossary/`, `$UX/out/refute_glossary2/` |
| Two names for one key on a confirm box | The body says "Enter start", the hint bar "Enter confirm"; even where Enter means "close", the hint bar says "confirm"; the meeting box draws the key row twice | `shell_render.cpp:127` vs `:171-173`; `rec_overlay.cpp:73,85` vs `:140-141` |
| Ages always rounded down, three formats | 1 h 59 min shows "1h", 47 hours shows "1d", the board next to it says "updated 2811 minutes ago", and the system page says "1d ago" | `text_fit.cpp:81-93`; `board_module.cpp:117`; `$UX/scripts/r_age_1h59.cpas`, `r_age_47h.cpas` → `$UX/out/r_age_*/contact.png` |
| English abbreviations, inconsistent spelling | tty, bg, plan (the y of tty is cut by the screen edge); "Opt+1..9" vs "Opt+1-9" | `inbox_module.cpp:29`; `$UX/crop_tty.png`; `compose.cpp:123` |
| English glued to Chinese | "Opt+D忽略" looks like "Opt+I…", and Opt+I happens to be the Inbox | `$UX/refute_hintgap/hints_s4.png`. The theme's `hint_layout` already fixes this |
| The editor breaks lines mid-word | "Bingo shots o / n Friday" | `shell_render.cpp:49-60`; `$UX/refute_wordwrap/out/001-long.png` |
| Home's most urgent item is the oldest | A session idle for 2 days takes the only slot, and the one that just finished comes after it | `home_logic.cpp:42-48`; `$UX/out/refute_urgent_order/001-idle-home.png` |

**Fix** (small):
- One glossary: 记一条 (Note) · 排队给 AI (AI queue) · 开 session (New session) · 回复 (Reply) · 项目 (Projects) · 等你 (Inbox). Tiles, menu rows, editor titles and confirm boxes all follow it, with a test comparing `action_help`, `kHomeTiles` and `compose_kind_label`.
- The editor title says "Today's notes · No project" (今日笔记 · 未归属).
- Use "terminal / background" (终端 / 后台) instead of tty / bg.
- One age function in Chinese ("9 分钟", "2 小时", "1 天"), rounded to nearest; Chinese is wider, so check the fit first.
- Always write "Opt+1-9".
- The confirm box's hint bar uses the same verb as its body.
- The editor breaks lines at spaces.
- Home's most urgent item: waiting sessions stay "longest waiting first"; idle ones become "newest first", marked "idle"; anything older than a day folds into "+N older".

#### U23 Meeting page: typing adds ★, digits change the project, ` opens the end box

**Minor · occasional · small · A1 + A8 · D3**

- **What you hit**:
  - In a meeting you press Opt+N to jot something down, and nothing opens. In what you type next, each space adds a ★, each digit refiles the recording to another project, and 0 makes it "No project".
  - The theme's hint bar dropped "Space", which makes it even less visible.
- **Evidence** [meas]: `$UX/scripts/fm08_meeting_keys.cpas` → `$UX/fm08/002-opt-n.png` (★0 becomes ★3); `$UX/scripts/fm09_meeting_digits.cpas` → `$UX/fm09/002-typed.png` (#acmeshop → #cardputer → no project).
- **Code** [src]: `keymap.cpp:98-106`.
- **Fix**:
  - Unchanged: single digits changing the project is deliberate; the last one wins, and the change shows at once.
  - A8: ignored keys get a hint (U17).
  - A1: ` no longer opens the end box (U1).
  - B2: the theme's hint bar puts "Space" back.

#### U24 No dimming before the screen turns off

**Minor · occasional · small · A2**

- **What you hit**: on battery, the screen is fully bright at 59 seconds and fully black at 61, with no dimming in between. Once black, the first key only lights the screen.
- **Evidence** [meas]: `$UX/daily/t11_wake_status.cpas` → `$UX/daily/out/t11/001-w-59s.png`, `002-w-61s.png`, `005-w-semicolon.png`.
- **Code** [src]: `power.cpp:26-34`.
- **Fix** (A2, small):
  - dim 10–15 seconds before turning off; keys pressed while dimmed take effect directly;
  - once fully black, the first key still only lights the screen;
  - always-on while plugged in is unchanged.
  - If D2 is adopted, the auto-lock timer starts at the moment the screen goes fully black.

#### U25 An open-session request with an unknown result looks like an ordinary queue item

**Minor · rare · medium · A10 + B5**

- **What you hit**: you press Opt+S to open a session just as the cable is pulled. The toast "Unknown result: check board (queued)" only shows for 2.5 seconds. Once the cable is back:
  - the session is already running on the Mac;
  - the same text sits in the queue as an ordinary "open" item;
  - pressing Enter on it opens an identical second one.
- **Evidence** [meas]: `$UX/r08_real_daemon.cpas` → `$UX/refute-s08/out/contact.png`; `$UX/s08_unknown_spawn.cpas` → `$UX/out/s08/unplug_pair.png`.
- **Code** [src]: `agent_req.cpp:64` and `encode.cpp:120` send `uncertain`, but the daemon's `QueueRow` (`protocol/messages.py:114-120`), `hub/snapshots.py:101-106`, the `queue.snapshot` schema, and the device's `decode_host.cpp:127-140` and `domain.h:85-91` all drop it. PRD AC4 (docs/04:261) requires it to be marked "uncertain".
- **Fix** (medium):
  - every row of `queue.snapshot` carries `uncertain`, a new optional field (additive only);
  - mark it "open?" (开?) on the board;
  - the confirm box adds a line "May have started: see Sessions".

### 2.4 After the review

#### U26 A memo held on G0 suddenly cuts off; holding the whole time is tiring (gh #51)

**Important · daily · small · no decision needed** (feedback received after the review)

- **What you hit**:
  - (a) A memo held to talk suddenly cuts off mid-sentence, several times. In gh #51 one memo ended and the next began 0.82 seconds later: the hand never let go, but the recording broke.
  - (b) Holding G0 the whole time you speak is tiring; a single press currently takes a Note, which is of little use since you rarely type on the device; double-tapping G0 should enter a voice mode, and one more press should end it.
- **Cause** [src]: M5Unified's `Button_Class` only debounces when `update()` runs at most 10 ms apart; a loop with a full-screen redraw takes longer, so a single bad-contact sample on the side key is taken as a release, and a memo stops as soon as it is released.
- **Fix** (all done, failing tests first):
  - **Release debounce**: a press counts at once; a release only counts when two samples at least 30 ms apart (`kG0ReleaseMs`) both read "released" (`lib/input/g0_debounce`, the device's `hal_keys()`).
  - **0.6-second grace** (`kMemoGraceMs`): a held memo keeps recording for 0.6 seconds after release, with a yellow pill at the top of the page saying "Let go · hold within 0.6 s" (松开了 · 0.6 秒内按回继续) and the hint bar "[G0]hold again"; pressing G0 again during that time continues the same segment (no new file, no break); only if it is not pressed again does it end, and what was recorded during the grace is saved too. Memos held on the lock screen work the same way. The 5-minute limit is unchanged. The grace counts towards `rec_min_ms` (1 second): the audio task judges by the total length recorded, so holding about 0.9 seconds (0.5 s to start + 0.4 s) and letting go now saves a 0:01 memo, where it used to take about 1.5 seconds of holding; a 0.7-second hold is still discarded ("Too short: discarded"). This is deliberate: whatever is recorded in the grace is kept, with no extra discarding.
  - **Double-tap G0 = hands-free** (`lib/input/g0_gesture`, `kDoubleTapMs` = 350 ms): wherever "tap G0 = Note", two short presses (the second pressed within 350 ms of the first being released) start a hands-free memo. **Recording starts the moment the second press goes down**, so no word is lost; its release does not end it. The top of the page says "Hands-free · tap G0 to end" (the theme's Info pill, Mando listening as usual), and the hint bar "G0 ends, 1-9 project, 0 none, Opt+L lock" (after gh #53, "[G0]stop [1-9]project [Opt+L]lock", with 0 none in the legend). **One more G0 press ends it** (at the moment of pressing; this press's release is eaten and does not go on to open a Note), with the usual result "Recording saved · 0:42".
  - Other keys during hands-free: 1–9 / 0 set the project; Tab menu (three rows on this page: G0 stop and save, 0-9, Opt+L); Opt+L locks and recording continues; all other keys do nothing and do not end it (pocket presses). The menu row "Stop and save" (G0 结束并保存) is **only a note**: Enter on it does not end recording but says "Tap G0 to stop and save" (Tab and Enter are two stray presses and should not end it). The 5-minute limit is unchanged and saves automatically when reached ("5 min limit reached · saved 5:00").
  - **A single tap is still a Note**, but it only opens 350 ms after release if no second tap comes; pressing another key during that time opens the Note at once with that key in it, so no note is eaten. Holding from the start is still hold-to-talk at 500 ms.
  - **Tap, then hold** (the second press within the window, held to 500 ms): recording starts on the second press, becomes hold-to-talk at 500 ms, and ends with the 0.6-second grace after release. That way the lock card's old "tap G0, then hold" gesture is still hold-to-talk and never turns into hands-free by surprise.
  - The earlier experiment "press Space while holding to latch" was removed; Space on a memo is back to doing nothing.
- **Changed again after review** (2026-10-22, two review rounds, failing tests written first for each):
  - **A slow second tap still counts** (`kLateTapMs` = 1 second): with a stiff key the gap between two taps easily exceeds 350 ms. A second press between 350 ms and 1 second after the first release, with no other key in between (the Note is already open and still empty), also counts as a double tap: the Note closes and hands-free starts from that press. Beyond 1 second, G0 works as usual inside the Note (empty: "Empty: not saved").
  - **Holding G0 in an empty Note = hold-to-talk**: holding long after a tap used to record nothing and say nothing; now it closes the empty Note and records as usual (starting at 500 ms). With text in the Note, after merging into v0.4.1 it follows D4: it records a separate memo and keeps the draft (§5.7).
  - **Double tap to end works too**: a press within 350 ms of the release of the press that ended hands-free is eaten and does not open a Note (it used to depend on how fast the save was, sometimes opening, sometimes not).
  - **Hands-free started from the menu reaching 5 minutes**: it used to stay on the "Let go of [G0]" screen and only came back after a G0 press (when started from the menu G0 was not held, but the core thought it was). Now the core knows from the start that G0 is released, returns straight to Home at the limit, and Tab works immediately.
  - **Grace and pressing again in the same loop**: when a loop is long, keys are read first and only then is the grace checked, so pressing again is never one step late (`Runtime::tick_rec`).
  - **Ending while locked says so**: when hands-free ends on the lock screen, the ending press lights the card for another 3 seconds; the card's second line says "Saving…", then "Recording saved · 0:06" (green; too short is yellow "Too short: discarded"). Once the screen has gone black and is lit again, it is back to "Tap [G0]then hold · 2 taps: hands-free". Memos held while locked also leave their result on the card. After merging into v0.4.1, the result goes where the clock is (B1: "√ Saved 0:06", Mando says "Saved"), and the second line goes back to the G0 instructions (§5.7).
  - Hints: Home's hint bar "[G0]hold to talk / double tap hands-free" (after merging into v0.4.1, "[G0]hold, talk after REC" plus double tap, D8); the lock card "Tap [G0]then hold · 2 taps: hands-free"; in the menu's "Go" (去哪) list the key is named "G0 x2" (双击G0), the same width as "Hold G0" (按住G0), so the following text no longer gets squeezed; the hands-free hint bar gains "0 none".
- **Choices on the lock screen**: G0 only works while the lock card is lit, the same rule as "tap, then hold". The first tap of a double tap must be made while the card is lit (a first press on the black screen only lights the card and does not count as the first tap; after merging into v0.4.1, it also does not count when the card lit up on its own (D7), §5.7), so two presses on a black screen in a pocket never record; while locked the second tap must come within 350 ms (not relaxed to 1 second). A hands-free memo keeps recording after locking; pressing G0 while locked: with the card lit it ends; with the screen black it only lights the card and does not end (tap once to light the card, press again to end). The card says "Hands-free: [G0]ends it".
  - **Starting is harder than ending, on purpose**: from a black screen, starting takes three presses (light the card, then a double tap less than 350 ms apart), ending only two (light the card, then one press within 3 seconds). A false start could record people nearby while in a pocket; a false end loses nothing: everything recorded is saved, the card says "Recording saved", and a double tap continues. So ending stays "one press while the card is lit", matching the request that one more press should end it.
- **Key timing table** (all times are edges as the firmware sees them; see debounce below):

| Gesture | Timing | Result |
|---|---|---|
| Tap | No second press within 350 ms of release | Opens a Note (another key meanwhile: opens at once with that key in it) |
| Double tap | Second press < 350 ms after the first release, shorter than 500 ms | Hands-free memo, recording from the second press |
| Double tap (slow) | Second press between 350 ms and 1 s, no other key in between | The empty Note closes; hands-free memo recording from the second press |
| Tap, then hold | Second press within < 1 s, held to 500 ms | Hold-to-talk memo, recording from the second press |
| Second press too late | ≥ 1 s, or another key pressed in between | This press works as usual inside the Note (empty: "Empty: not saved") |
| Hold in an empty Note | Held to 500 ms | Closes the Note; hold-to-talk memo |
| Hold | ≥ 500 ms (no tap before) | Hold-to-talk memo, starting at 500 ms (as before) |
| Release during hold-to-talk | Pressed again within < 600 ms | The same segment continues |
| Release during hold-to-talk | Not pressed again within 600 ms | Ends and saves (discarded only if the total recorded is under 1 second, grace included) |
| G0 during hands-free | At the moment of pressing | Ends and saves; this press's release and hold are eaten |
| Another tap after ending | < 350 ms after the ending press's release | Eaten; nothing opens |
| G0 release debounce | Two "released" samples ≥ 30 ms apart | Only then counts as a release; one bad contact does not |

  - The 0.35-second window makes the Note capture page appear 0.35 seconds later, so the PRD's 300 ms target is relaxed to 650 ms (docs/04 AC1, §12, HIL `latency`).
  - **Debounce shifts G0's timing later**: a release counts 30 ms later at the earliest (later still in a slow loop), while a press counts at once. So the 500 ms (tap vs hold boundary) and the 350 ms window in the table are measured from "the release as the firmware sees it": a real press of 470–499 ms becomes a hold (starting a hold-to-talk, which is discarded as too short after the grace); the real double-tap window is about 380 ms.
  - **The shortest gap debounce can see is two loops**: between two presses there must be two "released" samples at least 30 ms apart. With 10 ms loops that is 40 ms; with 100 ms loops, as during a full-screen redraw or a save, it is 200 ms (the longest save in docs/07 is 195 ms). Shorter gaps merge two presses into one: usually a Note; in a slow loop the merged press may exceed 500 ms and become a hold-to-talk that is discarded as too short. It never starts hands-free by mistake. The pure test `test_g0_pipeline` verifies this at 10/50/100 ms loops; **it still needs one measurement on the real device**: double-tap 20 times each on Home, on the board and right after a save, and count the ones not recognised (docs/07 §8, not yet run). The simulator and shell tests start after debounce, and keys injected by HIL do not go through debounce either.
- **Tests**: `test_g0_debounce`, `test_g0_gesture` (tap, second press at 100/300/349/351 ms, slow second press up to 1 s, hold, tap then hold, double tap then end, the ending press's release eaten, a second press after ending eaten), `test_g0_pipeline` (from contact to gesture: double taps at various loop speeds, merged presses, a single bad contact during a hold), `test_rec_core` (grace expiry, pressing again, hands-free, Hold, limit), `test_g0_double_tap` (shell routing: Note only after the window, another key opens the Note first, slow second press, hold in an empty Note, keys during hands-free, the menu row is only a note, G0 released when started from the menu, the card lit again when ending on the lock screen), `test_runtime_rec` (real keys plus the audio task: `test_memo_grace.cpp`, and the review's `test_memo_review.cpp`), `test_lock_power` (results on the lock card); fuzzing gained G0 slips, double taps at the window edge, tap then hold, menu rows and double tap to end; simulator scripts `ux/memo_grace.cpas`, `ux/memo_handsfree.cpas`; HIL (passing on the simulator) `rec_memo_grace`, `rec_memo_handsfree`.

#### U27 While speaking you cannot see which project each of 1–9 is (gh #53)

**Important · daily · small · no decision needed** (feedback from memos)

- **What you hit**: while talking you can press 1–9 to pick a project, and it works, but you have to remember which project each of 1–9 is; it would be much better if the talking screen showed what 1–9 are; when trying v0.4.0, this hint was not there yet. The recording page only says "Save to #acmeshop": after choosing you know where it goes, but before choosing you have to remember which digit is which project.
- **Fix** (done, failing tests first; all four layouts were drawn and compared in the simulator, see docs/19 §6.6):
  - The memo page (hold-to-talk, grace, hands-free, Cue, saving) and the meeting page always show a **legend**, in the same order as the number row on the keyboard: `[1] acmeshop [2] cardputer … [0] 不归属` ("none" in English). The digit is the keycap, followed by the project key (the same name as `#key` in titles and on the board); `0` says "none" (不归属), the same word as in the hint bar.
  - **The current one is highlighted** (in the style of the old "Save to" live-words pill), and it switches the moment you press a digit; pressing a digit with no project highlights `[9] ?` in the slot reserved after `[0]` on the memo page (the recording is still tagged #9, U13), and the meeting page title says `#9`. The old "Save to #key" line is gone; when the selected key is truncated, the memo page's title line says "Save to #full-key" (存到 #完整的键).
  - **No extra presses, nothing moves by itself**: no timers, no paging; **pressing a digit only changes which one is highlighted; the legend's number of lines, truncation and the position of every entry stay the same** (in review, with 6 projects, pressing 8 made the legend jump a line and the waveform shrink; fixed); the lock screen and the Tab menu are unchanged. The hint bar dropped "0 none" (the legend says it right above, and two copies stacked looked like a drawing bug).
  - **With many projects it takes an extra line, keeping keys whole first**: on the memo page your two projects fit on one line (below Mando's sign); three 12-letter keys take two lines; 5–6 projects take three lines, with the waveform at 2/3 height and still 35 bars, keys all whole. Only when three lines are not enough are keys truncated: all keys are cut to the same width, followed by two small dots to show truncation (9 keys of 12 letters: 6 letters each). On the meeting page, one line below the timer; if that is not enough, the line below the timeline with `0:00 现在` ("now") is used; with 9 long keys `0` is only the keycap `[0]` (the title says "No project"), and each key gets 3 letters plus two dots, the tightest case.
  - **Digits stand out more**: the digit on an unlit keycap uses Ink (it used Ink2, dimmer than the key after it).
  - **Recording untouched, screen not slowed**: the legend is only drawn on full redraws; animation ticks only ask how much room it leaves and never draw over it; SPI traffic is the same or less; a full redraw costs 40–110 µs more on the host, about 2 ms on the device (docs/19 §7.5).
- **Tests**: `test_rec_legend` (0, 1, 2, 3 long keys, 5 and 9 projects, 12-letter keys and Chinese names, truncation between characters with two dots when truncated, the selected one highlighted, 0 is none, a digit with no project is ?, with 0–9 projects no digit moves the legend, a truncated selected key written in full in the title, every memo state present, ticks never touch the legend), `test_rec_overlay` ("Save to #cpa" becomes the highlighted cpa in the legend); simulator `ux/memo_legend.cpas` (also included in `test_theme_sim`'s palette and corner checks), `test_anim_cost` gained memos and meetings with 9 projects; `test_runtime_budget` gained `memo_us` and `memo_tick_us` (< 50 ms, real device still to run).
- **Changed after review** (2026-10-22, two reviews, `feat/memo-legend`): the hint bar no longer repeats 0 none; digits do not move the legend; keys that need no truncation are not truncated (find the fewest lines with no truncation first); truncated keys get two dots, and a truncated selected key is written in full in the title; the meeting page's second line at y 109; unlit digits in Ink; lines starting from the left begin at x 7; the `legend_plan` fallback uses tight spacing; the stack budget gained three legend call chains. Not done: a save toast saying "Saved to cardputer" (needs the project carried into `RecEffect`, separate work), and caching the legend's area in ticks (a tick only costs a few more microseconds).
- **Still to do**: a look on the real device (readable at arm's length, highlight changes promptly on a digit); whether to show project names instead of keys (names often get cut mid-word and fewer fit on a line in Chinese, so keys for now).

## 3. For you to decide

These changes touch keys or behaviour you already know. If you do not reply, the defaults are used.

> **Decided 2026-10-22**: D1–D10 all use the recommended defaults. All ten were done with the defaults below and shipped with the Mando theme as v0.4.1 (D10); implementation and tests in §5.6.

1. **D1 Single letters on Home** (U5, U11): R and L on Home need Opt (Opt+R meeting, Opt+L lock); the other tiles stay single keys. **Default: agree**. Alternative: a non-tile letter on Home starts a Note directly ("type to note", medium effort).
2. **D2 Auto-lock** (U2): **default: on battery, lock as soon as the screen turns off (60 seconds); no auto-lock while plugged into the Mac**. Cost: typing a note from the black screen goes from one G0 press to Opt+L, Enter, G0. Speaking a memo is still "tap, then hold". Alternatives: lock only 5 minutes after the screen turns off; or no auto-lock, but apply the lock screen's G0 rules to the unlocked black screen too.
3. **D3 Meeting confirm boxes** (U1, U23):
   - Opt+L on the start box means "Cancel and lock", on the end box "Keep recording, lock";
   - the start box cancels after 15 seconds untouched, the end box returns to the meeting page after 10 seconds untouched;
   - typing on the start box cancels;
   - ` on the meeting page only shows a hint and no longer opens the end box.

   **Default: agree**. This changes the "deal with the confirm box first" cell in docs/13 §5.3.
4. **D4 Holding G0 under overlays** (U4): as long as nothing is recording, holding G0 with the editor, a confirm box or a menu open records a memo and keeps the draft, the same as on the lock screen. **Default: agree**. Alternative: show a hint only, do not record.
5. **D5 The editor's back key** (U10): ` in the editor becomes exit (asking "Discard?" when there is text), and the backtick character is typed with Fn+`. **Default: agree**. "Parking drafts" (exit without losing, restore next time) is **not done by default**.
6. **D6 ` on confirm boxes** (U9): on the open-session and reply confirm boxes, ` goes back to the editor with the text kept. **Default: agree**. Cost: discarding the whole thing goes from 1 press to 3.
7. **D7 When to light the card while locked** (U6, U11):
   - 2 seconds at the moment of locking ✓
   - 2 seconds when recording ends (including automatic stops), showing the result ✓
   - while plugged into the Mac, 3 seconds whenever the Inbox count goes up, showing only the number ✓
   - Enter or Space lights the card while locked (rate-limited) ✗

   **Default: do the first three, not the last**, keeping docs/13 §7.1's "random presses in the pocket never light the screen".
8. **D8 Memo pre-roll** (U8): the microphone starts as soon as G0 is pressed; it only counts as recording after 0.5 seconds, and is dropped otherwise. Cost: every G0 tap for a note opens the microphone for about 0.5 seconds (the content is not saved); and it needs real-device verification. **Default: only change the hints for now and show "● Wait" (● 准备) on press; pre-roll waits for your go-ahead**.
9. **D9 Key combinations with a menu open** (U17): with a menu open, pressing a combination listed on the menu (e.g. Opt+L) closes the menu and runs it. **Default: agree**. This changes docs/13 §5.4 step 3, "an open menu eats every key".
10. **D10 Release order**: **default: package A (§5.1) ships first as v0.4.1 "steady", without waiting for the theme; package B ships with the theme; the originally planned v0.4.1 (recording history and todos) moves to v0.4.2**. Alternative: merge package A into the theme release as well.

**Small changes done by default, no decision needed**:
- Fn+arrows work;
- dim before the screen turns off;
- toasts jump the queue;
- accurate hints for empty slots;
- lists track the cursor by item;
- Home's most urgent item: idle ones newest first, anything over a day folded away;
- a guard on the discard prompt, and "back to editing" selected first in the menu;
- consistent wording.

If you disagree with any of them, just say so.

## 4. Stability

### 4.1 What was tested

| Test | How | Scale | Result |
|---|---|---|---|
| Run-time fuzzing (`$UX/stab/src/fuzz.cpp`, with UBSan, bounds checks and stack protection) | The firmware's real `Runtime` with the simulator's fake hardware; a script plays the daemon: handshake, heartbeat, snapshots, acks, agent results, pulling recordings with sha256 checks, cable pulls, daemon restarts, stalls, junk lines, `debug.reboot`; randomly plays the user: single keys, combinations, three-key ghosts, G0 taps and holds, Tab, Opt+L, holding Backspace. After each stretch, it returns to Home by "look at the screen, take a step" | 13 seeds: 8 with an SD card, about 45 minutes each; 3 with internal flash, 30 minutes each; 2 with no storage, 30 minutes each. In all about 8.7 hours, 375 stretches, 31,924 key presses, 31 restarts, 156 cable pulls, 119 stalls, 117 daemon restarts, 114 junk lines, 783 G0 holds, 349 meetings (`$UX/stab/out/matrix3.txt`). Plus 20 SD seeds totalling 26.5 hours just to count G0 holds (`$UX/stab/out/g0_hold_aggregate.txt`) | **No findings at all**: no out-of-bounds access, no hangs, not one capture lost (620 into the outbox, all 620 present), 345 recordings of about 94 MB pulled with all checksums correct; returned to Home every time, 1.4–2.6 presses on average, at most 5 |
| Soak test with a real cpad (`$UX/stab/soak.py`, `cpa-sim serve` plus the real cpad) | Repeat everyday flows: notes, memos, meetings, queuing, opening sessions, locking, navigation, cable pulls | Seeds 11 and 12, 32 minutes each, 342 plus 349 flows | All 186 plus 161 captures reached the Mac, 120 plus 150 wav files, zero cpad errors, back to Home every time (`$UX/stab/out/soak-11.txt`, `soak-12/result.json`). The run **without registered projects** (seed 7) lost 6/13; that is U13 |
| Pocket fuzzing (`$UX/daily/gen_pocket_fuzz.py`) | 200 pocket-style random presses per run, 17% of them G0, about 2 seconds apart | 8 unlocked, 8 locked, 4 during a meeting | Unlocked: 6/8 left junk notes; locked: 0/8 (U2) |
| Unlocked random presses (`$UX/fuzz.py` → `$UX/out/fuzz_run.txt`) | 40 runs of 120 presses in a row | — | 41 notes, 3 sessions opened, 9 recordings, 3 dismissals in all; 39/40 back to Home within 3 rounds (seed 24 was a problem with the test tool itself) |
| Lock-screen fuzzing (`$UX/lockfuzz.py` → `$UX/out/lockfuzz_run.txt`) | Walking, sitting and mixed postures, 3 minutes each | 157 / 49 / 51 presses | Walking 0 recordings, sitting 11, mixed 7; the device stayed locked (U3a) |
| Random keys on Home (`$UX/fuzz/fuzz.py 300 12 20261022`) and phrase test (`$UX/fuzz/phrases.py`) | 300 runs of 12 random keys; 14 everyday phrases | — | 4 meetings, 19 locks, 107 stuck in the editor; 12 of 14 phrases had unintended effects (U5, U10) |
| Fault injection (`$UX/stab/.pio/build/fuzz/program 1 finishing`) | Drop the audio task's "recording finished" event once | 1 run | Hangs, needs a restart (§4.3) |

### 4.2 What was found

Overall no crashes, out-of-bounds access or data corruption. The stability problems are all "pressed, nothing happened, nothing said":

- **st01** an empty slot reports E_STORAGE, and the draft sucks in later captures → U13.
- **st02** holding G0 under overlays, 0/367 recorded → U4.
- **st03** the race in the lock card's last 500 ms → U3b.
- **st04** toast queue lag → U20.
- **s17** two long presses bypass the lock → U3a.
- **s02 / s03** cursor by position → U12.
- **Pocket** no auto-lock → U2.

### 4.3 Fault injection: the recording finish event is lost and the device hangs (S1, blocking if it happens)

- **How**: drop the audio task's finish event once. After a memo is released, the recording state stays in the "finishing" phase (Finishing).
- **Result** [meas]:
  - Opt+H, Fn+`, `, Tab, Enter, Opt+L, G0 tap, G0 hold, Opt+N and Opt+R **all do nothing**;
  - the same after 10 minutes, and after unplugging and replugging;
  - only a restart recovers. After the restart the card has 1 file; the recording was not lost.
  - Evidence: `$UX/stab/out/fin2/report.txt`, screenshots `$UX/stab/out/fin2/s1-T7730-fin-1-released.png`, `s1-T610490-fin-2-after-10min.png`, `s1-T619990-fin-3-after-reboot.png`.
- **Cause** [src]: `rec_tick` in `rec_core.cpp:181-183` only runs in the recording and end-box phases; the "finishing" phase has no timeout at all.
- **Can it really happen** [todo]: only if the audio task really drops the finish event. It never happened in 783 natural G0 holds and 349 meetings, nor on the real device. But if it does, it means "restart needed"; and since this device makes no sound, stuck here you would just think it had died.
- **Fix** (A12, small):
  - if "finishing" lasts over 5 seconds, send stop again;
  - after another 5 seconds, finish as a failure, return to idle, and say "Didn't close: parts are on the card" (录音收尾超时：已写下的段还在卡上);
  - verify once on the real device with a `debug` hook.

### 4.4 Regression tests to add for each stability issue

| Issue | Test | Assertion |
|---|---|---|
| (status) | All added (`feat/ux-stable` added U12, U13, U20, U6, S1, plus U15's `ux/memo_cap`, `ux/memo_cap_battery`, U25's `ux/unknown_spawn`, U20's `ux/locked_backlog`; `feat/ux-decisions` added U1–U4, U9, test names in §5.6); U16 is a device gate, not run yet | |
| U1 start and end boxes | New cases in `test_rec_core`; simulator script `ux/meeting_boxes.cpas` (adapted from `$UX/refute_t02/r2_habit.cpas` and `$UX/refute_t02b/sweep_300.cpas`) | An Enter at 300 ms produces the "press again" effect; the start box returns to idle after 15 seconds; the end box returns to recording after 10 seconds; Opt+L on the start box → idle and locked; Opt+L on the end box → keep recording and locked; a letter on the start box → idle; during the start box the backlight may turn off and `file.*` does not answer busy |
| U2 auto-lock (D2) | `test_lock_power` | On battery, after the screen turns off `locked == true`; no lock when plugged in; after locking on the black screen, holding G0 does not record |
| U3a two long presses | `test_lock_power`, replacing lines 109-122 | After a swallowed long press on the black screen, another long press within 3 seconds does not start recording; a short press followed by a long press does |
| U3b card race | `test_lock_power` | The card expires 300 ms after G0 is pressed and the long-press event arrives after expiry: recording still starts |
| U3c card goes dark while holding | `test_lock_power` | While `eat_up` is true, `lock_tick` does not turn the card off |
| U4 holding G0 under overlays (D4) | `test_shell_dispatch_order` replacing lines 151-172; simulator script `ux/g0_hold_overlays.cpas` (from `$UX/stab/scripts/st02_g0_hold_overlays.cpas`) | Holding G0 under the editor, a confirm box or a menu starts a memo, and afterwards the overlay and draft are intact |
| U12 cursor | New cases in `test_mod_inbox`, `test_mod_board` | After a snapshot moves the selected row, Opt+D and Enter still act on the original sid or qid; once it disappears, they do nothing |
| U13 empty slot | New `test_compose_*` cases; simulator script `ux/unknown_slot.cpas` (from `$UX/stab/scripts/st01_queue_unknown_slot.cpas`) | Queuing or opening a session to an empty slot returns NeedProject, never E_STORAGE; a digit for an empty slot does not open a page |
| U20 toasts | New case in `test_toast` | With 2 "needs you" toasts queued, the result of your own action is shown within 600 ms |
| U6 results while locked | New case in `test_runtime_rec` | An automatic stop while locked lights the card, and the stop reason is kept until the next time the screen is lit |
| U9 confirm cancel | `test_confirm_*` or a shell case | Cancel on the open-session confirm box returns to the editor with the text unchanged |
| S1 finishing timeout | New case in `test_rec_core` | With no finish event for 10 seconds in "finishing", return to idle with a failure effect |
| U16 SD pull and reinsert | Device gate [todo] | Pull, reinsert, save: the notice says "reseat it, restart"; after restart the draft is still there (if draft persistence is done) |
| U27 1–9 legend while speaking | `test_rec_legend`, `test_rec_overlay`; simulator script `ux/memo_legend.cpas`; `test_anim_cost` (9 projects); `memo_us` in `test_runtime_budget` [todo real device] | The memo and meeting pages always have a 1–9 / 0 legend in number-row order, with the selected one highlighted and changing on press; when it does not fit, one more line, all keys cut to the same width, no "…"; areas pushed by ticks never touch the legend |
| U26 memos cutting off, double tap for hands-free | `test_g0_debounce`, `test_g0_gesture`, `test_g0_pipeline`, `test_rec_core`, `test_g0_double_tap`, `test_runtime_rec`, `test_lock_power`, `test_g0_decisions` (where it meets D1–D10, §5.7); simulator scripts `ux/memo_grace.cpas`, `ux/memo_handsfree.cpas`; HIL `rec_memo_grace`, `rec_memo_handsfree`; double-tap count on the real device [todo] | One bad-contact sample does not count as a release; pressing again within 0.6 s of release is the same segment and one file; with a double tap recording starts on the second press, its release does not end it, one more press ends it without opening a Note (and a double tap to end does not open one either); a slow second press (up to 1 s) closes the empty Note and then starts; a single tap opens a Note only after 350 ms; hands-free started from the menu does not get stuck at the limit; while locked, G0 only works with the card lit, and the card shows the result after ending |

### 4.5 Keeping the fuzzing

Move `$UX/stab/src/fuzz.cpp` into `firmware/sim/test/test_fuzz_runtime`; CI runs 2 seeds of 3 minutes each every time, and the full matrix nightly. Invariants:
- from any state, at most 6 presses back to Home;
- no hangs;
- not one capture in the outbox lost;
- all shas correct when pulling recordings;
- after U4 is fixed, holding G0 in any state that is not recording always records;
- no meeting recording ever starts except from "an Enter pressed at least 400 ms after the box appeared, with no other key in between" (guards against U1 and U5).

### 4.6 What was not tested

- Real keyboard scan timing and ghost keys: the simulator presses everything in one frame, the real device one key at a time.
- Pulling and reinserting the SD card at run time: the simulator does not support it.
- The memo's "● Wait" phase: the simulator's recording port is synchronous, so it cannot be measured.
- The battery curve under load (G-A7 not measured yet).
- Real pocket press frequency.
- Whether the audio task really drops events (S1).

These all go into the device gates in §5.5.

## 5. Work packages

### 5.1 Package A: logic and interaction, no drawing (suggested as v0.4.1 "steady")

The theme did not change any of these files: `keymap.cpp`, `chord_detector`, `shell.cpp`, `compose.cpp`, `confirm.cpp`, `lock.{h,cpp}`, `power.{h,cpp}`, `rec_core.*`, `rec_engine`, `toast.*`, `inbox_logic.*`, `menu_model.cpp`, `action_help.cpp`, `text_fit.cpp`, `runtime_outbox.cpp`, `runtime_link.cpp`, or the daemon. So package A can be merged into main first, and the theme rebased afterwards. Items marked ⚠ in the table touch files the theme also changed; take care when merging.

| Package | Content | Main files | Tests | Size | Decision | Status |
|---|---|---|---|---|---|---|
| UX-A1 meeting boxes | All of U1; typing on the start box cancels (U5); ` on the meeting page only hints (U23) | `rec_core.{h,cpp}`, `keymap.cpp`, `rec_key_event` in `shell.cpp`, `power.cpp`, `runtime_files.cpp` (after `rec_live` is split) | §4.4 U1 | Small | D3 | Done |
| UX-A2 lock and black screen | U2 auto-lock; U3 a/b/c logic; U11 light the card on locking and when the Inbox count rises; U24 pre-dim | `lock.{h,cpp}`, `power.{h,cpp}`, ⚠`runtime.cpp` (the theme added animation; the lock gate is at lines 157-171) | §4.4 U2, U3 | Small | D2 D7 | Done (device gate H1 not run) |
| UX-A3 holding G0 under overlays | U4 | Dispatch in `shell.cpp`, `keymap.cpp`, ⚠`shell_menu.cpp:146` | §4.4 U4; change `test_shell_dispatch_order` | Small | D4 | Done |
| UX-A4 Home letters | U5: R and L need Opt; a hint once for non-tile letters | ⚠`home_logic.{h,cpp}` (the theme only added functions; `home_next` untouched) | `test_mod_home` | Small | D1 | Done |
| UX-A5 editing and confirming | U9 ` back to the editor; U10 the editor's back key; U18 discard guard and menu order | `compose.cpp`, `confirm_key_event` in `shell.cpp`, the edit layer in `keymap.cpp`, ⚠`kDiscardRows` in `shell_menu.cpp` (the theme only changed the slide animation) | Change `test_compose_limits_and_dash_guard:154-157`; add a confirm-cancel case | Medium | D5 D6 | Done |
| UX-A6 select by item | U12 | `inbox_logic.*`, ⚠`board_logic.*` (the theme only added functions); ⚠ two calls at `board_module.cpp:145,163-164` changed to look up by id (the theme rewrote much of this file; only these two lines are touched); daemon `cpa queue reopen` | §4.4 U12 | Medium | — | Done |
| UX-A7 project targets | U13 | `compose.cpp`, `shell.cpp` (opening projects, `submit_compose`), `toast.cpp`, ⚠`board_logic.cpp:40` | §4.4 U13 | Small | — | Done |
| UX-A8 feedback and wording data | U7 confirm wording and the width test; U17 hints for refused keys, Fn+arrows; U20 toasts jumping the queue; U21 status bar explanation on the Control page; U22 glossary, age function, Opt+1-9 | `confirm.cpp`, `toast.*`, `shell.cpp`, `keymap.cpp`, `menu_model.cpp`, `action_help.cpp`, `text_fit.cpp`, `compose.cpp` | Change `test_confirm_execute_vs_queue:170`; add a glossary test, a 240 px width test, `test_toast` | Small | D9 (menu pass-through is in B6) | Done (menu pass-through done too) |
| UX-A9 recording state | U6 stop-reason state, the 3.55 V warning line, light the card when recording ends while locked; U15 stay on the notice at the limit until released; U16 notice for locked-up storage, "insert, restart", drafts saved to cpafs | `rec_core.*`, ⚠`runtime_rec.cpp` (the theme added a brief "Saved" animation), `runtime_outbox.cpp`, `lock.cpp` | §4.4 U6 | Small (draft persistence medium) | D7 | Done |
| UX-A10 Mac side and protocol | U14 drop the repeated key prefix, Chinese names; U25 carry `uncertain` all the way; U12's `reopen` | daemon `agents/claude_code/argv.py`, `protocol/messages.py`, `hub/snapshots.py`, the `queue.snapshot` schema (only a new optional field), device `decode_host.cpp`, `domain.h`, `runtime_link.cpp:141` | daemon unit tests; schema tests | Medium | — | Done |
| UX-A11 memo pre-roll | U8 | `audio_task`, `rec_engine`, `rec_core` | New unit tests plus a device gate | Large | D8 | Not done (D8: hints only, no pre-roll) |
| UX-A12 stability guards | S1 "finishing" timeout; fuzzing in CI (§4.5) | `rec_core.cpp`, `firmware/sim/test/test_fuzz_runtime` | §4.4 S1 | Small plus medium | — | Done (device gate H5 not run) |

**Order**: first A1, A2, A3, A4 (false recordings and missed recordings), then A5, A6, A7, A8, A9, A12; schedule A10 with the Mac side; A11 waits for D8. Within each package, failing tests first (RED), then the change (GREEN). Changes to old v0.1–v0.4 tests are listed one by one in the commit message, per the rule in docs/13 §17:
- `test_lock_power:109-122`
- `test_shell_dispatch_order:151-172`
- `test_recording_gestures_ignored_under_compose_and_confirm`
- `test_confirm_execute_vs_queue:170`
- `test_compose_limits_and_dash_guard:154-157`

### 5.2 Package B: things drawn on screen, done with the Mando theme

These all land in files the theme rewrote (`lock_screen.cpp`, `rec_overlay.cpp` and the new `rec_pages.cpp`, `status_bar.cpp`, `shell_render.cpp`, `menu_view.cpp`, `home_module.cpp`, `board_module.cpp`, `inbox_module.cpp`). Do them on top of the theme once it is merged, or hand them to whoever does the theme.

| Package | Content | Depends on A | Status |
|---|---|---|---|
| UX-B1 lock card | On locking, "Locked · how to unlock" for 2 seconds; while holding G0, large "Not recording: let go, tap, then hold"; recording result line; automatic stop reason line; "Inbox N". Mando must not use the napping look for "not recording", since napping reads as "all is well" | A2, A9 | Done |
| UX-B2 recording page | "● Wait" as soon as G0 is pressed; countdown from 4:30, staying on the notice at the limit; the meeting page keeps "Bat N%"; below 30% the confirm box says "about N min"; the confirm box only draws Enter after 400 ms (or flashes "press again"); remove the duplicated key row; put "Space" back in the meeting hint bar; "insert, restart" | A1, A9 | Done |
| UX-B3 status bar | Width budget, zero values not drawn; ※k for 10 seconds only; battery yellow below 30%, red below 17%; ※SD; "N segments"; "✓ synced" | A8, A9 | Done |
| UX-B4 Home | The R and L tiles say Opt; offline, the greeting and the most urgent item switch to offline text and Mando to an offline look; the order of the most urgent item and an "idle" mark; G0 hint "speak once you see ● REC"; mark R and G0 when recording is impossible | A4 | Done |
| UX-B5 Inbox and board | Per-row hint bar (deliverable or not); full names, repeated prefix removed; the confirm page shows the end of the name plus a short id; the "open?" mark; "[`]back" added to the board overview; no Opt+D when offline | A6, A8, A10 | Done |
| UX-B6 wording and layout | The confirm box's hint bar and body use the same verb; "[`]edit" (` 返回修改); the editor breaks lines at spaces; key combinations pass through with a menu open (D9); Enter on a note row gives a hint; a selected note row must still look like a note; the 240 px width test also covers the theme's pill toasts (which are narrower) | A5, A8 | Done |

### 5.3 Where the theme makes problems more visible, or already fixes them

- **More visible**:
  - Offline, the greeting says "Hello, all done", and Mando looks exactly the same offline and online (U19).
  - The recording page dropped "Bat N%" (U6).
  - The meeting hint bar dropped "Space" (U23).
  - The status bar's ※k completely covers "R N" (U21).
  - Mando napping on the lock card reads as "all is well" (U3c).
  - On the lock card "待传 N" (waiting N) drops the "录" (R), giving the same thing yet another name (U21).
  - Pill toasts are narrower and truncate more (U7).
  - A selected note row is no longer dim (U17).
  - The theme's Home hint bar dropped "[Tab]menu", but Home has a Tab tile, so it works fine; only the sentence in docs/13 §5.6 needs changing.
- **Already fixed or better**:
  - Hint bar keys are drawn as keycaps, so English no longer sticks to Chinese (U22).
  - The memo page has a large breathing ● and "Listen" (在听) (U8 can use it directly).
  - Board cards show full project names (the board part of U14).
- **The theme's baseline**: the theme first merged main (v0.4.0), and on 2026-10-22 it was merged with package A and the stability fixes on `integrate/v041` (stability #47 first, then package A #49, then the theme #45). The trade-offs in that merge are at the end of §5.6.

### 5.4 Order

1. A1–A4 → v0.4.1 release candidate → device gates H1, H2.
2. A5–A9, A12 → v0.4.1.
3. Merge the theme (`integrate/v041`) → the rest of B1–B6 ships with the theme release.
4. Schedule A10 with the Mac side.
5. A11 waits for D8 and real-device verification.

### 5.5 Device gates (👤 schedule with you in advance)

- **UX-H1 carry it for a day** (about 5 minutes of setup, plus a day): do not lock on purpose (D2); in the evening count how many notes or recordings on the Mac you did not mean to make (target 0); while locked, sit down and stand up 10 times each, with no recording (U3a).
- **UX-H2 one real meeting** (about 10 minutes): start with "Opt+R, Enter, Opt+L" at your usual speed and end with "Opt+R, Enter", confirming both take effect; below 30%, check the battery hint on the confirm box.
- **UX-H3 pull and reinsert the SD card** (about 5 minutes): pull the card while running, reinsert it, save one item and read the notice; after a restart, check whether the draft is still there.
- **UX-H4 pre-roll** (only if D8 is agreed, about 10 minutes): start saying "一二三四" (one two three four) as you press; the transcript must contain "一" (one).
- **UX-H5 fault hook** (done by development): drop one finish event with a `debug` hook and confirm it returns to idle after 5–10 seconds (S1).

### 5.6 Where things stand (2026-10-22)

- **First round, branch `feat/ux-stable` (#49)**: A6, A7, A8, A9, A10, A12, U24 from A2, U18 from A5. Regression scripts in `firmware/sim/scripts/ux/`: `unknown_slot`, `queue_cursor`, `toast_order`, `memo_cap`, `unknown_spawn`, plus `memo_cap_battery`, `locked_backlog`. Fuzzing went into CI (`test_fuzz_runtime`, 2 seeds of 3 simulated minutes each per run, about 3 seconds; `fuzz-nightly.yml` runs the full matrix nightly). Nothing covered by D1–D10 was touched at that point.
- **Second round, branch `feat/ux-decisions` (v0.4.1, based on `integrate/v041`)**: D1–D10 were all set to their defaults, so A1–A5, A9's lit card while locked, and B1–B6 were all done:
  - D1: R and L on Home are no longer single keys; use Opt+R, Opt+L (or select the tile and press Enter); the tiles say Opt; pressing a non-tile letter shows how to take a Note, once per visit to Home.
  - D2: on battery, lock as soon as the screen turns off (60 seconds), without lighting the card; no auto-lock while plugged into the Mac. U3 was done with it: only a G0 tap lets the card record, two long presses do not; whether it records is decided at the moment of pressing; the card stays lit while G0 is held.
  - D3: the meeting start and end boxes got their own key layer: Opt+L on the start box cancels and locks, on the end box keeps recording and locks; the start box goes away after 15 seconds, the end box after 10 seconds without a key (start cancels, end returns to the meeting page); on the start box anything other than Enter and Tab cancels; the start box does not count as recording (the screen can turn off, the Mac can pull recordings); on the meeting page ` and Enter only hint "In a meeting: Opt+R ends it"; an Enter within 400 ms says "Too fast: press Enter again" instead of being silently swallowed.
  - D4: holding G0 with the editor, a confirm box, the discard prompt or a menu open records a memo (when not already recording), leaving the draft and box as they were; holding G0 on either meeting box says "Answer the box first".
  - D5: ` in the editor exits (asking "Discard?" when there is text), and Fn+` types a backtick; no draft parking.
  - D6: on the open-session and reply confirm boxes, ` goes back to the editor with text and target kept; the box says "[Enter]start [`]edit". Confirm boxes for queue items opened from the board (with no draft behind them) still cancel.
  - D7: light the card for 2 seconds at the moment of locking ("Locked"); when recording ends while locked (including automatic stops), light it for 2 seconds with the result; while plugged into the Mac, light it for 3 seconds with only the number whenever the Inbox count rises; Enter or Space while locked does not light it.
  - D8: no pre-roll; as soon as G0 is pressed (when it will record), show "● Wait"; the Home hint says "speak once you see ● REC".
  - D9: with a menu open, a key combination listed on the menu closes the menu and runs it; combinations not on the menu are still eaten.
  - D10: shipped with the theme as v0.4.1; the device UI of the original v0.4.1 "recording history and todos" moved to v0.4.2 (docs/13 §15).
  - More regression scripts: `home`, `home_letters`, `autolock`, `lock_card`, `lock_gestures`, `meeting_boxes`, `editor_back`, `g0_hold_overlays`, `menu_chords`, `rec_pages`, `status_bar`, `inbox_board`, `copy_layout`.
- **Not done yet**: UX-H1, H3 and the human-speed H2 from §5.5, which need a person, have not run (H1 and H2 were meant to run before v0.4.1 shipped; on 2026-10-22 v0.4.1 shipped without waiting for them, and H1 and H2 moved to after the release, see the known issues in docs/releases/v0.4.1.md); A11 pre-roll is not done per D8, so H4 is not needed. On 2026-10-22 an agent ran H2's key timing and H5 with scripts on the real device, and verified D2 by simulating the battery with a temporary probe firmware (docs/07 §8.4).
- **Changed again after the first review** (another verification round found 13 issues; each got a failing test first, then the fix):
  1. Toasts that piled up while locked or dark: after the screen lights, only the latest recording result and errors are kept, and results of your own actions after that go in front of them (U20 came back after U6); `test_toast`, `ux/locked_backlog`.
  2. The "pick a project first" hint names the keys and fits on one line: "Opt+digit: 1 acmeshop · 2 cardputer", and "… (9 in all)" when it does not fit.
  3. The age on the right of Inbox rows uses a compact form ("45分钟"), no longer saying terminal/background (the detail row has it), and the name width goes back to v0.4.0; the board's session rows use the compact age too.
  4. The hint for Enter on a note row becomes "Tab closes the menu, then Opt+1-9" / "then hold G0", since an open menu eats key combinations (D9 was undecided at the time); fact rows (battery, SD, status bar explanation) give no hint.
  5. When the selected item is gone, Enter and Opt+D say "That row is gone: pick one with ; ." (Inbox and board).
  6. The memo limit page spells out the order: "Longer? Let go, then Opt+R" (要录更久：松开 G0，再按 Opt+R 开会).
  7. The board's "Updated" also uses the common age: "Updated 2 d ago".
  8. The confirm box for an open-session request with an unknown result keeps "New session in acmeshop", with "May have started: see Sessions" on a separate line (yellow).
  9. After the S1 timeout, a late finish event from the old recording cannot end the next one: each start carries a code (gen), the audio task writes it into that recording's Started and finish events, and events with the wrong code are ignored; a Started received while idle triggers a stop.
  10. On battery, the screen at the 5-minute limit with G0 still held stays on (dimming like during recording, never turning off); a recording result only counts as "seen" when the screen is fully lit.
  11. An SD card filling up during recording counts as an automatic stop with the recording saved: "Meeting stopped: SD full · saved …".
  12. `cpa queue reopen` records include the original item, and the queue fold no longer remembers closed items (20,000 items back from 4.5 seconds to linear).
  13. The fuzzing S1 case confirms that after the timeout another segment really is recorded, plus a "late finish event" case.
- **Trade-offs in the merge with the theme** (`integrate/v041`, 2026-10-22; rule: drawing follows the theme, logic, behaviour and wording follow package A and the stability fixes):
  1. The theme's pill toast leaves only 224 px for text (`theme::kToastTextW`), while package A was written for 240 px. `test_text_width` now draws every fixed toast with the theme's `toast_bar` and requires the whole sentence to be drawn (B6). Three texts changed for this: the reason "No Claude Code on the Mac" became "No Claude Code"; when a reply cannot find its project it says "Project not on device: reply on Mac"; when the project list does not fit it says "… 9 in all" (counted by the pill's width). Confirm box titles still fit one line of 240 px.
  2. The board's tab goes back to `Sessions(N)` and "To Sessions": U25's "see Sessions" refers to it, and sessions have only one name across the device (U22).
  3. The status bar explanation on the Control page only covers what the status bar draws (↑N always; !N, R N and ※kN when not 0), and the explanation lines up in one column with the values of facts like sound and battery.
  4. When a memo reaches the limit with G0 still held, Mando's jump only starts after G0 is released and Home is back.
  5. Home's "S new session" tile: text that does not fit keeps 2 px from the tile's rounded corner.
  6. With the theme's list rows (a colour bar on the left, 8 px inset on each side), Inbox row names fit about 22 characters, two fewer than the 24 package A wanted; the inset is the theme's drawing and was not changed.
  7. HIL's `Device.home()`: the stability fixes' "`, Fn+`, Enter" ran into U18's 400 ms discard guard, and the Enter was eaten; it now waits 600 ms before Enter.
- **Review after D1–D10 were done** (another round from the user's point of view found 9 issues; each got a failing test first, then the fix; regression scripts in parentheses):
  1. When a held G0 was swallowed while locked, the moment it was released the card flipped back to the 10-minute-old "√ Saved 0:02" and Mando jumped saying "Saved"; on a device with no sound, that reads as "what I just said was saved" (U3c in a new form). Now "Not recording / let go, tap, then hold" and a head-tilted Mando stay until the card turns off or another key is pressed; a card that has answered a hold also drops the old result (its toast still waits to be seen after unlocking), and a tap then shows the clock (`ux/lock_after_hold`).
  2. The Tab menu on confirm boxes said "Enter confirm / ` cancel" while the box said "[Enter]start [`]edit", two names for one key (exactly what U22 and B6 were meant to remove); the meeting start box's menu also said "Enter confirm". Now menu rows follow the box's wording: Enter start / save as queued / close; ` and Fn+` say "back to edit" when there is a draft behind, "cancel" when there is not; the start box is Enter start, ` cancel, Opt+L cancel and lock (`ux/confirm_menu`).
  3. The lock card said "Inbox 0" even with no session data (booted away from the Mac), and after a long time offline it showed the cached "Inbox 1" without saying offline; Home, with the same data, says "No Mac yet" and "Offline · as of N ago" (U19: unknown is not 0). Now the chip is not drawn when there is no data; when the data dates from before the Mac disconnected, the top of the card says "Locked · offline" and the chip is no longer yellow ("offline Inbox N" on the chip would squeeze R N and ↑N off the card) (`ux/lock_need_chip`).
  4. "Answer the box first", shown when G0 was held on the start box, stayed on Home for about 2 seconds after the box was gone. Now however the box goes away (`, Opt+R, Enter, Opt+L, 15 seconds), its hint goes with it (`ux/box_hint`).
  5. HIL's `latency` scenario still closed the empty editor with Fn+`; after D5, Fn+` types a backtick, the editor does not close, and the first step timed out. It now uses `; `harness/keys.py` presses Fn+` to type the backtick character.
  6. The end box's 10 seconds counted from opening and ran out even with the Tab menu open; back on the meeting page, the Enter meant to end the meeting landed on the meeting page and said nothing, and the meeting recorded on to the 3-hour limit (exactly what U1 is meant to prevent). The start box likewise: after looking at the menu for a while, Enter landed on Home and opened the selected tile. Now both boxes count from the last key press (any key, including ones the menu eats), and do not run while a menu is open; Enter on the meeting page also says "In a meeting: Opt+R ends it" (`ux/end_box_menu`).
  7. While locked, holding G0 (the swallowed kind) kept the card fully lit until release, with no limit: a side key pressed in a pocket for minutes drains the battery. Now holding keeps the card lit for at most 10 seconds more, then it turns off, and the hold is still swallowed (`ux/lock_after_hold`).
  8. An unlocked meeting on battery dims after a minute without keys; when it then stopped by itself (low battery, limit reached, card full), the next loop turned the screen off and auto-locked, so D7's "light 2 seconds when recording ends, showing the result" was never visible. Now when recording ends while unlocked, the screen lights as if a key had been pressed, so the result page and toast are visible, then it dims, turns off and locks as usual (`ux/meeting_stop_unlocked`).
  9. "● Wait" only checked whether it could record, whether it was already recording and whether the lock card was lit, so it also appeared with a full card, battery ≤3.45 V, 256 segments not yet transferred or ids used up, and only then was the refusal shown. Now it uses the same check as actually starting a recording (`rec_refusal`), and "● Wait" is not shown when it would be refused (`ux/ready_refused`).

### 5.7 Merging gh #51's memo fixes and the level meter fix (2026-10-22)

The three things asked for in voice memo feedback go into v0.4.1 together: memos no longer cut off mid-sentence (`fix/memo-hold`: G0 release debounce, 0.6-second grace), double-tap G0 for hands-free recording with one more press to end (same branch), and a waveform that follows the voice rather than the room's hum (`fix/level-meter`: dB scale, a 4th-order 130 Hz high-pass on the level path). U26's rules follow §2.4 and D1–D10 follow §5.6; where they meet, it is settled as follows (tests first, `test_g0_decisions`, plus additions to `test_rec_pages_ux`, `test_mod_home`, `test_home_ux`):

1. **D4 and "holding in a Note with text"**: U26 said holding with text does nothing; D4 says holding on the editor, a confirm box or a menu records a separate memo and keeps the draft. D4 wins; an empty Note closes and records per U26 (there is nothing to keep).
2. **The grace is the same everywhere**: memos held on the editor, a confirm box or a menu (D4), and on the lock card, all get 0.6 seconds after release; the press that resumes belongs to that recording, and its tap neither submits the draft nor opens a Note (U26's "eaten").
3. **Where a double tap counts**: only where a tap means "Note". With the Tab menu open, the first tap follows D9's "G0 works as usual": it closes the menu and starts waiting for the second; not on the meeting start box (D3) or in the editor.
4. **Lock card** (D2, D7, U3, B1 and U26): on the auto-locked black screen, and on the black screen after the 2 seconds following a lock, the first press only lights the card; when the card lit up on its own (D7: recording ended, Inbox count rose), the first press also only lights it, since it may have lit up in a pocket (the same rule as "a card that lit up on its own does not count as seen"). Hands-free while locked: pressing G0 on the black screen lights the card at the moment of pressing (like a tap), and that press belongs wholly to the recording, so the card never says "not recording"; one press with the card lit ends it, the card shows the result per B1, and its second line says "Saving…" while finishing. The corner of the black screen says "● 免提 0:12" (● hands-free 0:12). The lock card's second line: "Tap [G0]then hold · 2 taps: hands-free"; during hands-free "Hands-free: [G0]ends it"; during a meeting "Meeting: [G0]adds ★"; all within 240 px.
5. **Auto-lock and hands-free**: while recording the screen only dims, never turns off, so a hands-free recording never locks by itself; when it ends the screen lights as if a key had been pressed (review fix 8), then turns off and locks as usual.
6. **Hands-free with a menu open**: title "Hands-free memo"; Enter on the "Stop and save" row only says "Tap G0 to stop and save"; Opt+L, per D9, closes the menu and locks, and recording continues; G0 ends it and closes the menu.
7. **Hint bars and pages**: Home "[G0]hold, talk after REC / double tap hands-free, [Enter]open" (D8's "speak once you see ● REC" and the double tap both fit, with keys drawn as keycaps). On the memo page the grace and hands-free pills take the title line; in the last 30 seconds (U15) the pills switch to the short "hold within 0.6 s" and "G0 ends it", and "0:30 left" stays in the top right corner without overlapping.
8. **State size**: `RecInfo` gained two bools, grace and latched; `free_known` moved into the row of bools, so `AppState` does not grow (3520 B).

## Appendix: evidence index

- **Review scripts and screenshots**: `$UX/scripts/` (first use fm01–fm16, information design s01–s20, refute_* re-run during verification); `$UX/*.cpas` (error recovery s01–s17); `$UX/daily/` (daily pocket t01–t14, pocket fuzzing); `$UX/stab/` (stability: `src/fuzz.cpp`, `soak.py`, `scripts/st01–st04`, `out/`); `$UX/fuzz/` (random keys on Home, phrases); `$UX/refute*`, `$UX/verify*` (refutation scripts from verification). Each directory's `contact.png` is an overview of all its screenshots.
- **Simulator**: `firmware/sim/README.md` (script format; what is not simulated is in the "Fidelity gaps" and "Hardware is not simulated" sections).
- **Keys and dispatch**: `firmware/app/lib/input/keymap.cpp`, `chord_detector.{h,cpp}`; `lib/shell/shell.cpp:113-203,237-335`; `shell_menu.cpp`; `menu_view.cpp`.
- **Lock and power**: `lib/shell/lock.{h,cpp}`, `power.{h,cpp}`, `lock_screen.cpp`, `runtime.cpp:155-171`.
- **Recording**: `lib/rec/rec_core.{h,cpp}`, `rec_engine.cpp`, `rec_overlay.cpp`, `runtime_rec.cpp`, `protocol_limits.h:75-77`.
- **Editing and confirming**: `lib/shell/compose.cpp`, `confirm.cpp`, `toast.{h,cpp}`, `shell_render.cpp`.
- **Modules**: `modules/home/home_logic.{h,cpp}`, `home_module.cpp`; `modules/inbox/inbox_logic.*`, `inbox_module.cpp`; `modules/board/board_logic.*`, `board_module.cpp`; `modules/system/system_logic.cpp`.
- **Docs**: docs/04 §8 (keys), §10 (error table); docs/11 §3–§4; docs/13 §3, §5, §7, §17, §18; docs/17 §1, §4; docs/releases/v0.4.0.md; docs/devlog/2026-10-19-20.md:42.
- **Theme**: `$TH/docs/19-visual-theme.md`; `$TH/firmware/sim/out/theme_tour/`; theme comparison screenshots `$UX/out/theme_s*/`.
