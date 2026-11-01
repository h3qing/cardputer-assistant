**English** · [中文](audio-capture.zh-CN.md)

# Audio capture and transfer: device-side design input (v0.3, no device connected)

> 2026-10-20 · Role: device-side capture and transfer. **This doc is design input only**: no device connected, nothing flashed, no serial port opened; every "measured" number is quoted from existing gate reports and capture files.
> Sources: [sd-card.md](sd-card.md) §5–§7 (60 s continuous recording, file pull at 138 KB/s, 16/59 replies held back while recording), [09-m2-design.md](../09-m2-design.md) §3, §5, §6, §12, [ADR-0001](../adr/0001-toolchain.md), 05 §5.8, §6, 03 §3, 04 §5.2, §7.7, §8.2; code `firmware/app` (`usb_transport`, `chord_detector`, `keymap`), `firmware/hwtest/src/sd_rec.cpp`, `daemon/src/cpad/device/{link,credit,framing}.py`; Arduino core 2.0.17's `HWCDC.cpp`, M5Unified 0.2.26 (`.pio/libdeps`), the SDK's `sdkconfig.h` / `ffconf.h`; WhisperWoof `767ae8d` (read-only via `gh api`, nothing run); M5Stack's official Cardputer-Adv page; the Espressif USB FAQ.
> Marks: **[meas]** measured on the real device (quoting existing reports) · **[src]** source code or official docs · **[calc]** calculated · **[tbv]** needs a device gate (§9).
> Units: KB in transfer rates = 1024 B (as in sd-card.md); recording data volume in decimal MB (1 hour = 115.2 MB).
> Scope: recording interaction on the device, format, budgets, link, transfer protocol, and where the daemon stores things. The interface contract between the Mac app and WhisperWoof is not in this doc; §8.2 only gives input.

## 0. One-page summary

