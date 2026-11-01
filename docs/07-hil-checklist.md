**English** · [中文](07-hil-checklist.zh-CN.md)

# 07 · HIL checklist (v0.1, v0.2, v0.3, v0.4.0)

> The deliverable of 06 WP5. The first half is the automated scenario results that `tools/hil/run_e2e.py` produced on the real device (05 §9.3 items 1–11). The second half is the checks that need a person's hands or eyes, or another piece of hardware.
> **The v0.2 part (M2-D, doc 10) is in [§5](#5-v02-m2-sd-migration-and-launcher)**: device tests, SD / migration / Launcher scenarios, Launcher gates G-L1–G-L9 and the SD manual items. §0–§4 are the v0.1 record, kept as they were.
> **The v0.3 part (M3-H, doc 12) is in [§6](#6-v03-m3-recording)**: recording device tests (G-A6), recording HIL scenarios (G-A2, G-A4), the v0.2 regression (G-R) and the recording manual items (G-A5 listening by ear (not applicable from v0.3.1), G-A7, G-A8, G-A9, G-A10, G-A12, G-G5, G-W2, G-E2E).
> **The v0.4.0 part (V40-H, doc 13 §17) is in [§7](#7-v040-home-tab-menu-lock-screen)**: device tests and the HIL scenarios `nav` / `menu` / `lock` for Home, the Tab menu and the lock screen; the v0.3.2 cold start `test_mic_cold` / `rec_cold_start`; the v0.1–v0.3.2 regression (card in and nosd); and two manual checks (carry the device on the lock screen for a day, read through the wording of the meeting confirm dialog). The real-device part is **waiting for the Lead to run it**, and the result fields are left blank.
> **Marks**: `[x]` = done, with the date and result recorded; `[ ]` = not done yet. Items marked **needs user** need you in person (pressing keys, flipping the switch, pulling cables, looking at the screen, another board, a brand-new macOS account, a logged-in claude). The agents in the orchestration do not have these and cannot do them for you. Their result fields are left blank, and **no result has been faked**.
> The acceptance for 06 WP5 requires every item in this list to be checked (`! grep -q '\[ \]'`). Until the needs user items are done, this acceptance fails by design.

## 0. Environment

| Item | Value |
|---|---|
| Device | Cardputer ADV, MAC `02:00:00:00:00:01`, USB serial number same as the MAC, port `/dev/cu.usbmodem1101` |
| Firmware | `cardputer-adv-test` (`-DCPA_TEST_HOOKS=1`), `cpa-fw/0.1.0`, built and flashed 2026-10-19 20:33 (after the WP4 fix; the source has no newer changes) |
| Mac side | `cpad 0.1.0` (this repo's `daemon/`, Python 3.12.13), running inside the `run_e2e.py` process, with a temporary `$CPA_HOME`, a temporary notes_dir and a temporary project directory |
| Claude | `daemon/tests/fixtures/bin/fake-claude` (the output shape of 2.1.272), set through the config's `claude_bin` + `spawn_env`; **no** real Claude session was started |
| System | macOS 26 (Apple Silicon) |

## 1. How to run

Run from the repo root (the script itself checks whether another program holds the port, then sets up the daemon in a temp directory and deletes it at the end):

```bash
cpa pause 1 || true     # if you installed the cpad service, have it release the serial port first (or uv run --project daemon cpa pause 600)
uv run --project daemon python tools/hil/run_e2e.py --scenarios all --report "$TMPDIR/hil.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 60 --report "$TMPDIR/soak.json"
uv run --project daemon pytest tools/hil/tests -q   # the script's own unit tests
```

- `--scenarios` can be a comma-separated list; `all` = items 1–10 in the table below. `soak` and `real_claude_smoke` only run when named, and the latter must also be run on its own with `--project-dir`.
- Reports: the file given by `--report` holds one JSON object per scenario (`ok`, `checks[]`, metrics). Each scenario is also written to its own `<报告名>.<场景>.json` (report name, scenario) and printed line by line on stdout. A failed scenario also carries the daemon state and the last 60 serial lines (`wire_tail`). Every scenario carries `device_not_ready`: how many times the device replied `E_NOT_READY` (see §3).
- `--keep` keeps the temp directory (`$CPA_HOME/logs/cpad.log`, `events.jsonl`, the notes, fake-claude's `calls.jsonl`) for debugging.

### How it is implemented (where it differs from the literal wording of 05 §9.3)

| Scenario | How |
|---|---|
| Driving the device | The daemon runs inside the test process. The test firmware's `debug.key` / `debug.state` / `debug.reboot` go through the daemon's own `DeviceLink.request_raw` (the same IDs, credit window and blank-line nudge). The serial object is wrapped in a tap (`serial_factory`) that records every line in both directions; SafeOpen runs as usual |
| 3 offline catch-up, 8 offline spawn | The device accepts `debug.key` only while Ready. So while it is Ready, all the keys are pushed into the device's injection queue in one batch (16 entries): first 4 lone Shift presses of 5 seconds each (they produce no key event), and only then the real keys. Right after that, `cpa pause 120`. After 15 seconds without data the device falls back to Attached (offline), and only after that do the real keys come up. Because of the queue length, each of the 3 notes in the offline catch-up is 2 characters |
| 4 dropped ack | cpad has no test switch. The tap swallows the first `outbox.ack` before it is written to the serial port (the daemon believes it was sent); the device resends after 3 seconds, and the daemon replies `duplicate` |
| 9 latency | Board: from writing fake-claude's `state.json` to the first `debug.state` reply that shows the new count (host timing, including the 0.1-second polling interval, so on the conservative side). Each flip comes right after the previous one was seen, that is just after the daemon's 3-second poll, close to the worst phase. Compose: device timestamps `overlay_ms − key_ms` (G0 press to Compose open; the injected G0 is held 50 ms, and G0Tap fires on release); the host's time from sending to seeing Compose is recorded as well |
| 10 Chinese | The names come from the real session names on this Mac quoted in 06 and from the daemon's redacted fixtures; `~/.claude` was not read. Missing glyphs are checked character by character on the host against the `efontCN_12` glyph table the firmware links (`harness/glyphs.py`, which reads the M5GFX source in the PlatformIO libdeps). How it actually looks on the screen needs a person (§4 item 9) |
| 11 soak | The daemon's `poll_s` is set to 1 second for the test, fake-claude flips the state once a second, the test script sends an extra `ping` every second, and a note is typed on the device every minute. "TX stuck" = any host request with no reply within 5 seconds |

## 2. Automated scenario results

**Final full acceptance run (after the plug_filter fix)**: 2026-10-19. Firmware `cardputer-adv-test`, built at 21:44, including the `lib/link/plug_filter` debounce fix (see §3), flashed at 21:46 with `pio run -e cardputer-adv-test -t upload`, no keys pressed.

- 21:46–21:53: `--scenarios handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk`, **10/10 PASS, exit code 0**.
- 21:53–22:03: `--scenarios soak --minutes 10`, **PASS, exit code 0**.
- Before flashing, native tests passed 215/215, and the 121 tests in `tools/hil/tests` passed.
- Results before the fix: the full run at 21:29–21:35 was also 10/10 PASS. An earlier run the same day was 9/10: the latency scenario aborted because of the issue in §3. The "before the fix" values in parentheses below all come from these runs.

- [x] **1 Handshake without reset** (2026-10-19, PASS)
  - First handshake 0.003 s; reopening the port after `cpa pause` also 0.003 s. Both sent only 1 hello (limit 3 s). Before the fix: 0.003–0.029 s.
  - The boot was `039T61N-0000E` before and after, unchanged. `up_ms` grew continuously from 7088 to 9168, 0 ms off the host clock. The daemon did not detect a reboot.
  - First boot after flashing, `rst=unknown`. This is normal: for a reset triggered over USB, the firmware reads the reason as 0, see 03 §4.
- [x] **2 Quick note round trip** (2026-10-19, PASS)
  - G0 → type → Enter; the note appeared in notes_dir after 0.103 s (limit 2 s). Before the fix: 0.101–0.122 s.
  - Written only once, one `item.received`, device outbox emptied.
- [x] **3 Offline catch-up** (2026-10-19, PASS)
  - 0 items stored during the pause. After resuming, the device's hello reported `pending=3`.
  - Each of the 3 notes appeared once, exactly 3 new ones, outbox emptied.
- [x] **4 Dropped ack dedupe** (2026-10-19, PASS)
  - The first ack was swallowed. The device sent 2 times in all, and the second time the daemon replied `duplicate`.
  - One note, one `item.received`, outbox emptied.
- [x] **5 debug.reboot resync** (2026-10-19, PASS)
  - The boot id went from `039T61N-0000E` to `-0000F`. The new `up_ms` was 1444, `rst=software`.
  - The daemon counted 1 reboot and resent `time.set` and the three snapshots. Ready again after 1.65 s, after which the device revs matched the host. Before the fix: 1.65–5.06 s.
  - There was 1 `E_NOT_READY` in this scenario, which is expected: the freshly rebooted device is in Attached, the daemon's next ping gets it, and then the handshake runs again.
- [x] **6 Board counts** (2026-10-19, PASS)
  - fake-claude gives project 1 two sessions (busy + waiting). On the device, slot 1 shows `run=1 need=1`, updated in 0.15 s (limit 5 s).
  - Before the fix: 2.5–3.1 s. This time depends on how long it is until the daemon's next 3-second poll when fake-claude is written; this time it landed just before a poll. For the worst phase see item 9.
- [x] **7 Spawn argv** (2026-10-19, PASS)
  - fake-claude was called only once, with argv `--bg --name cpa/hil-spawn-gw5m --permission-mode plan -- hil spawn gw5m` and cwd the project root (realpath).
  - The device received `agent.result ok`, short `a0000001`. There is an `agent.spawned` event.
- [x] **8 Offline spawn** (2026-10-19, PASS)
  - Confirm was pressed while offline; after resuming, the device's hello reported `pending=1`.
  - The daemon logged `queue.added`: qid `q1`, intent `spawn`, origin `device`, not uncertain.
  - fake-claude opened 0 sessions, the device never sent `agent.spawn`, and project 1 on the device shows the queue count.
- [x] **9 Latency** (2026-10-19, PASS)
  - Board: 100 flips, `p95_board_s` = 3.098 s (limit 5 s). p50 3.015 s, max 3.128 s, poll 3 s.
  - Compose: 30 G0 presses, `p95_compose_ms` = 51 ms (limit 300 ms, device timestamps). The host saw p95 112.9 ms.
  - The scenario ran for 309.8 s with 0 `E_NOT_READY`. Before the fix, the first full run had 1 at 168 s (§3).
- [x] **10 Chinese rendering** (2026-10-19, PASS)
  - Snapshots with 6 Chinese names were acked, with no error replies. One is a 54-byte long name, which the daemon cut at a character boundary to `登录页面改版的会议录` (30 bytes).
  - The project page and the inbox were both opened. `efontCN_12` (7545 glyphs) has **0 missing glyphs** for all the names and project names.
- [x] **11 Soak 10 minutes, after the fix** (2026-10-19 21:53–22:03, PASS; **the orchestration asked for only 10 minutes this time**)
  - 10 notes typed on the device, 0 lost, 0 duplicated.
  - 600 flips, one per second, 1126 snapshots. All 2028 host requests got replies, TX stuck 0 times. Reply times p50 2.8 ms, p95 22.0 ms, max 112.3 ms; 0 over 1 s; 0 blank-line nudges.
  - The port was never reopened, and the outbox was empty at the end.
  - **`device_not_ready` = 0**. For the whole run the cpad log has only the one `device ready` at the start (boot `039T61N-0000F`), with no second handshake on the same link.
- [x] **11 Soak 20 minutes, before the fix** (2026-10-19, PASS; the orchestration asked for only 20 minutes)
  - 20 notes typed on the device, 0 lost, 0 duplicated.
  - 1199 flips, all 4076 requests got replies, TX stuck 0 times.
  - But `device_not_ready` = 4: cpad redid the handshake 3 times on the same link (+293 s, +667 s, +1065 s, boot unchanged). This is the issue in §3.
- What the fix means statistically: before it, Ready dropped about once every 300–400 s (4 `E_NOT_READY` and 3 re-handshakes in the 20-minute soak). After it, the latency scenario plus the soak spent about 920 s under continuous traffic with 0 drops. At the old rate, 2–3 would be expected, and the chance of seeing none is about 5–10%. This is consistent with "the fix works", but the time is too short to count as proof. The 60-minute soak (next item) is G6's verdict.
- [ ] **needs user** · **11 Soak 60 minutes** (G6's v0.1 requirement)
  - How: set aside 1 hour and run `uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 60 --report "$TMPDIR/soak.json"`.
  - Pass if: `soak.ok` in the report is true (0 lost, 0 duplicated, TX stuck 0 times). Also look at `soak.device_not_ready`, which should be 0 after the fix.
  - Date: ＿＿ Result: ＿＿

## 3. Issues found

- [ ] **needs user** (firmware owner) · **With USB plugged in all the time and the host Ready all the time, the device occasionally drops out of Ready** (it replies `E_NOT_READY`).
  - Evidence: in the first full acceptance run on 2026-10-19, at 168 s into the latency scenario, one `debug.state` got `error E_NOT_READY`. The script treated it as a failure and aborted at the time (it was later changed to wait for the re-handshake, resend, and count it); rerunning the same scenario alone for 310 s did not reproduce it. The 20-minute soak had 4 `E_NOT_READY`, and cpad's log recorded 3 extra `device ready` on the same link (+293 s, +667 s, +1065 s; the boot was always `039T61N-0000C` and there was no `link closed`, so it was neither a reboot nor a port reopen), 38 s, 52 s and 30 s after the last note was stored.
  - Inferred cause (not confirmed on the device): during this time the host sent data every second, so the device's 15 s no-data timeout cannot have fired. The only remaining path is `UsbTransport::plugged()` (`HWCDC::isPlugged()`) briefly going false → Detached → Attached. Lines 47–68 of `HWCDC.cpp` in Arduino core 2.0.17 use a FreeRTOS tick hook to check SOF: the number of missed ticks allowed (5) is reset only on "disconnected → connected". SOFs missed now and then while connected (for example from phase drift between the tick and the 1 kHz SOF, or ticks delayed by flash writes) keep adding up, and the 6th in total reports one "unplug".
  - Effect: cpad redoes the handshake by itself, so normally the status bar just flickers briefly and in-flight outbox items go back to be resent (dedupe guarantees no duplicates). But if an `agent.spawn` happens to be in flight, the device treats it as "result unknown" and stores it as an uncertain queue item, while the session on the Mac may already have been opened.
  - Date: ＿＿ Conclusion: ＿＿

## 4. Checks that need a person

When an item is done, change its `[ ]` to `[x]` and fill in the date and result.

- [ ] **needs user** · **1. G1 keyboard: copy-type for 10 minutes, CER ≤0.5%, no stall over 1 second**
  - How: flash the release build (`pio run -e cardputer-adv -t upload`), with the daemon running. Prepare a known English ASCII text and copy it sentence by sentence after pressing G0, saving each sentence with Enter (at most 500 bytes per item), for 10 minutes in all.
  - Pass if: compare the body text in that day's notes with the original character by character; CER = (substitutions + deletions + insertions) / characters in the original. Meanwhile watch for anything taking over 1 second to appear on the screen; write down the "ghost N reset N" (幽灵 N 重置 N) on the system page (Opt+M).
  - Date: ＿＿ CER: ＿＿ Longest stall: ＿＿ Ghost / reset: ＿＿
- [ ] **needs user** · **2. 10 real power cuts (G10)**
  - How: first put the daemon on `cpa pause 1800` (so items stay on the device). Each time: press G0 and type a numbered note; as soon as "Saved" (已保存) appears, **immediately** flip the side switch to OFF and unplug the USB cable, wait a few seconds, then plug it back in and flip back to ON. Repeat 10 times, then `cpa pause 1` to resume the daemon.
  - Pass if: all 10 notes appear in notes_dir, with no duplicates; the system page shows no "Invariant broken" (不变式被打破).
  - Date: ＿＿ Result: ＿＿/10, duplicates: ＿＿
- [ ] **needs user** · **3. Unplug and replug USB**
  - How: daemon running, device on `● 在线` (● online). Keep the switch ON, unplug the USB cable for 10 seconds (it runs on battery meanwhile; write 1–2 notes), then plug it back in. Repeat 3 times.
  - Pass if: when unplugged, the status bar changes to `○ 离线` (○ offline), and within a few seconds of plugging back in it returns to `● 在线`; the notes written offline sync over; the device did not reboot (boot on the system page unchanged).
  - Date: ＿＿ Result: ＿＿
- [ ] **needs user** · **4. Backlight off on battery, and waking**
  - How: unplug the USB cable (switch ON) and press no key for 60 seconds; then wake it once with a normal key and once with G0. Repeat once with a charger that **only charges and carries no data**.
  - Pass if: the backlight goes off after 60 seconds; a normal key only lights the screen and triggers no action; G0 lights it and goes straight into a quick note; with only the charger it is also treated as battery (backlight off after 60 seconds).
  - Date: ＿＿ Result: ＿＿
- [ ] **needs user** · **5. G0 long press and download mode (E5 drill)**
  - How: with the firmware running, hold G0 for over 0.5 seconds; then press it briefly once. Then do one E5 recovery drill: flip the side switch to OFF, unplug, wait a few seconds and plug back in.
  - Pass if: a long press shows "语音在 v0.2 支持" (voice comes in v0.2), and a short press opens a quick note; pressing G0 while running never enters download mode; the E5 recovery steps (README) work.
  - Date: ＿＿ Result: ＿＿
- [ ] **needs user** · **6. Plug in another ESP32-S3 board (E16)**
  - How: daemon running. (a) With a serial number bound, plug in another `303A:1001` board; (b) unbind with `cpa device forget`, unplug the Cardputer, and plug in only that board.
  - Pass if: (a) the daemon does not touch it at all; (b) it releases the port within 6 seconds and does not try again for 30 seconds (`last_release` / `backoff_s` in `cpa status`, `logs/cpad.log`).
  - Date: ＿＿ Result: ＿＿
- [ ] **needs user** · **7. Time the README quick start in a brand-new macOS account (≤15 minutes)**
  - How: create a new macOS user, start from `git clone`, and follow the README "Quick start" strictly until the device shows `● 在线` (● online) and the project list, timing the whole thing. This includes the key sequence in README step 3 for putting the factory firmware into download mode the first time (G0 + BtnRst, ⏳ not verified on the ADV).
  - Pass if: ≤15 minutes, with no steps outside the README; write down where you got stuck.
  - Date: ＿＿ Time taken: ＿＿ Problems: ＿＿
- [ ] **needs user** · **8. Real claude smoke test (manual trigger only)**
  - How: in a logged-in terminal (`claude auth status` shows `loggedIn: true`), with the test firmware connected, run `uv run --project daemon python tools/hil/run_e2e.py --scenarios real_claude_smoke --project-dir "$(mktemp -d)"`. It opens **one** real session from the device in plan mode (body "Reply with the single word OK"), confirms that it appears under that temp directory, then runs `claude stop` + `claude rm`.
  - Pass if: `real_claude_smoke.ok` in the report is true; it no longer appears in `claude agents --json --all`.
  - Date: ＿＿ Result: ＿＿
- [ ] **needs user** · **9. Visual check of Chinese rendering**
  - How: run `--scenarios cjk --keep` (or, in a real setup, open the project page with Opt+1 and the inbox with Opt+I) and look at the screen.
  - Pass if: the names are readable, there is no □ (the glyph table check found 0 missing), truncated long names do not end in half a character, and lines do not overlap.
  - Date: ＿＿ Result: ＿＿

## 5. v0.2 (M2): SD, migration and Launcher

> The deliverable of M2-D ([doc 10](10-m2-work-packages.md)). The automated part was run on 2026-10-20 by the agent with the device to itself. The Launcher gates and the SD manual items all need a person's hands; they are marked **needs user** and their result fields are left blank. The final rerun after §5.9 items 1 and 2 were fixed, and flashing the release build back, are in §5.10.

### 5.0 Environment

| Item | Value |
|---|---|
| Device | Same as §0 (MAC `02:00:00:00:00:01`, `/dev/cu.usbmodem1101`), USB powered, side switch OFF |
| SD card | Development card: SDXC 512GB, FAT32 (formatted by FatFs, 1 FAT, 32 KB clusters, see sd-card.md §3), with a `/cpa/` directory on it; in the whole time |
| Firmware | HEAD `a6114eb` of `release/v0.2.0` (PR #18; no local firmware changes; `tools/hil` has this run's changes, not yet committed). `cardputer-adv-test` (`-DCPA_TEST_HOOKS=1`) and `cardputer-adv-test-nosd` (adding `-DCPA_STORAGE_NO_SD=1`), both flashed with `pio run -e <env> -t upload`, no keys pressed, G0 not touched |
| Mac side | `cpad` inside the `run_e2e.py` process (this repo's `daemon/`), with a temporary `$CPA_HOME`, notes_dir and project directory, and fake-claude. The user's own LaunchAgent `cpad 0.1.0` stayed on `cpa pause 5400` the whole time (renewed along the way); the user's `~/CardputerNotes`, `~/.claude` and LaunchAgent config were not touched |
| Flashing caveat | `firmware/app/scripts/pre_upload.py` runs `cpa pause 120` before uploading, which **shortens** the long pause above to 120 s; after that the real cpad could connect to the device in the middle of HIL. So every `pio run -t upload` / `pio test` in this run used a `PATH` **without `cpa`** (holding only links to `pio`, `uv`, `uvx`), and the log says `pre_upload: skip: cpa not found`. See §5.9 item 2: fixed. The final rerun in §5.10 used the normal `PATH`, and the log says `pre_upload: skip: already paused for 3446 s` |

### 5.1 How to run

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*          # the serial port must be free
cd firmware/app
pio test -e cardputer-adv-test -f 'device/test_storage' -f 'device/test_sd_store' -f 'device/test_runtime_budget'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report "$TMPDIR/hil-sd.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios back_keys --report "$TMPDIR/hil-keys.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 10 --report "$TMPDIR/soak-sd.json"
# Migration (in the order of doc 10's M2-D acceptance; the state file carries the seeded items between the three stages)
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty --report "$TMPDIR/m0.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_seed --state "$TMPDIR/mig.json" --report "$TMPDIR/m1.json"
(cd firmware/app && pio run -e cardputer-adv-test -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_check --state "$TMPDIR/mig.json" --report "$TMPDIR/m2.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios migrate_drained --state "$TMPDIR/mig.json" --report "$TMPDIR/m3.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report "$TMPDIR/hil-nosd.json"
uv run --project daemon pytest tools/hil/tests -q
```

- New flags: `--state FILE` (only for `migrate_*`; each migration stage must be run on its own, and `migrate_check` / `migrate_drained` require the file to have been written by `migrate_seed`); `--expect-launcher` (`launcher_boot` must have it; with it, if the device's `hello.launcher` is not true, the whole run fails before the first scenario).
- `all` is still only the 10 v0.1 scenarios; the v0.2 scenarios must all be named. The report's `meta` gains `store`, `launcher` and `expect_launcher`; the `handshake` result also carries `store` / `launcher`.

### 5.2 How it is implemented

| Scenario | How |
|---|---|
| Writing many items "offline" (`sd_store`, `sd_bulk`, `migrate_seed`) | The device accepts `debug.key` only while Ready, and the injection queue holds only 16 entries, so there is no way to type dozens of notes while truly offline. Instead, the tap holds back the `outbox.item` lines the device sends (`Tap.hold_rx`) until the next device `hello`. The device stays Ready and takes keys as usual, but the daemon hears none of them, and every item stays in the device's storage: the same effect as offline. The next handshake (reboot or port reopen) ends the hold, and at the same time the device puts in-flight items back to pending (05 §5.8). The tap now passes only complete lines to the daemon (the daemon's own line splitter waits for the newline anyway) |
| `sd_store` | Hold → type 3 → `debug.state` pending 3 → `debug.reboot` → the new boot's hello reports `pending=3`, `store=sd` → each delivered exactly once |
| `sd_bulk` | Hold → type 100 (`<字母>00`–`<字母>99`, a letter followed by 00–99) → pending 100 → `cpa pause 2`, then resume (re-handshake) → timed from the device `hello` arriving after the reopen to the last item's `stored` ack being sent (both are host times on the tap) |
| Migration in three stages | `migrate_seed` (nosd): confirm `store=int` and 0 pending at the start, hold, type 20, write the device id, the boot number and the 20 bodies into `--state`; the hold lasts until the end of the run (nothing is delivered). `migrate_check` (test): first hello `store=sd`, `pending=20`, same device; each of the 20 exactly once, the ids received in this run are exactly the ids of these 20, and all start with the boot number from seeding (migration keeps ids); write the ids back into the state. `migrate_drained` (nosd): first hello `store=int`, `pending=0`, nothing received in this run |
| `launcher_boot` | The hello of the run's first handshake: `launcher==true`, `rst=="software"`, `store` one of the three. It must come before any scenario that reboots the device (the selection order already puts it first) |
| `back_keys` (#15) | `debug.key` injection: G0 opens Compose → one `bksp` while it is empty closes it; with one character, the first `bksp` deletes the character and stays in Compose, and only the second closes it; Opt+I inbox and Opt+M system page, each closed with one press of the top-left `` ` ``, back to board |

### 5.3 Device tests (`pio test -e cardputer-adv-test -f 'device/*'`, card in)

2026-10-20 17:00–17:05, `-f 'device/test_storage' -f 'device/test_sd_store' -f 'device/test_runtime_budget'` (`test_sd_absent` needs the card out, see §5.7 SD-3). Result: of 18 cases, 16 passed and 2 failed. Both failures are the v0.1 LittleFS save timings (§5.9 item 1).

- [x] **`test_runtime_budget`** (2026-10-20, PASS 2/2, SD mounted and the primary store, the workload includes one rejected ack on SD)
  - `stack_left=5844` B (≥4 KB) · `heap_free=235048` B (229.5 KB, ≥150 KB) · `heap_min=224484` B · `max_block=212980` B (208.0 KB, ≥64 KB).
  - `sent=22 pending=0 bad=1 store_errors=0 missing_glyphs=0`; 8 KB serial burst `got=8192 bad=0`.
- [x] **`test_sd_store`** (2026-10-20, PASS 7/7, root `/cpatest`, deleted after the test)
  - Mount: `state=3` (Ok) `size_mib=479772` `hz=20000000`; mount + `prepare()` **17.6 ms** (≤200 ms), heap added by mounting **15068 B** (≤16 KB, with only about 1.3 KB to spare).
  - 200 saves, each acked immediately: p50 9.1 ms, **p95 10.5 ms**, max 51.1 ms; the first item in a new epoch directory **120.9 ms** (≤250 ms).
  - 200 saves, all backlogged: p50 15.3 ms, **p95 20.2 ms**, max 90.9 ms (≤150 ms); first item 78.2 ms.
  - list restores the MonoId; LFS → SD migration of 50 items: moved 50, failed 0, 2805 ms in all (**56 ms per item**; 09 §5 estimated about 40 ms).
- [x] **`test_storage`** (2026-10-20, **FAIL 7/9**, run twice; follow-up in §5.9 item 1)
  - Passed: `cpafs` mount, label and size (`CPA_PART launcher=0 cpafs=1572864 legacy_spiffs=0`, 32 shards), read, write and rename, the NVS boot counter (`boot=20 (was 19)`), the shard list.
  - Failed (1st run): LittleFS 200 saves, immediate-ack p95 **101.4 ms** (p50 41.4 ms, max 107.4 ms), backlogged p95 **120.2 ms** (p50 47.0 ms, max 195.0 ms); both limits are 100 ms. 2nd run: immediate ack passed, backlogged still failed.
  - This is the LittleFS slowness v0.1 already had (09 §5 says "LittleFS with 200 backlogged items takes 108–158ms, a known issue"), and with SD as primary this path is not used. But the test still asserts ≤100 ms, so `-f 'device/*'` will not go all green. Handed back to the firmware owner (§5.9 item 1).
  - **Rerun after the fix (2026-10-20 17:48–17:50, PASS 9/9, run twice)**: the two timing cases were renamed `*_within_200ms` and assert only p95 ≤ 200 ms; the 100 ms target is now printed as `CPA_SAVE_TARGET`. 2nd run (`-v`): immediate-ack p95 74.9 ms (`met`), backlogged p95 147.0 ms, max 185.0 ms (`missed (known on LittleFS)`). See §5.10.
- [ ] **needs user** · **`test_sd_absent`**: needs the card out, see §5.7 SD-3.

### 5.4 Automated scenario results: `cardputer-adv-test`, card in (SD as primary)

Firmware flashed at 17:06 (`cpa-fw/0.2.0`, `hello.store=sd`, `launcher=false`).

- 17:07–17:14, first full run: 12/13. `handshake`'s `no_reboot_detected` failed: it requires the daemon's reboot count to be 0, but `sd_store` had already rebooted the device once in the same run (count 1). This is a harness problem, not a device one: it was changed to compare the count before and after the scenario (`scenarios/link.py`), and rerun.
- **17:14–17:22, second full run (the acceptance command): 13/13 PASS, exit code 0**; then `back_keys` 1/1 PASS.

- [x] **sd_empty** (2026-10-20, PASS) `hello.store=sd`, `pending=0` on connect.
- [x] **sd_store** (2026-10-20, PASS)
  - 3 items stored to SD while the daemon could not hear them, `debug.state` pending 3; after `debug.reboot`, the hello of the new boot (`039T61N-0000S` → `-0000T`) reported `store=sd`, `pending=3`.
  - Each of the 3 was delivered once, and the ids received in this scenario are exactly these 3 (the first run also passed). 1 `E_NOT_READY` at the reboot, expected (as in §2 item 5).
- [x] **sd_bulk** (2026-10-20, PASS)
  - 100 items backlogged on SD (typing took 50.7 s); at the re-handshake the hello reported `pending=100`.
  - From the device hello to the last `stored` ack: **1.55 s** (limit 5 s; first run 1.546 s). Each of the 100 exactly once, no other or duplicate ids, outbox empty at the end.
- [x] **The 10 v0.1 scenarios, SD as primary** (2026-10-20, 10/10 PASS)
  - handshake: first and reopen both 0.003 s, 1 hello; boot unchanged, `up_ms` continuous (drift 0 ms); reboot count 1 before and after the scenario (no new reboot). The result has `store=sd`, `launcher=false`, `rst=software`.
  - capture: Enter to note **0.104 s** (limit 2 s), written once.
  - offline_replay: truly offline (`cpa pause 120`), 3 items, `pending=3` on resume, each once.
  - dup_ack: the first ack swallowed, the device sent 2 times, the second got `duplicate`, one note.
  - reboot: Ready again in **1.34 s**, `rst=software`, `time.set` and the three snapshots resent, revs match.
  - board: slot 1 shows `run=1 need=1`, updated in 3.08 s (limit 5 s; depends on the phase relative to the daemon's 3 s poll).
  - spawn: argv identical to the letter (`--bg --name cpa/hil-spawn-… --permission-mode plan -- …`), cwd the project root.
  - offline_spawn: offline Confirm → `queue.added` (intent spawn, not uncertain), 0 fake-claude calls.
  - latency: board `p95_board_s` **3.116 s** (limit 5 s), Compose `p95_compose_ms` **50 ms** (limit 300 ms, device timestamps); the host saw p95 125.9 ms. 0 `E_NOT_READY` in 310.6 s.
  - cjk: 6 Chinese names acked, the long name cut to `登录页面改版的会议录`, 0 missing glyphs in `efontCN_12`.
- [x] **back_keys (#15 one key to go back)** (2026-10-20, PASS 8/8)
  - G0 opens Compose → `bksp` while it is empty → Compose closes (`board/none`).
  - After typing one character, the first `bksp` deletes it and it stays `board/compose`; only the second `bksp` closes it.
  - Opt+I inbox → `` ` `` → `board/none`; Opt+M system page → `` ` `` → `board/none`.
- [x] **Soak 10 minutes, SD as primary** (2026-10-20 17:22–17:32, PASS 6/6, exit code 0)
  - 10 notes typed on the device, 0 lost, 0 duplicated.
  - 600 flips, one per second, 1118 snapshots, all 2020 host requests got replies, TX stuck 0 times; replies p50 2.7 ms, p95 17.9 ms, max 37.4 ms; 0 over 1 s; 0 blank-line nudges.
  - The port was never reopened, `device_not_ready` = 0, outbox empty at the end.
  - The 60-minute soak is still the needs user item in §2 (v0.1's G6); this orchestration asked for only 10 minutes.

### 5.5 Migration (nosd → test → nosd) and nosd regression

- [x] **Migration in three stages** (2026-10-20 17:32–17:34, **4/4 PASS**, exit code 0 at every step; the firmware was switched each time with `pio run -e <env> -t upload`, which keeps NVS and `cpafs`)
  - Prerequisite `sd_empty` (test): `store=sd`, `pending=0`.
  - `migrate_seed` (nosd, 5/5): `store=int`, 0 pending at the start; 20 items (`mn00`–`mn19`) stored into `cpafs` while the daemon could not hear them, pending 20, none delivered; the state recorded boot `039T61N-0000W`.
  - `migrate_check` (test, 7/7): first hello `store=sd`, `pending=20`, same device; each of the 20 exactly once, and the ids received are exactly the 20 from `039T61N-0000W-0000` … `-000K` (migration kept the ids), with no other ids; outbox empty at the end.
  - `migrate_drained` (nosd, 5/5): first hello `store=int`, `pending=0`, `debug.state` pending 0, and nothing received in this run: migration had already emptied `cpafs`.
  - Not verified: the "Moving to the SD card i/n" (正在迁移到 SD 卡 i/n) screen shown when boot migration takes over 0.5 s, and `已迁移 20` (moved 20) on the system page. These need a person looking at the screen (20 items at §5.3's 56 ms each is about 1.1 s, so it should show).
- [x] **The 10 v0.1 scenarios, `cardputer-adv-test-nosd` (card still in)** (2026-10-20 17:33–17:40, **10/10 PASS, exit code 0**)
  - The handshake result has `store=int`, `launcher=false` (nosd does not touch the SD bus, and the internal `cpafs` is primary, as in v0.1); first and reopen handshakes both 0.003 s.
  - capture 0.101 s; reboot Ready again in 1.66 s; latency `p95_board_s` 3.092 s, `p95_compose_ms` 51 ms; cjk 0 missing glyphs.
  - Only the 1 expected `E_NOT_READY` in the reboot scenario. v0.1 behaviour unchanged.
- [x] **End state** (2026-10-20 17:40): as the orchestration asked, flashed back to `cardputer-adv-test` (not the release build of 08 §4 item 5, since the later rerun stage still needed it); `sd_empty` PASS (`store=sd`, `pending=0`). The user's own cpad was still paused (last `cpa pause 5400` at 17:41, until about 19:11), and the serial port was free. For the final state see §5.10: the release build was flashed back at 18:02 and the daemon resumed.

### 5.6 Launcher gates (launcher.md §8, 09 §9.4)

Prerequisite: the Launcher is the **2.9.1** release the Lead chose (the official release asset, 1,422,720 B; after downloading, check the size first, then record the SHA-256). Build the test image with the commands in §5.8.

- [x] **G-L1 Install the Launcher, then install our firmware (actually installed over serial)**
  - How: first do step 0 in §5.8. Install Launcher 2.9.1 with the web flasher or esptool; put `cpa-v0.2.0-hil.bin` on the card at `/cpa/fw/`; in the Launcher choose SD → this file → install. If the Launcher cannot read this 512GB card (quite likely the same ACMD41 problem, sd-card.md §9), use the Launcher's serial install `flash overwrite` instead; ask the user before proposing a fix upstream.
  - Pass if: our firmware boots; `hello` is normal; the third line of the system page is `启动 Launcher` (Boot Launcher); with the card in, the storage line is `存储 SD …` (Store SD …), and when booted without the card it is `存储 内部 448…K` (Store Flash 448…K) (`cpafs` = 0x70000; the size is only shown when internal is primary). Then run `uv run --project daemon python tools/hil/run_e2e.py --scenarios launcher_boot,handshake,capture,reboot --expect-launcher --report "$TMPDIR/hil-launcher.json"`, exit code 0 (`launcher_boot`: `hello.launcher==true`, `rst=="software"`).
  - Date: 2026-10-20 Result: **passed (partly)**. Launcher 2.9.1 (official release, 1,422,720 B, SHA-256 `2adff1ad67b5e08319dae682dc4edb01f1d857767c1738bdd0ccd097a8acb024`) was written to 0x0 with esptool; after flashing, a USB reset went straight into the Launcher (there was no ota partition yet). Then, with the Launcher serial command `flash firmware <名字> <大小>` (name, size) (READY/ACK chunks, about 10.7 KB/s), `factory-v0.9` (the factory app, 2,600,704 B) and `cpa-v0.2.0` (`cpa-v0.2.0-rc1-app.bin`, 880,240 B) were installed one after the other; both went on. The install finished after the user pressed RST and then Enter to get into the Launcher menu. Once our firmware booted, the daemon showed `link: ready fw=cpa-fw/0.2.0 store=sd launcher=yes`. Whether the Launcher can read this 512GB card directly: not tested (we went over serial, which does not need to read the card). The `launcher_boot` HIL was not run; the system page needs a visual check, left to the user. **Note**: as soon as the Launcher's serial port appears, the daemon rushes to connect to it. Run `cpa pause` first, or it interrupts the serial install (the daemon logs a `no_reply` and then releases the port). Also, a serial install **does not create the `cpafs` data partition** (`flash firmware` writes only the app). So under the Launcher, with an SD card in, storage goes to SD; with no card there is no storage (`store=none`). This is still to be verified.
- [ ] **needs user** · **G-L2 Four kinds of reset**
  - How: under the Launcher, one at a time: press the RST key; flip the power switch (OFF, then ON; it is only a real power cut with USB unplugged, 03 §2); `debug.reboot` (the HIL `reboot` scenario); a USB reset from `pio` (reset only, no upload).
  - Pass if: the first two go to the Launcher boot screen; the last two go straight back to our firmware.
  - Date: ＿＿ Result: RST ＿＿ switch ＿＿ debug.reboot ＿＿ USB reset ＿＿
- [ ] **needs user** · **G-L3 Switch back and forth with Bruce 1.16.1 10 times**
  - Pass if: our outbox, epoch and boot counter are all still there; Bruce's `spiffs` was not touched.
  - Date: ＿＿ Result: ＿＿/10
- [ ] **needs user** · **G-L4 `cpafs` capacity under the Launcher**
  - How: boot without the card (or install the image built with `--env cardputer-adv-test-nosd`), disconnect the daemon with `cpa pause 1800`, and keep saving notes until one is refused ("full", 已满).
  - Pass if: record the count (09 §5 estimates about 80) and fill it back into launcher.md 6.4 item 2. Finally, `cpa pause 1` to let them all sync.
  - Date: ＿＿ Count: ＿＿
- [x] **G-L5 "Back to the Launcher" (回到 Launcher) on the system page**: **not applicable to v0.2** (09 §1.2: v0.2 has no menu item to go back to the Launcher), not tested.
- [ ] **needs user** · **G-L6 Upgrading under the Launcher (09 §4.4)**
  - How: step 0: ↑N on the status bar is 0 (or boot once with the card in so migration empties `cpafs`, and confirm on the system page). Then install the new test image with an overwrite install (the "Use <label> partition" option the Launcher offers when space runs short, or serial `flash overwrite`). Do not delete the old version while leaving other versions around.
  - Pass if: not one confirmed item is lost; every duplicate the daemon receives is judged `duplicate` (no second `item.received` in `events.jsonl`).
  - Date: ＿＿ Result: ＿＿
- [ ] **needs user** · **G-L7 SD mount and verify at 20 / 25 MHz (bare board, with the expansion hat, with GPIO5 pulled high)**
  - Already have: the bare-board results at 20 / 25 MHz are in sd-card.md §4 (25 MHz actually runs at 20 MHz). This run's device `test_sd_store` also reports `hz=20000000` (§5.3).
  - Still missing: with the expansion hat and with GPIO5 pulled high. A person has to fit the hardware.
  - Date: ＿＿ Actual clock: ＿＿ Throughput: ＿＿
- [ ] **needs user** · **G-L8 The Launcher's USB drive mode mounts this card on macOS 26**
  - Note: the macOS USB drive fix (PR #448) is only in the beta; 2.9.1 does not have it. Test with the beta, or record "not supported in 2.9.1".
  - Pass if: it mounts, in ≤30 s.
  - Date: ＿＿ Version: ＿＿ Result: ＿＿
- [ ] **needs user** · **G-L9 Boot from the Launcher 10 times while the daemon is running (new, 09 §7 item 3)**
  - How: Mac connected, daemon (LaunchAgent) running, the test image installed under the Launcher. Each time: switch OFF, unplug, plug back in, switch ON (a true cold boot, which first shows the Launcher boot screen for about 5 s). With a stopwatch or by polling `cpa status`, record the time from power-on to `link: ready`; also watch `last_release` / `backoff_s` in `cpa status` and `logs/cpad.log` for a 30 s `no_reply` suspension.
  - Pass if: `link ready` within 12 s all 10 times. If not, report it to the Lead, who dispatches M2-C's conditional item (a bound device is suspended for only 3 s on `no_reply`).
  - Date: ＿＿ Times for the 10 boots: ＿＿ 30 s suspension seen: ＿＿

### 5.7 SD manual items (09 §9.4)

- [ ] **needs user** · **SD-1 10 real power cuts (SD as the primary store)**
  - How: card in, `cardputer-adv-test` or the release build, and the daemon on `cpa pause 1800` first. Each time: press G0 and type a numbered note; as soon as "Saved" (已保存) appears, **immediately** flip the switch to OFF and unplug, wait a few seconds, then plug back in and switch ON. After 10 times, `cpa pause 1`.
  - Pass if: all 10 appear in notes_dir with no duplicates; no "Store errors" (存储错误) on the system page. **Record it as low-strength evidence (R16)**: 0 loss on FAT is proved at the protocol level by the native `core/test_outbox_fat`; 10 manual power cuts are only a sample.
  - Date: ＿＿ Result: ＿＿/10, duplicates: ＿＿
- [ ] **needs user** · **SD-2 Pull the card while running**
  - Pass if: the save right after the card is pulled stalls at most once (≤2 s), and keys work normally after that; the save shows "Save failed (E_STORAGE)" (保存失败（E_STORAGE）), and the system page shows `存储错误 N，请重启` (Store errors N: restart); after putting the card back in and restarting, everything is normal again, and the items on the card that were not sent go out after the restart.
  - Date: ＿＿ Longest stall: ＿＿ Result: ＿＿
- [ ] **needs user** · **SD-3 Boot without a card: device `test_sd_absent`**
  - How: card out, `cpa pause 600`, `cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_sd_absent'`.
  - Pass if: `hal_sd_mount` returns Absent in ≤500 ms. If it takes longer, first record the MISO idle level, then try the GPIO39 internal pull-up. Also glance at the system page: `存储 内部 1.5M` (Store Flash 1.5M) + `SD：未检测到卡` (SD: no card).
  - Date: ＿＿ Time: ＿＿ Result: ＿＿
- [ ] **needs user** · **SD-4 Insert an exFAT card**
  - Pass if: the system page shows `SD 卡无法使用：请在 Mac 上格式化为 FAT32` (SD unusable: format FAT32 on a Mac), and storage falls back to internal.
  - Date: ＿＿ Result: ＿＿
- [ ] **needs user** · **SD-5 Reformat the development card with `diskutil`, then run device `test_sd_store` again**
  - How (the user has authorised erasing this card): put the card in the Mac's card reader, run `diskutil list` and **check the disk number twice**, then `diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN`; put the card back in the device, `cpa pause 600`, `cd firmware/app && pio test -e cardputer-adv-test -f 'device/test_sd_store'`.
  - Pass if: it passes; record p95, the time of the first item in a new directory and the write stalls (2 FATs, alignment unknown, R17), and compare them with the same card as formatted by FatFs in §5.3.
  - Date: ＿＿ Disk number (checked twice): ＿＿ p95: ＿＿ First item: ＿＿ Max: ＿＿
- [ ] **needs user** · **SD-6 Test 2–3 more cards (at least one ≤32GB, FAT32 from the factory)**
  - For each card, run device `test_sd_store` and HIL `sd_empty,sd_store`.
  - Date: ＿＿ Card / result: ＿＿
- [ ] **needs user** · **SD-7 Mac Disk Utility "First Aid" to look for lost clusters**
  - Do it after SD-1 and SD-2. Pass if: no cross-linked files; record the number of lost clusters (the device has no fsck, so lost clusters cannot be reclaimed, sd-card.md §6.3).
  - Date: ＿＿ Result: ＿＿

### 5.8 Manual install steps under the Launcher, and the commands to build the test image

```bash
# Build the merged test image (cpa-v0.2.0-hil.bin shows as cpa-v0.2.0-hil in the Launcher, no more than 20 characters)
cd firmware/app && pio run -e cardputer-adv-test
python3 scripts/make_image.py --env cardputer-adv-test --tag v0.2.0-hil --out "$TMPDIR/dist"
# The no-card version for G-L4: change the env to cardputer-adv-test-nosd
```

Built once with the commands above on 2026-10-20: `cpa-v0.2.0-hil.bin` 951,200 B, `cpa-v0.2.0-hil-app.bin` 885,664 B (partition table check passed).

Install (09 §4.4):
0. **Step 0**: empty the internal pending items: ↑N on the status bar is 0; or boot once with the card in so migration empties `cpafs`, and confirm on the system page. Do this in both directions (whole flash → Launcher, Launcher → whole flash).
1. Install Launcher 2.9.1 with the web flasher or esptool. **Do not hold G0 at power-on** unless you mean to enter the Launcher menu (the latch problem in 03 §2 only happens when G0 is held at power-up).
2. Put `cpa-v0.2.0-hil.bin` on the SD card at `/cpa/fw/` (Mac card reader, or the Launcher's USB drive mode).
3. In the Launcher, choose SD → `/cpa/fw/cpa-v0.2.0-hil.bin` → install. If it cannot read the card, use serial `flash overwrite`.
4. To upgrade, use an overwrite install ("Use <label> partition" or `flash overwrite`), or delete and reinstall right away; do not delete the old version and leave other versions lying around.
5. Writing `cpa-<tag>.bin` to 0x0 erases NVS (the epoch is generated again, which is safe); `pio run -t upload` does not touch NVS.

### 5.9 Issues found

- [x] (firmware owner, M2-A) · **1. Two LittleFS save timings in device `test_storage` fail**
  - Evidence: §5.3. 1st run: immediate-ack p95 101.4 ms, backlogged p95 120.2 ms (max 195.0 ms); 2nd run: the backlogged one still failed. Both limits are 100 ms.
  - 09 §5 already lists "LittleFS with 200 backlogged items takes 108–158 ms" as a known issue (with a card in, v0.2's outbox is on SD, p95 10–20 ms), but the test was not changed to match, so `pio test -e cardputer-adv-test -f 'device/*'` will not go all green. The firmware owner needs to decide: relax the LittleFS limit, mark it as known (print only, no assert), or fix the LittleFS slowness.
  - Date: 2026-10-20 Conclusion: **relaxed, with the regression guard kept**. The 100 ms target (G3) is asserted only on SD (`test_sd_store`). `test_storage` prints `CPA_SAVE_TARGET <label> p95_us=… target_us=100000 met|missed (known on LittleFS)` and asserts p95 ≤ 200 ms (`kLfsSaveP95MaxUs`; the 513 ms seen with shards lost would still be caught). Real-device rerun 9/9, twice (§5.3, §5.10). LittleFS itself did not get faster; it is still a known issue.
- [x] (firmware owner, M2-B, `firmware/app/scripts/pre_upload.py`) · **2. The `cpa pause 120` before upload shortens a longer pause**
  - `cpa pause N` means "pause for N seconds from now", not "pause for at least N seconds" (`daemon/src/cpad/app.py:348`: `_paused_until = clock() + seconds`; `cpa pause 1` relies on exactly this to end a pause). So after `cpa pause 5400`, a `pio run -t upload` / `pio test` turns the pause into 120 s. After that the user's own cpad could connect to the device in the middle of HIL or device tests, and test notes from the test firmware would land in the user's real notes_dir.
  - Workaround in this run: run pio in a `PATH` without `cpa` (§5.0). Suggestion: `pre_upload.py` checks `cpa status` first and does nothing if the pause is already longer; or the daemon gives `pause` an "only extend, never shorten" option.
  - Date: 2026-10-20 Conclusion: **fixed** (`pre_upload.py`). Before uploading it reads `paused_s` from `cpa status --json`: with ≥120 s left it skips (`skip: already paused for N s`), and only with less does it run `cpa pause 120`. If the status cannot be read it falls back to the old `cpa pause 120`, and the upload never fails because of it. Verified on the real device (§5.10, with `cpa` in `PATH`): after `cpa pause 3600`, two `pio test` runs and one `pio run -t upload`, the pause still had 3431.5 s left (not shortened), and the upload log says `pre_upload: skip: already paused for 3446 s`.
- [ ] (HIL owner, M2-D, `tools/hil/scenarios/link.py`) · **3. Run right after `sd_store`, `handshake` gets the hello from before the reboot**
  - Evidence: in one `--scenarios sd_store,handshake,capture` run in §5.10 (17:50:53), `handshake` was 4/6: `same_boot_after_reopen` before `039T61N-00013` / after `039T61N-00014`, `up_ms_continuous` 9506 → 4607 ms (drift −12032 ms). But `no_reboot_detected` passed (reboot count 1 before and after the scenario), and `-00014` is exactly the boot after `sd_store`'s own `debug.reboot`, so the device did not reboot during `handshake`. Right after that, `handshake,capture` run on its own (17:51:34) was 2/2 PASS, 6/6.
  - Cause: `handshake` uses `handshakes(tap.since())[-1]` as the handshake "before the reopen", which is the **first** device hello after the last port open. On a software reboot the USB serial does not disconnect and the tap gets no new `open` record, so the hello after the reboot is the second one in the same window, and "before the reopen" picked up the old `-00013` hello. The acceptance order in §5.4 is `sd_store` → `sd_bulk` → `handshake`, and `sd_bulk` redoes the handshake partway through, which hid this problem.
  - Suggestion: first add a failing case to `tools/hil/tests` with "two device hellos in one open window", then have `handshake` use the last device hello before the reopen (or `rig.link.hello` at the start of the scenario). Device and daemon need no change.
  - Date: 2026-10-20 Conclusion: ＿＿
- Recorded, not counted as issues:
  - The heap added by SD mounting, 15068 B, is only about 1.3 KB under the 16 KB limit (§5.3). A larger `max_files` or buffer later would go over it.
  - Migration takes 56 ms per item, slower than 09 §5's estimate of about 40 ms; 200 items take about 11 s, and it happens only once.
  - The harness's own problem was fixed in this run: `handshake`'s "no reboot" check used to require a reboot count of 0, which gave a false failure after `sd_store` had run in the same run (§5.4).

### 5.10 Final rerun after the fixes, flashing the release build back (2026-10-20 17:48–18:03)

Firmware tree: `a6114eb` plus the uncommitted fixes for §5.9 items 1 and 2 (only `scripts/pre_upload.py`, `test/device/test_storage` and a comment in `platformio.ini` changed; `src/` and the libraries did not, and the test app is still 885,664 B). This time `cpa` was in `PATH`. The user's own cpad was first put on `cpa pause 3600`, and `lsof` confirmed the serial port was free; no keys pressed, G0 not touched.

```bash
cd firmware/app
pio test -e cardputer-adv-test -f 'device/test_storage'        # run twice, the 2nd time with -v
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_store,handshake,capture --report "$TMPDIR/hil-rerun.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture --report "$TMPDIR/hil-hc.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios soak --minutes 10 --report "$TMPDIR/soak.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)        # release build
cpa pause 1 && cpa status
```

- [x] **`pre_upload.py` no longer shortens the pause** (§5.9 item 2): upload log `pre_upload: skip: already paused for 3446 s` (2974 s when uploading the release build); after `cpa pause 3600`, two `pio test` runs and one upload, the pause still had 3431.5 s left.
- [x] **device `test_storage`** (17:48–17:50, **PASS 9/9, twice**, 60.6 s / 44.3 s)
  - 2nd run (`-v`): immediate ack p50 31.4 ms, **p95 74.9 ms**, max 84.4 ms → `CPA_SAVE_TARGET acked … met`; backlogged p50 35.0 ms, **p95 147.0 ms**, max 185.0 ms → `missed (known on LittleFS)`, both within 200 ms. `CPA_PART launcher=0 cpafs=1572864 legacy_spiffs=0`, `boot=34 (was 33)`.
- [x] **HIL `sd_store,handshake,capture`** (17:50:53, 2/3, exit code 1: a harness problem, not a device one)
  - `sd_store` PASS 8/8: 3 items stored on SD; after `debug.reboot` (`039T61N-00013` → `-00014`) the hello reported `store=sd`, `pending=3`, each exactly once.
  - `capture` PASS 5/5: Enter to note 0.104 s.
  - `handshake` FAIL 4/6: `same_boot_after_reopen` and `up_ms_continuous` failed, because the hello "before the reopen" was taken to be the one from before the reboot. Reboot count 1 before and after the scenario; the device did not reboot. See §5.9 item 3.
- [x] **HIL `handshake,capture` run on its own** (17:51:34, **2/2 PASS, exit code 0**): `handshake` 6/6, first and reopen both 0.003 s, boot unchanged, drift 0 ms, `store=sd`; `capture` 0.103 s.
- [x] **Soak 10 minutes, SD as primary** (17:51:45–18:01:56, **PASS 6/6, exit code 0**)
  - 10 notes, 0 lost, 0 duplicated.
  - 600 flips, 1126 snapshots, all 2028 host requests got replies, TX stuck 0 times: p50 2.8 ms, p95 18.6 ms, max 37.7 ms; 0 over 1 s; 0 nudges.
  - The port was never reopened, `device_not_ready` = 0, outbox empty at the end.
- [x] **Release build `cardputer-adv` flashed back** (18:02:10–18:02:21, `pio run -e cardputer-adv -t upload`, SUCCESS)
  - RAM 63,444 B (19.4%), app 880,240 B written to 0x10000. After "Hard resetting via RTS pin" it booted normally and did not enter download mode.
  - There is no `debug.key` in `firmware.bin`.
- [x] **daemon resumed** (18:02:27): after `cpa pause 1`, `cpa status` shows `link: ready  dev=020000000001 fw=cpa-fw/0.2.0 pending=0`, boot `039T61N-00015`, `backoff_s` 0.
  - But the same line says `store=- launcher=-`. Reason: the user's LaunchAgent (`com.cardputer-assistant.cpad`, pid 86881) started at 10:31:17, before `4ce5e50` (14:51) added these two fields, so the process is still running the old code. The new `cpa` client shows the two fields, but the old process does not report them.
  - `store=sd` on the release build was checked separately with this repo's daemon: at 18:03:09, `cpa pause 600` first, then `run_e2e.py --scenarios handshake`, 6/6 PASS, `hello.store=sd`, `launcher=false`, `fw=cpa-fw/0.2.0`, `rst=unknown` (the value after esptool's RTS reset). At 18:03:17, `cpa pause 1` again; `link: ready` as above.
- [x] **needs user** (or Lead) · **Restart the LaunchAgent so that `cpa status` shows `store=sd`**
  - How: `launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad`, then look for `link: ready … store=sd launcher=false` in `cpa status`.
  - Why it was not done: this is the user's own service. It is an editable install of `daemon/`, so after a restart it would run the current working tree's daemon code against the real notes_dir. The orchestration only allowed the agent to use `cpa pause`, so it was left to the user or the Lead.
  - Date: 2026-10-21 Result: done by the agent on the Lead's instruction during the v0.3 final rerun (last item of §6.8): `cpa status` shows `link: ready  dev=020000000001 fw=cpa-fw/0.3.0 pending=0 store=sd launcher=no` (the new `cpa` shows `launcher=false` as `no`).

## 6. v0.3 (M3): recording

> Deliverable of M3-H phase 1 ([docs/12](12-m3-work-packages.md)). The automated part was run on 2026-10-21 by the agent, with the device to itself; items that need a human ear or hands (power cut, pulling the card, carrying it around for a day) or the real WhisperWoof are marked **needs user**, with the result left blank: **no result was faked**. Design basis: [docs/11](11-m3-design.md) §11, §14; gate measurements: [gates/m3-device-gates.md](gates/m3-device-gates.md); the SD driver fix: [gates/sd-card.md](gates/sd-card.md) §11.
> **The final rerun after fixing §6.7 items 1–5 is in §6.8** (2026-10-21 04:21–04:48): device suite 20/20, recording HIL 6/6 (`rec_soak` cut to 5 minutes), `rec_reboot` another 4/4, G-R 14/14; the device was flashed back to the v0.3 release build, and the user's cpad was restarted on the new code with `link: ready`, `store=sd`. §6.0–§6.5 are the record of the round before the fixes, kept as they were.
> **v0.3.1's gain calibration, check and recording regression are in §6.9** (2026-10-21 12:18–12:41): `kMicGain` set to `{9, 0xC4, 2}`, `mic_cal` 8/8, `mic_verify` 5/5, recording regression all passed; the device was flashed back to the v0.3.1 release build, and the user's cpad shows `link: ready`, `fw=cpa-fw/0.3.1`.
> **v0.3.2 (#31, the first recording after power-on): the device test `test_mic_cold`, HIL `rec_cold_start` and the regression are in §6.10** (2026-10-21 17:20–17:32): `test_mic_cold` 3/3, `rec_cold_start` 15/15 in 3 of 4 runs (run 1 had the volume too low and missed only the 3 "sound audible at the start" checks), recording regression 8/8; `mic_verify` did not run because the Mac's volume was 25; the device was flashed back to the v0.3.2 release build, and the user's cpad shows `link: ready`, `fw=cpa-fw/0.3.2`.

### 6.0 Environment

| Item | Value |
|---|---|
| Device | Same as §0 (MAC `02:00:00:00:00:01`, `/dev/cu.usbmodem1101`), USB powered |
| SD card | The same development card as §5.0 (512GB FAT32). The firmware links `third_party/SD` with CPA patch 5 applied (sd-card.md §11), so 9.6 MB 5-minute segments can be written |
| Firmware | HEAD `689f66a` of `release/v0.3.0` (PR #20, no local firmware changes). `cardputer-adv-test` and `cardputer-adv-test-nosd`, both flashed with `pio run -e <env> -t upload`; no key was pressed and G0 was never touched (every recording gesture went through `debug.key`). ⚠️ In this build the version string in hello and `.M` is still `cpa-fw/0.2.0` (§6.7 item 4) |
| Mac side | `cpad` inside the `run_e2e.py` process (this repo's `daemon/`), with a temporary `$CPA_HOME`, notes_dir and project directory, and fake-claude; on the WhisperWoof side, `tools/hil/harness/fakewoof.py` (a deterministic implementation of contract v1 that does not transcribe; the text is given by the scenario). The user's own LaunchAgent `cpad 0.1.0` was under `cpa pause 5400` throughout (extended along the way), and `lsof /dev/cu.usbmodem*` was empty every time before the device was touched; the user's `~/CardputerNotes`, `~/.claude` and LaunchAgent config were not touched |
| Privacy | The recordings contain only the room's noise floor. All WAVs, transcripts and summaries were deleted with the temporary directory at the end of each run; the only `--keep` (the first trial run) was deleted right after it was looked at. Reports and logs hold only numbers, ids and states |

### 6.1 How to run

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*          # the serial port must be free
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_rec_sd' -f 'device/test_rec_rotation' -f 'device/test_rec_budget' \
  -f 'device/test_runtime_budget' -f 'device/test_sd_store'          # G-A6 and the other device tests
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo,rec_meeting,rec_resume,rec_reboot,rec_live_hold,rec_rotation,rec_soak --report "$TMPDIR/hil-rec.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys --report "$TMPDIR/hil-v02-regress.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_nosd,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk --report "$TMPDIR/hil-nosd.json"
(cd firmware/app && pio run -e cardputer-adv-test -t upload)          # stay on the test firmware, waiting for the rerun
uv run --project daemon pytest tools/hil/tests -q                     # the harness's own unit tests (including fakewoof and the recording checks)
```

- New scenarios must all be named; `all` is still only v0.1's 10. `rec_nosd` needs the nosd firmware and cannot be in the same run as the other `rec_*` (`select.validate` rejects it outright).
- Timeouts: `rec_*` 600 s, `rec_rotation` 1200 s, `rec_soak` 1800 s (`select.TIMEOUT_S`). `rec_soak` is fixed at 10 minutes (G-A2) and ignores `--minutes`.
- In the report every `rec_*` scenario carries `transfer` (app-level effective throughput from the first `file.get` to the last `file.data` on the tap, per segment and in total) and the numbers from reconciling against the device clock (`start_latency_ms`, `diff_samples`).

### 6.2 How it is implemented

| Scenario | How |
|---|---|
| Start / stop recording | All through `debug.key` pressing real keys: memo = hold `g0` for `hold_ms` (G0Long starts at 500 ms, release stops); meeting = Opt+R → wait for `debug.state.overlay=confirm` → a lone Shift at 600 ms (past the 400 ms guard) → Enter; stopping is the same Opt+R → Shift → Enter. `debug.rec` is only used to set `seg_ms`. Projects: before a memo, Opt+1 first opens project 1 (the context slot); after a meeting starts, press `1` to tag it |
| Device-clock reconciliation (G-A4 and the sample-count checks) | `debug.state.key_ms` is the device's `millis()` at the last key press; `at.up_ms` in segment 0's `.M` is `millis()` right after `Mic.begin` (`audio_task.cpp` `begin_session`). Start latency = `at.up_ms −` the start key's `key_ms`; wall clock = the stop key's `key_ms − at.up_ms`; `diff_samples` = the sum of the segments' sample counts − wall clock × 16. The whole chain is on one device clock and unaffected by USB latency. A memo's stop time is G0 down + `hold_ms` |
| Marks | Read `key_ms` right after each Space press; expected session time = `key_ms − at.up_ms`, compared with the manifest's `marks[].t_ms`, tolerance ±400 ms (★ is stamped by the number of samples already written, so it can lag by one or two blocks at most) |
| Landing checks | Per segment: size = manifest `bytes`, the 44 B header byte-identical to the standard header, whole-file SHA-256 = manifest `sha256`, `bytes = 44 + 2 × samples`; the SHA-256 of `file[44:]` = the device's `.M` value in `rec.seg.stored` (`rcv` segments have none); session directory 0700, files 0600; the manifest passes the contract v1 reference validator |
| fakewoof | During the scenario it scans `<notes_dir>/recordings` every 0.5 s: valid manifest + audio checks pass → `status.json` (queued / transcribing / summarizing) → `transcript.json` / `.md` → `summary.md` → finally `result.json` (done / summary), all `.tmp` + fsync + rename, 0600; invalid manifest → `failed` + `E_INVALID` / `E_VERSION`. A memo whose text starts with "待办" (to-do) is a todo. A meeting gets 1 to-do |
| `rec_resume` | 3-segment meeting (30 s segments, 65 s recorded). Once the tap shows the second segment's `file.data` past 256 KiB, `cpa pause 4`; 1 s after the port is released, note the `.part` size; after reconnecting, the offset of the second segment's first `file.get` must be exactly `.part − 64 KiB` |
| `rec_live_hold` | The same 3-segment meeting; when the first segment has been pulled to 100 KiB, hold G0 for 4.5 s to start a memo. E_BUSY window = from the first `E_BUSY` returned to a `file.*` on the tap to the first pong / hello after it with `files.live:false`. Pass if: every `E_BUSY` in the window is `live`; every `file.*` sent in the window gets `E_BUSY`; at most 1 late `file.data` (the read the audio task had already taken before Start, 11 §4.7); the daemon sends no `file.*` more than 20 ms after the E_BUSY arrives (those within 20 ms are requests interleaved with the E_BUSY on the wire, which the daemon had not read yet at that point); the interrupted file does not restart from 0; the memo is queued ahead of the meeting's remaining segments |
| `rec_reboot` | (a) 10 s segments; once `debug.state` shows segment 1, record 5 s more, then `debug.reboot`: segment 1 must land as an `rcv` segment, with the session complete; loss = `up_ms` read before the reboot − (`at.up_ms` + samples / 16), at most 1.5 s. (b) As soon as `debug.state.rec.seg` turns 1 (20 ms polling), `debug.reboot`: the new segment's 44 B header only reaches the card at the first fsync 1 s later, so this is usually the "trailing gap" case: the session is closed with `gaps:[1]`. After every reboot, check that the new boot's hello is `store=sd` and carries `files` (if not, reboot once more so the session can still be checked, but this check is recorded as failed) |
| `rec_rotation` (G-A4) | `seg_ms` 10 s (the minimum `debug.rec` allows), 305 s recorded: 30 rotations, 31 segments. Every full segment must be exactly 160,000 samples, `t0_ms` contiguous, `diff_samples` within 1 block (2048 samples = 128 ms) |
| `rec_soak` (G-A2) | 10-minute meeting, default 5-minute segments (two 9.6 MB segments, exactly the write pattern that sd-card §11 fixed). During recording, one ping per second; fake-claude flips a session every second and the daemon with `poll_s=1` pushes `sessions.snapshot`. Only requests sent during recording are counted. "Dropped frames" are judged by device-clock reconciliation (`debug.state` has no mic dropped-block counter, §6.7 item 6) |
| `rec_nosd` | nosd firmware: hello has no `files`; `debug.state` has no `rec`; after holding G0 for 1.5 s there is no overlay and the outbox is unchanged; after Opt+R there is no confirm box; the daemon sends no `file.*` during the whole scenario |
| 30-day retention (dry check only) | After `rec_memo`, call `cpad.recordings.retention.due` with the daemon's in-memory index: not due now; due after 30 days + 1 minute; `transcript.json` and `summary.md` both present; no `rec.audio.swept` at all in this run. No files are actually deleted |

### 6.3 Device tests (G-A6 and others, `cardputer-adv-test`, card in)

2026-10-21 02:10–02:15, `pio test -v -e cardputer-adv-test -f 'device/test_rec_sd' -f 'device/test_rec_rotation' -f 'device/test_rec_budget' -f 'device/test_runtime_budget' -f 'device/test_sd_store'` (`pre_upload: skip: already paused`, the pause was not shortened). Result: 3 suites passed, 2 failed (§6.7 items 1 and 2).

- [x] **`test_rec_budget` (the automated part of G-A6)** (PASS 3/3, 20.1 s)
  - `CPA_REC_BUDGET idle_free=275532 idle_block=262132 rec_free=269080 rec_block=258036 xfer_free=274988 audio_stack_left=2404 ring_high=1`
  - Idle 269.1 KB / 256.0 KB (≥150 / ≥64 KB ✅); recording + cue 262.8 KB / 252.0 KB (≥96 / ≥32 KB ✅); transfer 268.5 KB (≥128 KB ✅); audio task stack headroom 2,404 B (≥1.5 KB ✅); the ring buffer backed up at most 1 block (of 6).
  - ⚠️ This is a stripped-down test program with only the audio task and SD, not the whole app (the screen, the three modules and the link are not started). For the whole app's idle heap see the next item; the whole app's heap while recording + cue was not measured (§6.7 item 6).
- [x] **`test_runtime_budget` (whole app, recording buffer allocated at boot)** (PASS 2/2, 17.2 s)
  - `CPA_BUDGET stack_left=5788 heap_free=185856 heap_min=176312 max_block=163828 sent=22 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3720`; `CPA_RX_DONE got=8192 bad=0`.
  - Free heap 181.5 KB, largest block 160.0 KB (≥150 / ≥64 KB ✅), loop stack headroom 5,788 B (≥4 KB ✅), audio task stack headroom 3,720 B. Compared with G-A0's v0.2 baseline (234,836 / 208,884, m3-device-gates §2) it is 48,980 B less; about 38 KB of that is the recording buffer allocated at boot (11 §4.7), and about 4 KB the 3rd file slot.
- [x] **`test_rec_rotation` (short version of G-A4)** (PASS 1/1, 108.3 s)
  - `CPA_ROT segments=4 samples=1521664 wall_ms=95000 expect=1520000 ring_high=1 stack_left=2372`: the three 30 s segments are all 480,000 samples (exact to the sample); total samples exceed the wall clock by 1,664 (104 ms, within 1 block).
- [ ] **`test_sd_store`** (**FAIL 6/7**, 28.3 s; §6.7 item 2)
  - The failing one is v0.2's `test_write_read_remount_within_200ms_and_16kb`: `CPA_SD_MOUNT us=18143 heap_delta=19204`, the mount adds 19,204 B of heap > 16,384 B. v0.2 had 15,068 B (§5.3); the extra ~4.1 KB is exactly the cost of 11 §4.5 raising `max_files` from 2 to 3, as the "noted" list in §5.9 already predicted.
  - The other 6 passed: mount 18.1 ms; 200 entries acked immediately p50 9.3 ms / p95 10.9 ms / max 49.1 ms, the first 76.4 ms; a 200-entry backlog p50 15.5 ms / p95 20.3 ms / max 79.2 ms; LFS → SD migration of 50 entries, 48 ms each.
- [ ] **`test_rec_sd`** (**crash**, §6.7 item 1)
  - `test_mount_and_clean` and `test_memo_commits_with_the_hardware_sha` passed; the 3rd, `test_cut_off_session_is_recovered_at_boot`, overflowed the stack on `loopTask` (`Stack canary watchpoint triggered (loopTask)`), and the device rebooted again and again, rerunning the first two each time; `pio test` was stopped by hand (Ctrl-C) and the app test firmware flashed back at once. The last two (`test_get_and_del`, `test_clean_up`) never ran, and `/cpatest` on the card was left with the files this test wrote (only under the test root, which the app does not touch); at 03:36 the first test of the `test_sd_store` rerun had already cleared `/cpatest` (§6.5 wrap-up).
- [x] **`test_mic_gain`** (new in v0.3.1, no card needed; 2026-10-21 **PASS in 3 of 4 runs**; run 1 came right after flashing from v0.3.0, see below; run 4, after flashing from the v0.3.1 release build, PASS)
  - How to run: `cpa pause 600`; `cd firmware/app && ~/.local/bin/pio test -v -e cardputer-adv-test -f 'device/test_mic_gain'`; afterwards `pio run -e cardputer-adv-test -t upload` to flash the app test firmware back (`mic_cal` needs it), or flash the release build back as in m3-device-gates §5.2 step 8.
  - It tests the real `MicRing` path: after `start({10, 0xC5, 2})`, REG14 / REG17 read back 0x1A / 0xC5 and the samples' greatest common divisor is 1 (`mag` 2 does not magnify); after `stop` it starts again with M5Unified's own values `{0, 0xBF, 16}` and reads back 0x10 / 0xBF (nothing left over from the previous setting), and every sample is a multiple of 8 (`over_sampling` 1); no register write ever took the fallback. Each start prints one `CPA_MIC …` line of numbers, two lines in all.
  - Pass if: all PASS. This is the precondition for `mic_cal`: if the registers cannot be written, `mic_cal` only sees gains that do not match.
  - Date: 2026-10-21 12:18–12:20 Result: **run 1 FAIL 2/3, runs 2 and 3 PASS 3/3** (11–16 s each). The two `CPA_MIC` lines (the same in runs 2 and 3): `pga=10 vol=0xC5 mag=2 reg14=0x1A reg17=0xC5 gcd=1 gain_failures=0`, `pga=0 vol=0xBF mag=16 reg14=0x10 reg17=0xBF gcd=8 gain_failures=0`.
  - Run 1 was the first right after flashing from the v0.3.0 release build (`cpa-fw/0.3.0`, which plays cues at start and end): at the `{10, 0xC5, 2}` setting, the first 512 ms, 8,192 samples, were **all 0** (`gcd=0`), yet the registers read back 0x1A / 0xC5 and `gain_failures=0`; the second half of the same run was normal (0x10 / 0xBF, `gcd=8`). Runs 2 and 3 both ran after a test image and passed.
  - It has not happened since: the 16 segments of `mic_cal`, the 2 of `mic_verify` and the 7 of the §6.9 recording regression: none of them starts with all zeros, including the first segment after flashing and the first after each reboot. Cause unknown; the guess is that the codec started from state left by the previous firmware (which had used the speaker); the v0.3.1 release build no longer turns the speaker on. The effect is about half a second of silence at the start of a recording. On 2026-10-21 it was run once more after flashing from the v0.3.1 release build (`cpa-fw/0.3.1`, already running, and never having turned the speaker on): **PASS 3/3** (16 s), and the start was not all zeros. This fits the "previous firmware used the speaker" guess; after v0.3.1 there will be no such previous firmware.

### 6.4 Automated scenario results: recording (`cardputer-adv-test`, card in)

Firmware flashed at 02:15 (`hello.store=sd`, `launcher=false`). 02:27–02:39: trial runs per scenario, fixing 3 problems in the harness itself (see the end of this section); **02:41:36–03:03:52: the acceptance command** (§6.1 above, item 3): **6/7 PASS, exit code 1**; the only failure was one harness check in `rec_live_hold` (item 5 below; the check has since been changed). After that, `rec_live_hold,rec_reboot` were run back to back twice more (see items 5 and 6).

- [x] **rec_memo** (PASS 18/18, 6.1 s)
  - Hold G0 for 4 s (Opt+1 first): one segment `seg-000.wav`, 3,328 ms, 53,248 samples, `lead_ms` 150, project `cpa`. From G0 down to the first sample **607 ms** (including the 500 ms long-press threshold; ≤1.0 s ✅. `at.up_ms` is taken after `Mic.begin` and before the first block fills, so it is about 48 ms less than the G-A5 hwtest's "key → first block" of 647 ms; the two agree). The sample count is 1,040 (65 ms) less than the device wall clock of "press + 4 s".
  - Landing: size, 44 B header, whole-file SHA-256 and the device's `.M` data SHA all correct; directory 0700, file 0600; the manifest passes contract v1. Pull 334.7 KB/s (106,540 B in 0.31 s; the floor for a 10 s memo is ≤6 s).
  - fakewoof wrote done / summary, `memo.intent=todo`; the daily note got exactly one line `- [ ] HH:MM #cpa 待办 HIL 便条 … [音频](…/seg-000.wav) ^cpa-<sid>` (待办 = to-do, 便条 = memo, 音频 = audio); `files_n` on the card back to 0, `rec.seg.stored` and `rec.seg.deleted` once each for the segment; no `queue.added` at all.
  - 30-day retention dry check: not due now; due after 30 days + 1 minute; `transcript.json` and `summary.md` both present; no `rec.audio.swept`.
- [x] **rec_meeting** (PASS 20/20, 84.2 s)
  - `seg_ms` 30 s, Opt+R → Enter → press `1` → one ★ each at 20 s and 50 s → at 70 s Opt+R → Enter: 3 segments, the first two exactly 480,000 samples each, `t0_ms` 0 / 30,000 / 60,000, `gaps` empty, `consent:"ack"`, project `cpa`, duration 70,528 ms.
  - Enter to first sample **368 ms** (≤0.8 s ✅; hwtest's "Enter → first block" was 424 ms, and minus the ~48 ms of the first block they agree). Total samples 192 (12 ms) less than the device wall clock.
  - The session times of the two ★ are 56 / 104 ms earlier than the key presses (the second is in segment 1, converted via `t0_ms`), within ±400 ms.
  - fakewoof → done / summary, 1 to-do; the daily note has one line `- HH:MM #cpa 会议：[HIL 会议：定价和交付](…/summary.md) 1 分钟 · 待办 1 ^cpa-<sid>` (会议 = meeting, 定价和交付 = pricing and delivery, 分钟 = minutes, 待办 = to-do); card emptied, no `queue.added`.
  - **App-level effective throughput 334.2 KB/s** (3 segments, 2,257,028 B in 6.6 s, 333.5–335.4 KB/s per segment; target ≥100 KB/s ✅); at this speed a 1-hour recording takes about **5.6 minutes** (floor ≤20 minutes ✅). This matches G-A1's prediction of "about 302–322 KB/s when reopening and closing the file per chunk", which shows that the app's pull bottleneck is reading the card, not the link.
- [x] **rec_resume** (PASS 10/10, 81.0 s)
  - After the second segment had been pulled to 256 KiB, `cpa pause 4`: `.part` 284,280 B; after reconnecting, the offset of the second segment's first `file.get` was **218,744 = 284,280 − 65,536** ✅; 2 connections, the session archived only once, `rec.seg.stored` / `rec.seg.deleted` once each per segment, SHA all correct, card emptied.
  - The second segment's 142.7 KB/s includes the 4 s pause and the reconnect; the other two segments 334.4 / 326.1 KB/s.
- [x] **rec_reboot** (PASS 18/18, 81.4 s; the same scenario was run 4 more times, see item 6)
  - (a) `debug.reboot` when segment 1 had about 5 s recorded: new boot `store=sd`, with `files`; segment 1 landed as an `rcv` segment (`recovered:[1]`), with the header already the standard one (the device backfilled it during recovery, so cpad's `repaired` is empty), the session complete (`gaps` empty), `consent` kept; **28 ms lost** (the last `up_ms` read before the reboot minus the end of what was recorded, within the 1 s fsync cadence).
  - (b) `debug.reboot` as soon as `debug.state` showed segment 1: new boot `store=sd`; segment 1's `.W` was deleted in the boot scan (its header had not been fsynced yet), and the session was closed with **`gaps:[1]` and only segment 0**; the manifest passes contract v1. **The trailing-gap case really was reached**. But in the 45 s after segment 0 was deleted, the daemon did not close the session; it was only archived after the harness reconnected with a `cpa pause 2` and the file list was fetched again (§6.7 item 5).
- [ ] **rec_live_hold** (acceptance run **FAIL 15/16**, 78.5 s; for after the check was changed, see below)
  - Hold G0 for 4.5 s when the first segment had been pulled to 103,500 B: the device returned 3 `E_BUSY msg:"live"` (the queued requests were flushed) and 1 late `file.data` (the read the audio task had already taken); no reply other than E_BUSY in the E_BUSY window; no other `file.data` until the pong with `live:false`; the interrupted file resumed from where it stopped and did not restart from 0; after the file list was fetched again **the memo was queued ahead of the meeting's remaining two segments**; SHA correct for both sessions, card emptied.
  - The failing check: `host_sent_no_file_requests_while_busy`. In the window the daemon sent 1 `file.get` (off 285,660), **0.1 ms after the first E_BUSY reached the tap**, and the device answered E_BUSY as usual. 0.1 ms cannot be the daemon reacting after reading E_BUSY (the tap records in the serial read thread, while the daemon only reads that line after going through the event loop); it is a request interleaved with the E_BUSY on the wire. 11 §5.4's host invariant says "after receiving E_BUSY". So the harness check was changed to "no `file.*` sent more than 20 ms after the E_BUSY reaches the tap", and the time of every in-window request is written to the report (`tools/hil/tests` has a matching case). The same interleaving happened once in the second trial run (the times were not recorded then).
  - **Rerun after the check change**: 03:04–03:09, `rec_live_hold,rec_reboot` twice back to back, **4/4 PASS**. `rec_live_hold` in the two runs: 2 / 3 `E_BUSY live`, 1 late `file.data` each; the second run again had 1 interleaved request (0.1 ms, off 280,140, the device answered E_BUSY); a single run before the acceptance run also passed (16/16, no interleaving). In all, in 3 of the 5 runs the window had 1 `file.*` sent by the daemon (the 2 with recorded times were both 0.1 ms), all answered with E_BUSY; not once did the daemon send one after reading E_BUSY.
- [x] **rec_reboot repeatability** (2 trial runs, 1 acceptance run, 2 reruns: 5 in all)
  - (a) Reboot mid-segment, recovered 5/5: `rcv` segment landed, session complete; loss 21 / 28 / 13 / −7 ms (the last is the `up_ms` read before the reboot being a few ms earlier than the end of the recording, which is normal).
  - (b) Reboot right after a rotation: 4 times `store=sd`, all 4 reached the trailing gap (`gaps:[1]`), and **all 4 needed a reconnect to close** (§6.7 item 5). **Once (the second trial run) the new boot came up with `store=int`, and neither hello nor pong carried `files`**: the device treated the card as unusable, and recording was unavailable for that whole boot; the harness did not yet have the "reboot once more" handling then, and even after 45 s + a reconnect no session arrived; one more `debug.reboot` brought it back to `store=sd`, and the boot scan recovered as usual (§6.7 item 3).
- [x] **rec_rotation (G-A4)** (PASS 11/11, 341.5 s)
  - `seg_ms` 10 s, 305.5 s recorded: **31 segments, 30 rotations, all 30 full segments exactly 160,000 samples**, `t0_ms` contiguous, no missing segments.
  - Total samples 4,886,528, device wall clock 305,509 ms: **1,616 samples (101 ms) short, within 1 block (2,048)** ✅. Start latency 367 ms. All 31 segments SHA correct; pull 332.0 KB/s (9.77 MB in 28.8 s, 318.7–336.4 KB/s per segment, 0 refetches).
  - Not measured: the worst rotation time and the ring buffer high-water mark (which G-A4 asks to record) cannot be had in HIL, since `debug.state` has no such numbers (§6.7 item 6); the device test `test_rec_rotation` reports `ring_high=1` (at most 1 of 6 blocks backed up).
- [x] **rec_soak (G-A2)** (PASS 15/15, 663.8 s)
  - 10-minute meeting (default 5-minute segments: 2 segments of 9,600,044 B + 1 tail segment); during recording 600 pings and 1,134 `sessions.snapshot`, **1,854 host requests in all, every one answered: p50 4.3 ms, p95 22.5 ms, max 38.6 ms** (p95 ≤100 ms ✅, 0 over 600 ms ✅), **0 `E_NOT_READY`** ✅, the port was never reopened, all 720 pongs `live:true`.
  - "Dropped frames": total samples are 1,856 (116 ms) short of the device wall clock (601,460 ms), within 1 block, and `t0_ms` is contiguous → no samples dropped ✅ (indirect evidence, §6.7 item 6).
  - Both 9.6 MB segments were written and landed with the SHA correct (the sd-card §11 driver fix works in the app); pulled 19.24 MB in 56.3 s, **334.1 KB/s**.
- Harness problems fixed during the trial runs (all in this commit's `tools/hil`; firmware and daemon not involved):
  - `rec_reboot` used to just wait for the timeout when the new boot had no card; now it records the new boot's `store` / `files`, and if it is not SD it marks a failure and reboots once more, so the session can be checked to the end.
  - "New sessions" in a run used to be told apart only by `rec.archived`; a session left on the card by an earlier run and closed with `gaps` partway through this run would be mistaken for this scenario's recording (`rec_live_hold` at 02:37). Now they are told apart by the set of sessions that are archived or already have a landed segment (`rec_common.known`), and pull progress is only read from the tap after this scenario's recording ends.
  - `rec_live_hold`'s host check got a 20 ms interleave grace period (item 5 above).

### 6.5 v0.2 regression (G-R)

- [x] **`cardputer-adv-test`, card in: v0.2's 14 scenarios** (03:10:06–03:17:23, **14/14 PASS, exit code 0**; `hello.store=sd`)
  - `sd_empty` 0 pending; `sd_store` 3 entries on SD, each delivered once across `debug.reboot`; `sd_bulk` 100-entry backlog, from re-handshake to the last ack **1.54 s** (≤5 s).
  - `handshake` 6/6 (boot unchanged after reconnecting); `capture` Enter to note **0.096 s**; `offline_replay`, `dup_ack` (the device sends twice, 1 note), `reboot` (Ready again in 1.28 s), `board` (3.05 s), `spawn`, `offline_spawn` (0 fake-claude calls) all passed.
  - `latency`: board p95 **3.08 s** (≤5 s), Compose p95 **50 ms** (device timestamps, ≤300 ms), p95 seen by the host 119.9 ms; `cjk` 6 Chinese names, `efontCN_12` 0 missing glyphs; `back_keys` 8/8.
- [x] **10-minute soak, SD as the main store** (03:17:33–03:27:33, **PASS 6/6**): 10 notes, 0 lost, 0 duplicated; all 2,048 host requests answered, p50 2.9 ms, p95 22.0 ms, max 57.6 ms, 0 TX stalls, 0 nudges, the port never reopened, outbox empty at the end (v0.2 §5.10 had p95 18.6 ms / max 37.7 ms).
- [x] **Migration in three phases (nosd → test → nosd, v0.3 firmware)** (03:27:47–03:28:41, **4/4 PASS**; firmware switched with `pio run -e <env> -t upload` each time, and the log says `pre_upload: skip: already paused for 53xx s`)
  - Precondition `sd_empty` (test): `store=sd`, `pending=0`.
  - `migrate_seed` (nosd): `store=int`, 20 entries (`mr00`–`mr19`) stored in `cpafs`, none delivered.
  - `migrate_check` (test): first hello `store=sd`, `pending=20`, same device; each of the 20 entries exactly once, every id starting with the boot at seeding time, `1833562-0000K`.
  - `migrate_drained` (nosd): first hello `store=int`, `pending=0`.
- [x] **`cardputer-adv-test-nosd`: `rec_nosd` + v0.1's 10 scenarios** (03:28:41–03:35:01, **11/11 PASS, exit code 0**; `hello.store=int`)
  - `rec_nosd` 6/6: neither hello nor `debug.state` has recording fields (no `files`, no `rec`); after holding G0 for 1.5 s there is no overlay and the outbox is unchanged; Opt+R shows no confirm box; the daemon did not send a single `file.*`.
  - `capture` 0.126 s; `reboot` 1.55 s; `board` 3.07 s; `latency` board p95 3.095 s, Compose p95 50 ms (host 127.3 ms); `cjk` 0 missing glyphs; the rest all passed.
- [x] **Wrap-up**: at 03:36, the device test `test_sd_store` once more (same result as §6.3: only the mount-heap check fails, `heap_delta=19204`; its first test clears `/cpatest`, so the files left by `test_rec_sd` are gone), then `pio run -e cardputer-adv-test -t upload` (RAM 66,152 B, 2,708 B more than v0.2's 63,444 B, ≤4 KB ✅), `sd_empty,handshake` 2/2 PASS (`store=sd`, `pending=0`, boot `1833562-0000Q`). **The device stays on `cardputer-adv-test` and the user's cpad stays paused**, waiting for the Lead's rerun; the release build was not flashed back.

### 6.6 Recording checks that need a person (11 §14, 12 "Real-device gates")

When an item is done, change `[ ]` to `[x]` and fill in the date and result. For anything that needs flashing, first `cpa pause <秒>` (seconds) and make sure `lsof /dev/cu.usbmodem*` is empty before touching the device; **do not hold G0 while powering on** (03 §2). G0 during recording is a normal gesture and can be pressed.

- [x] ~~**needs user** · **G-A5 by ear: set the cue volume** (about 2 minutes; blocks M3-A's volume constant)~~
  - Date: 2026-10-21 Conclusion: **not applicable**, not done. Requirement changed: recording is silent, for personal use. From v0.3.1 recording plays no cue at all; the meeting confirm box stays; the screen is the only cue. With no cue there is no volume to set, and `kToneVolume` is no longer needed. The three lines below are the original method, kept as a record.
  - How: per m3-device-gates.md §4.5: `cpa pause 900`; `cd firmware/hwtest && pio run -e cardputer-adv -t upload`; `uv run --no-project --with pyserial==3.5 python host/hwtest_host.py cue-listen --vols 32,64,128,192,255` (the terminal first prints "now playing …", and each volume in turn plays the memo start cue and the meeting's three-tone cue). Listen once with the device in a pocket and once on a desk; if needed, narrow it down with `--vols 48,64,96`.
  - Pass if: pick the lowest setting that **can still be heard from a pocket and is not harsh on a desk**; the meeting's three-tone cue should not be rude in a quiet meeting room. Write the number into `kToneVolume` in `firmware/app/lib/hal/audio_io.h` (now a placeholder, 64) and into m3-device-gates §4.5, then flash the app back.
  - ~~Date: ＿＿ Volume chosen: ＿＿ Pocket / desk impression: ＿＿~~
- [x] **needs user** · **G-A10 levels: HIL `mic_cal` calibration + `mic_verify` check** (about 25 minutes, about 7 minutes of it played out loud on the Mac; blocks v0.3.1's `kMicGain` and WhisperWoof's `CARDPUTER_SILENCE_RMS`)
  - From 2026-10-21 this replaces the old hwtest steps (which only tuned magnification / PGA and wrote the result into `kMicMagnification`, since removed). In v0.3.1 the gain is in the ES8311 (REG14 PGA, REG17 ADC volume) and `magnification` is 2; the constant is `kMicGain` in `firmware/app/lib/core/mic_gain.h`, a placeholder `{0, 0xBF, 16}` (v0.3.0's gain) before calibration, set to `{9, 0xC4, 2}` on 2026-10-21. Target: a sample peak of about −12 dBFS (−14 to −10) when holding the device and speaking at a normal level.
  - How: exactly the 9 steps of m3-device-gates.md §5.2.
    - ⚠️ The order is the reverse of usual: before running, cpad must be **resumed** (`cpa pause 1` after flashing the test firmware), and `cpa status --json` must show `on_device` 0 and `pulling` 0. preflight pauses cpad by itself.
    - Command: `uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_cal --report "$TMPDIR/mic-cal.json"`.
    - Change `kMicGain` (and `test_the_placeholder_keeps_v030` in the same commit), flash the test firmware again, `cpa pause 1`, then run `--scenarios mic_verify --report "$TMPDIR/mic-verify.json"`.
    - Finally flash `cardputer-adv` back, `cpa pause 1`, and `cpa status` shows `link: ready`.
  - Pass if: every `mic_cal` check is ok and `decision.ok` is true; `mic_verify`'s `kmicgain_verified` and the card-emptying checks are all ok. On an error, look at the end of `error` in the report: if it is `cleanup:`, you can simply rerun; if it is `cleanup FAILED`, work through "If the run fails" in §5.2, then `cpa pause 1`.
  - Mixed Chinese-English accuracy (MER) is not in `mic_cal`; whether to do it, and whether it blocks, is your call (end of §5.2).
  - Date: 2026-10-21 (run by the agent, with the device to itself; `mic_cal` 12:21–12:27, `mic_verify` 12:34–12:35) `kMicGain` (PGA / REG17 / mag): **9 / 0xC4 / 2** (+27 dB / +2.5 dB / no magnification) Expected peak / RMS / noise floor (dBFS): **−11.93 / −35.23 / −73.74** `lead_ms`: **150, unchanged** (5 runs at the new gain, 70–90 ms; `recommend_lead` 100) SILENCE_RMS: **0.000843** `mic_verify` gain / peak: **+11.74 dB (nominal +11.44) / −11.96 dBFS**
  - Conclusion: `mic_cal` 8/8 checks ok, `decision.ok` true, and two independent recomputations reached the same setting; `mic_verify` 5/5 (`kmicgain_verified` and card emptied both ok). The numbers for each setting and how the decision was made are in m3-device-gates.md §5.4; the rerun is in §6.9. Mixed Chinese-English accuracy (MER) was not done; it is still your call.
- [ ] **needs user** · **G-A7 battery life: recording (backlight dimmed) ≥8 hours** (overnight)
  - How: flash the release build `cardputer-adv` (v0.3) and charge it full; unplug USB (side switch ON, on battery). Opt+R → Enter to start a meeting and leave it alone: after 60 s the screen should dim (not go off), with `● REC` and the timer visible the whole time. A meeting ends by itself at the 3-hour limit (v0.3.0 has a meeting end cue; from v0.3.1 it is silent, so watch for the meeting page closing); **at once Opt+R → Enter to start another**, until the device stops by itself after the voltage has been ≤3.45 V for 10 s (v0.3.0: three low tones; from v0.3.1 silent, so look for "Battery low: can't record" (电量低，不能录音) on the screen), or a start reports low battery. At every start / automatic end, glance at `电池 NNNNmV` (Bat NNNNmV in English) on the System page (Opt+M) and note the time. Finally plug in USB and add up these sessions' durations in `cpa rec ls`.
  - Pass if: total recording time ≥8 hours. The noted voltages are used to calibrate the 3.45 V stop / 3.55 V warning thresholds (`kBatStopMv` in `rec_core.h`). ⚠️ docs/12 asks to "log bat_mv every minute", but on battery the device is not connected to a Mac, and the v0.3 firmware does not log voltage to the card either, so only the manual readings above are possible (§6.7 item 7).
  - Date: ＿＿ Total time: ＿＿ Readings (time / mV): ＿＿ Why it finally stopped: ＿＿
- [ ] **needs user** · **G-A8 power-cut recovery: 10 times, 3 of them exactly at a rotation**
  - How: v0.3 firmware with the card in (release or test build, either is fine); the daemon gets `cpa pause 3600` first (so the segments stay on the card). 7 ordinary power cuts: Opt+R → Enter to start a meeting, record 20–60 s, then flip the side switch to OFF and unplug USB, wait 5 s, plug back in and switch ON. 3 power cuts at a rotation: with the default 5-minute segments, watch the timer on the meeting page and cut the power between **05:00–05:01** (or 10:00–10:01) (the new segment's header only reaches the card 1 s later, so a power cut then is exactly the trailing-gap case). After each power-up wait 2 s (boot scan) and check whether the status bar's `录N` (RN in English) went up. When all are done, `cpa pause 1`, wait for `录N` to drop back to 0, and look at each session with `cpa rec ls` / `cpa rec show <sid>`; open the WAV of each session's last segment in QuickTime. Finally put the card in the Mac and run Disk Utility → First Aid.
  - Pass if: every boot recovers the segment being recorded as a playable WAV (`rcv:true`, `recovered:true` in the manifest); the session is joined up via `next`, or closed with gaps (for the 3 cuts at a rotation, `gaps` should contain the segment after the last one); everything syncs and `录N` returns to zero; First Aid finds no cross-links, and the number of lost clusters is written down. Two things to watch: (1) if on the first boot after a power cut the System page shows "SD unusable" (SD 卡无法使用) or the status bar has no `录N`, write it down: this is §6.7 item 3 (seen once on a software reboot) showing up under a real power cut; (2) a session with a trailing gap is only archived at the next reconnect (or the next recording) (§6.7 item 5); if needed, `cpa pause 2` once.
  - Date: ＿＿ Results of the 10 (recovered / gap / not playable): ＿＿ First Aid: ＿＿
- [ ] **needs user** · **G-A9 card pulled and card full during recording**
  - How: (1) Pulling the card: Opt+R → Enter to start a meeting, pull out the SD card after 20 s, and watch for screen stalls and messages (silent from v0.3.1); put the card back in, flip the switch to restart, `cpa pause 1` to sync. (2) Card full: with a card that **can be wiped**, first write a big file to it on the Mac so only about 30 MB is left (e.g. `mkfile -n <大小> /Volumes/<卡>/fill.bin`, with the size worked out from `df -h`; 大小 = size, 卡 = card), put it back in the device and power on (free space is read once at boot), and record a meeting until it stops; then try once more to start a recording with <8 MB left. Delete `fill.bin` afterwards.
  - Pass if: (1) after the card is pulled there is at most one stall (≤2 s), a failure cue (330 Hz × 3; from v0.3.1 no sound, the screen says "SD card error: recording stopped" (SD 卡出错，录音已停止) instead), and recording stops; after reinserting the card and restarting, the segment is recovered and synced. (2) When the card fills up, the segment closes normally (header backfilled, `.M` written), with the message "SD card full" (SD 卡已满), and the segment syncs and plays; with <8 MB left, starting is refused outright.
  - Date: ＿＿ Longest stall on card pull: ＿＿ Card-full result: ＿＿
- [ ] **needs user** · **G-A12 feel: a day of use**
  - How: release build v0.3, carried normally for a day: start and stop memos (hold G0) and meetings (Opt+R → Enter, with G0 / Space for ★ during them) 20 times each.
  - Pass if: the cues are audible but not annoying (not applicable from 2026-10-21: v0.3.1 recording makes no sound at all); the meeting confirm box ("Tell everyone here first" (请先告知在场的人)) is not a bother; note the number of accidental recording starts in a pocket; `● REC` is readable when the screen is dimmed.
  - Date: ＿＿ Accidental starts: ＿＿ Impressions: ＿＿
- [ ] **needs user** · **G-G5 summary quality (Mac side)**
  - How: WhisperWoof dev build (the 4 PRs of M3-W), with Qwen3.5 2B chosen in Intelligence. One bench-style recording (with clear to-dos and owners) plus 2–3 device recordings (with ★), through the Cardputer inbox to produce `summary.md`.
  - Pass if (scored by hand): to-do recall; **0 made-up to-dos**; every ★ appears under "重点（你的标记）" (highlights, your marks); also check whether the window of 30 s before / 10 s after each ★ is right.
  - Date: ＿＿ Recall: ＿＿ Made up: ＿＿ ★ coverage: ＿＿
- [ ] **needs user** (person + agent) · **G-W2 dictation does not slow down during background summarisation** (blocks merging W2)
  - How: WhisperWoof dev build, real llama-server. First measure the baseline latency of 20 dictation polishes; then have the inbox summarise a 60-minute recording while dictating 20 times.
  - Pass if: polish p95 is ≤300 ms above the baseline; 0 silent fallbacks to the raw text because of the 3 s timeout (`POLISH_TIMEOUT`); after the background call is aborted, llama-server can take the next request at once (otherwise switch the background calls to `stream:true` or add a second slot).
  - Date: ＿＿ Baseline p95 / p95 while summarising: ＿＿ Timeouts: ＿＿
- [ ] **needs user** · **G-E2E end to end (real WhisperWoof)** (blocks release)
  - How: flash the v0.3 release build; the user's own cpad runs the v0.3 code (`launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad`); in WhisperWoof (with the Cardputer inbox), the inbox is turned on in settings and the models are downloaded. Unplug USB, start a 20-minute meeting offline (Opt+R → Enter), mark 3 ★, and tag it with a project by pressing the project slot number on the meeting page; then record a memo that starts with "待办" (to-do); then plug in USB.
  - Pass if (within the time budgets of 11 §10): `summary.md` has 3 entries under "重点" (highlights) and ≥1 under "待办" (to-dos), each linking back to `transcript.md`; that day's daily note has the meeting line and one `- [ ]` memo line; the device's `录N` is back to 0; `cpa rec ls` shows done; no queue items were created at all (open queue items in `cpa status` unchanged); from plugging in to the memo appearing in the daily note, p95 ≤60 s.
  - Date: ＿＿ Result: ＿＿

### 6.7 Issues found

- [x] (firmware owner, M3-A, `test/device/test_rec_sd`) · **1. device `test_rec_sd` overflows the stack on loopTask, then reboots forever**
  - Evidence: `Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception). Debug exception reason: Stack canary watchpoint triggered (loopTask)`. Backtrace (`addr2line`): `test_cut_off_session_is_recovered_at_boot` (`test_main.cpp:132`) → `RecEngine::start` → `RecWriter::start` / `open_segment` (`rec_writer.cpp:21,27`) → `VfsRecFilePort::mkdir` (`vfs_rec_file_port.cpp:132`) → `esp_vfs_mkdir` → `f_mkdir` → `ff_sd_status` → `AcquireSPI` (`third_party/SD/src/sd_diskio.cpp:589`) → `SPIClass::beginTransaction` → `xQueueSemaphoreTake`. `-fstack-usage`: this test function's own stack frame is **6,256 B** (two `RecEngine` objects on the stack), and unlike `test_runtime_budget`, `test_rec_sd` does not `SET_LOOP_TASK_STACK_SIZE(16 * 1024)`, so it uses Arduino's default 8 KB loop stack. After each crash Unity reruns from the start; `pio test` does not time out on its own and can only be stopped by hand.
  - Suggestion: add `SET_LOOP_TASK_STACK_SIZE(16 * 1024)` to `test_rec_sd`, or make the two `RecEngine` objects static. In product code the engine runs on the audio task (`test_rec_budget` measured 2,404 B of margin), so this is not a product problem. After the fix this one suite needs a rerun on the real device (the last two items, `test_get_and_del` and `test_clean_up`, have never been reached).
  - Date: 2026-10-21 Conclusion: **fixed, confirmed on the real device**. The real culprit was `g_index = RecIndex{}`, a temporary of about 4.1 KB (the two `RecEngine` objects are about 850 B each and share one stack area). The fix removes it and instead asserts that the index is still as it was at boot. This function's stack frame drops from 6,256 B to 2,176 B, and `SET_LOOP_TASK_STACK_SIZE` is not needed. Two more safeguards were added: `stack_budget.py` caps each device suite's own stack frames at 4,096 B when `pio test` builds; `device_test_host.py` fails on the first `Rebooting...` instead of rerunning forever. At 04:23 on the real device: **PASS 5/5**, with the last two items reached for the first time (§6.8).
- [x] (firmware owner M3-A + Lead; `test/device/test_sd_store` is a v0.2 test guarded by G9) · **2. `test_sd_store`'s "mounting adds ≤16 KB of heap" no longer holds**
  - Evidence: both runs gave `CPA_SD_MOUNT … heap_delta=19204` (v0.2 was 15,068 B, §5.3). The extra 4.1 KB or so comes from 11 §4.5 deliberately raising FatFs `max_files` from 2 to 3 (`sd_card.cpp:49 kMaxFiles = 3`). Doc 12's M3-A real-device acceptance commands include this suite, and it is also on G9's "not one line changed" list, so under the current rules it can never pass.
  - Needs a decision: following the daemon's G9 exception, raise this one limit to 20 KB (or "16 KB + 4.6 KB per extra file slot") and write it down; or go back to 2 file slots (11 §4.5's "peak of 2" was enough all along; the 3rd slot is only spare).
  - Date: 2026-10-21 Conclusion: **the firmware goes back to `kMaxFiles = 2`** (the second option: no G9 exception needed, `test_sd_store` not changed by a single line; **this is the firmware fix agent's choice, waiting for the Lead to confirm**). Real device: `test_sd_store` **PASS 7/7**, `CPA_SD_MOUNT us=18432 heap_delta=15068`, exactly as in v0.2; the whole app gets 4,136 B of free heap back. The recording paths with 2 slots also ran on the real device: apart from `rec_rotation`, which was not rerun this time, every recording scenario passed, including `rec_live_hold` (hold G0 to start a memo during a pull) and `rec_soak` (§6.8).
- [x] (firmware owner, M3-A, `lib/hal/sd_card.cpp` / `third_party/SD`) · **3. A software restart during segment rotation leaves the SD card unmounted on the next boot (`store=int`), with no recording for that whole boot**
  - Evidence: `rec_reboot` (b) (`debug.reboot` about 20–50 ms after `debug.state.rec.seg` becomes 1), 1 time in 5: the hello after the restart was `"boot":"1833562-00007","up_ms":1745,"rst":"software"`, `store=int`, and none of the pongs after it had `files` (for example `{"t":"pong",…,"up_ms":41907,"pending":0,"revs":{…}}`); daemon status `store: int`; running `handshake` on its own still gave `store=int`. One more `debug.reboot` (`-00008`) brought back `store=sd`, and that session was then pulled as usual and closed with a missing segment. Restarting mid-segment (a) never showed it, 5/5.
  - Inference: `esp_restart()` does not cut the card's power, so at restart the card may still be in the middle of a multi-block write (CMD25, writing `.M` or a new segment), or in the busy period after Stop Tran; `mount()` tries only once (`sd_card.cpp` comment "no slower retry"), and when `sdcard_mount` fails it falls back to internal storage. In the real world this matches a panic, watchdog or brownout reset during recording (the kind that leaves the card powered): recording is refused for that whole boot with "Needs an SD card" (录音需要 SD 卡), and the recovery scan is put off until the next boot as well.
  - Suggestion: when mounting fails, first send a Stop Tran token / give some extra clocks with CS high, then retry the whole mount once (only on the first failure, at the cost of a few tens of ms more at boot); add a device test: `esp_restart()` halfway through a CMD25 write, and the next boot must be `SdState::Ok`. HIL `rec_reboot` now fails in this case (`*_card_mounted_after_reboot`).
  - Date: 2026-10-21 Conclusion: **fixed, confirmed on the real device**. The new pure-logic `lib/rec/sd_wake` runs in `hal_sd_mount`, before the driver's first CMD0: if the card answers CMD0 it is done (a normal boot only gets one extra CMD0); if not, it first finishes the interrupted transfer (520 clocks, Stop Tran + Nbr, CMD12, waiting for busy to end after each step), then sends CMD0. No mount retry was added (after `sdcard_mount` fails, upstream unregisters the VFS without `f_mount(NULL)`, so a retry would hit a dangling FatFs volume pointer). The new device suite `test_sd_wake` uses raw SPI to interrupt a CMD25 in the test's own file and then mounts. Real-device evidence (§6.8):
    - With the fix: `test_sd_wake` **3/3**; interrupted between blocks, `state=Ok` in 19 ms; interrupted mid-block, `state=Ok` in 32 ms (same in both runs).
    - **Control (same suite, on a copy of the firmware with only `wake_card()` commented out)**: interrupted between blocks it still mounts (the driver's own init is enough), **interrupted mid-block it gives `state=Absent`, 301 ms**, and the mount in the cleanup item after it fails too: exactly this issue's `store=int`. Flashing the build with the fix right after rescued the card on its first mount. So it is `sd_wake` that lets a card interrupted mid-block mount.
    - HIL: 5 runs of `rec_reboot`, 10 `debug.reboot` calls in all (5 of them restarts right after rotation), **all 10 new boots were `store=sd` with `files`**. Before it was 1 time in 5, so 0 in 5 is not enough proof on its own; the direct evidence is the control above.
- [x] (firmware owner / Lead, version number in `firmware/app`) · **4. The v0.3 build still reports `cpa-fw/0.2.0`**
  - Evidence: `cardputer-adv-test` built from `release/v0.3.0` HEAD `689f66a` reports `fw=cpa-fw/0.2.0` in hello, and every segment's `.M` and the manifest's `device.fw` also say `cpa-fw/0.2.0`. It must become 0.3.0 before release, or the manifests of v0.3 recordings will carry the wrong firmware version.
  - Date: 2026-10-21 Conclusion: **fixed**. `runtime.h` now has `kFwVersion = "cpa-fw/0.3.0"` (the version assertions in two UI tests were changed first, as RED). Real device: the test firmware's hello says `fw=cpa-fw/0.3.0` (HIL report meta and `handshake`), the string in the release `firmware.bin` is `cpa-fw/0.3.0`, and the user's `cpa status` shows `fw=cpa-fw/0.3.0`. `device.fw` in the manifest was not checked separately (the temp dirs are deleted with each run).
- [x] (daemon owner, M3-B, `daemon/src/cpad/recordings/puller.py` / `service.py`) · **5. A session with a missing tail segment is closed only at the next reconnect or recording**
  - Evidence: `rec_reboot` (b) 4/4: after the session's segment 0 (`last:false`) was pulled and deleted, there was no `rec.archived` for 45 s; once the harness reconnected with `cpa pause 2` and listed again, it was archived right away (`gaps:[1]`). Cause: `manifest.finalisable` is only checked in `Recordings.listed()`, and `RecPuller` only lists again when the observed rev differs from the expected one; after the daemon itself deletes the last segment, the rev matches what it expects, so it does not list again. 11 §6.4 says "when a complete listing shows…", so the behaviour matches the text, but what the user actually sees is this: the last meeting before a power cut gets no manifest until the next plug-in or the next recording, WhisperWoof does not process it, and the daily notes have no line for it.
  - Suggestion: after deleting a `last:false` segment, if its session has no other segment in the listing, schedule a new listing (or treat "`files.n == 0` after our own delete" as an empty complete listing). Add cases to `tests/unit/test_rec_pull.py` / `tests/integration/test_recordings.py` first.
  - Date: 2026-10-21 Conclusion: **fixed, confirmed on the real device**. At the end of a pull round, if deletes the daemon itself confirmed have removed all of a not-yet-archived session's segments from the listing, `RecPuller` lists once more (pure function `pull.relist_to_finish`), and `Recordings.listed()` closes the session per 11 §6.4; a complete session sends no extra `file.list`. Real device: `rec_reboot` (b) **5/5 missing tail segment (`gaps:[1]`), 5/5 `rot_needed_relist=false`**, meaning it was archived within 45 s on the same connection, with no reconnect; each `rec_reboot` went from 81 s to about 36 s.
- [ ] (firmware M3-A + protocol M3-P, `debug.state.rec`) · **6. HIL cannot get mic dropped blocks, the ring buffer high-water mark or rotation time, and the whole app's heap while recording was not measured**
  - G-A2 says "0 dropped frames from debug.state", and G-A4 asks to record "the worst rotation time and the ring buffer high-water mark", but `debug.state.rec` only has `live/mode/seg/files_n/files_rev`. `MicRing` actually has `missing_` (times the callback could not re-arm a buffer) and `high_` (highest backlog); they are just not reported. This HIL run reconciled sample counts against the device clock instead (`rec_soak` 10 minutes, short by 116 ms; `rec_rotation` short by 101 ms; both within 1 block). That shows no whole blocks were lost, but does not give the counts directly.
  - Also, G-A6's "heap while recording + cue ≥96 KB / ≥32 KB" was only measured in the trimmed `test_rec_budget` (262.8 / 252.0 KB); for the whole app only idle was measured (181.5 / 160.0 KB). By 11 §4.7's estimate recording takes about 6.7–8 KB more, which should leave about 170 KB, but it was not measured.
  - Suggestion (test firmware only): add `ring_high`, `mic_missing`, `rot_ms_max`, `heap_free`, `heap_block` to `debug.state.rec`, and have HIL assert on them directly.
  - Date: 2026-10-21 Conclusion: **the protocol half is done, the firmware half is not, so this item stays open**. The schema, vectors, protocol README, daemon spec and the firmware encoder/decoder all gained these 5 optional, nullable counters (absent or null = this build does not measure it); `heap_free` / `heap_block` take the minimum during recording. But the firmware runtime does not fill them yet, the test firmware's `debug.state.rec` still lacks these fields, and HIL does not assert on them yet. This run's `rec_soak` (5 minutes) again used device-clock reconciliation: short by 672 samples (42 ms), within 1 block. Next step (firmware): the audio task fills in `MicRing`'s `high_` / `missing_`, the rotation time and the lowest heap; then HIL's `rec_soak` / `rec_rotation` assert `mic_missing == 0` and record `rot_ms_max`, `ring_high` and the whole app's heap while recording.
  - 2026-10-22 (`fix/stability-followups`): **the firmware half is done, waiting for the real device**. The test firmware's audio task resets these 5 counters each time recording starts and updates them on every block written (`lib/rec/rec_health`, native `test_rec_health`); loop puts them into `debug.state.rec` through `RecPort::health()` (left out if nothing has been recorded since this boot); `rec_soak` / `rec_rotation` read them once after recording stops, assert that all 5 are present and `mic_missing == 0`, and write them into the report's `health`. The test firmware also gained `debug.state.diag` (heap, both tasks' stack margins, loop timing, keyboard ghosts / resets; protocol README item 21). Tick this item once these two scenarios have run on the real device.
- [ ] (Lead / firmware) · **7. G-A7's "log bat_mv every minute" cannot be done**: on battery the device is not connected to the Mac, and the v0.3 firmware does not log voltage to the card either. §6.6 G-A7 was changed to reading the System page by hand; a continuous curve would need a voltage log on the card in the test firmware (or a USB isolation setup that powers the device without charging it).
- Noted, not issues:
  - Constants still at placeholder values: `kToneVolume = 64` (waiting for G-A5's listening test; no longer applies from 2026-10-21, as v0.3.1 has no cues), `kMicMagnification = 16` (waiting for G-A10; from v0.3.1 replaced by `kMicGain` in `lib/core/mic_gain.h`, set to `{9, 0xC4, 2}` on 2026-10-21 by the `mic_cal` of §6.6 G-A10). Doc 12 states that M3-A is not done until G-A10 has a result. `lead_ms` is already the 150 set by G-A5 (the manifest shows 150).
  - `repaired` on the `rcv` segment is always false: the device already backfilled the standard header during boot recovery (11 §4.6), so the header is already right when cpad checks it and needs no repair. Doc 12's "the rcv segment lands with a repaired header" holds in this sense (the header is right); it is just that the device repaired it, not cpad.
  - Pulls hold steady at 332–335 KB/s at the app layer (the same for memos, meetings, 30 segments and 9.6 MB segments), matching G-A1's prediction of "about 302–322 KB/s when reopening and closing the file for every block"; to go faster, keep the file open during consecutive reads of the same `fid`, as m3-device-gates §1 suggests.
  - §5.9 item 3 (`handshake` right after `sd_store`) did not trigger this time: in the acceptance order `sd_bulk` sits in between.

### 6.8 Final rerun after the fixes, release build flashed back (2026-10-21 04:21–04:48)

Firmware tree: `689f66a` plus the uncommitted fixes for §6.7 items 1–5 (firmware: `test_rec_sd` drops the 4 KB `RecIndex{}` temporary, `kMaxFiles` back to 2, the new `lib/rec/sd_wake` and device suite `test_sd_wake`, `kFwVersion = cpa-fw/0.3.0`; daemon: `RecPuller` lists again after its own deletes empty an unarchived session; protocol: the 5 optional counters in `debug.state.rec`, not yet filled by the firmware). The user's cpad was first paused with `cpa pause 5400` (04:21:36), and `lsof /dev/cu.usbmodem*` was empty; every flash used `pio run -e <env> -t upload` (log `pre_upload: skip: already paused`), with no keys pressed and G0 untouched. HIL used a temporary `$CPA_HOME` / notes_dir, fake-claude and fakewoof; the recordings held only room noise and were deleted with the temp dirs; the user's `~/CardputerNotes` and `~/.claude` were not touched.

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*
uv run --project daemon pytest tools/hil/tests -q && uv run --project daemon pytest daemon/tests -q
cd firmware/app
pio test -v -e cardputer-adv-test -f device/test_rec_sd -f device/test_sd_wake -f device/test_sd_store \
  -f device/test_runtime_budget -f device/test_rec_budget
# Control: copy the firmware tree into a temp dir, comment out only the wake_card() call in sd_card.cpp, run device/test_sd_wake;
# then run test_sd_wake again with this repo's build (rescues the card, clears /cpatest). The copy is deleted right after use
pio run -e cardputer-adv-test -t upload && cd ../..
# rec_soak shortened to 5 minutes: a temporary wrapper script imports scenarios.rec_long, sets SOAK_MIN = 5.0, then calls harness.runner.main (repo code unchanged)
uv run --project daemon python <wrapper script> --scenarios rec_memo,rec_meeting,rec_resume,rec_reboot,rec_live_hold,rec_soak --report "$TMPDIR/hil-rec.json"
for i in 1 2 3 4; do uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_reboot --report "$TMPDIR/hil-reboot-$i.json"; done
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys --report "$TMPDIR/hil-v02-regress.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,handshake --report "$TMPDIR/hil-final-test.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)          # release build
launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad
cpa pause 1 && cpa status && cpa rec ls
```

- [x] **Host side**: `pytest tools/hil/tests` 229 passed; `pytest daemon/tests` 836 passed; `protocol/tools/check_vectors.py` OK (25 message types, 15 flows).
- [x] **device suites** (04:22:33–04:24:04, **20/20 PASS**, 88 s)
  - `test_rec_sd` **5/5** (10.8 s): item 3 no longer overflows the stack; `test_get_and_del` and `test_clean_up` reached for the first time (§6.7 item 1).
  - `test_sd_store` **7/7** (28.2 s): `CPA_SD_MOUNT us=18432 heap_delta=15068` (≤16,384 B ✅, same as v0.2); 200 items acked immediately p50 9.1 / p95 10.0 / max 44.2 ms, 200 items backlogged p50 15.1 / p95 20.1 / max 92.5 ms; migrating 50 items took 46 ms each (§6.7 item 2).
  - `test_sd_wake` **3/3** (10.2 s): `CPA_SD_WAKE partial=0 state=3 ms=19`, `partial=100 state=3 ms=32` (3 = Ok).
  - `test_rec_budget` **3/3**: `idle_free=279668 idle_block=270324 rec_free=271424 rec_block=262132 xfer_free=279124 audio_stack_left=2396 ring_high=1` (every item of the trimmed G-A6 passes).
  - `test_runtime_budget` **2/2** (whole app): `stack_left=5788 heap_free=189992 heap_min=180448 max_block=167924 audio_stack_left=3744`, `CPA_RX_DONE got=8192 bad=0`. Idle 185.5 KB / largest block 164.0 KB (≥150 / ≥64 KB ✅), loop stack margin 5,788 B (≥4 KB ✅); 4,136 B more than §6.3, which is the 3rd file slot that was removed.
- [x] **`sd_wake` control** (04:24–04:25): the same `test_sd_wake`, on a copy of the firmware with only `wake_card()` removed → **1/3**: interrupted between blocks still `state=3` (18 ms), **interrupted mid-block `state=1` (Absent), 301 ms**, and the mount in `test_clean_up` failed too. Rerun right after with this repo's build: **3/3 PASS** (19 / 32 ms); the first mount rescued the card the previous round had left stuck. This is where the conclusion of §6.7 item 3 comes from.
- [x] **Recording HIL** (04:25:50–04:36:13, **6/6 PASS, exit code 0**; `fw=cpa-fw/0.3.0`, `store=sd`, boot `1833562-0000R`)
  - `rec_memo` 18/18: G0 press to first sample 588 ms (≤1.0 s ✅); sample count 1,344 short of "press + 4 s"; pull 333.4 KB/s; one todo line in the daily notes, card cleared, no `queue.added`, all 4 checks of the 30-day retention dry run passed.
  - `rec_meeting` 20/20: 3 segments, both full segments exactly 480,000 samples, `gaps` empty, `consent:"ack"`; ★ error −55 / −88 ms; Enter to first sample 368 ms (≤0.8 s ✅); 1,744 samples short over 70,509 ms; **330.9 KB/s at the app layer** (per segment 332.7 / 332.2 / 322.5), about 5.7 minutes for an hour of audio.
  - `rec_resume` 10/10: `.part` 282,900 B, resume offset **217,364 = 282,900 − 65,536** ✅; 2 connections, archived only once.
  - `rec_live_hold` 16/16: 2 `E_BUSY live`, 1 late `file.data`; during the window the daemon **sent not a single `file.*`** (no interleaving this time); the interrupted file did not restart from 0; the memo (`…000A`) went ahead of the meeting's remaining two segments (`…0008`, `…0009`). This ran with `max_files = 2`.
  - `rec_reboot` 18/18 (35.6 s, was 81.4 s): (a) `rcv` segment `[1]` landed, session complete, 34 ms lost; (b) new boot `store=sd`, `gaps:[1]`, **`rot_needed_relist=false`**.
  - `rec_soak` (**shortened to 5 minutes at the Lead's request**, default 5-minute segments) 15/15 (338.5 s): all 929 requests (300 pings, 570 `sessions.snapshot` pushes) answered, **p50 2.9 ms, p95 22.3 ms, max 37.4 ms**, 0 over 600 ms, 0 `E_NOT_READY`, the port was not reopened, all 359 pongs `live:true`; 672 samples short over 300,970 ms (42 ms, within 1 block), `t0_ms` continuous; 2 segments (one full 9.6 MB segment + a tail segment), 9,629,784 B in all, pulled at 332.5 KB/s. The 10-minute version is in §6.4.
- [x] **`rec_reboot` 4 more runs** (04:36–04:39, **4/4 PASS**). With the run above, 5 runs and 10 `debug.reboot` calls in all: **10/10 new boots `store=sd` with `files`**; (b) **5/5 missing tail segment, 5/5 archived without reconnecting**; (a) lost 34 / −26 / 13 / 35 / 36 ms (limit 1.5 s). Each run had `device_not_ready` = 2, that is one `E_NOT_READY` per `debug.reboot`, the same as v0.2's `reboot` and `sd_store` (one per restart).
- [x] **G-R: v0.2's 14 scenarios, mainly SD** (04:39:24–04:46:41, **14/14 PASS, exit code 0**): `sd_bulk` backlog drained in 1.563 s; `capture` 0.104 s; `reboot` Ready again in 1.24 s; `board` 3.07 s; `latency` board p95 3.088 s, Compose p95 51 ms (host 126.2 ms); `cjk` `efontCN_12` 7,545 glyphs, 0 missing; `back_keys` 8/8; `handshake` `fw=cpa-fw/0.3.0`, `store=sd`.
  - Not rerun this time: the 10-minute SD soak, the three migration stages, the nosd build (`rec_nosd` + v0.1 scenarios). The nosd build returns Disabled straight away in `hal_sd_mount` and never reaches `sd_wake`; it differs from the build tested in §6.5 only in the version string and the encoding of `debug.state.rec` (nosd sends no `rec`).
- [x] **Test firmware wrap-up** (04:46:59): `sd_empty,handshake` 2/2, `pending=0`, `store=sd`, boot `1833562-00014`; every recording scenario ended with "card cleared".
- [x] **Flash the release build `cardputer-adv` back** (04:47:09, SUCCESS 18.1 s, `pre_upload: skip: already paused for 3865 s`): RAM 65,872 B (20.1%), app 955,616 B written at 0x10000 (SHA-256 `2505a42b…e5ae2a67`), booted normally after "Hard resetting via RTS pin" and did not enter download mode. `debug.key` appears 0 times in the release `firmware.bin` (once in the test firmware), version string `cpa-fw/0.3.0`.
- [x] **The user's daemon switched to the new code and resumed** (04:47:45): `launchctl kickstart -k gui/$(id -u)/com.cardputer-assistant.cpad`, pid 80674 → 14514 (it is an editable install of this repo's `daemon/`, so it runs the working tree code with the M3-B fix). The restarted process did not inherit the pause and connected right away; at 04:47:51, as asked, `cpa pause 1`, then `cpa status`: `link: ready  dev=020000000001 fw=cpa-fw/0.3.0 pending=0 store=sd launcher=no`, `recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`; `cpa rec ls` exit code 0, header only (empty). `cpad.log`: `device ready` boot `1833562-00015`, no errors. This step also completed the last item of §5.10 (restart the LaunchAgent so that `store=sd` shows).

### 6.9 v0.3.1: gain calibration, check and recording regression, release build flashed back (2026-10-21 12:18–12:41)

Firmware tree: `release/v0.3.1` HEAD `8381d7f` (silent recording `aa76501` merged into main) plus the uncommitted v0.3.1 gain changes (`lib/core/mic_gain.h`, `MicRing` writing and reading back REG14 / REG17 at every start, `debug.mic`, the `mic_cal` / `mic_verify` scenarios). Device and card as in §6.0. All recordings stayed in HIL's temp dirs. The temp dirs kept by `--keep` were used only to measure the first 512 ms and the noise floor; only the numbers were noted, and the dirs were deleted right after measuring. The user's `~/CardputerNotes` was not touched.

```bash
cd firmware/app
pio test -v -e cardputer-adv-test -f device/test_mic_gain          # 3 times
pio test -v -e cardputer-adv-test -f device/test_rec_budget
pio run -e cardputer-adv-test -t upload && cd ../.. && cpa pause 1  # preflight needs cpad resumed and 0 segments on the card
uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_cal --report "$TMPDIR/mic-cal.json"
# kMicGain changed to {9, 0xC4, 2}; test_the_placeholder_keeps_v030 replaced by test_the_calibrated_gain (seen failing first)
(cd firmware/app && pio test -e native && pio run -e cardputer-adv-test -t upload) && cpa pause 1
uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_verify --keep --report "$TMPDIR/mic-verify.json"
cpa pause 1800; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,handshake,capture,rec_memo,rec_meeting,rec_resume --keep --report "$TMPDIR/hil-regress.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_bulk,handshake --report "$TMPDIR/hil-handshake.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)            # release build
cpa pause 1 && cpa status
```

- [x] **Host side**: firmware native 416/416 (gcovr 94%); `tools/hil/tests` 335 passed; daemon 848 passed (96.8%); `protocol/tools` 246, `protocol/recording` 87 passed; `check_vectors.py` OK (26 message types, 15 flows).
- [ ] **device suites** (12:18–12:21): `test_mic_gain` PASS 2 times out of 3 (§6.3, ⏳ run 1 started with all zeros). `test_rec_budget` **3/3**: `CPA_REC_BUDGET idle_free=279604 idle_block=270324 rec_free=273224 rec_block=262132 xfer_free=279168 audio_stack_left=2364 ring_high=1`; 1,800 B more left while recording than in §6.8 (there are no cues any more). `test_sd_absent` needs the card pulled out; not run.
- [x] **`mic_cal`** (12:21:43–12:26:55, **8/8 PASS**)
  - preflight: `cpa status` 0 segments on the card, not pulling; output is MacBook Pro Speakers, volume 69, not muted (read only, not changed); voices Samantha / Tingting. The B1 probe landed in the window on the first try (tone A −44.57 dBFS, the reference tones need no scaling), reference audio 14.1 s.
  - All 11 steps valid, decision `{9, 0xC4, 2}`, expected peak −11.93, RMS −35.23, noise floor −73.74 dBFS, `silence_rms` 0.000843; neither N33 nor N36 clipped; 5 leads of 90 / 80 / 70 / 80 / 90 ms → `recommend_lead` 100 → `kLeadMs` stays 150. The numbers and verdict for each step are in m3-device-gates §5.4.
  - Card cleared; each of the 16 segments got exactly one `rec.seg.stored` and one `rec.seg.deleted`; the scenario restarted the device at the end, which cleared `debug.mic`.
- [x] **Change the constant, `mic_verify`** (test firmware flashed at 12:34, `pre_upload: skip: already paused for 3265 s`; 12:34:16–12:35:17, **5/5 PASS**)
  - `kmicgain_verified`: read `{9, 196, 2}` from `mic_gain.h`; B1 probe tone A −44.52 dBFS; after a restart, with no `debug.mic` sent, recorded at the firmware's own gain: gain **+11.74 dB** (nominal +11.44), voice +11.83 dB; extrapolated peak **−11.96 dBFS**, RMS −35.26, noise floor (high-passed) −74.26 dBFS; 0 clipped; step size 1; this segment's lead 100 ms.
  - First 512 ms (the case behind run 1's failure in §6.3): B1 was the first segment after flashing; its first sample was not 0, and 1,599 of its 8,192 samples were 0 (normal at ×8); the `kMicGain` segment was the first after a restart: 221 zeros, transient peak 4,554 in the first 150 ms, then from 150 ms to 3 s max 44, RMS 10.9 (−69.6 dBFS, unfiltered).
- [x] **Recording regression, test firmware with the new gain** (12:35:44–12:38:48, **6/7**; `sd_bulk,handshake` rerun at 12:39, **2/2**)
  - `sd_empty` 2/2; `sd_store` 8/8 (3 items across a `debug.reboot`, each delivered once, boot `…0001C` → `…0001D`); `capture` 5/5, 0.103 s; `sd_bulk` 8/8, backlog of 100 items drained in 1.562 s.
  - `handshake`: first run 4/6, `same_boot_after_reopen` and `up_ms_continuous` failed: the "before reopen" hello it took was the one from before `sd_store`'s restart (`…0001C`, up_ms 30,628), and the restart count was 1 both before and after the scenario. This is §5.9 item 3: this time `handshake` came right after `sd_store`, with no `sd_bulk` in between. Rerun in G-R's order (`sd_bulk` first): **6/6**, same boot, `drift_ms` −1.
  - `rec_memo` 18/18: G0 press to first sample **506 ms** (588 ms in §6.8; the difference is the cue and `Speaker.end`); 3,456 ms, sample count 608 short of "press + 4 s"; `lead_ms` 150; pull 316.6 KB/s; one todo line in the daily notes, card cleared.
  - `rec_meeting` 20/20: Enter to first sample **8 ms** (368 ms in §6.8; the three-tone cue is gone); 3 segments, both full segments exactly 480,000 samples, `gaps` empty; ★ error −47 / −95 ms; 2,112 samples short over 70,916 ms (132 ms, tolerance 4,096; §6.8 was 1,744 short); **335.9 KB/s**, about 5.6 minutes for an hour of audio.
  - `rec_resume` 10/10: `.part` 284,280 B, resume offset **218,744 = 284,280 − 65,536** ✅; 2 connections, archived only once.
  - Levels at the new gain (7 segments, all of a room with nobody speaking): after the first 150 ms, peak 48–72 (−57 to −53 dBFS), RMS 11.0–14.7 (−69.5 to −67.0 dBFS); sample GCD 1 in every segment; the `Mic.begin` transient only in segment 0, peak 4,603–4,653 in the first 150 ms (about −17 dBFS). No segment's first 512 ms was all zeros (151–355 zeros out of 8,192).
- [x] **Flash the release build `cardputer-adv` back** (12:40, SUCCESS 13.9 s, `pre_upload: skip: already paused for 1495 s`): RAM 65,920 B (20.1%), app 945,312 B written at 0x10000, "Hash of data verified", booted normally after "Hard resetting via RTS pin". `debug.(key|state|reboot|rec|mic)` appears 0 times in the release `firmware.bin` (the test firmware has `debug.mic`).
- [x] **The user's cpad resumed** (12:41:06): after `cpa pause 1`, `cpa status`: `link: ready  dev=020000000001 fw=cpa-fw/0.3.1 pending=0 store=sd launcher=no`, `recordings: on_device=0 pulling=0`, `processing=0 blocked=0 failed=0` (waiting holds the user's own recordings waiting for WhisperWoof; the number is the same as before the run).

### 6.10 v0.3.2: the first recording after power-on (#31), release build flashed back (2026-10-21 17:20–17:32)

Firmware tree: `release/v0.3.2` (codec warm-up at power-on, start only once there is sound, the test firmware's `debug.codec`, the `rec_cold_start` scenario, the device test `test_mic_cold`). Device and card as in §6.0. `rec_cold_start` plays a 1 kHz tone through the Mac's built-in speaker (−20 dBFS; the system volume is only read, never changed); like `mic_cal` it runs on its own and must pass preflight first (built-in speaker not muted, `cpa status` 0 segments on the card, not pulling), then pauses the real cpad for 1800 s. Recordings stayed only in HIL's temp dirs, and the report has only numbers. The codec's cold state on the device is produced by the soft reset in `debug.codec` (REG00 = 0x1F): plugged into USB, it never goes cold on its own, however long you wait. From v0.3.2, `at.up_ms` in segment 0's `.M` is the device time of the first sample written (with a warm codec, the moment right after `Mic.begin`); the reconciliation algorithm of §6.2 is unchanged; the wall clock in `device/test_rec_rotation` is now also counted from the `up_ms` carried by `Started` (`Started` now arrives only with the first block of sound).

```bash
cpa pause 3600; lsof /dev/cu.usbmodem*                               # the serial port must be free
cd firmware/app
pio test -v -e cardputer-adv-test -f device/test_mic_cold            # 3 times; no card, no tone needed
pio test -v -e cardputer-adv-test -f device/test_mic_gain -f device/test_rec_budget
pio run -e cardputer-adv-test -t upload && cd ../.. && cpa pause 1    # preflight needs cpad resumed and 0 segments on the card
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_cold_start --keep --report "$TMPDIR/rec-cold-1.json"
# runs 2-4: before each, cpa pause 1 and wait for link ready and 0 segments on the card; tone file at -1 dBFS (see the rec_cold_start item below)
cpa pause 1800; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,rec_memo,rec_meeting,rec_resume --report "$TMPDIR/hil-regress.json"
cpa pause 1                                                          # preflight
uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_verify --report "$TMPDIR/mic-verify.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)              # release build
cpa pause 1 && cpa status
```

- [x] **Host side** (from 2026-10-21 17:33, after the real-device tests, every step of the repo, firmware, daemon and launcher jobs in `ci.yml` was run in the `release/v0.3.2` worktree, **all passed**): firmware native 438/438 (gcovr 97%, after deleting old `.gcda` files); `tools/hil/tests` 360 passed; daemon 854 passed (96.8%); launcher 227 passed; `protocol/tools` 248, `tools/repo` 25, `protocol/recording` 87 passed; `check_vectors.py` OK (27 message types, 15 flows); `check_recording.py` OK; build gate scripts 53 passed; release build 946,448 B, budget and partition table checks pass, no `debug.*`; `make_image` OK; size, lizard and ruff all clean (the warning on `daemon/src/cpad/app.py` at 402 lines was already there). `cardputer-adv-test` and the 10 device tests all compile.
- [x] **`test_mic_cold`** (17:20–17:21, **PASS all 3 times**, 4/4 each, about 15.5 s)
  - It tests the production `MicRing` path: after `cold()` (REG00 = 0x1F), start at `kMicGain`; of the first 12 blocks (1.5 s), at least 5 blocks counting from the first are forced (`rec::block_muted`); start again right after: 0 blocks; `cold()` again, then `warm()` (48 kHz, about 0.47 s), then start: 0 blocks. Each run prints one `CPA_COLD …` line.
  - Pass if: all PASS. Fewer than 5 blocks on the cold start means the soft reset did not make the codec cold, and `cold_was_cold` in `rec_cold_start` would fail as well.
  - Date: 2026-10-21 Result: 3/3 `CPA_COLD`: cold **7 blocks (896 ms)**, warm **0 blocks**, after_warm_up **0 blocks**, warm_up_ms **491**. The numbers were identical in all three runs.
- [x] **`test_mic_gain`, `test_rec_budget`** (17:21:35–17:22:05, both PASS)
  - `test_mic_gain` 3/3: `CPA_MIC pga=10 vol=0xC5 mag=2 reg14=0x1A reg17=0xC5 gcd=1 gain_failures=0`; stopped and started again with M5's defaults: `reg14=0x10 reg17=0xBF gcd=8 gain_failures=0`.
  - `test_rec_budget` 3/3: `CPA_REC_BUDGET idle_free=273296 idle_block=262132 rec_free=273032 rec_block=262132 xfer_free=278940 audio_stack_left=2344 ring_high=1`.
  - "Idle" is 6.3 KB lower than in §6.9 (279,604 → 273,296), but that is because the moment of measuring changed: `IndexReady` is sent after the boot scan, the boot warm-up follows the scan, and the test measures as soon as it gets `IndexReady`, so it measures the heap during warm-up (microphone on), which is close to the recording figure (273,032). After warm-up, `xfer_free` 278,940 matches §6.9's 279,168, so warm-up leaves nothing allocated. Audio task stack margin 2,344 B (§6.9: 2,364 B).
- [x] **`rec_cold_start`** (17:22:49–17:26:20, test firmware flashed at 17:22, app 955,456 B; **4 runs: 12/15, 15/15, 15/15, 15/15**)
  - Three memos, each holding G0 for 4.5 s, with the speaker playing the tone for 1 s first: `cold` (`debug.codec{warm:false}`), `warm` (right after, nothing sent), `power_on` (`debug.codec{warm:true}`: soft reset, then warm-up; `Start` may have to wait for the warm-up to finish).
  - Checks for each memo: the forced part at the start (|x| ≤ 2, counting any offset-correction decay tail before it) is no more than 20 ms; the 1 kHz tone is audible in the first 250 ms (≥ −50 dBFS); start to `● REC` under 2.5 s (device clock: segment 0's `at.up_ms` + one 128 ms block − (G0 press + 500 ms), an upper bound). Also `cold_was_cold` (start to `● REC` at least 600 ms: the codec really was cold) and `power_on_warm_up_worked` (under 800 ms: the warm-up worked); landing checks, card cleared.
  - **Volume**: preflight read the output as MacBook Pro Speakers, not muted, but at volume **25** (it was 69 when #31 was reproduced and in §6.9; read only, not changed). Run 1 ran the scenario as is (tone file −20 dBFS): the tone reached the device at about −55 dBFS, all three memos failed `*_tone_from_the_start` (−55.16 / −55.11 / −55.06), and the other 12 checks passed. Runs 2–4 used a temporary wrapper script that set `scenarios.rec_cold.TONE_DBFS` to −1.0 in the process and then called `harness.runner.main` (no file in the repo changed, and neither did the system volume). The tone reached the device at about −44 dBFS, and all 3 runs were 15/15.
  - `--keep` was added each time. Once the numbers in the table below were measured (with functions from `harness.cold_start` and `harness.mic_cal`), the temp dirs were deleted; no recording was kept.
  - Date: 2026-10-21

    | Run | Start to `● REC` (cold / warm / power_on, ms) | Forced at start (ms) | Tone, first 250 ms (dBFS) | Peak in first 150 ms / peak after 500 ms (cold / warm / power_on) |
    |---|---|---|---|---|
    | 1 (−20 dBFS) | 1,133 / 150 / 134 | 0 / 0 / 0 | −55.16 / −55.11 / −55.06 | 94/105 · 5,195/123 · 3,687/120 |
    | 2 (−1 dBFS) | 1,131 / 134 / 134 | 0 / 0 / 0 | −43.86 / −44.01 / −44.01 | 264/313 · 4,935/354 · 4,571/248 |
    | 3 (−1 dBFS) | 1,128 / 141 / 147 | 0 / 0 / 0 | −43.92 / −44.10 / −44.06 | 239/249 · 5,176/277 · 5,242/305 |
    | 4 (−1 dBFS) | 1,143 / 134 / 134 | 0 / 0 / 0 | −43.91 / −44.05 / −44.08 | 237/242 · 4,808/368 · 3,387/282 |

  - In all 12 memos the first sample already had sound. The tone in the first 250 ms differed from the steady part at 1–2.5 s by at most 0.2 dB, so the 3 failures in run 1 were only because the tone was too quiet; the start was not forced. The cold memo was about 1 s shorter than the other two every time (2.97 s vs 3.97 s): the roughly 1 s that was forced was dropped and not written to the file. None of the cold memos had an offset-correction decay tail at the start (the first few samples were −4 to 5).
  - Start transient: in `warm` and `power_on` the first block is the `Mic.begin` transient, RMS 1,064–1,520 in the first 20 ms, peak 3,387–5,242 in the first 150 ms (about −20 to −16 dBFS), the same order as §6.9's 4,603–4,653. `cold` has no transient; its first 150 ms is like the steady part (just the tone). Measured with the `gate_cue.lead_ms` algorithm: cold 0 ms every time, warm 60 / 10 / 10 / 10 ms, power_on 50 / 10 / 10 / 10 ms. With the tone playing, the noise floor is the tone itself, so these leads come out low; `lead_ms` 150 still rests on §6.9's silent measurement.
  - In the first 512 ms, 2–52 samples (out of 8,192) were exactly 0, none of them at the start.
- [x] **Recording regression** (test firmware, 17:26:33–17:30:25, **8/8 PASS, exit code 0**): `sd_empty` 2/2, `sd_store` 8/8, `sd_bulk` 8/8, `handshake` 6/6, `capture` 5/5, `rec_memo` 18/18, `rec_meeting` 20/20, `rec_resume` 10/10.
  - `sd_store`: 3 items across a `debug.reboot`, each delivered once, boot `…0001P` → `…0001Q`, `device_not_ready` 1 (from the restart, expected). `sd_bulk`: backlog of 100 items drained in 1.565 s. `handshake`: `fw=cpa-fw/0.3.2`, `store=sd`, same boot, `drift_ms` 0. `capture`: 0.093 s.
  - `rec_memo`: G0 press to first sample **525 ms** (v0.3.1: 506 ms); 3,456 ms, sample count 304 short of "press + 4 s"; `lead_ms` 150; pull 316.1 KB/s; one todo line in the daily notes, card cleared.
  - `rec_meeting`: Enter to first sample **8 ms**; 3 segments, both full segments exactly 480,000 samples, `gaps` empty; ★ error −150 / −71 ms; 1,536 samples short over 70,880 ms (96 ms); pull 336.9 KB/s, about 5.6 minutes for an hour of audio.
  - `rec_resume`: `.part` 288,420 B, resume offset **222,884 = 288,420 − 65,536** ✅; 2 connections, archived only once.
- [ ] **`mic_verify`** (17:30:51–17:31:23, **did not complete**, 0 checks)
  - It stopped at its first step, the B1 probe: `B1 probe too_quiet`, tone A **−62.07 dBFS** (−44.52 in §6.9 at volume 69); even with the reference sound turned up to the maximum it could not reach −48. Wrap-up was normal: device restarted (no `debug.mic` left behind), card cleared.
  - This is not a firmware result: v0.3.2 did not change `kMicGain`, and `test_mic_gain` passed. To rerun it, first set the Mac's volume back to 69 (the user does this; the agent only reads it, never changes it), then `cpa pause 1`, then run the command above.
- [x] **Flash the release build `cardputer-adv` back** (17:31:37–17:31:49, SUCCESS 11.8 s, `pre_upload: skip: already paused for 1754 s`): RAM 65,920 B (20.1%), app 946,448 B written at 0x10000, "Hash of data verified", booted normally after "Hard resetting via RTS pin". The SHA-256 of `firmware.bin` is `fd3835bd…4a98e1a2`. `debug.(key|state|reboot|rec|mic|codec)` appears **0 times** in the release `firmware.bin` (each of these 6 appears once in the test firmware), version string `cpa-fw/0.3.2`.
- [x] **The user's cpad resumed** (17:31:57): after `cpa pause 1`, `cpa status`: `link: ready  dev=020000000001 fw=cpa-fw/0.3.2 pending=0 store=sd launcher=no`, `recordings: on_device=0 pulling=0`, `processing=0 blocked=0 failed=0`. waiting was the same as before the run.
- [ ] **needs user** · **The first memo after a real power cut**: flip the switch to OFF, unplug USB, wait 10 s, then plug in and power on; start talking as soon as you see `● REC`, and record a 5 s memo. Pass if: the first word of the transcript is the first word you said (or the audio does not start with 1 second of zeros). Date: ＿＿ Result: ＿＿

## 7. v0.4.0: Home, Tab menu, lock screen

> The deliverable of V40-H ([docs/13](13-assistant-v2-plan.md) §17). The code and the host-side checks are done (§7.3). **All real-device parts are waiting for the Lead to run**: the Lead has the device to itself, so V40-H opened no serial port, flashed nothing and ran no HIL. The result fields below are all left blank, and **no result has been made up**. The manual items (§7.5) need you in person.
> The firmware is in the `release/v0.4.0` worktree (the changes from V40-F0, M, L, P, F and H, not yet committed). `kFwVersion` was still `cpa-fw/0.3.1` then; after merging v0.3.2 and the Mac side of v0.4.1 it was changed to `cpa-fw/0.4.0`.
> **The real-device part has run** (2026-10-21 18:38–18:53, the agent had the device to itself, firmware tree `7bf847d` on `release/v0.4.0`, see §7.2). The nosd regression and `test_sd_store`, `test_rec_sd`, `test_rec_rotation` were not run this time.

### 7.0 What changed in the harness

- **Root page**: `src/module_list.cpp` registers Home (first in the list), so the root page changes from the project board to Home (`debug.state.mod=home`, 13 §5.5). The project board now opens with Opt+P.
- **`Device.home()`**: reads `debug.state.ui` first. If locked, it presses Opt+L, Enter to unlock; if the menu is open or there is an overlay, it presses Fn+`; on Compose it presses `, Fn+`, waits out the 400 ms guard of "Discard?" (放弃？) (docs/20 U18) and then presses Enter, so the draft is discarded, not saved; finally it presses Opt+H and waits for `mod=home`, `overlay=none`, the menu closed and no lock. Most scenarios call it at the start.
- **`back_keys`**: ` from the Inbox (等你) page and the System page goes back to Home (it used to go back to the board). In the v0.1–v0.3.1 scenarios this is the only expectation that changed.
- **New scenarios `nav`, `menu`, `lock`**: run only when named; `all` is still the 10 from v0.1. `menu` and `lock` each record once (a meeting and a memo), with a 600 s timeout, and first wait for the card to be empty (like `rec_*`). On nosd firmware the recording parts are skipped and the report says `card: false`.
- **Random keys are used only in `lock`**, and exclude Opt+L, Enter and G0 (13 §19), so the device never unlocks itself or starts recording. The seed goes into the report's `seed`, so a run can be reproduced.
- The pure logic is in `tools/hil/harness/ui.py`. The unit test `tools/hil/tests/test_ui.py` checks the letters of the Home tiles (`modules/home/home_logic.h`) and the global layer's Opt keys (`lib/input/keymap.cpp`) against the `nav` plan, one by one.

| Scenario | How it runs, and pass if |
|---|---|
| `nav` | Every step starts from Home. On Home, P / I / M (Projects, Inbox, System), Enter (the first tile, P), `/` then Enter (I), `1` (the first project page) and Opt+P / I / M each reach their page, and then ` goes back to Home; N / Q / S open Compose and Fn+` closes it; L locks, and Opt+L, Enter unlocks; R with a card opens the meeting confirm box (after the 400 ms guard, ` cancels, nothing is recorded), and with nosd it is refused and stays on Home; Opt+P, Opt+I, then Opt+H goes back to Home, and ` there still stays on Home (the stack was cleared) |
| `menu` | Tab on Home, the board, Inbox, System and Compose: `ui.menu` becomes true, `mod` / `overlay` do not change (on Compose `overlay` is still compose); Tab again closes it. Enter runs a row: row 1 of "Here" (本页) on Home (P) goes to the board; row 1 of "Go" (去哪) on the board (Opt+H) goes back to Home; i on "Go" goes to Inbox; row 1 of "Control" (控制) (Opt+L) locks. With a card, one more meeting: with the menu open, Space, 1, 0 and Opt+R never reach the meeting page (the menu stays open, no end confirm box, still recording); close the menu and open it again, and Enter runs row 1 of "Here" (★); then Tab, G0 (the menu closes and a ★ is added). The manifest pulled after the end has exactly 2 ★ and no project |
| `lock` | Opt+L on Home locks; after 200 random keys, `mod`, `overlay`, the menu, the lock, `outbox.pending` / `bad` and `rec.live` / `mode` / `files_n` are all unchanged and `key_ms` has changed (the keys did reach the device); the Mac has no new note and no `item.received`. With a card: holding G0 for 3 s on the black screen never starts a memo recording; once the card has gone dark, tap G0 and then hold it for 3 s: one memo is recorded, the device stays locked, and exactly 1 new memo is pulled. Unlock: Opt+L then Enter right away unlocks and returns to Home; lock again, then Opt+L, wait 3.5 s, Enter: still locked (the report's `gap_ms` is the gap between the two key presses on the device, and must be ≥3000); Opt+L, Enter again unlocks |

### 7.1 How to run (for the Lead, with the device to itself)

```bash
cpa pause 5400; lsof /dev/cu.usbmodem*          # the serial port must be free
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_rec_budget' -f 'device/test_sd_store' \
  -f 'device/test_rec_sd' -f 'device/test_rec_rotation' -f 'device/test_mic_gain' -f 'device/test_mic_cold'   # card in
pio run -e cardputer-adv-test -t upload && cd ../..
cpa pause 1                                      # rec_cold_start's preflight needs cpad resumed and 0 segments on the card (§6.10)
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_cold_start --report "$TMPDIR/hil-v040-cold.json"
cpa pause 5400; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios nav,menu,lock --report "$TMPDIR/hil-v040-ui.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_empty,sd_store,sd_bulk,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys --report "$TMPDIR/hil-v040-regress.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo,rec_meeting,rec_resume,rec_live_hold,rec_reboot --report "$TMPDIR/hil-v040-rec.json"
(cd firmware/app && pio run -e cardputer-adv-test-nosd -t upload)
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_nosd,handshake,capture,offline_replay,dup_ack,reboot,board,spawn,offline_spawn,latency,cjk,back_keys,nav,menu,lock --report "$TMPDIR/hil-v040-nosd.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)          # release build (kFwVersion is already cpa-fw/0.4.0)
cpa pause 1 && cpa status                                          # link: ready, fw=cpa-fw/0.4.0
```

- The order in `--scenarios` does not matter: `run_e2e.py` sorts by `harness.select.CANONICAL`, with the storage scenarios first and `sd_bulk` before `handshake` (so §6.9's problem, `handshake` right after `sd_store`, cannot happen).
- `rec_rotation` and `rec_soak` are long; as in v0.3, they can run at another time.
- v0.4.0 includes v0.3.2's cold-start fix (#31), so `test_mic_cold` and `rec_cold_start` also need one run on the v0.4.0 firmware. Like `mic_cal`, `rec_cold_start` runs on its own and plays its tone through the Mac's speakers; with the Mac's volume low the tone is too quiet, see §6.10. The simulator cannot run it (`firmware/sim/README.md`).

### 7.2 Device tests and automated scenarios (2026-10-21 18:38–18:53, the agent had the device to itself)

Firmware tree: `7bf847d` on `release/v0.4.0` (clean working tree). Device and card as in §6.0, on USB the whole time. Before starting, `cpa status --json`: 0 segments on the card, not pulling; the Mac's output was MacBook Pro Speakers, not muted, volume **25** (read only, not changed). All recordings stayed in HIL's temporary directories, and the user's `~/CardputerNotes` was not touched; the user's cpad connected to the test firmware only during `rec_cold_start`'s preflight (0 segments on the card, nothing pulled). Reports and logs are in the agent's scratchpad.

```bash
cpa pause 3600; lsof /dev/cu.usbmodem*                              # serial port free
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_rec_budget' -f 'device/test_mic_gain' -f 'device/test_mic_cold'
pio run -e cardputer-adv-test -t upload && cd ../.. && cpa pause 1   # wait for link ready and 0 segments on the card
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_cold_start --report "$SP/hil-v040-cold-1.json"
# runs 2 and 3: before each, cpa pause 1 and wait for ready and 0 segments on the card; a temporary wrapper script sets scenarios.rec_cold.TONE_DBFS to -1.0 and then calls harness.runner.main (as in §6.10)
cpa pause 3600; lsof /dev/cu.usbmodem*
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,board,cjk,back_keys,nav,menu,lock,rec_memo,rec_meeting,rec_resume --report "$SP/hil-v040-main.json"
uv run --project daemon python tools/hil/run_e2e.py --scenarios sd_store --report "$SP/hil-v040-sdstore.json"   # on its own: it restarts the device
(cd firmware/app && pio run -e cardputer-adv -t upload)              # release build
cpa pause 1 && cpa status
```

- [ ] **`test_runtime_budget` (the whole app, now with the Home module)**: **the test in the repo FAILs** (1/2); the firmware is fine, the test's workload is out of date.
  - New screens drawn, all reached with real keys: Home, the three menu pages (Here / Go / Control), the black screen and the lock card after locking, and the meeting confirm box (v0.3.1 wording, cancelled, no recording); the ● line while locked (meeting, meeting with the card, memo) is drawn directly from a copy of a "recording" state (this test does not record).
  - Pass if: PASS 2/2; in `CPA_BUDGET`, `missing_glyphs=0` (now an assertion), `stack_left` ≥ 4096, `heap_free` ≥ 153,600, `max_block` ≥ 65,536 (13 §10.3). Compare with §6.3's `stack_left=5788 heap_free=185856 max_block=163828` and note the change.
  - Glyphs were checked on the host first: every character in every string literal on these pages is in `efontCN_12` (`tools/hil/harness/glyphs.py` reads the M5GFX glyph table in libdeps; 0 missing). The `missing_glyphs` on the device is the number that counts for acceptance.
  - Date: 2026-10-21 18:38–18:45
  - **The version in the repo**: `test_rx_burst_8k_is_not_lost` PASS (8,192 B, 0 errors); `test_firmware_stack_and_heap_budgets` FAILs at line 202: after Opt+R, `rec.phase` is not Confirm, and `CPA_BUDGET` is never printed.
  - **Cause** (found on the real device by adding prints to a copy of the firmware in the scratchpad): `workload()` presses Opt+S on the root page to open a session. In v0.4.0 the root page is Home, where `contextSlot()` is 0, so the Enter that submits gets `NeedProject` and Compose stays open. The screens after it (the confirm box, `agent.spawn`, saving as queued, Opt+I / M / 1) were all eaten by Compose. When `v040_screens()` starts, `overlay=compose`; the Tab menu and locking with Opt+L still work, but Opt+R goes into Compose, so the meeting confirm box never opens. The firmware behaves as designed; no firmware problem was found.
  - **Result in the copy**: at the start of `v040_screens()`, press Fn+`, Enter to close that Compose; only these two keys were added, nothing else changed. With that it is **PASS 2/2**: `CPA_BUDGET stack_left=5788 heap_free=182944 heap_min=176532 max_block=163828 sent=34 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3696`.
  - Compared with §6.3: stack headroom and largest block unchanged, free heap 2,912 B lower (185,856 → 182,944). Note: the open-session path was not reached this time. In v0.3 the root page was the board overview, whose `ui_.slot` is also 0, so reading the code, §6.3's run did not reach it either.
  - **The test in the repo needs a separate fix**: enter a project page before Opt+S (so opening a session has a project), or close the overlay at the start of `v040_screens()`. Rerun once it is fixed. The copy has been deleted; only the diff was kept.
- [x] **The other device suites** (`test_rec_budget`, `test_mic_gain`, `test_mic_cold`, regression; `test_sd_store`, `test_rec_sd`, `test_rec_rotation` not run this time)
  - Pass if: they pass as in §6.3 / §6.8 / §6.9 / §6.10 (`test_sd_store`'s mount heap ≤16 KB is the known issue in §6.7 item 2, recorded as before; in `test_mic_cold`'s `CPA_COLD`, the cold run has at least 5 blocks, warm and after_warm_up 0 blocks).
  - Date: 2026-10-21 18:38–18:40 Result: all three PASS (11 cases in all).
    - `test_rec_budget` 3/3: `CPA_REC_BUDGET idle_free=273168 idle_block=262132 rec_free=272900 rec_block=262132 xfer_free=278808 audio_stack_left=2372 ring_high=1`, only a little over a hundred bytes from §6.10.
    - `test_mic_gain` 3/3: `reg14=0x1A reg17=0xC5 gcd=1 gain_failures=0`; back on M5's defaults, `reg14=0x10 reg17=0xBF gcd=8`.
    - `test_mic_cold` 4/4: `CPA_COLD` cold **7 blocks (896 ms)**, warm 0 blocks, after_warm_up 0 blocks, warm_up_ms **491**, exactly as in §6.10.
- [x] **HIL `nav`, `menu`, `lock`** (card in, `cardputer-adv-test`, `hil-v040-main.json`)
  - Pass if: all three scenarios ok; `menu.card` and `lock.card` are true; in `lock`, `opt_l_enter_within_3s_unlocks` has `gap_ms` <3000 and `enter_after_3s_stays_locked` has `gap_ms` ≥3000.
  - Date: 2026-10-21 18:48–18:50 Result: `nav` **30/30** (15 steps, `card: true`), `menu` **24/24**, `lock` **12/12** (seed **4191819892**).
    - `menu`: `card: true`; the manifest of meeting `…000A` has exactly 2 ★ and no project.
    - `lock`: `card: true`. 200 random keys changed nothing. Holding G0 on the black screen recorded nothing. Tap then hold recorded exactly 1 memo (`…000B`), and it stayed locked afterwards. `gap_ms`: Enter within 3 s **124** (unlocked), after 3.5 s **3,616** (still locked), unlocking again **133**.
- [x] **HIL `rec_cold_start`** (card in, `cardputer-adv-test`, `hil-v040-cold-{1,2,3}.json`): **15/15** (2 runs) once the tone file is raised to −1 dBFS.
  - Pass if: 15/15 as in §6.10: on all three memos (cold / warm / power_on), the muted part at the start is no more than 20 ms, the tone is audible in the first 250 ms, and start to `● REC` is under 2.5 s; `cold_was_cold` and `power_on_warm_up_worked` both pass.
  - Date: 2026-10-21 18:45–18:48 Mac volume: **25** (MacBook Pro Speakers, not muted, read only, not changed)
  - Run 1, the scenario as it is (tone file at −20 dBFS): **12/15**; only the three `*_tone_from_the_start` checks fail, all on level: −54.46 / −54.68 / −54.73 dBFS against a threshold of −50. The other 12 pass, with 0 ms muted at the start on all three. Same as §6.10's run 1.
  - Runs 2 and 3 used §6.10's wrapper script with the tone file at −1 dBFS (neither the repo's files nor the system volume were changed): **15/15, 15/15**.

    | Run | Start to `● REC` (cold / warm / power_on, ms) | Muted at the start (ms) | Tone, first 250 ms (dBFS) | Length (s) |
    |---|---|---|---|---|
    | 1 (−20 dBFS) | 1,128 / 137 / 134 | 0 / 0 / 0 | −54.46 / −54.68 / −54.73 | 2.97 / 3.97 / 3.97 |
    | 2 (−1 dBFS) | 1,128 / 142 / 153 | 0 / 0 / 0 | −43.35 / −43.51 / −43.50 | 2.97 / 3.97 / 3.97 |
    | 3 (−1 dBFS) | 1,128 / 145 / 156 | 0 / 0 / 0 | −43.35 / −43.61 / −43.46 | 2.97 / 3.97 / 3.97 |

  - Every run passed the landing checks and emptied the card, and each segment had one `stored` and one `deleted`. The cold memo was about 1 s shorter every time: the muted part is dropped, not written into the file (as in §6.10).
- [x] **v0.1–v0.3.2 regression, card in** (`hil-v040-main.json`, `hil-v040-sdstore.json`): **every scenario ok** (`nav`, `menu`, `lock` were in the same run, see above).
  - Pass if: every scenario ok. The root page changed, and only `back_keys`'s expectation changed with it (back to Home).
  - Date: 2026-10-21 18:48–18:52
  - Automated scenarios: `handshake` 6/6, `capture` 5/5, `board` 3/3, `cjk` 7/7, `back_keys` 8/8; recording `rec_memo` 18/18, `rec_meeting` 20/20, `rec_resume` 10/10; `sd_store` 8/8 (run on its own).
  - `handshake`: `fw=cpa-fw/0.4.0`, `store=sd`, the same boot, `drift_ms` 1. `capture`: 0.103 s. `board`: 1.15 s. `back_keys`: ` from the Inbox page and the System page both return to `home/none`.
  - `sd_store`: 3 items across a `debug.reboot`, each delivered once, boot `…0001T` → `…0001V`, `device_not_ready` 1 (the restart, expected).
  - `rec_memo`: G0 press to first sample **507 ms**; 3,456 ms, 592 samples fewer than "press + 4 s"; `lead_ms` 150; one todo line in the daily notes, card emptied.
  - `rec_meeting`: Enter to first sample **8 ms**; 3 segments, both full segments exactly 480,000 samples, `gaps` empty; ★ error −149 / −70 ms; 1,536 samples (96 ms) short over 70,880 ms.
  - `rec_resume`: `.part` 285,660 B, resume offset **220,124 = 285,660 − 65,536** ✅; 2 connections, archived only once.
  - **Pulls got slower; cause not investigated**: `rec_meeting` 256.0 KB/s (about 7.3 minutes for 1 hour), against 336.9 KB/s in §6.10; `rec_memo` 212.5 KB/s, against 316.1 KB/s in §6.10. Both still pass the thresholds (≥100 KB/s, ≤20 minutes for 1 hour). This HIL run used this repo's cpad after v0.4.1 was merged.
- [ ] **nosd regression** (`cardputer-adv-test-nosd`, `hil-v040-nosd.json`): **not run this time**.
  - Pass if: every scenario ok; `nav`, `menu`, `lock` report `card: false` (recording parts skipped, R refused).
  - Date: ＿＿ Result: ＿＿
- [x] **Flash the release build back, cpad resumed**
  - Pass if: `cpa status` shows `link: ready`, `fw=cpa-fw/0.4.0`; `debug.(key|state|reboot|rec|mic|codec)` appears 0 times in the release `firmware.bin`; it boots to Home.
  - Date: 2026-10-21 18:52
  - Flash: SUCCESS 11.2 s, `pre_upload: skip: already paused for 3326 s`. RAM 65,968 B (20.1%), app 969,824 B written at 0x10000, "Hash of data verified", and a normal boot after "Hard resetting via RTS pin".
  - Release image: the SHA-256 of `firmware.bin` is `847d67ac…4be5c021`, `debug.(key|state|reboot|rec|mic|codec)` all **0 times** (1 each in the test build), version string `cpa-fw/0.4.0`.
  - cpad resumed (18:52:49): `cpa status` after `cpa pause 1`: `link: ready  dev=020000000001 fw=cpa-fw/0.4.0 pending=0 store=sd launcher=no`, `recordings: on_device=0 pulling=0`; the other recording and item counts were unchanged from before.
  - "Boots to Home" was not checked: the release build has no `debug.state`, and the agent cannot see the screen.

### 7.3 Host side (2026-10-21, V40-H, no device connected)

- [x] **Firmware** (`firmware/app`): `pio test -e native` 55 suites, 525/525 pass; gcovr (old `.gcda` files deleted first) 97% (≥80%); `pio run -e cardputer-adv` SUCCESS, 0 warnings, 0 stack budget violations; `check_budget.py` passes, static RAM 65,968 B, app 961,504 B; `check_partitions.py` passes; 0 `debug.*` names in the release `firmware.bin`, and `HomeModule` is linked in. `check_sizes.py --warn 400 --fail 800` 0 warnings over 280 files; `lizard -L 50` passes; ci.yml's include rules pass, and `lib/help` has no Arduino / M5 / esp headers either.
- [x] **The two test firmwares for HIL, built but not flashed**: `pio run -e cardputer-adv-test` (static RAM 66,248 B) and `-e cardputer-adv-test-nosd` (65,544 B) both SUCCESS, 0 stack budget violations.
- [x] **Device suites, built but not run**: `pio test -e cardputer-adv-test -f 'device/*' --without-uploading --without-testing`; all 9 suites compile and link; every suite's own stack frames are within 4,096 B (the largest frame, in `test_runtime_budget`, is 784 B).
- [x] **Scripts and harness**: `pytest scripts test/core/support` 60 passed (new tests for v0.4.0's stack chains and the `help` / `home` directories); `pytest tools/hil/tests` 367 passed; `ruff check` / `ruff format --check` (0.17.1) pass.
- [x] **After merging v0.3.2 and the Mac side of v0.4.1** (2026-10-21, `kFwVersion` already `cpa-fw/0.4.0`): `pio run -e cardputer-adv` static RAM 65,968 B (unchanged), app **969,824 B** (8,320 B more than above); `debug.(key|state|reboot|rec|mic|codec)` 0 times in the release `firmware.bin`. The device suites, now 10 with v0.3.2's `test_mic_cold`, built but not run: all compile and link.

### 7.4 Budget (13 §10.3)

| Item | v0.3.1 | v0.4.0 (this worktree) | Limit | Status |
|---|---|---|---|---|
| Static RAM | 65,920 B | 65,968 B (+48 B) | growth ≤4 KB, total ≤96 KB | ✅ host |
| App image | 945,312 B | 969,824 B (+24.5 KB; 961,504 B before merging v0.3.2 and v0.4.1) | ≤2.5 MB (a notice above 1.75 MiB) | ✅ host |
| Loop stack, static chains (`stack_budget.py`) | — | New chains: menu keys 4,432 B, drawing the menu 4,832 B, drawing Home 2,448 B, drawing the lock screen 1,824 B, keys while locked 1,440 B; the deepest of all chains is still `outbox tick` at 9,904 B | ≤12,288 B (4 KB left of the 16 KB stack) | ✅ host |
| `AppState` | 3,488 B | 3,488 B (`UiFlags` went into existing padding bytes; host `sizeof`, the same on the device) | ≤4,096 B | ✅ host |
| Loop stack measured, free heap, largest block | 5,788 B / 185,856 B / 163,828 B (§6.3) | 5,788 B / 182,944 B / 163,828 B (`test_runtime_budget`, which only got through with two keys added in a copy, §7.2) | ≥4,096 B / ≥153,600 B / ≥65,536 B | ✅ real device (the test in the repo still to fix) |

### 7.5 Checks that need a person (👤)

When an item is done, change its `[ ]` to `[x]` and fill in the date, the result and your name.

- [ ] **needs user** · **V40-H-1 carry the locked device for a day** (13 §17, §19)
  - How: the Lead flashes release build v0.4.0, and your own cpad runs as usual. In the morning press Opt+L to lock, put the device in your pocket and carry it through a normal day. Use it as usual when you want to note something: tap G0 once, then hold G0 to record a memo; to unlock, press Opt+L, then Enter within 3 seconds. In the evening plug it into the Mac and look at today's daily notes, `cpa rec ls` and `cpa status`.
  - Pass if: **0 actions you did not want** (pages you did not mean to open, notes you did not mean to write, queue items you did not mean to add, sessions you did not mean to open); **0 memos or meetings you did not mean to record**. Also note roughly how many times the screen lit up in your pocket, how many memos you recorded on purpose, and how many unlocks failed.
  - Date: ＿＿ Unwanted actions: ＿＿ Unwanted recordings: ＿＿ Screen lit / memos / failed unlocks: ＿＿ Signed: ＿＿
- [ ] **needs user** · **V40-H-2 read through the meeting confirm box wording** (about 2 minutes, 13 §7.4)
  - How: press Opt+R, read the confirm box on the screen, then press ` to cancel (no recording starts). v0.4.0 keeps the v0.3.1 wording:
    ```
    开始会议录音？
    请先告知在场的人
    Enter 开始    ` 取消
    ```
    (`render_meeting_confirm` in `firmware/app/lib/shell/rec_overlay.cpp`; in English: "Start a meeting recording? Tell everyone here first".) The six-line wording proposed in 13 §7.4 (which spells out that "telling people and getting their consent is your responsibility") was **not adopted**, because you have not agreed to it yet.
  - Pass if: you have read it and decided whether to keep the original wording or switch to §7.4's. A switch is a separate change, with firmware and tests changed together.
  - Date: ＿＿ Decision: ＿＿ Signed: ＿＿

## 8. v0.4.1: G0 grace period and double tap for memos (gh #51, docs/20 U26)

### 8.1 How to run (test firmware, card in)

```bash
uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo,rec_memo_grace,rec_memo_handsfree --report "$TMPDIR/hil-g0.json"
```

| Scenario | What it does, and pass if |
|---|---|
| `rec_memo` | Holds G0 for 4 s as before; the stop time now adds the 0.6 s grace period (`MEMO_GRACE_MS`), and the sample count is checked against "press + 4 s + 0.6 s" |
| `rec_memo_grace` | Holds G0 for 2.5 s, releases it for 0.3 s (within the grace period), then holds it for 2 s. Pass if: only one session and one `seg-000.wav`; the sample count is within 4,096 of "first sample to the second release + 0.6 s" (the 0.3 s release did not break it); the card is emptied |
| `rec_memo_handsfree` | Double taps G0 (the second tap 30 ms after the first is released), leaves G0 released for 5 s, then presses G0 once more. Pass if: still recording after 5 s; one session, one file, length ≥ 4.5 s, samples up to "the ending press going down"; afterwards a single tap on G0 still opens a Note |

Injected keys (`debug.key`) skip the device's G0 debounce, just as they used to skip M5Unified's debounce, so how debounce works with real presses needs a person to try (8.3 below).

### 8.2 Results on the simulator (2026-10-22, `tools/sim/run_hil_sim.py`)

- `rec_memo` 18/18, `rec_memo_grace` 10/10 and `rec_memo_handsfree` 13/13 pass, run together with `handshake`, `nav`, `menu`, `lock`, `capture`, `rec_meeting`, `rec_live_hold`. The numbers are the simulator's, not the real device's.
- Rerun after merging into v0.4.1 (`feat/ux-decisions`, together with docs/20 D1–D10): `handshake`, `nav`, `menu`, `lock`, `capture`, `board`, `spawn`, `cjk`, `back_keys`, `rec_memo`, `rec_meeting`, `rec_resume`, `rec_live_hold`, `rec_memo_grace`, `rec_memo_handsfree` 15/15 in one run (`rec_memo` 18/18, `rec_memo_grace` 10/10, `rec_memo_handsfree` 13/13); `latency` passes too (capture page p95 400 ms, limit 650 ms).

### 8.3 Checks that need a person (👤)

- [ ] **needs user** · **U26-1 is a double tap recognised** (about 10 minutes, the debounce part of docs/20 U26)
  - How: flash the test firmware. In three places, Home, the project board, and right after saving a Note (just as "Saved" (已保存) appears), double tap G0 20 times each, and look at the screen after each one: if it is "Hands-free" (免提录音), press once more to end it; if it is a Note, press `` ` `` to close it (from v0.4.1, Fn+` types a backtick in the editor, docs/20 D5).
  - Pass if: on Home and on the board, ≥ 19 of 20 recognised; right after a Note, write down how many were recognised (a loop is slowest then, and two taps too close together merge into one). A miss should never record by mistake (at most it opens a Note, or gives a "Too short: discarded" (录音太短，已丢弃)).
  - Then 5 slow slips: on Home, hold G0 and talk; halfway through, let go for about half a second (the page says "Let go · hold within 0.6 s" (松开了 · 0.6 秒内按回继续)), press again and keep talking, then let go at the end. Pass if: each time there is only one segment (one entry in `cpa rec ls`, as long as the whole utterance), not cut into two. A G0 release only counts when it is read twice, 30 ms apart (debounce); only a real press of the side key can test this, since keys injected with `debug.key` skip debounce (§8.1).
  - Date: ＿＿ Home / board / right after a Note: ＿＿ / 20, ＿＿ / 20, ＿＿ / 20 Slow slips cut in two: ＿＿ / 5 Signed: ＿＿
- [ ] **needs user** · **U26-2 hold and talk for a day** (gh #51: memos cut off mid-sentence)
  - How: release build v0.4.1. Through a day, record memos by holding G0 as usual, and record a few longer ones with a double tap.
  - Pass if: no memo breaks off mid-sentence and carries on as a new one (`cpa rec ls` has no two entries less than 1 second apart); every hands-free memo ends the moment you press G0.
  - Date: ＿＿ Held / hands-free count: ＿＿ Broken off: ＿＿ Signed: ＿＿

### 8.4 Real device: v0.4.1 test firmware (2026-10-22 14:50–15:36, the agent had the device to itself)

Firmware tree `199b823` on `feat/ux-decisions` (D1–D10, the theme, `fix/memo-hold` and `fix/level-meter` are all in it; clean working tree), `kFwVersion` is `cpa-fw/0.4.1`. Device and card as in §6.0, on USB the whole time. Before starting, `cpa status --json`: 0 segments on the card, not pulling, `pending=0`; `cpa pause 7200`, `lsof` empty. All recordings stayed in HIL's temporary `$CPA_HOME`, and `~/CardputerNotes` was not touched; the user's cpad stayed paused until the release build was flashed back (its item counts unchanged from before). Scratch scripts, reports and logs are in the agent's scratchpad (`dev041/`), not in the repo.

```bash
cpa pause 7200; lsof /dev/cu.usbmodem1101                       # serial port free
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_rec_budget' -f 'device/test_mic_cold' -f 'device/test_rec_rotation'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,nav,menu,lock,capture,board,back_keys,cjk,rec_memo,rec_meeting,rec_resume,rec_rotation,rec_memo_grace,rec_memo_handsfree,rec_live_hold --report "$SP/hil-main.json"
# scratch: ux_gates.py (UX-H2's key timing, the D3 start box, no lock while on USB); debug.rec seg_ms 300000 (rec_rotation leaves the segment length at 10 s until a restart);
#          mixed_soak.py --minutes 15 (the mixed soak used for integrate/v041, with this branch's harness)
# temporary probe firmware (below), scratch probe_checks.py: levels, UX-H5, simulated battery
(cd firmware/app && pio run -e cardputer-adv -t upload)              # release build
cpa pause 1 && cpa status
```

**Temporary probe firmware** (for this run only, not in the repo): a copy from `git archive 199b823` in the scratchpad, 50 lines added across 6 files, all inside `CPA_TEST_HOOKS`: when `debug.rec`'s `seg_ms` is 10001, the runtime drops the next finish event as it reads events (Saved / Discarded / Failed, S1's fault injection); 10002 / 10003 make `power_tick` act as if USB is unplugged / plugged in (only this one input to the power policy changes; the link carries on as before); the health fields of `debug.state.rec` are replaced with levels (`ring_high` is the RMS of the last block after the high-pass, and `mic_missing` packs the block count, q, whether there is voice, and Mando's action, the same as the probe from `fix/level-meter`); `diag.ghost_n` and `kb_reinit_n` are replaced with the drop count and the battery switch. None of these had an existing hook: the regular `debug.*` hooks have no switch for dropping events or for running on battery, and levels are not in the protocol.

- [x] **Device tests**: 4 suites, 10/10 (14:51–14:54).
  - `test_runtime_budget` 2/2: `CPA_BUDGET stack_left=4844 heap_free=177376 heap_min=171400 max_block=155636 sent=36 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3724 max_render_us=40616 max_tick_us=14081 meeting_us=21820`; 8 KB RX burst, 0 errors. Longest redraw 40.6 ms (limit 50); it was 40.5 ms in integrate/v041.
  - `test_rec_budget` 3/3: `idle_free=272912 idle_block=262132 rec_free=272632 rec_block=262132 xfer_free=278556 audio_stack_left=2364 ring_high=1`, only two or three hundred bytes from §7.2.
  - `test_rec_rotation` 1/1: 95 s in 30 s segments, `segments=4 samples=1523712 wall_ms=95328 expect=1525248` (1,536 samples short, 96 ms, within two blocks), `ring_high=1 stack_left=2324`.
  - `test_mic_cold` 4/4: the cold run 7 blocks (896 ms), warm and after_warm_up 0 blocks, `warm_up_ms` 491, same as §7.2.
- [x] **HIL 15/15** (197 checks, 14:54–15:06, `hil-main.json`, boot `…0002R`, `fw=cpa-fw/0.4.1`, `store=sd`, `device_not_ready` 0 throughout)
  - `handshake` 6/6 (the same boot), `capture` 5/5 (0.103 s), `board` 3/3 (0.81 s), `cjk` 7/7 (0 missing glyphs), `back_keys` 8/8, `nav` 32/32 (17 steps, `card: true`), `menu` 24/24, `lock` 12/12 (seed 2004812512, 200 random keys changed nothing).
  - `rec_memo` 18/18: G0 press to first sample 507 ms, 3,968 ms, `lead_ms` 150, 2,000 samples fewer than "press + 4 s + 0.6 s"; pull 483 KB/s.
  - `rec_meeting` 20/20: Enter to first sample 10 ms; 3 segments, ★ error −150 / −70 ms; 1,600 samples (100 ms) short over 70,784 ms; pull 475.6 KB/s (about 3.9 minutes for 1 hour; 256 KB/s in §7.2).
  - `rec_resume` 10/10: `.part` 322,920 B, resume offset **257,384 = 322,920 − 65,536**. `rec_live_hold` 16/16 (recording started during a pull, 3 busy, caught up afterwards).
  - `rec_rotation` 13/13: 31 segments of 10 s, 305,885 ms, 1,488 samples (93 ms) short; health counters `ring_high` 1, `mic_missing` 0, `rot_ms_max` 103 ms, lowest free heap while recording 192,128 B; 31 files, 9.8 MB, 454.7 KB/s.
  - `rec_memo_grace` 10/10: hold 2.5 s, release 0.3 s, hold 2 s again: one session, one file, 4,864 ms, sample difference −464. `rec_memo_handsfree` 13/13: double tap, still recording after 5 s released, one more press ends it, 4,992 ms, sample difference −528, and a single tap afterwards still opens a Note.
- [x] **A scripted UX-H2** (`ux-gates.json`, 15:06–15:09): the keys were pushed into the device in one batch with `debug.key` (a Shift held on its own serves as the delay in between and produces no key event), so the gaps are milliseconds on the device. Opt+L always came 250 ms after Enter. Human speed is still for you to try in UX-H2.

  | Enter after Opt+R | Start: "Opt+R, Enter, Opt+L" | End: "Opt+R, Enter" |
  |---|---|---|
  | 250 ms, 350 ms (inside the 400 ms guard) | Not started; Opt+L cancels the start box and locks, no box left, nothing recorded. Tried again with a second Enter 500 ms after the first: started and locked | The end box stays, still recording; after 10,176 / 10,213 ms (from Enter) it returns to the meeting page and keeps recording; ` and Enter on the meeting page do nothing; then Opt+R, 600 ms, Enter ends it |
  | 450, 600, 800 ms | Started and locked, the box gone | Ended at once |

  All 5 meetings reached the Mac, `consent: ack` (the two fast ones 15 s and 2 segments each, because of the 10 s segment length left behind by `rec_rotation`; the three slow ones about 4 s and 1 segment each).
- [x] **D3 start box** (same run): with no key pressed, it closes by itself after 15,156 ms (from Opt+R), nothing recorded, back to Home; `a` or `1` on the box (after the guard): both cancel and stay on Home, opening neither the editor nor a project page.
- [x] **D2 no auto-lock while on USB** (same run): 75 s on Home with no key pressed, not locked.
- [x] **UX-H5: a lost finish event** (probe firmware, `probe-checks.json`, 15:31): a memo held for 3 s, with its finish event dropped. 8 s after stopping it was still finishing (`live`); it returned to idle at **10,146 ms** (stop sent again at 5 s, given up at 10 s, plus one loop); at that point the card still had 1 file, which was pulled soon after; drop count 1. The next memo, held 2.5 s, was normal: idle 101 ms after stopping. Both reached the Mac, and the card was empty at the end.
- [x] **D2 on battery (simulated)** (probe firmware, 15:31–15:35): with the battery switch on, it locked **60,948 ms** after the last key press (Opt+H) (dim at 50 s, screen off at 60 s, plus 1 s of polling); holding G0 for 1.5 s twice (less than 1 s apart) on the locked black screen recorded nothing both times; after unlocking, a double tap recorded hands-free for 80 s, checked every 5 s: all 16 times recording and not locked (while recording it only dims); one G0 press ended it, and it locked again **60,990 ms** later (on ending, the screen lit up as if a key had been pressed). This is a simulation: the day carried in a pocket off USB (UX-H1) is still yours to do.
- [x] **Levels in a quiet room** (probe firmware, 15:30): nobody talking, a double-tap hands-free recording of 20 s (19,968 ms, 156 blocks), reading `debug.state` every 40 ms (380 reads).
  - Block 1 counts as quiet by rule; the other 155 blocks all have q 0 (all 3 px dots on the waveform; a dot goes up to q 34), "voice" 0 times, 0 blocks above kVoiceOn (q 71); all 363 reads of Mando show Tilt (head tilted), **not a single nod** (Listen 0).
  - Room level (after the high-pass, per block): RMS 7–17, median 11; **−73.4 to −65.7 dBFS, median −69.5 dBFS**, 95th percentile −66.8. That is 9.7 dB below the "voice" hold line (−56 dBFS) and 15.7 dB below the start line (−50 dBFS). The raw WAV (no high-pass, first 250 ms removed) is −66.5 dBFS.
  - The pulled WAV, run on the host through the firmware's own `rec::level_of` and `theme::level_push` (scratch `level_replay`): the RMS and q of 156/156 blocks match what was read on the device.
  - This time the room had none of the low hum from the `fix/level-meter` run (raw −52 to −57 dBFS then), so this only shows that silence draws dots and Mando does not nod; behaviour under hum is still covered by the synthetic cases in `test_rec_level`.
- [ ] **15-minute mixed soak** (`mixed-soak.json`, 15:09–15:25, seed 2608940095, 13 rounds, 948 s): 61 of 62 checks pass; **`mic_missing_0_every_recording` fails** (below).
  - What passed: 39 notes, each exactly once; 26 recordings (13 memos, 13 meetings), each archived once, each segment with one `stored` and one `deleted`; one boot (`…0002R`), 0 restarts, `up_ms` monotonic; apart from 20 port closes (7 disconnects mid-pull, 13 offline) 0 TX stalls, `E_NOT_READY` 0; random keys while locked had no effect 13/13; meeting plus Tab menu plus lock 13/13 (exactly 2 ★ and no project each time); the two notes typed offline 13/13 (`pending` 2 at hello); after 40 random keys while unlocked (no Enter, G0 or Opt+D), back to Home 13/13, with not a single extra capture; outbox and card both empty at the end.
  - `debug.state.diag` every 30 s (32 reads): idle free heap averaged 195,552 B over the first third and 196,173 B over the last third (flat), 195,948 at the start and 198,852 at the end; lowest 176,928 B, lowest largest block 163,828 B; loop stack headroom 5,948 B and audio task 2,152 B, unchanged from start to end; longest loop 108 ms (>50 ms twice, >100 ms once, >500 ms 0 times; integrate/v041 had a longest of 98 ms and >50 ms twice); ghost 0, keyboard restarts 0.
  - **The one that failed**: after 12 of the 13 memos, `debug.state.rec.mic_missing` was 1; all meetings were 0. After the soak it was looked into separately on the test firmware (15:26–15:30, `missprobe*.json`): the counter becomes 1 at the moment of stopping (already finishing by then), and it has nothing to do with whether notes were taken earlier in the soak, only with how long G0 is held: 5 more holds of 3,000 ms had it 5 times out of 5; holds of 2,500, 3,032, 3,064 and 3,096 ms once each, 4,000 ms three times, plus one hands-free and one meeting, never had it; for holds of 3,000 to 3,096 ms, with or without the extra 1, the recorded length was 3,072 ms and `ring_high` stayed 1, so no audio was lost. From the source, the likely cause is that when `AudioTask::end_session` closes the mic (`Mic.end()`), the failed buffer re-arm in `MicRing::on_release` is also counted as a miss. The soak's memos happened to be held exactly 3,000 ms each; the integrate/v041 soak (also 3,000 ms holds) did not have the 0.6-second grace period yet, so the stop fell at a different moment and it was 0. What needs fixing is the counter (stop counting once stopping has begun); the recording itself needs no change; the release firmware does not carry this counter.
  - 2026-10-22 (`feat/memo-legend`): **fixed, waiting for the real device**. `MicRing::stop` marks "stopping" before `Mic.end()`; after that, buffers that cannot be re-armed in the capture task's callback neither count as `mic_missing` nor stay owed (`rec::rearm_missed`; native `test_rec_health` was written first as a failing test: failures after stopping do not count, failures before it still do, and a segment that stops on a block edge, like a 3,000 ms hold, reports 0). To rerun on the real device: on the test firmware, the 3,000 ms hold probe gives 0 every time over 10+ runs, and the mixed soak's `mic_missing_0_every_recording` passes.
- [x] **Flash the release build back, cpad resumed** (15:35)
  - Flash: `pre_upload: skip: already paused`, RAM 66,784 B, app 1,046,144 B written at 0x10000, "Hash of data verified", and a normal boot after "Hard resetting via RTS pin". The release `firmware.bin` has SHA-256 `e919a34e…df674165`, `debug.(key|state|reboot|rec|mic|codec)` all 0 times (1 each in the test build), version string `cpa-fw/0.4.1`.
  - `cpa status` after `cpa pause 1`: `link: ready  dev=020000000001 fw=cpa-fw/0.4.1 pending=0 store=sd launcher=no`, `recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`.
- **Not run**: the nosd regression, `sd_store`, `rec_cold_start` (needs the Mac to play a tone), `latency`; device tests `test_mic_gain`, `test_rec_sd`, `test_sd_absent`, `test_sd_store`, `test_sd_wake`, `test_storage`; the checks that need a person: UX-H1, H3, H2 at human speed, and §8.3's U26-1, U26-2.

## 9. v0.4.3 "Settings and English" (docs/21): real device

### 9.1 Real device (2026-10-27 01:03–01:10, the agent had the device to itself)

Firmware tree `feat/fw-english` (after the English review's fixes), `kFwVersion` still `cpa-fw/0.4.1`. Device and card as in §6.0, on USB the whole time; the serial port this time was `/dev/cu.usbmodem101`. Before starting, `cpa status --json`: 0 segments on the card, not pulling, `pending=0`, `fw=cpa-fw/0.4.1`, `store=sd`. No key was pressed and G0 was not touched.

```bash
cpa pause 7200; lsof /dev/cu.usbmodem101                       # serial port free
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_storage'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,capture,nav,menu,lock,lang,rec_memo,rec_meeting,rec_memo_handsfree --report "$SP/hil-main.json"
(cd firmware/app && pio run -e cardputer-adv -t upload)              # release build
cpa pause 1 && cpa status
```

- [x] **Device tests**: 2 suites, 11/11 (01:03–01:04).
  - `test_runtime_budget` 2/2: `CPA_BUDGET stack_left=5020 heap_free=173636 heap_min=167656 max_block=155636 sent=54 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3644 max_render_us=40944 max_tick_us=14166 meeting_us=23824 memo_us=23141 memo_tick_us=2183`; **English measured for the first time**: `CPA_BUDGET_EN max_render_us=38160 max_tick_us=11850 meeting_us=22230 memo_us=21703 memo_tick_us=2121`, a little faster than Chinese, all within 50 ms; 8 KB RX burst, 0 errors. Free heap 173,636 B, 3.7 KB less than §8.4 (the Settings page, the Mac's name and more pages have now been drawn), still above 150 KiB.
  - `test_storage` 9/9: the new `test_settings_lang_round_trip_and_a_bad_byte` prints `CPA_NVS_LANG was=missing:0 en=1 bad_is_zh=1`: this device had no `cpa/lang` before (never switched); English stored reads back as English, a 7 written reads back as Chinese, and finally the key is deleted to restore "none". LittleFS save p95: acked 75.4 ms (meets the 100 ms target), accumulated 147.0 ms (known, §5.9 item 1).
- [x] **HIL 9/9** (135 checks, 01:04–01:08, `hil-main.json`, boot `1833562-00033`, `fw=cpa-fw/0.4.1`, `store=sd`)
  - `handshake` 6/6, `capture` 5/5 (0.101 s), `nav` 32/32 (17 steps, `card: true`), `menu` 24/24, `lock` 12/12 (seed 944915827, 200 random keys changed nothing), `device_not_ready` 0 in all.
  - **`lang` 5/5 (new)**: starts as `zh`; after Opt+M . Enter, `debug.state.ui.lang == "en"`; after `debug.reboot` the new boot is still `en`; Opt+M . Enter goes back to `zh`, still `zh` after a restart. `device_not_ready` twice, both the one moment after a restart before cpad handshakes again (the same on the simulator). After the run the fixture sends `debug.lang zh` (the starting language), acked.
  - `rec_memo` 18/18: press to first sample 507 ms, 3,968 ms, sample difference −2,000; pull 484.4 KB/s. `rec_meeting` 20/20: Enter to first sample 9 ms, 3 segments, ★ error −32 / −80 ms, 1,696 samples short over 70,784 ms; 474.4 KB/s. `rec_memo_handsfree` 13/13: 4,992 ms, sample difference −1,056.
- [x] **Flash the release build back, cpad resumed** (01:09): `pre_upload: skip: already paused`, RAM 66,816 B, "Hash of data verified". The release `firmware.bin` has SHA-256 `f12622f2…48a1933`, `debug.(key|state|reboot|rec|mic|codec|lang)` 0 times. `cpa status` after `cpa pause 1`: `link: ready  dev=020000000001 fw=cpa-fw/0.4.1 pending=0 store=sd launcher=no`, `recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`. The device was left in Chinese.
- **Not run**: the nosd regression, `back_keys`, `cjk`, `board`, `rec_resume`, `rec_rotation`, `rec_memo_grace`, `rec_live_hold`; a build with GCC 13 on Linux (this Mac only has clang); 👤 docs/21 §9.7 (still English after pulling power and restarting, recording a memo and running a meeting in English, the text printed on the "退格" (Backspace) keycap).

### 9.2 Simulator (same day, `tools/sim/run_hil_sim.py --scenarios handshake,nav,lang`)

3/3 pass; `lang` 5/5, the same as on the real device.

## 10. v0.5.0 "English or Chinese, projects on the Mac, one-line install, small easter eggs": real device

### 10.1 Real device (2026-10-27 01:54–02:34, the agent had the device to itself)

Firmware tree `916e29b` on `release/v0.5.0` (the easter eggs and the review fixes are all in it; clean working tree), `kFwVersion` is `cpa-fw/0.5.0`. Device and card as in §6.0, on USB the whole time, serial port `/dev/cu.usbmodem101`. Before starting, `cpa status --json`: 0 segments on the card, not pulling, `pending=0`, `fw=cpa-fw/0.4.1`, `store=sd`. No key was pressed and G0 was not touched. All recordings stayed in HIL's temporary `$CPA_HOME`; the user's cpad stayed paused until the release build was flashed back (its item counts unchanged from before). Scratch scripts, reports and logs are in the agent's scratchpad (`hil050/`), not in the repo.

```bash
cpa pause 7200; lsof /dev/cu.usbmodem101                       # serial port free
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' -f 'device/test_storage'
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,nav,menu,lock,lang,capture,rec_memo,rec_meeting,rec_memo_grace,rec_memo_handsfree --report "$SP/hil-main.json"
# scratch: eggs_check.py (easter eggs, below); mixed_soak.py --minutes 15 (§8.4's mixed soak)
(cd firmware/app && pio run -e cardputer-adv -t upload)              # release build
cpa pause 1 && cpa status
```

- [x] **Device tests**: 2 suites, 12/12 (01:54–01:55).
  - `test_runtime_budget` 2/2: `CPA_BUDGET stack_left=5004 heap_free=173552 heap_min=167604 max_block=155636 sent=54 pending=0 bad=1 store_errors=0 missing_glyphs=0 rec=1 audio_stack_left=3728 max_render_us=40944 max_tick_us=14124 meeting_us=23933 memo_us=23143 memo_tick_us=2231`; `CPA_BUDGET_EN max_render_us=38300 max_tick_us=11830 meeting_us=22338 memo_us=21722 memo_tick_us=2148`. Only tens of microseconds and tens of bytes from §9.1: the easter eggs did not slow redraws down. 8 KB RX burst, 0 errors.
  - `test_storage` 10/10: `CPA_NVS_LANG was=0 en=1 bad_is_zh=1` (this device has stored Chinese since §9.1, hence was=0); LittleFS save p95: acked 88.4 ms (meets the 100 ms target), accumulated 123.0 ms (known, §5.9 item 1).
- [x] **HIL 10/10** (145 checks, 01:56–01:59, `hil-main.json`, boot `1833562-00038`, `fw=cpa-fw/0.5.0`, `store=sd`)
  - `handshake` 6/6 (0.003 s, reopening the same boot), `capture` 5/5 (0.102 s), `nav` 32/32 (17 steps, `card: true`), `menu` 24/24, `lock` 12/12 (seed 2537295214, 200 random keys changed nothing), `lang` 5/5 (zh → en → still en after a restart → zh → still zh after a restart; `device_not_ready` twice, as in §9.1). `device_not_ready` 0 in all other scenarios.
  - `rec_memo` 18/18: press to first sample 507 ms, 3,968 ms, sample difference −2,000; pull 479.3 KB/s. `rec_meeting` 20/20: Enter to first sample 9 ms, 3 segments, ★ error −149 / −70 ms, 1,536 samples short over 70,784 ms; 472.3 KB/s. `rec_memo_grace` 10/10: one session, one file, 4,864 ms, sample difference −464. `rec_memo_handsfree` 13/13: 4,992 ms, sample difference −1,248, and a single tap afterwards still opens a Note.
- [x] **Easter eggs** (scratch `eggs_check.py`, `eggs.json`, 17/17, 02:00, 28 s): the pat count lives only in RAM, not in `debug.state`, so the check is on what can be seen: after every Opt+W, the page, overlay, menu, lock, outbox and recording (`harness.ui.changed`) are unchanged, the Mac receives nothing, and the device did get the key (`key_ms` grows).
  - On Home, 1 pat, then 109 more (110 in all; the 100th is the rare line); after each one, Opt+P to the board and Opt+H back to Home both work.
  - Lock screen: Opt+L locks, a tap on G0 lights the card, 5 pats; 3 more pats after the card goes dark: still locked each time, no recording; Opt+L Enter unlocks.
  - 3 pats on the board: nothing happens (Opt+W works only on Home and on a lit lock card). 5 pats during a meeting: the meeting keeps recording (`live`, `meeting`), the page does not change, and it ends normally afterwards.
  - Switched to English, 12 pats on Home; Home responds as usual; then back to Chinese.
  - One boot, 0 restarts; `debug.state.diag` before and after: free heap 198,732 → 198,764 B, longest loop 30 ms, >50 ms 0 times.
- [x] **15-minute mixed soak** (`mixed-soak.json`, 02:17–02:33, seed 3575258321, 13 rounds, 962 s): **62/62 pass**.
  - 39 notes, each exactly once; 26 recordings (13 memos, 13 meetings), each archived once, each segment with one `stored` and one `deleted`; one boot (`1833562-0003A`, no restart in the 36 minutes from the easter-egg check to the end of the soak), `up_ms` monotonic; apart from 20 port closes (disconnects mid-pull and offline) 0 TX stalls, `E_NOT_READY` 0; random keys while locked, meeting plus Tab menu plus lock, two notes typed offline, and back to Home after 40 random keys while unlocked: all 13/13; outbox and card both empty at the end.
  - **`mic_missing_0_every_recording` passes**: `mic_missing` is 0 after all 26 recordings (in §8.4, 12 of the 13 memos had 1; the `feat/memo-legend` fix is confirmed on the real device).
  - `debug.state.diag` every 30 s (32 reads): free heap averaged 193,920 B over the first third and 194,770 B over the last third (flat), 198,032 at the start and 198,732 at the end; lowest 170,988 B, lowest largest block 163,828 B; loop stack headroom 6,700 B, audio task 2,184 B; longest loop 106.5 ms (>50 ms twice, >100 ms once, >500 ms 0 times; §8.4 had 108 ms, twice, once); ghost 0, keyboard restarts 0.
  - The first soak (02:00–02:16, seed 1262960464) passed 61 of 62: `every_recording_archived_once` counted 27 archives for 26 recordings. The extra one was the meeting from the easter-egg check: the rig was closed as soon as the check ended, with that meeting's file still on the card (the soak's first sample showed `files_n=1` before the soak had recorded anything), and the soak's cpad pulled and archived it. A test ordering problem, not a firmware one; the rerun after emptying the card is the 62/62 above. The other 61 passed in that run too, with `mic_missing` 0 in 26/26.
- [x] **Flash the release build back, cpad resumed** (02:34): `pre_upload: skip: already paused`, RAM 66,872 B, "Hash of data verified", and a normal boot after "Hard resetting via RTS pin". The release `firmware.bin` is 1,074,560 B, SHA-256 `8b87a16f…f05e3833`, `debug.(key|state|reboot|rec|mic|codec|lang)` 0 times, version string `cpa-fw/0.5.0`. `cpa status` after `cpa pause 1`: `link: ready  dev=020000000001 fw=cpa-fw/0.5.0 pending=0 store=sd launcher=no`, `recordings: on_device=0 pulling=0 waiting=0 processing=0 blocked=0 failed=0`. The device was left in Chinese.
- **Not run**: the nosd regression, `back_keys`, `cjk`, `board`, `rec_resume`, `rec_rotation`, `rec_live_hold`, `latency`; how the easter eggs actually look on screen (the simulator screenshots were reviewed; the real screen needs a person); the moods that follow the clock (on the real device's clock they only show after 23:30, at 5–7 in the morning or at weekends; covered by host tests and the simulator); "All done" (全处理完了) on the real device (needs a session really waiting in Inbox and then cleared; fake-claude could do it, but it was not scheduled this time); 👤 docs/21 §9.7, v0.4.1's UX-H1, H3.

## 11. docs/28 S1a "keep until the right Mac": tonight's device plan (2026-10-27, to run)

Branch `feat/keep-until-right-mac` (kept copies, deliver again, give to this Mac, the 5-second plug-in hint, clear kept copies, plus the fixes from two review rounds). The device was away during the day, so everything has only been proven on the host so far (the checklist in docs/28 §12); this section is what runs tonight once the device is back, and **the branch merges only after it has run**. Rules as in §10.1: `cpa pause`, serial port free, recordings only into HIL's temporary `$CPA_HOME`, never touching `~/CardputerNotes`, the user's `$CPA_HOME` or `~/.claude`; scratch scripts, reports and logs go in the agent's scratchpad, not in the repo; at the end, flash the release build back and `cpa pause 1`.

Before starting: `cpa status --json` shows 0 segments on the card and no pull; note the `fw`. First take a look at any `/cpa/rec/*/SENT.L` already on the card (a v0.5.0 card has none).

### 11.1 Device tests (`cardputer-adv-test`, card in, automated)

```bash
cpa pause 7200; lsof /dev/cu.usbmodem101                       # serial port free
cd firmware/app
pio test -v -e cardputer-adv-test -f 'device/test_rec_ledger_sd' -f 'device/test_rec_sd' \
  -f 'device/test_rec_budget' -f 'device/test_runtime_budget' -f 'device/test_storage'
```

- [ ] **`test_rec_ledger_sd` (new, 6 cases)**: the ledger on FatFs. Three hand-overs each open the file once to append (`O_APPEND`), and `SENT.L` is exactly 3 × 28 B; on the next boot the boot scan leaves the kept copies out of the index, `kept=3`; append 5 bytes to the tail by hand (a torn write), and the next boot tidies it into `SENT.K` (GEN + 3 records, 4 × 28 B), `SENT.L` is gone, and the kept copies are still 3; deliver again queues the 3 segments back, none of them for A; after B takes them, "clear" deletes all the files and the ledger holds only GEN. Note each boot's `CPA_LEDGER scan_us=… kept=… waiting=…`.
- [ ] `test_rec_sd`, `test_rec_budget`: the v0.3 recording regression; **the audio task's stack headroom** (`audio_stack_left`, 3,728 B in §10.1) must not drop below 1 KB after the ledger commands. `test_rec_budget` does not run the ledger commands yet: either add a case to it tonight (deliver again + clear 200 segments, read `uxTaskGetStackHighWaterMark`), or read 11.3's `debug.state.diag` once after the two-Mac steps.
- [ ] `test_runtime_budget`: `max_render_us` < 50 ms in both Chinese and English; the Storage page (three rows + three fact lines) and the hint bar are new screens, and `test_runtime_budget` only draws up to the Settings page, never turning to the Storage page or showing the hint bar, so tonight look at `debug.state.diag` (longest loop) after step 7 of 11.3, and note it down if it is > 50 ms.
- [ ] `test_storage`: the new NVS keys `offb` / `offs` (offline markers) do not affect `cpa/lang`; free heap is no more than 1 KB below §10.1 (`RecIndex` grows by 4 B per entry).

### 11.2 HIL regression (automated)

```bash
pio run -e cardputer-adv-test -t upload && cd ../..
uv run --project daemon python tools/hil/run_e2e.py --scenarios handshake,nav,menu,lock,lang,capture,rec_memo,rec_meeting,rec_memo_grace,rec_memo_handsfree --report "$SP/hil-main.json"
```

- [ ] 10/10. After `rec_memo` / `rec_meeting` finish pulling, HIL's cpad gets the ack and archives once; **those segments on the device are now kept copies** (no longer deleted), so segments pile up on the card scenario after scenario: in the `rec_*` checks, "0 segments on the card" must go by `files.n` (waiting to sync), not by the file count. If a scenario fails because of this, note it down first: it is a test assumption, not a firmware fault.

### 11.3 Two Macs (simulated, scratch `two_macs.py`, needs user only at steps 8 and 9)

Two HIL rigs on one Mac (`create_rig()` twice = two temporary `$CPA_HOME`s, each with its own `host_id`, which makes two Macs), with only one rig's cpad holding the serial port at a time; "unplug" = stop that rig's cpad and wait for the device to drop off (15 s). Keys go in with `debug.key` and state is read with `debug.state` (`rec.files_n`, `ui`); the words on the screen ("2 offline" (离线 2 条), "kept N" (留N), the Storage page) are for the person in step 9 to look at, or to photograph. After each step, check in **another** boot (`debug.reboot`, then handshake again): no segment is both waiting to sync and a kept copy, and each `rec` appears only once in the Mac's archive.

1. [ ] **A pulls, copies kept**: rig A handshakes, record 2 memos, A finishes pulling. `files.n=0`, the lock card shows "kept 2" (留2), and the Storage page shows "Send again · from XXXX: 2" (再交一次 ·XXXX 的 2 次) (XXXX = the first 4 hex digits of the CRC-32 of A's `host_id`).
2. [ ] **Record offline, plug into B, press nothing for 5 seconds**: stop A, record 2, start B. Within 5 seconds B's `file.list` does not list the 2 (`files.n` is 0); after 5 seconds they are listed, `files.rev` changes, and B pulls them. 0 presses.
3. [ ] **Record offline, plug into B, Tab Enter**: stop B, record 2, start B; once the hint shows, `debug.key tab`, `enter`. B never lists them (0 on two more handshakes); stop B, start A: A pulls the 2. 2 presses.
4. [ ] **Stop B right after Tab Enter** (review 3, the user's most common case: unplugging right after pressing): as 3, but stop B's cpad within 0.1 s of Enter; start B again: 0.
5. [ ] **Plugged in while locked** (review 1): Opt+L locks; stop, record 1, start B, wait 30 s: B's `files.n` is 0; Opt+L Enter unlocks, and the hint shows (the 5 seconds start again); press nothing: B pulls after 5 seconds. Once more, waiting 2 minutes while locked: B pulls (held back for at most 2 minutes).
6. [ ] **Plugged in with the Tab menu open** (review 2): Tab opens the menu; stop, record 1, start B; Tab and Enter belong to the menu (the hint does not turn into "Not this Mac?" (不给这台 Mac？)), and the hint's 5 seconds only start running once the menu is closed.
7. [ ] **Deliver again, give to this Mac, clear**: after A has pulled 2, still connected to A: Opt+M `,` Enter (a confirm box, cursor on "Cancel" (取消)) Enter: nothing changes; Enter `/` Enter: "Queued again" (已排回), A's `files.n` is 0, and the page shows "To this Mac 2 files" (给这台 Mac 2 段); `.` Enter: A's `files.n` goes back to 2, and A pulls them. Pull another batch, deliver again, stop A, start B, B pulls: both Macs' manifests have the same `rec`, and A never archives it a second time. Finally, on the Storage page, `.` `.` Enter `/` Enter: cleared, and the lock card has no "kept N" (留N). Read `debug.state.diag` once per step (longest loop, audio task stack headroom).
8. [ ] **Real power cut** (needs user: flip the switch, pull the cable): battery switch off, powered by USB only (then pulling the cable is a real power cut, not a soft reset).
   - 8a during a hand-over: A pulls a batch of 20 segments; pull the cable halfway through; plug back in, leave the switch alone, start A. Expect: segments acked before the cable was pulled are kept copies; the last segment is either a kept copy or waiting to sync and pulled again by A (dedup: still once in the archive, `test_a_segment_listed_again_is_deleted_unpulled_and_archived_once`).
   - 8b during a clear: 40 recordings kept (clear takes 8 recordings per command), clear on the Storage page, pull the cable within 0.5 s of confirming; plug back in: no segment has "a `.W` but no `.M`", the kept count only goes down, never up, clearing once more clears everything, and nothing cleared comes back as waiting to sync.
   - 8c during the boot-time tidy: before `debug.reboot`, first make the ledger "dirty" (a pull like 8a's often does it), then pull the cable again within 1 s of plugging back in (while the boot scan is tidying); plug back in: `SENT.L` and `SENT.K` are never both left, and the kept count is the same as after 8a.
   - At least 5 times each, with the timing spread out; after each one, do the "check in another boot".
9. [ ] **A person looks** (needs user): once in Chinese and once in English, look at the hint bar ("离线 2 条 → 这台 · Tab 不给" / "2 offline go to this Mac · Tab: no"), the Storage page's three rows (no overlap; in Chinese, gaps between the three fact lines), the two confirm boxes, and the lock card's "kept N" (留N); after plugging in and the handshake, does the greeting still say "No Mac yet" (还没连上 Mac)? (It does in the simulator; the review says this is most likely because the simulator sends no sessions; take a look on the real device.)

### 11.4 Flash the release build back

- [ ] `(cd firmware/app && pio run -e cardputer-adv -t upload)`, `debug.(key|state|reboot|rec|mic|codec|lang)` 0 times; `cpa pause 1`, `cpa status`: the kept copies on the card are HIL's test recordings, so clear them with "Clear kept" (清掉留底) on the Storage page, `files.n=0`.

**Merge only after tonight**: 11.1–11.3 all pass, or any failure is clearly explained as a test assumption (11.2). Step 8's real power cut is S1a's "hardware power-cut gate" in docs/28 §8.