1. **Gesture (proposed, awaiting a decision)**: holding G0 for 500 ms plays the "start" tone and starts recording, and **recording continues after release**; pressing G0 again stops it and plays "saved". "Push to talk" is the alternative (§2.1).
2. **While recording**: Space = mark, Opt+1–9 = set project, Opt+0 = clear. The speaker cannot sound while recording (microphone and speaker share the BCK/WS pins, so only half duplex), so marks and warnings use only the screen and the RGB LED.
3. **No "pre-roll before the press" from 04 §7.7**: pre-roll needs the microphone already recording at the moment of the press, while the start tone needs the same I2S pins. The user wants audible feedback on the long press, so the tone wins, and the rule becomes "start talking after the tone". From press to recording start is about 0.7–0.9 s [calc/tbv].
4. **Format**: 16 kHz, 16-bit, mono PCM WAV (standard 44 B header), one segment per 5 minutes, with **no gaps** between segments. The daemon joins them per session; WhisperWoof re-cuts at pauses itself; the device does no VAD.
5. **SD layout** follows 09 §12: `/cpa/rec/<E7>/<B5><S前3>.W<S末1>` (S前3 = the first three characters of S, S末1 = its last character). **New**: a metadata file of the same name, `.M<S末1>`, is the "commit" marker; no `.part`, no rename. After power loss the device repairs the header and writes the metadata (§3.4).
6. **Integrity**: SHA-256 of the PCM data is computed incrementally while recording (hardware accelerated) and written into `.M`; every transfer chunk carries a CRC32; the Mac sends delete only after checking the SHA-256.
7. **Threads**: a new audio task (core 0, priority 1, below mic_task's 2) owns writing recordings to the card, switching for tones, and reading the card for transfers. The loop no longer touches recording I/O. sd-card §7's "27% of replies held back" was measured with hwtest's weak kick; the app's transport re-arms IN_EMPTY every 20 ms, and the v0.1 HIL soak had 0 stalls. Most of this problem is expected to disappear [tbv G-A2].
8. **Memory**: about 53 KB more (the 6×4 KB recording ring buffer is the bulk). Allocated once from the heap at boot, only when SD is the primary storage; the static RAM budget is untouched [calc/tbv G-A6].
9. **Battery**: with the official 1500 mAh and 120 mA, recording lasts about 9–12 hours [calc/tbv G-A7].
10. **Transfer**: **host pull** (`file.list` / `file.get` / `file.del`). Each host request gets exactly one reply, so the existing credit window and blank-line nudge apply directly; resume by offset; the device deletes only after the Mac confirms. The outbox `kind:"rec"` is not used (replacing the reservation in 09 §6, §12; reasons in §7.1).
11. **Throughput**: working back from sd-card §7, the HWCDC line rate is about 185–225 KB/s [calc]. Base64 in 2048 B lines is about 69% efficient; a binary sub-mode about 96%. A 1-hour recording: about 12–15 minutes with b64, about 9–11 minutes binary; if the line rate reaches 500 KB/s, about 5.4 and 3.9 minutes. **Run G-A1 first, then decide whether to build binary** (§7.3).
12. **The protocol changes are all additions**: new message types, plus an optional `files` object in `hello` / `pong`; `v` stays 0. An old daemon never pulls; an old device answers `E_UNKNOWN_TYPE`.
13. **Where the daemon stores things**: first `$CPA_HOME/rec-staging/…part`, then after checking an atomic rename to `~/CardputerNotes/recordings/<date>/<session>/NNN.wav`, with a `session.json` beside it (fields aligned with the manifest in WhisperWoof's meeting design), then handed to WhisperWoof (contract to be defined).
14. **Gates** G-A1–G-A12 (§9). Throughput, recording with concurrent sync, tone timing and memory can be run by an agent that has the device to itself; battery, power loss, audio quality and feel need a person.

## 1. Input facts

| # | Fact | Source | Impact on the design |
|---|---|---|---|
| F1 | 60 s continuous recording with a 4×4 KB ring buffer: 0 frames dropped, longest single write 45 ms, longest fsync 78 ms, at most 1/4 of the ring buffer used | [meas] sd-card §7.2 | 16 KB is enough in steady state; segment rotation is separate (§3.3) |
| F2 | hwtest writes the card in the loop, and USB only gets a `(bool)Serial` kick: 16 of 59 pings held back about 1 s, p95 1037.9 ms | [meas] sd-card §7.2; `firmware/hwtest/captures/sd-rec3-20261020-114610.json` | hwtest's structure cannot be copied |
| F3 | The app's `UsbTransport::tick` checks every 20 ms, and when the TX ring is not empty it flushes the FIFO and re-arms IN_EMPTY (`rearm_tx`); the v0.1 HIL soak had 2028 requests with 0 stalls, p95 22 ms | [src] `firmware/app/lib/usb_transport/usb_transport.cpp:19,56`; [meas] 07 §2 item 11 | As long as the loop turns at least every 20 ms, a held-back reply waits at most one turn |
| F4 | File pull: 12 KB per chunk, base64 plus CRC32, one request per chunk; 1,916,972 B in 13.6 s (138 KB/s), 0 retransmits | [meas] sd-card §7.2; `cmdGet` in `sd_rec.cpp` | A 1-hour recording takes 13.6 minutes |
| F5 | HWCDC send: each IN_EMPTY interrupt moves at most 64 B from the ring buffer into the FIFO. When `write()` lacks space it writes a part first, then loops with `delay(1)` waiting for the rest; with `tx_timeout_ms=0`, failing to get the lock returns 0 | [src] core 2.0.17 `HWCDC.cpp:117-139`, `:410-468` | The line rate is limited by interrupts and 64 B packets; write only when a whole line or frame fits, and have only one writer |
| F6 | HWCDC receive: `xQueueSendFromISR` byte by byte in the interrupt | [src] `HWCDC.cpp:151-160` | The host-to-device direction sends only small requests |
| F7 | Espressif FAQ: ESP USB is limited by FIFO size, currently up to about 500 KB/s (about the OTG camera case). The S3's USB-Serial-JTAG and OTG can be used only one at a time, and with OTG there is no auto-download | [src] Espressif USB FAQ | Line rate ceiling estimated at about 500 KB/s; USB drive mode (OTG) is not the main path |
| F8 | The ADV's microphone is I2S0 (BCK 41, WS 43, DIN 46), the speaker is I2S1 (BCK 41, WS 43, DOUT 42), sharing the clock pins | [src] `M5Unified.inl:2758-2765`, `:3105-3115` (0.2.26) | Half duplex: no sound while recording; tones only before start and after stop |
| F9 | For the first 100–200 ms after `Mic.begin()` there is a codec restart transient; after recording starts, the first buffer arrives after 33–35 ms | [meas] ADR-0001 §3; sd-card §7.2 | Record `lead_ms` and let the Mac trim it |
| F10 | M5Unified's microphone defaults: 16 kHz, `over_sampling` 1, `magnification` 16, DMA 8×128 frames, mic_task priority 2, no core affinity, stack 2048+512 B. Speaker DMA 8×256 frames. (Corrected 2026-10-21: the `over_sampling` 2 written earlier is the struct default in `Mic_Class.hpp`, but `M5.begin()` changes it to 1 for all built-in microphones. Gain is `magnification / (over_sampling × 2)`, applied after the 16-bit samples, so v0.3.0 is 16/2 = ×8, about +18 dB; every sample of the user's two recordings is a multiple of 8, which confirms this. From v0.3.1 `MicRing::start` pins `over_sampling` to 1) | [src] `Mic_Class.hpp:58-93`, `Mic_Class.inl:619-620`, `Mic_Class.inl:967`, `M5Unified.inl:2625`, `Speaker_Class.hpp:66-73` (0.2.26); [meas] the two recordings of 2026-10-21 | The card-writing task's priority must be below 2; microphone gain should be set in the codec (PGA, ADC volume), see docs/releases/v0.3.1.md |
| F11 | Quiet room RMS about 6; a 1 kHz test tone played from the Mac's speakers RMS 43–74, peak 144 (full scale 32767) | [meas] sd-card §7.2 | The level is low; the level of human speech is not measured yet (G-A10) |
| F12 | SD at 20 MHz: read 1619–1714 KB/s, write 1363–1634 KB/s; saving in a flat 8.3 folder p95 29 ms, longest 102 ms | [meas] sd-card §5, §6 | SD is not the transfer bottleneck; segment rotation within about 300 ms |
| F13 | FatFs: `FF_FS_REENTRANT 1` (volume lock, 10 s timeout); each open file has its own 4 KB sector buffer; the long file name buffer is on the stack. mbedtls hardware SHA is on; ROM has `esp_rom_crc32_le` | [src] `ffconf.h:216,248,287`; `sdkconfig.h:434-439,568` (`qio_qspi`); `esp_rom_crc.h:70` | Two tasks can use the volume at once; a 3rd file slot is +4 KB; CRC costs no code |
| F14 | G0 held for a full 500 ms sends `G0Long` at once; release after a long press sends **no** event | [src] `chord_detector.cpp:61-79`; `keymap.cpp:44`, `shell.cpp:111` | "Push to talk" needs a new release event; "long press to start, press again to stop" needs no change to the input layer |
| F15 | Static RAM 63,444 B, budget 96 KB | [meas] local `scripts/check_budget.py .pio/build/cardputer-adv` (build of 2026-10-20 14:47) | New buffers placed statically would hit the budget |
| F16 | Official: battery 1500 mAh (changed from 1750 mAh on 2026-07-13); operating current 120.2 mA at 4.2 V; has an RGB LED (power controlled by GPIO38; in M5Unified's table the data pin is GPIO21); amplifier NS4150B, codec ES8311 | [src] docs.m5stack.com Cardputer-Adv; `M5Unified.inl:259` | Battery estimate; LED as recording indicator |
| F17 | The daemon writes an extra blank line every 500 ms only while "a host request holds an unanswered credit" | [src] `daemon/src/cpad/device/link.py:281` `_maybe_nudge` | With host requests for transfer, held-back replies get pushed automatically |
| F18 | WhisperWoof's meeting design: 16 kHz, 2–5 minute clips cut at pauses, one manifest per meeting (clip index, wall-clock start, sample count, status). Mixed Chinese–English test: SenseVoice MER 15.6%, whisper-large-v3-turbo 25.8%. File import accepts `.wav`, each ≤500 MB | [src] WhisperWoof@767ae8d `docs/design/local-meeting-transcription.md` §3, §3.1, §4.1; `src/whisperwoof/bridge/file-import.js:17-18` | The device need not cut segments at pauses; the hand-off unit is "clips + manifest" |

## 2. Recording interaction

### 2.1 Start and stop: two gestures

- **A: long press to start, keep recording after release (locked); press G0 again to stop.**
- **B: push to talk, release to stop; press Enter while holding to lock.**

| | A (proposed) | B |
|---|---|---|
| Over 1 minute (meetings, long thinking) | No need to keep holding | Must lock first, or the finger gets tired |
| Memos under 10 seconds | Two actions (long press, then a press) | One action |
| Accidental press in a pocket | Once started by mistake it keeps recording, relying on the tone, LED, low-battery stop and silence auto-stop (an open question) as safety nets | Stops on release |
| Input layer change | None: `G0Long` starts, `G0Tap` or `G0Long` stops | Needs a new `G0Up` (release after a long press) in chord_detector |
| Relation to G0's short press "quick text note" | No conflict | No conflict |

**Proposal A**: the product goal is Granola-style long recordings, and the user also finds typing tiring and wants fewer actions; one extra press for a short memo costs little. B can become a setting later.

### 2.2 Keys while recording

Recording is a **background state**: once started, the "recording page" is shown; on it, `` ` `` goes back to the previous page while recording continues, and the status bar shows a red dot and the duration.

| Key | Recording page | Other pages |
|---|---|---|
| G0 (short or long press) | Stop and save | Same. While recording, G0's meaning changes from "submit / quick note" to "stop"; in Compose, submit uses Enter instead |
| Space | Mark: at most 16 per segment, extra ones only counted, not recorded; the screen and LED each flash once | Normal input |
| Opt+1–9 / Opt+0 | Set project / clear; the recording page shows the project key | Keep their usual meaning (open the project page) |
| `` ` `` | Back to the previous page, recording continues | — |
| Fn+`` ` `` | Discard this recording (Confirm, 400 ms guard against accidental presses) — an open question | — |

- **Default project**: the project page you are on when recording starts; the same rule as Compose's default target (`UiContext`).
- **With the screen off** (on battery, after 60 s without input): any key only wakes the screen; G0 is the exception and stops recording directly (consistent with v0.1's "G0 exception").

### 2.3 Tones and I2S switching

| Event | Sound | When |
|---|---|---|
| Long press reaches 500 ms | Two rising tones: 880 Hz → 1320 Hz, 60 ms each | The microphone is not on yet |
| Saved | Two falling tones: 1320 Hz → 880 Hz | After `.M` is committed, about 0.1–0.3 s after stop [calc] |
| Refused or failed | Three 330 Hz beeps, 80 ms each | No card, card full, low battery, storage error, unsynced segment limit reached |
| Mark, battery warning | Silent | The speaker cannot be used while recording (F8) |

**Start path**: `G0Long` → the loop posts `Start` → the audio task in turn runs `Speaker.begin()` → plays the two tones → waits until `isPlaying()` is false → `Speaker.end()` → `Mic.begin()` → first buffer.
- From press to recording start about 0.7–0.9 s [calc]: 500 ms long-press threshold + about 150 ms of tone + switching time [tbv G-A5] + first buffer 35 ms [meas].
- The first 100–200 ms is the codec transient (F9). It is not trimmed on the device; `lead_ms` is recorded in segment 0's `.M`, and the Mac trims it.

**Stop path**: `Mic.end()` → finish (§3.3) → `Speaker.begin()` → the two falling tones → `Speaker.end()`.

**Other rules**:
- When idle, both microphone and speaker are off (`end()`), which saves power and keeps the amplifier from hissing.
- These calls (installing the I2S driver, writing ES8311 registers) all run in the audio task, so the loop never blocks.
- [tbv] The ES8311 and the keyboard's TCA8418 are both on In_I2C (GPIO 8/9), and no lock was seen in M5Unified's I2C class: while the audio task configures the codec, the loop may be reading the keyboard. G-A5 must verify this; if it is unsafe, add a shared lock, or move begin/end back into the loop (at the cost of blocking the loop once, duration [tbv]).
- Default volume medium, set by ear by the user [tbv]. "Meeting silent" (LED and screen only) is an open question. (Changed 2026-10-21: recording is silent, for personal use; from v0.3.1 recording makes no sound at all; the meeting confirmation box stays; the screen is the only indicator. The volume no longer needs deciding.)

### 2.4 Screen and LED

```
● REC 12:31                 #cpa  ★2
▁▂▅▇▅▂▁▁▂▃                         level, one bar per 128 ms
Segment 3 · 5 segments waiting · battery 78%
G0 stop  Space mark  ⌥1-9 project  ` back
```

- The level bar uses the RMS the audio task computes from each buffer along the way (almost free), so the user can confirm the microphone really works.
- RGB LED: slow low-brightness red blinking while recording, one white flash on a mark [tbv G-A12 for how it looks]. Visible even with the screen off.
- Status bar: shows `●12:31` while recording; shows `录5` (R5 in the English UI) when recordings are waiting to sync.

### 2.5 Refusals and automatic stops

| Situation | Action |
|---|---|
| No SD card, or the primary storage is not SD | Refuse: failure tone + "No SD card, cannot record" (04 §7.7) |
| Less than 8 MB free at start (about 4 minutes) | Refuse. Below 256 MB it starts as usual but says "About N hours left". Free space comes from reading FSINFO once at boot; if it cannot be read, it shows "unknown" (09 §12) |
| A write returns ENOSPC | Finish this segment normally (repair header, write `.M`), failure tone, "SD card full" |
| SD locked up (EIO, 09 §3.8) | Stop at once; this segment stays on the card uncommitted and is recovered at next boot (§3.4); failure tone |
| Voltage ≤3.45 V for 10 s [tbv G-A7] | Finish normally + failure tone. At ≤3.55 V only a warning on screen |
| Unsynced segments reach 256 (about 21 hours at 5 minutes per segment) | Refuse new recordings: "Connect to the Mac to sync first" |
| Reboot | Recording ends; this segment is treated as a power loss |

No maximum duration. Silence auto-stop (against accidental starts in a pocket) is an open question.

## 3. Recording format and SD layout

### 3.1 Format and segments

- 16 kHz, 16-bit, mono PCM, with a standard 44 B WAV header. Data rate 32,000 B/s; a 5-minute segment is 9,600,000 B of data plus the 44 B header; 1 hour is 115.2 MB.
- **Segment length 5 minutes** (04 §7.7). No gaps between segments: the same ring buffer keeps writing into the new file, so no samples are lost.
- The daemon joins segments into continuous audio per session; WhisperWoof cuts at pauses itself. Its tests show fixed cut points do not affect streaming models, and offline models cut again into ≤20 s pieces anyway [src F18]. So the device does no VAD and does not cut at pauses.
- Segment length affects only the number of files and how fresh transcription is when "syncing while recording"; it **does not affect power-loss damage**: with an fsync every second, a power loss loses at most about 1 s. Segment length is a compile-time setting, 1–10 minutes.
- **Why keep the WAV header instead of raw PCM**: a card put into a computer (or read through Launcher's USB drive mode, §7.7) plays directly. The cost is that the header has to be filled in at the end, and the SHA-256 can only cover the data part (§3.5).

### 3.2 Layout and naming (under the v0.2 layout)

```
/cpa/rec/<E7>/<B5><S前3>.W<S末1>   one segment of audio (WAV)
/cpa/rec/<E7>/<B5><S前3>.M<S末1>   this segment's metadata (JSON, ≤512 B); present and parseable = committed
```

- One MonoId per segment; **session id = the first segment's MonoId**. Flat per epoch with 8.3 names, like the outbox (09 §3.5).
- The unsynced limit is 256 segments, so an epoch folder has at most about 512 entries (16 KB of directory entries, half a cluster), and scanning the folder when creating a file still takes only tens of milliseconds [calc].
- The MonoId sequence number S is now taken by both the loop (outbox) and the audio task (recording), so it must become an atomic counter.

Contents of `.M` (example: a session's 2nd segment):

```json
{"v":1,"id":"K3M9Q2T-00042-0A1D","rec":"K3M9Q2T-00042-0A1C","seg":1,"last":false,"next":"K3M9Q2T-00042-0A1E","bytes":9600000,"sha256":"<64 hex digits>","at":{"up_ms":735000,"wall_ms":1792440345000},"project":"cpa","marks":[12800,201344],"fw":"cpa-fw/0.3.0"}
```

| Field | Meaning |
|---|---|
| `bytes` | Bytes of PCM data (excluding the 44 B header), always even |
| `sha256` | SHA-256 of the data part (file offset 44 to the end). Recovered segments do not have this field |
| `next` | The next segment's id. On rotation the id is taken first, then this segment's `.M` is written; after a power loss this links the uncommitted next segment back into the session |
| `marks` | Millisecond offsets within the segment, at most 16 |
| `lead_ms` | Only in segment 0: the length of the codec transient at the start, trimmed by the Mac [tbv G-A5 for the value] |
| `at` | `up_ms` at this segment's first sample; with `wall_ms` when the wall clock is known, otherwise null |
| `rcv` | true means this segment was recovered at boot |

**Wall clock**: the device has no RTC. After an offline boot `wall_ms` is null; as long as the same boot later connects to the Mac, the daemon can work it out from the boot id plus `up_ms` (§8.1).

### 3.3 Write flow (audio task)

1. **Open a segment**: take an id → `open(.W, O_CREAT|O_TRUNC)` → write a 44 B placeholder header (length fields 0) → initialise SHA.
2. **Write**: whenever a block of the ring buffer is full (4 KB, 128 ms), write it and update SHA; `fsync` once per second. This is sd-card §7's method, with 0 frames dropped [meas].
3. **Rotate** (after 5 minutes): take the next segment's id → fill in this segment's header (`lseek 0` plus a 44 B write) → `fsync` + `close` → write this segment's `.M` (09 §3.6's FAT commit protocol: write the final file name directly → `fsync` → `close`) → open the next segment.
   - Throughout, only one recording file is open at a time, and the ring buffer keeps receiving samples meanwhile.
   - Worst case ≈ fsync 78 ms + write stall 65 ms + writing `.M` 102 ms + creating the new file 45 ms ≈ 290 ms [calc]. 6 buffers cover 640 ms, 4 only 384 ms, too thin a margin, so 6 it is.
4. **Stop**: same as step 3, but with `last:true` and no next segment; then play "saved".

No `.part` plus rename: rename on FAT is not atomic (sd-card §6.3), as 09 §12 already settled. **`.M` is the commit marker.**

### 3.4 Power-loss recovery (replaces 04 §7.7's `.part`)

After boot the audio task scans `/cpa/rec/*/` in the background, listing only entry names and sizes, without reading file contents:

| `.W` | `.M` | Action |
|---|---|---|
| Present, size = 44 + `bytes` | Parseable | Committed; add to the to-sync index |
| Absent | Present | Leftover from a half-done delete (delete order is `.W` first, then `.M`): delete the `.M` |
| Present | Absent, zero length or unparseable | **Recover**: if `.W` is under 44 B, delete it; otherwise data = (size − 44) rounded down to even → fill in the header → fsync → find `rec` and `seg` through the previous segment's `.M` `next` (if not found, it becomes its own session with seg 0) → write a new `.M` (`rcv:true`, no `sha256`, `last:true`) |
| Present, size ≠ 44 + `bytes` | Parseable | Leave the file alone; report by the actual size of `.W`, drop `sha256`, mark `rcv:true` (should not happen in theory) |

- Recovery does only one small 44 B write and one `.M`, never reads the whole segment, and does not slow boot. Recording requests that arrive during the scan wait in a queue, about 1–2 s [calc].
- **Loss**: audio after the last fsync (≤1 s). Clusters already allocated but not yet in a directory entry become lost clusters, about 1–2 clusters of 32 KB per power loss [calc], which the Mac's Disk Utility → First Aid can reclaim (09 §3.6).
- **Power loss while filling in the header**: the file's first sector may be damaged, affecting at most the header and about 15 ms of audio at the start; the next boot fills it in again [calc].
- These rules are written as a pure function `plan_rec_recovery(entries) -> actions` (`lib/rec`), with every combination tested under native.

### 3.5 Integrity

- **SHA-256 computed incrementally while recording** (mbedtls, hardware SHA on [src F13]); at 32 KB/s of data the cost is negligible [calc]. It covers only the data part, because the header is filled in last. The header is fully determined by the data length; the Mac rebuilds it from `bytes` and compares byte by byte.
- **Every transfer chunk carries a CRC32** (`esp_rom_crc32_le` in ROM): it protects only that chunk's content. SHA-256 covers end to end, including storage on SD and whether offsets were stitched correctly.
- **Recovered segments have no SHA-256**: they rely on the SD driver's per-block CRC16 (09 §3.9) plus the transfer CRC32; the daemon marks them `rcv`.

## 4. Tasks, memory and stack

### 4.1 Thread model

```
loop (core 1, priority 1)   keys, UI, link, outbox, USB send (the only place that writes Serial)
   │ command queue: Start{project} / Stop / Mark / Tag / Discard / FileReq{list | get | del}
   ▼
audio task rec_task (core 0, priority 1, stack 6 KB)
   tones, Mic / Speaker switching, ring buffer to disk, SHA, rotation, boot recovery scan, card reads for transfer
   ▲ ring buffer (refilled by mic_task's release callback, as in sd-card §7)
mic_task (M5Unified, priority 2, no core affinity)
```

- **Why a separate task**: in sd-card §7 card writes happen in the loop, which then stalls for 45–78 ms; replies are held back, and single steps exceed the 50 ms rule. With writes moved out, the loop touches SD only when saving the outbox.
- **Priority must be below mic_task**: the Arduino SD driver polls SPI with the CPU. If the audio task had a higher priority and landed on the same core as mic_task, mic_task could not run during a 78 ms fsync, and the I2S DMA (8×128 frames, about 32 ms) would overflow and lose samples [calc]. In hwtest card writes happen in the priority-1 loop, just below mic_task, hence 0 frames dropped.
- **The FatFs volume lock** (F13) lets two tasks use the card at once. The cost is that the loop's outbox save may queue behind an fsync, waiting at most 80 ms more, still within 09 §5's 150 ms exception [calc].
- **Order inside the audio task**: first write out every full buffer → fsync when due → then serve one transfer read request (at most 4 KB, about 3 ms). Recording always comes first.
- **Only the loop writes Serial**: HWCDC's `write()` writes part and loops waiting when the buffer is short, and returns 0 directly when it cannot get the lock (F5). "Check space, then write" is not atomic between two writers and could stall the loop or fail the write.
- **The SD lock-up flag** (09 §3.8) becomes an atomic shared by both tasks: if either side hits EIO, both stop.
- **Pure logic lives in `lib/rec`** (testable under native): the `RecCore` state machine (idle → tone → recording → finishing), rotation decisions, `.M` encode/decode, the WAV header, recovery planning, `file.get` argument validation. The task itself is only a thin shell.

### 4.2 Memory (the new part)

| Item | Bytes | Basis |
|---|---|---|
| Recording ring buffer 6×4 KB | 24,576 | §3.3: worst-case rotation 290 ms; 4 blocks are enough in steady state [meas] |
| Transfer: read buffers 2×1380 B + base64 line buffer 2 KB (2×4.2 KB with binary) | 4,808 (8,512) | §7 |
| HWCDC send ring from 4 KB to 8 KB | +4,096 | §7.3 |
| A 3rd FatFs file slot (each file has its own 4 KB sector buffer) | ≈4,600 | [src] `CONFIG_FATFS_PER_FILE_CACHE` |
| Microphone DMA and mic_task stack | ≈6,700 | [src] F10: 8×128 frames about 4 KB, stack 2,560 B |
| Audio task stack | 6,144 | Long file name buffer 512 B on the stack, plus VFS, SPI and mbedtls frames [calc]; margin per G-A6 |
| To-sync index 256×12 B, SHA context, queues | ≈3,600 | |
| **Total** | **≈53 KB** (about 57 KB with binary) | |

- The speaker (DMA 8×256 frames about 8 KB, plus task stack) exists only while a tone plays; it is mutually exclusive with the microphone, so the two do not add up.
- If the ring buffer, transfer buffers and index (about 32 KB) were all static, static RAM would rise from 63 KB to about 96 KB, right at the budget. **Proposal**: only when SD is the primary storage, allocate once from the heap at boot and never free (so no fragmentation); a device without a card does not pay this memory.
- **Proposed budget** ([tbv G-A6]):
  - when idle keep 09 §5's targets: free heap ≥150 KB, largest contiguous block ≥64 KB;
  - while recording, syncing and playing a tone at once: free heap ≥96 KB, largest contiguous block ≥32 KB;
  - audio task stack margin ≥1.5 KB.

### 4.3 Stack

- **The loop stack is unchanged** (16 KB, margin ≥4 KB). The only new things in the loop are parsing `file.*` requests and assembling `file.data` lines (using a static line buffer), with no large stack frames. `stack_budget.py` must add these two call chains.
- **Audio task 6 KB**: the long file name buffer of 512 B is on the stack (`CONFIG_FATFS_LFN_STACK` [src]). `.M` is produced with `snprintf`, not `JsonDocument`; only the boot recovery scan uses a small static `JsonDocument` to parse `.M`. The System page shows its stack high-water mark.

## 5. Keeping the link responsive while recording ("27% of replies held back")

**Cause analysis**:
- That measurement used hwtest: card writes in the loop (average 3.5 ms, longest 45 ms, fsync longest 78 ms), and USB had only the `(bool)Serial` kick. Held-back replies all waited for the host's next write (one ping per second) to come out, hence about 1 s.
- The app's transport does one more step (F3): when the TX ring is not empty it flushes the FIFO and re-arms IN_EMPTY. A comment in `usb_transport.h` records the symptom "the interrupt sometimes turns IN_EMPTY off with data pending, and even a blank line from the host does not release it". With this step the v0.1 HIL soak had 0 stalls. But it only works while the loop is turning.
- Inference [calc]: how long a reply is held back ≈ the interval between two loop ticks. Once the loop no longer writes the card, the worst left is rendering (a full-screen push, a dozen or so milliseconds [tbv]) and outbox saves (p95 29 ms).

**Measures**:
1. All recording I/O goes into the audio task (§4.1).
2. Keep the re-arm every 20 ms.
3. During transfers, lower the screen refresh to at most 5 frames per second, leaving loop time to USB.
4. The daemon already has the 500 ms blank-line nudge. Transfer requests take credits, so held-back transfer replies are covered too (F17).
5. `file.data` is produced only when the whole line (or whole frame) fits into the TX ring, and does not go into the 4-line LineQueue, so reply messages are still never dropped.

**Acceptance (G-A2)**: record for 10 minutes with the host pinging every second plus normal snapshot pushes: reply p95 ≤100 ms, 0 over 600 ms, 0 frames dropped.

Also watch whether 07 §3's "occasional drop out of Ready" (the SOF tick hook misjudging an unplug) gets more frequent while recording: G-A2 also counts `E_NOT_READY`.

## 6. Battery

- **Official figures**: battery 1500 mAh (early batches 1750 mAh), operating current 120.2 mA at 4.2 V [src F16]. The official page does not say whether this is with the screen on.
- **Extra power for recording**: codec ADC and I2S, SD writes (duty cycle about 3% [meas §7.2]), one fsync per second; estimated +5–15 mA [calc]. Screen off (backlight off after 60 s on battery) saves part of it back [tbv].
- **Estimate**: usable capacity taken as 85–90% (stopping at 3.45 V, §2.5).

| Case | Current [calc] | 1500 mAh | 1750 mAh |
|---|---|---|---|
| Recording, screen on | ≈135 mA | ≈9.4 h | ≈11 h |
| Recording, screen off | ≈110 mA | ≈12 h | ≈14 h |

  That is about 7–9% of battery per hour of recording.
- While connected to the Mac it charges as it records, so no impact.
- **Optional power saving** (off by default in v0.3, needs G-A7 to verify): lower the CPU to 80 MHz while recording (may affect USB throughput; only on battery). No light sleep while recording, because the I2S DMA is running.
- **G-A7**: charge fully → record with the screen off until it stops automatically → log `bat_mv` to a log on SD every minute. This gives the real number of hours and the discharge curve, and also sets the 3.45 V stop threshold.

## 7. Transfer to the Mac

### 7.1 Overall: host pull

The daemon lists → fetches chunks by offset → checks → stores → sends delete → the device deletes.

- Each host request gets exactly one reply (05 §6.3 item 2), so the credit window, `re` matching and the blank-line nudge all apply directly.
- The device needs no transfer state machine; after a disconnect, it resumes by offset.
- **Discovering new files**: the device carries an optional `files` object in `hello` and `pong`; whenever `rev` changes, the daemon lists again.

**Why not the outbox `kind:"rec"` reserved in 09 §6, §12**:
1. The v0.2 daemon only knows `note` and `queue` (05 §6.5) and would mark `rec` as rejected. The device then moves it into bad/, which keeps only 20 entries, and the record of this recording is lost. Avoiding that would mean making `OutboxCore` skip some kinds by capability, i.e. changing v0.1's core logic.
2. The outbox capacity (200 entries) and the in-memory id array are shared with notes; 10 hours of recording would take 120 entries.
3. A file's lifecycle (deleted after transfer) and an outbox entry's lifecycle (deleted on ack) are different things anyway; the daemon has to track files separately regardless.
4. 05 §6.5's hard rule "the outbox has only note and queue" need not change.

So a recording's "commit" lives in the `.M` on SD, and discovery is by the host listing.

### 7.2 Throughput estimate

**Line rate W** (raw byte rate from device to host) has not been measured on its own:
- Worked back from sd-card §7's pull: 157 chunks, 86.6 ms each, about 16.5 KB on the wire each. Counting it all as wire time gives 186 KB/s; subtracting about 15 ms per chunk for card reads, CRC and request overhead gives 224 KB/s [calc]. The host script's polling granularity may also have slowed it down, so this is only a rough estimate.
- Upper reference: the Espressif FAQ says ESP USB is FIFO-limited to about 500 KB/s (the OTG case, not Serial/JTAG) [src F7]; USB full speed is about 1.2 MB/s in theory. HWCDC moves only 64 B per interrupt (F5).
- So plan for 185–500 KB/s.

SD reads at 1.6–1.7 MB/s, and the CPU's base64 and CRC are not bottlenecks either [meas/calc]. Host requests are about 90 B per chunk and use little bus time.

**Efficiency of the two encodings** (including the JSON envelope, examples in §7.4):
- base64 in lines of ≤2048 B: at most 1380 B of data per line, a line about 2000 B, about 69% efficient.
- Binary sub-mode: a header line of about 158 B + 4096 B of data + 1 B, about 96% efficient.

| Line rate W | b64 effective throughput | 1-hour recording | Binary effective throughput | 1-hour recording |
|---|---|---|---|---|
| 185 KB/s (lower bound worked back) | 128 KB/s | 14.6 min | 178 KB/s | 10.5 min |
| 225 KB/s (worked back) | 156 KB/s | 12.0 min | 217 KB/s | 8.7 min |
| 350 KB/s | 242 KB/s | 7.7 min | 337 KB/s | 5.6 min |
| 500 KB/s (reference ceiling) | 346 KB/s | 5.4 min | 481 KB/s | 3.9 min |
| Measured: 12 KB chunks base64, waiting for each reply | 138 KB/s | 13.6 min | — | — |

- **Target (proposed)**: a 1-hour recording synced within 6 minutes (effective throughput ≥320 KB/s); the floor is 10 minutes (≥190 KB/s). At the worked-back line rate, b64 misses the floor and binary barely meets it. So binary will most likely be needed, but measure first.
- **If the line rate really is only about 200 KB/s**, these options remain (none in the v0.3 default):
  - IMA ADPCM on the device: 4:1, almost no CPU, with the effect on transcription accuracy to be measured in G-A10;
  - a simplified FLAC: lossless, about 1.6–2× [calc, not measured], a lot of work;
  - Launcher's USB drive mode (§7.7);
  - slow overnight sync while connected to the computer, which is good enough anyway.

### 7.3 Inline base64 vs binary sub-mode

| | Inline base64 | Binary sub-mode |
|---|---|---|
| Efficiency | ≈69% | ≈96% |
| Daemon framing | No change (each line ≤2048 B) | `framing.split` must recognise the `file.data` header line, then take `bin` raw bytes and one `\n` |
| Device send | Uses the existing whole-line send, only requiring "produce only when the whole line fits" | A new whole-frame send: header line, data and `\n` assembled into one contiguous buffer and written in one `write`; the TX ring must be 8 KB |
| Error recovery | A bad line is dropped and that chunk fetched again | If the frame is not followed by `\n`, or the CRC is wrong: drop bytes until a parseable `{"v":0,…` line appears; more than 3 times within 1 minute on the same connection falls back to b64 |
| Contract tests | Ordinary JSON vectors | Byte-level vectors too (there is the `line_b64` precedent) |
| Launcher console rule | Not affected (the rule only governs host to device) | Same |
| Old versions | Not affected | Used only when the host explicitly asks for `enc:"bin"` and the device reports `files.bin:true` |

**Decision rule (proposed)**:
1. First build both encodings as commands in hwtest and run G-A1. hwtest is only measuring firmware, so this is cheap.
2. b64 effective throughput ≥320 KB/s: the app does b64 only.
3. Otherwise, if binary is ≥1.3× b64: the app does both, binary as the default and b64 as the fallback.
4. Both below 190 KB/s: then consider ADPCM.

Both encodings are defined in the protocol from the start (the `enc` field), so whichever path is taken is only an addition and never touches `v` again.

### 7.4 Protocol additions (`v` stays 0)

**Optional field `files` in the device's `hello` and `pong`**:

| Field | Values | Meaning |
|---|---|---|
| `files.n` | 0–256 | Segments committed and not yet deleted |
| `files.rev` | 0–4294967295 | Incremented on every commit, delete and recovery (wraps around) |
| `files.live` | bool | Recording now |
| `files.bin` | bool | Supports `enc:"bin"` |

Without `files`, the device does not support recording transfer (v0.2 and earlier).

**Messages**:

| `t` | Direction | Mode | Fields |
|---|---|---|---|
| `file.list` | H→D | Host-initiated | `after?` (MonoId, start after it, excluding it), `max?` (1–4, default 4) |
| `file.items` | D→H | Reply | `items[≤4]`, `more` (bool) |
| `file.get` | H→D | Host-initiated | `fid` (MonoId), `off` (0 to 2³²−1), `len` (1–1380 for b64, 1–4096 for bin), `enc?` (`b64` by default, or `bin`) |
| `file.data` | D→H | Reply | `fid`, `off`, `n` (0 to `len`), `size` (total readable file length now, including the header), `final` (bool, this segment is committed), `crc` (CRC-32/IEEE of these n bytes); plus `b64` (when enc is b64 and n>0) or `bin` (when enc is bin, value equal to n, and this line is followed immediately by n raw bytes and one `\n`) |
| `file.del` | H→D | Host-initiated | `fid`, `size`, `sha256?` |
| `ack` / `error` | D→H | Reply | Deleted, or already absent: `ack`; otherwise `error` |

Fields of each entry in `items`: `id`, `kind` (only `rec` in v0.3), `size` (including the header), `final`, `sha256?`, `sha_from` (always 44), `rec` (session id), `seg` (0–9999), `last`, `dur_ms`, `at{up_ms, wall_ms|null}`, `project?`, `marks?[≤16]`, `lead_ms?`, `rcv?`. One entry is at most about 431 B, so a reply has at most 4 entries; the device truncates at 2048 B and sets `more:true`.

**Rules**:
- The segment being recorded also appears in the list: `final:false`, with `size` being the length at the last fsync, so it can be fetched while recording. Once `final` becomes true, the daemon must fetch [0, 44) again, because the header was filled in.
- `file.get` with `off ≥ size` answers `n:0`; it is not an error, and the daemon learns from it that the fetch is complete.
- `file.del` deletes only when `size` (and the `sha256` the device recorded, if any) matches; otherwise it answers `E_BAD_FIELD` (`field` is `size` or `sha256`). A segment being recorded answers `E_BUSY`. If the file is absent it answers `ack`: this makes it idempotent, so resending after a lost ack is safe. Delete order is `.W` first, then `.M` (§3.4).
- **In-flight limit**: the device queues at most 4 `file.*` requests (`file_inflight_max`), and answers any more with `E_BUSY` at once; the daemon itself also keeps ≤4 in flight.
- **All error codes are existing ones**: `E_UNKNOWN_ITEM` (the fid of `file.get` does not exist), `E_BAD_FIELD`, `E_BUSY`, `E_STORAGE` (card read failure or SD lock-up), `E_NOT_READY`, `E_UNKNOWN_TYPE` (old device). No new error codes.

**Additions to `protocol/limits.json`**:

```json
"file_list_max": 4,
"file_get_b64_max": 1380,
"file_get_bin_max": 4096,
"file_inflight_max": 4,
"file_pending_max": 256,
"file_marks_max": 16,
"file_seg_max": 9999,
"sha256_pattern": "^[0-9a-f]{64}$"
```

Where 1380 comes from: with `id` and `re` at their longest of 24 characters and every number at its maximum, a `file.data` line is 2039 B (including `\n`), not over 2048 [calc].

**Example**:

```jsonl
{"v":0,"t":"pong","id":"d40","re":"h40","up_ms":735000,"pending":0,"revs":{"projects":42,"sessions":17,"queue":9},"files":{"n":2,"rev":17,"live":false,"bin":false}}
{"v":0,"t":"file.list","id":"h41"}
{"v":0,"t":"file.items","id":"d41","re":"h41","items":[{"id":"K3M9Q2T-00042-0A1C","kind":"rec","size":9600044,"final":true,"sha256":"<64 hex digits>","sha_from":44,"rec":"K3M9Q2T-00042-0A1C","seg":0,"last":false,"dur_ms":300000,"at":{"up_ms":435000,"wall_ms":null},"project":"cpa","lead_ms":150}],"more":false}
{"v":0,"t":"file.get","id":"h42","fid":"K3M9Q2T-00042-0A1C","off":0,"len":1380}
{"v":0,"t":"file.data","id":"d42","re":"h42","fid":"K3M9Q2T-00042-0A1C","off":0,"n":1380,"size":9600044,"final":true,"crc":2914141237,"b64":"UklGRiz…"}
{"v":0,"t":"file.get","id":"h43","fid":"K3M9Q2T-00042-0A1C","off":9598620,"len":4096,"enc":"bin"}
{"v":0,"t":"file.data","id":"d43","re":"h43","fid":"K3M9Q2T-00042-0A1C","off":9598620,"n":1424,"size":9600044,"final":true,"crc":1056532021,"bin":1424}
<1424 raw bytes><\n>
{"v":0,"t":"file.del","id":"h44","fid":"K3M9Q2T-00042-0A1C","size":9600044,"sha256":"<64 hex digits>"}
{"v":0,"t":"ack","id":"d44","re":"h44"}
```

**Naming**: the `voice.begin` / `voice.chunk` / `voice.end` reserved in 05 §6.10 have the shape of "device push", unlike the host pull here. Proposal: do not reuse them, keep the names reserved; use the generic `file.*` plus `kind` instead, so archiving other files later needs no new message types. The Lead decides.

**Compatibility**:
- A v0.3 device with a v0.2 daemon: the daemon sends no `file.*` and ignores `files` (`protocol/schema/v0` has no `additionalProperties:false` anywhere, and 05 §6.2 says unknown fields are always ignored [src]). Recordings stay on the card, and the device shows the number of segments waiting to sync.
- A v0.2 device with a v0.3 daemon: without `files` there is no pull; even if one is sent by mistake, it only gets `E_UNKNOWN_TYPE`.
- Every line the host sends starts with `{`, which satisfies the Launcher console rule. Binary data appears only in the device-to-host direction.

**Contract**: each new message gets its schema and test vectors first (RED).
- Message vectors: valid cases; `len` over the limit; `enc:"zip"`; `fid` not a MonoId; b64 decoding to a length ≠ n; `file.items` with more than 4 entries; binary frames (byte-level vectors).
- Flows: resume; a lost ack for `file.del`; refetching the header after `final` flips.

### 7.5 Resume, integrity and delete

**Device side**: stateless. It caches only one open read handle: for the same fid with contiguous offsets it neither reopens nor seeks again; after 2 s idle or a different fid it closes it. Without FatFs fast-seek, lseek walks the cluster chain; a 5-minute segment has at most 293 clusters and at most 3 sectors of FAT, so the cost is negligible [calc].

**Daemon side** (one puller per device, one file at a time):
1. After entering Ready, and whenever `files.rev` changes, list page by page.
2. Segments without a `rec.stored` in the event log are pulled in ascending id order (that is, session order), staged at `$CPA_HOME/rec-staging/<dev>/<fid>.part` (mode 0600). On resume, first cut the last 64 KB off the `.part`, then continue from there, in case a crash left a half-written tail.
3. Check each chunk's CRC32 and that offsets are contiguous; refetch the same offset for a bad chunk; if the same chunk fails 3 times in a row, give up on this file for this connection.
4. When complete, check three things: `size` = 44 + `bytes`; the header is byte-identical to the standard header rebuilt from `bytes`; `sha256(file[44:])` equals the value in the list (skipped for `rcv` segments).
5. Store (§8.1) → append a `rec.stored` event and fsync → send `file.del` → on ack, append `rec.deleted`.
6. A segment still on the device whose `rec.stored` is already in the log with the same SHA: send `file.del` directly. This covers both a lost ack and a daemon restart.

- **When to pull**: whenever the link is Ready, including while recording (whether to throttle is decided by G-A3). `cpa pause` (before flashing) interrupts the transfer, which later resumes by offset.
- **Safe delete**: the device deletes only when `size` and `sha256` match; the daemon sends delete only after fsync. A power loss at any step means at worst the same segment is transferred again, and the daemon deduplicates by `(dev, fid)` plus SHA.

### 7.6 Fetching while recording (optional, the protocol already allows it)

During a meeting while connected to the Mac, the daemon can fetch the segment being recorded up to the last fsync every 10–30 s, so WhisperWoof can produce text clip by clip. v0.3 can do only "fetch after a segment ends" (delay ≤5 minutes plus transfer time), and turn this on later.

### 7.7 Fallback: Launcher's USB drive mode

- With a large backlog (for example tens of hours after a trip), you can go into Launcher → SD→USB by hand. This needs Launcher ≥ `09ffc59`, currently only in the beta (launcher.md §5).
- The daemon adds `cpa import-card /Volumes/<volume name>`: checks and stores by `.M` in the same way, then deletes the files on the card.
- Cost: leaving our firmware; macOS writes files such as `.Spotlight-V100` and `._*` to the card, which the device scan ignores as other [tbv].
- For the ESP32-S3 to act as a USB drive itself it needs the OTG peripheral, which can only be used one at a time with Serial/JTAG, and with it there is no auto-download (F7), so it is not the main path.

## 8. Daemon side

### 8.1 Where things are stored

```
$CPA_HOME/rec-staging/<dev>/<fid>.part                 in transfer (0600)
~/CardputerNotes/recordings/<YYYY-MM-DD>/<HHMM>-<session id, lowercase>/
    000.wav  001.wav …                                  one file per segment, kept as is (with the 44 B header)
    session.json                                        session manifest
```

- **Storing**: fsync the `.part` → `rename` to the target (atomic on the same volume; `$CPA_HOME` and `~/CardputerNotes` are both on the home volume by default) → fsync the folder. If `notes_dir` is set to another volume, copy to a temporary name in the target folder first, then rename [tbv].
- **`session.json`** fields align with the manifest in WhisperWoof's meeting design (clip index, wall-clock start, sample count, status):
  `{v, source:"cardputer", dev, rec, project, state: recording|complete|recovered, sample_rate:16000, channels:1, lead_ms, clips:[{index, file, id, samples, wall_start_ms, wall_est, sha256, rcv}], marks:[{clip, ms}]}`.
  Rewritten in full each time a segment is stored (write a temporary name, then rename).
- **Date and time**:
  - prefer `at.wall_ms`;
  - without it, if the same boot later connected to the daemon, work it back from the "wall clock minus `up_ms`" offset of that time, and mark `wall_est:true`;
  - with neither, use the storing time, prefixed with `~` (the same rule as 05 §7.8 for notes).
- **The daily note** gets one line (exact format up to the daemon owner), for example: `- 14:32 录音 12:31 #cpa → recordings/2026-10-20/1432-k3m9q2t-00042-0a1c/ ^cpa-k3m9q2t-00042-0a1c`.
- **Size**: 115 MB per hour. Whether to clean up periodically, and whether this folder gets synced by iCloud, are open questions.

### 8.2 Handing to WhisperWoof (input only, contract to be defined)

**Known facts** [src WhisperWoof@767ae8d]:
- File import accepts `.wav` (also mp3, m4a and so on), each file ≤500 MB, which at 16 kHz mono is about 4.3 hours.
- Meeting design: 16 kHz, one clip per 2–5 minutes, one manifest per meeting, re-queued from the manifest after a crash. Clips are cut at pauses, and offline models cut again into pieces of ≤20 s.
- In its mixed Chinese–English meeting test, SenseVoice (MER 15.6%) beats whisper-large-v3-turbo (25.8%) by about 10 points; FireRedASR2 AED is 13.3% but very slow (RTF 0.455). So SenseVoice is the suggested default for mixed Chinese and English, whereas the user had assumed Whisper.
- The existing "companion device" precedent is the Telegram companion: an external process writes `telegram-inbox.json`, and the desktop app polls and imports it every 10 s. It carries only text (`plugins/telegram-companion/index.js`, `src/whisperwoof/bridge/telegram-sync.js:16-17`).
- Encryption: the WhisperWoof vault uses the WWENC1 format (X25519 + HKDF + AES-256-GCM, ≤64 KiB chunks). Writing needs only the public key, so recording works even while the vault is locked (`vault/wwenc-pure.js:1-14`).

**Proposal**: the hand-off unit is "session folder + `session.json`", not one big joined WAV. This saves a copy on disk and never hits the 500 MB limit. Whether it goes through a watched folder, a local interface or a plugin is up to the WhisperWoof integration owner.

**Open**: if the user turned on WhisperWoof's encryption, plaintext WAVs in `~/CardputerNotes` go against its design. Possible approaches:
- the daemon seals into WWENC1 when storing;
- going further, the device encrypts directly with the vault's public key (the ESP32-S3 has AES and SHA hardware, and mbedtls supports X25519), so there is no plaintext on the SD card or the Mac.

v0.3 does no encryption; this is an open question.

## 9. Device gates

| ID | What | How | Pass criteria | Who |
|---|---|---|---|---|
| G-A1 | Sustained HWCDC throughput | Add the hwtest command `!xfer <b64\|bin> <len> <depth> <txring>`, with data from RAM and from SD (`rec.wav`); the host script pings once per second at the same time | A table: effective KB/s, CPU use, ping latency; choose the encoding by §7.3's rule | An agent with the device to itself, about 30 minutes |
| G-A2 | The link while recording (app structure) | Test app: the audio task records for 10 minutes while the daemon heartbeats and pushes snapshots normally | 0 frames dropped; reply p95 ≤100 ms, 0 over 600 ms; `E_NOT_READY` is 0 | Agent |
| G-A3 | Recording and syncing at once | Record 30 minutes (1-minute segments) while continuously pulling earlier segments | 0 frames dropped; ring buffer at most ≤4/6 used; throughput ≥70% of G-A1; every segment's SHA correct | Agent |
| G-A4 | Gapless rotation | 1-minute segments, record 30 | Sum of segment sample counts = wall clock × 16000 − first-buffer delay, within 1 block; record the longest rotation time and the most ring buffer used | Agent |
| G-A5 | Tone timing | Measure the time of `Speaker.begin`, the tone, `Speaker.end` and `Mic.begin` separately, the longest loop block, and the RMS 0–500 ms after `Mic.begin` (to set `lead_ms`); I2C concurrency: type continuously on the keyboard while starting a recording | Loop step ≤50 ms; in 20 tries no wrong keys and every codec configuration succeeds; `lead_ms` set | Agent, plus 2 minutes of listening by a person (volume, audibility; from v0.3.1 there is no sound, so listening does not apply) |
| G-A6 | Memory and stack | In idle, recording, and recording + sync + tone, record free heap, largest contiguous block, and the stack high-water marks of the audio task and the loop | Meets §4.2's budget | Agent |
| G-A7 | Battery | Charge fully, record with the screen off until it stops automatically, log `bat_mv` every minute | Real number of hours, stop threshold set; ≥8 h passes (proposed value) | A person (cable unplugged, about 10 hours, can run overnight) |
| G-A8 | Power-loss recovery | Flip the switch and pull the cable during recording 10 times, 3 of them right at rotation (1-minute segments) | Every boot recovers a playable WAV, `rcv:true`, sessions link up (`next`); lost clusters noted with the Mac's First Aid | A person |
| G-A9 | Card removal and card full | Pull the card while recording; record on a small or pre-filled card until full | Card removal: at most one stall (≤2 s), failure tone; after reinserting and rebooting that segment syncs. Card full: this segment finishes normally | A person |
| G-A10 | Audio quality and transcription | Record mixed Chinese–English reading on the device and the Mac microphone at the same time (at 30 cm and 1 m), measure peak and RMS (dBFS), transcribe with WhisperWoof's SenseVoice and whisper-turbo | The device recording's MER is ≤3 points worse than the Mac microphone's (proposed value); no clipping; `magnification` set from this | A person (needs speaking) |
| G-A11 | Robustness of binary framing (only if binary is chosen) | Test build injects errors: wrong CRC, short frames, frames not followed by `\n` | The daemon recovers within 1 s, 0 errors in stored files | Agent |
| G-A12 | Feel | Use for a day: start and stop 20 times each, add marks, look at the LED and screen | The user confirms subjectively; accidental presses counted | A person |

**HIL scenarios** (`tools/hil`, can run automatically):
- `rec_sync`: a test hook (`debug.rec{start|stop}`, only in test firmware) records 70 s of audio with 30 s segments → all 3 segments stored, every SHA correct, the card emptied.
- `rec_resume`: run `cpa pause` halfway through a pull, then finish by offset after resuming.
- `rec_del_lost_ack`: drop the ack of `file.del`; after a resend the result is still correct.
- `rec_old_daemon`: a v0.2 daemon does not pull and reports no error.

## 10. Tests (written first, RED)

- **Firmware native** (`lib/rec`):
  - the `RecCore` state machine: every key in every state;
  - rotation timing; the WAV header; `.M` encode/decode (too many marks, bad JSON);
  - `plan_rec_recovery`: every row of §3.4, and combinations;
  - `file.get` validation: out of range, `enc`, the in-flight limit;
  - b64 lines ≤2048 B in the worst case;
  - atomic MonoId numbering.
- **FakeFatPort power-loss injection**: cut power at every step of rotation, finishing and deletion, then run recovery. Required: 0 committed segments lost; sessions link up; never delete a `.W` that has a `.M`.
- **Daemon**: the puller (tail cut on resume, bad-chunk refetch, header refetch after `final` flips); integrity checks; atomic storing (crash injected at every step); idempotent `file.del`; binary framing (if built); `session.json` generation; working back the wall clock.
- **Contract**: the vectors and flows listed in §7.4.
- **Device tests** (`test/device`): `test_rec_budget` (the automatic part of G-A6), `test_rec_rotation` (a short version of G-A4).

## 11. Risks

| # | Risk | Mitigation |
|---|---|---|
| A1 | The HWCDC line rate is only about 200 KB/s | Binary sub-mode; ADPCM; USB drive fallback; overnight sync |
| A2 | No sound while recording | Marks and warnings use the screen and LED. A full fix would need a custom full-duplex I2S driver (one I2S port sending and receiving at once), and then the tones would be recorded too |
| A3 | A start delay of 0.7–0.9 s, so speech before the tone is lost | Teach "start talking after the tone"; the screen shows "Getting ready…" right after the press |
| A4 | Locked recording started by mistake in a pocket | 500 ms long-press threshold + tone + LED; silence auto-stop (open question); low-battery auto-stop |
| A5 | Concurrent I2C access by the keyboard and the ES8311 | Verified in G-A5; a shared lock, or begin/end back in the loop |
| A6 | Low microphone level hurts transcription | G-A10 sets the gain |
| A7 | FAT leaves lost clusters after power loss, or a directory sector is damaged | fsync every second; recovery rules; Mac First Aid; same as 09 R3, R16 |
| A8 | Only one card tested | Same as 09 R4 |
| A9 | Recordings are plaintext on the SD card and the Mac | Open question; WWENC1 sealing as a later option |
| A10 | An outbox save queues behind a recording fsync and occasionally exceeds 150 ms | Counted in G-A2; if needed, postpone fsync during outbox saves |
| A11 | No wall clock after an offline reboot | The daemon works it back; if it cannot, mark with `~` |
| A12 | Unsynced limit of 256 segments | At the limit, refuse new recordings with a message; the number can be tuned |

## 12. Questions to decide

1. Gesture A (long press to start and lock, press again to stop) or B (push to talk)? Proposal: A.
2. Tone volume, and whether to have a "meeting silent" mode (LED and screen only). Changed 2026-10-21: recording is silent, for personal use; from v0.3.1 recording makes no sound at all (memos and meetings alike), using only the screen; the meeting confirmation box stays.
3. Segment length 5 minutes (proposed) or 2 minutes (as long as WhisperWoof's clips).
4. Should `.M` plus host pull replace 09 §6, §12's outbox `kind:"rec"`? Proposal: replace.
5. Message names `file.*` (proposed), or the `voice.*` reserved in 05 §6.10.
6. Run G-A1 first and then choose b64 or binary (proposed), or build binary directly.
7. Should the device have "discard this recording" (Fn+`` ` `` plus confirmation)?
8. Should there be silence auto-stop, and with what threshold (for example below the noise floor +6 dB for 15 minutes in a row)?
9. Is the unsynced limit of 256 segments right?
10. Are plaintext WAVs on the Mac acceptable, how long are they kept, and will `~/CardputerNotes` be cloud-synced?

## 13. Impact on existing docs (to be changed by the Lead)

- **04 §7.7**: drop "pre-roll before the press"; `.wav.part` plus rename becomes writing the final file name directly and committing with `.M`; "rename after recording, compute SHA-256, register in the outbox" becomes computing SHA-256 incrementally while recording, pulled by the host. **§8.2**: G0 long press becomes start recording.
- **05 §6.10**: decide whether the `voice.*` reservation stays; add a section on `file.*`; add §7.4's items to `protocol/limits.json`.
- **09 §6's "v0.3 reservation" and §12**: the outbox `kind:"rec"` is replaced by §7.1 of this doc; `max_files` stays 3 (1 for recording writes, 1 for transfer reads, 1 for the outbox); the SD lock-up flag becomes shared across tasks. **09 §5 budget**: add the heap targets in the recording state and the audio task stack target (§4.2).
- The "send to Mac" row of **sd-card.md §8**: point it at §7 of this doc.
- **New ADR**: recording transfer uses host pull plus `file.*`.

## Appendix: external sources

- M5Stack's official Cardputer-Adv page (battery, current, amplifier and codec, RGB LED, SD pins): https://docs.m5stack.com/en/core/Cardputer-Adv
- Espressif USB FAQ (FIFO limit about 500 KB/s; the S3's Serial/JTAG and OTG can be used only one at a time): https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html
- WhisperWoof (MIT), commit `767ae8d2315507dd0dca31839e4552e0e4baa175`: `docs/design/local-meeting-transcription.md`, `src/whisperwoof/bridge/file-import.js`, `src/whisperwoof/bridge/user-audio-file.js`, `src/whisperwoof/bridge/telegram-sync.js`, `plugins/telegram-companion/index.js`, `src/whisperwoof/bridge/vault/wwenc-pure.js`
