**English** · [中文](m3-device-gates.zh-CN.md)

# M3 device gate measurements: G-A0 heap baseline, G-A1 HWCDC throughput, G-A5 cue timing and I2C concurrency, G-A10 steps

> Measured on 2026-10-20 on the real device (Cardputer ADV, MAC 02:00:00:00:00:01, with the 512GB FAT32 card used for v0.2 inserted), M3-H phase 0 (docs/12).
> Device side: `firmware/hwtest/src/gate_*.cpp` (`!xfer` `!xfermk` `!gateclean` `!cue` `!mic`), pure logic in `firmware/hwtest/lib/gate_core` (`pio test -e native`); host side: `firmware/hwtest/host/hwtest_host.py <subcommand>` (`gate_*.py`, `pytest host/tests`).
> Raw JSON: `firmware/hwtest/captures/g-a*` (gitignored, see §7). Marks: ✅ measured on the device · 📚 from docs or source code · ⏳ not verified · 👤 needs the user.

## 0. Environment and operation log

| Item | Value |
|---|---|
| Toolchain | A: `espressif32@6.10.0`, Arduino core 2.0.17 / ESP-IDF 4.4.7 (ADR-0001); hwtest also does a `-Wall -Wextra -O2` compile check with GCC 14 (p54 env), 0 warnings in the new files |
| Port etiquette | `cpa pause 5400` first; the device was only touched once `lsof /dev/cu.usbmodem*` was empty; the pause was extended 5 times along the way. Every upload was `pio run -t upload`, G0 was never pressed, the serial port was only opened with the safe-open sequence (03 §2). The device never got stuck in download mode |
| G-A0 firmware | `firmware/app` env `cardputer-adv-test`, `HEAD f0fa2f9` (v0.2.0), no changes in the `firmware/app` working tree |
| Re-check after refactor | After the gates ran, `gate_xfer.cpp` was tidied up (split out `gate_usb.cpp`, lower complexity), and the final hwtest was flashed once more to re-check: `xfer b64 1380 4 4 sd` 308.5 KB/s, `cue memo lockyield` start 147 ms, read-back matched, consistent with the earlier data; in the same run `!xfermk 4096` hit the EIO of §6 |
| End state | Flashed back the v0.2 release (`firmware/app` env `cardputer-adv`, same HEAD), `cpa pause 1`, `cpa status`: `link: ready … fw=cpa-fw/0.2.0 store=sd launcher=no`. The whole-flash write removed Launcher (expected; M3-L is responsible for restoring it) |
| Card contents | The gates only write `/sd/cpa_hwtest/{xfer.bin,mic.wav}`; at the end `!gateclean` removes the files and the directory; a `!sdtree` re-check shows only v0.2's `/cpa/…` and Launcher's `/config.conf` left. The G-A0 test only uses `/cpabudget` and removes it after itself. `/cpa` was not touched |
| Privacy | The only recordings were 4 segments of 5 s each of quiet-room noise floor (G-A10 reference). Only the numbers were kept; the WAVs were deleted right after being pulled (`--discard`), and the copies on the card were deleted too; the logs contain no audio content |

## 1. Conclusions and constants decided (for M3-A / M3-B / M3-W)

| Constant / decision | Value | Basis |
|---|---|---|
| How ring buffers etc. are allocated | **Allocate once at boot (default plan)**. Baseline free heap 234,836 B, largest contiguous block 208,884 B; minus about 38 KB that leaves about 196 KB / ≥170 KB, well above ≥150 KB / ≥64 KB | G-A0 §2 ✅ |
| App HWCDC TX ring | **4 KB** (not raised to 8 KB). b64 1380 B, 4 in flight: 4 KB 632.7 KB/s, 8 KB 631.4 KB/s, same throughput; 8 KB only pushes ping RTT p95 under load from 3.9 ms to 7.6 ms | G-A1 §3 ✅ |
| v0.3.x binary sub-mode | **Not done**. Per audio-capture §7.3 item 2: if b64 effective throughput is ≥320 KB/s, do b64 only. The link itself gives b64 633 KB/s; the app's real bottleneck is how it reads the card (§3.3), and under that, binary is only 1.03× faster | G-A1 §3 ✅ |
| `file.get` per chunk | **1380 B (protocol limit `file_get_b64_max`), ≥2 in flight** (the app design's 4 is enough). 1 in flight gives 449 KB/s; 2 already reach the link limit | G-A1 §3 ✅ |
| b64 throughput (expectation for the daemon) | Link limit **≈633 KB/s effective (≈908 KB/s on the wire)**, about 3.0 min for a 1-hour recording. With the app's current card reads (open→lseek→read→close per chunk) on a single core it is **302–322 KB/s** (about 6 min per hour); with the file kept open during transfer it is **632 KB/s**. All well above the app-level target of ≥100 KB/s and the floor of "1 hour in ≤20 min" | G-A1 §3 ✅ |
| Encoding implementation (new requirement) | **base64 with a plain lookup table, CRC-32 with `esp_rom_crc32_le`** (matches zlib, 0 errors in 129,644 frames). Do not use `mbedtls_base64_encode` (a constant-time implementation, ≈1.6 ms extra per 1380 B) or bitwise CRC (≈0.35 ms): with them core 1 is 88–100% busy at the same throughput; after replacing them, 39% | G-A1 §3.2 ✅ |
| Card reads during transfer (new recommendation) | Keep the file open across consecutive `file.get` for the same `fid`; close it before starting a recording or writing `.M` (the audio task still has at most 1 open file, so the 11 §4.5 invariant holds). Reopening and closing per chunk costs ≈2.6 ms more and halves throughput | G-A1 §3.3 ✅ |
| `lead_ms` | **150 ms**. Codec thump after `Mic.begin`: RMS 125–215 in the first 20 ms (peak about 300), then 20–35 slowly falling back to the noise floor; over 112 runs p50 60 / p95 90 / max 130 ms, plus 1 run at 200 ms where a room sound joined the tail. 150 = p95 90 + margin, and eats a little less of the first word of "speak right after the beep" than the design default of 200. 2026-10-21: these numbers were all measured with a speaker on/off (the cue) before `Mic.begin`; v0.3.1's silent start was measured afterwards by `mic_cal` at `kMicGain`: 5 runs at 90 / 80 / 70 / 80 / 90 ms, 150 kept (§5.4). The thump appears after the PGA and grows with digital gain (REG17, `magnification`): at v0.3.0's ×8 the silent start was 260 ms. From v0.3.2 (#31) it is counted from the first sample written: with the codec warm, the first chunk is the thump, same as these measurements; with the codec cold, there is no thump | G-A5 §4.3 ✅, silent start §5.4 ✅ |
| Cue volume | ~~👤 **Waiting for the user to listen** (`cue-listen`, §4.5)~~ **Not applicable**: from 2026-10-21 recording is silent, for personal use; from v0.3.1 there is no sound during recording at all, so no volume to decide. The agent used 64 for the timing tests | G-A5 |
| I2C lock | **Lock, but with yielding** ("lockyield": while waiting for the lock the audio task sets a want flag; after releasing the lock, loop sees the flag and yields 1 tick). With a bare FreeRTOS mutex and loop reading the keyboard continuously, the audio task waited up to 3.7 s (no handoff guarantee across cores), and a memo start dragged to 4.3 s. With yielding, 40 runs had 0 wrong keys, 0 codec config failures, and a max lock wait of 0.6 ms. Without an app lock, 25 runs were also 0 errors (M5GFX has its own port mutex around every I2C transaction, 📚 `common.inl:2278`) | G-A5 §4.2 ✅ |
| Start latency | Under lockyield: **memo 647 ms** (G0 down → first buffer, including the 500 ms long-press threshold; ≤1.0 s ✅), **meeting 424 ms** (Enter → first chunk; ≤0.8 s ✅); longest single loop step 10.5 ms (≤50 ms ✅). From v0.3.1 the start path has no `Speaker.begin`, cue or `Speaker.end` (the first three rows of the §4.1 table): a memo is only G0 long press + `Mic.begin` + first chunk, a meeting only `Mic.begin` + first chunk; not re-measured. From v0.3.2 (#31) `● REC` waits for the first chunk of sound that is not muted: with the codec warm it shows about one chunk (128 ms) later, with it cold it waits up to 2 s | G-A5 §4.1 ✅ |
| Microphone gain (from v0.3.1 this is `kMicGain{pga, vol, mag}` in `lib/core/mic_gain.h`) | **`{9, 0xC4, 2}`**: PGA +27 dB, ADC volume +2.5 dB, no magnification, +29.5 dB in total inside the codec. Expected peak for normal handheld speech −11.9 dBFS (`mic_verify` at the firmware's own gain extrapolates −11.96), voiced RMS −35.2, noise floor −73.7 dBFS (2026-10-21 `mic_cal`, §5.4). v0.3.1 moves the gain into the ES8311 (REG14 PGA, REG17 ADC volume, written and read back after every `Mic.begin`), and `magnification` becomes 2 (no magnification when `over_sampling` is 1); v0.3.0's gain was `{0, 0xBF, 16}`. Reference: PGA is analog gain and a little better than software magnification (+18 dB PGA raises the noise floor only 15.4 dB), but at +18 dB a quiet room's noise floor is already 0.0030, hitting WhisperWoof's 0.003 small-chunk threshold (that was PGA +18 dB then ×8; at `kMicGain` this room's unfiltered noise floor is about 0.0004) | G-A10 §5.4 ✅ |
| `CARDPUTER_SILENCE_RMS` | **0.000843** (`decision.silence_rms` from `mic_cal`, §5.2 step 6: the dB midpoint between the −73.7 dBFS noise floor and the estimated −49.2 dBFS of "speech 1 m away"). The default 0.0005 must be raised: at `kMicGain` this room's unfiltered noise floor is about 0.0004 (−68 dBFS, lower than the 0.0006 the brief expected), and 0.0005 is only slightly above it. At v0.3.0's default gain, quiet-room 100 ms window RMS p50 = 7.4 (0.00023) | G-A10 §5.4 ✅ |
| ⚠️ SD EIO on sustained writes (new finding) | **`write` returns EIO when writing one file sequentially**: requests of ≥8 MB failed 11/11 times (at positions anywhere from 0.24 to 8.4 MB; slow writes and frequent fsync made no difference); 2 MB 2/2 and 4 MB 3/4 were fine (the 4th failed at 245,760 B), `!sdbench` 3×4 MiB was fine. It looks like some area of the card, or the driver, cannot write certain LBAs; it is not about file size but about where clusters are allocated. **A 5-minute 9.6 MB segment will almost certainly hit it; v0.2 outbox small files might too (⏳ not tested)**. Do not run v0.3 recording tests on this card until it is understood, see §6 | ✅ reproduction command in §6 |

## 2. G-A0: v0.2 heap baseline (SD mounted) ✅

`cd firmware/app && pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget'`, run twice (about 1 hour apart, with hwtest flashed in between). Both PASS, with identical numbers:

```
CPA_RX_DONE got=8192 bad=0
CPA_BUDGET stack_left=5836 heap_free=234836 heap_min=223660 max_block=208884 sent=22 pending=0 bad=1 store_errors=0 missing_glyphs=0
```

- These are the values after the whole v0.2 firmware (screen, three modules, SD as main storage, one item moved to bad/) has run its deepest path.
- Allocating about 38 KB once at boot (11 §4.7): free heap about 196 KB (≥150 ✅); even if all 38 KB came out of the largest block, it still has about 170 KB (≥64 ✅). Adding a 4 KB 3rd file slot also fits. **M3-A uses boot-time allocation**; the compile-time switch for "allocate when recording" can stay as a fallback.
- Loop stack headroom 5,836 B (≥4 KB ✅).

## 3. G-A1: sustained HWCDC throughput ✅

### 3.1 Method

- `!xfer <b64|bin> <len> <depth> <txring KB> <ram|sd|sdkeep> [secs]`: the device sends lines identical to the app's `file.data` (11 §5.3, with `id`/`re`/`fid`/`off`/`n`/`size`/`crc` all present; at 1380 B the longest line is 1,997 B). bin is "header line + N raw bytes + `\n`" (audio-capture §7.3).
- **Sends by the app's rules**: a frame is written only if the whole frame fits in the TX ring (11 §5.4), and every 20 ms the same kick + re-arm as `UsbTransport::tick` runs; the TX ring is switched to 4/8/16 KB at run time (first wait for the ring to drain and mask IN_EMPTY, then switch).
- `depth`: the host keeps N requests in flight (one `!g` back per frame received), simulating the request-reply pipeline; `0` = the device just pushes.
- Data source: `ram` = a fixed pattern in memory; `sd` = `/sd/cpa_hwtest/xfer.bin`, **open→lseek→read→close per frame** (the audio task's "at most 1 open file, close before open" read style); `sdkeep` = the same file opened once and read sequentially.
- The host sends `!ping` once a second to measure reply latency under load; every frame's CRC-32 is checked; the device reports one `xs` line per second: frames, time waiting on the TX ring, framing time, longest card read, and busy percentage of both cores (idle hook counts against a 500 ms no-load baseline).
- Difference from the app: hwtest reads the card, encodes and sends in the same loop; the app reads the card in the audio task on core 0 and encodes and sends on core 1, so they can pipeline in parallel. So this table **does not verify app-level throughput**; that is measured by HIL `rec_meeting` (11 §5.8).

### 3.2 The encoder pitfall (fixed first)

The first run (RAM, b64 1380, 4 in flight, 4 KB ring) framed with hwtest's existing bitwise CRC (`sdt::crc32`) and `mbedtls_base64_encode`:

| Framing implementation | Effective KB/s | Framing time per frame | core 1 busy | Waiting on TX ring |
|---|---|---|---|---|
| Bitwise CRC + mbedtls base64 | 634 | 2,050 µs | 100% | 0.8% |
| ROM CRC + mbedtls base64 | 633 | 1,700 µs | 88% | 17% |
| ROM CRC + table base64 (gate_core, tested with RFC 4648 vectors) | 615–633 | 255 µs | 39% | 84% |

Conclusion: mbedtls base64 is a constant-time implementation and takes about 1.6 ms for 1380 B; bitwise CRC takes about 0.35 ms. The app must use table base64 + `esp_rom_crc32_le`, or core 1 is saturated during transfer and both the UI and the link suffer. This also shows that the "pull 138 KB/s" of sd-card §7, and the "wire speed 185–225 KB/s" derived from it (audio-capture §7.2), **are not the link limit**: `!sdget` used exactly these two slow implementations and also waited for a reply per chunk.

### 3.3 Results (10 s per setting, plus 2 settings soaked for 60 s; main table 32 settings 108,006 frames + card-read comparison 4 settings 21,638 frames, **0 CRC errors, 0 framing errors, ping 500/500 answered**)

| Encoding | Bytes per frame | In flight | TX ring | Source | Seconds | Effective KB/s | Wire KB/s | 1-hour recording | ping RTT p50 / p95 / max (ms) | core1 busy % | Longest frame gap ms | Time per frame µs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| b64 | 1380 | 1 | 4 KB | ram | 10 | **449.2** | 644.8 | 4.17 min | 1.1 / 2.1 / 2.1 | 28 | 3.9 | 185 |
| b64 | 1380 | 2 | 4 KB | ram | 10 | **632.6** | 908.0 | 2.96 min | 2.7 / 3.6 / 3.6 | 39 | 2.3 | 256 |
| b64 | 1380 | 4 | 4 KB | ram | 10 | **632.7** | 908.2 | 2.96 min | 3.2 / 3.9 / 3.9 | 39 | 2.3 | 254 |
| b64 | 1380 | push | 4 KB | ram | 10 | **648.8** | 931.3 | 2.89 min | 2.7 / 4.0 / 4.0 | 40 | 2.3 | 255 |
| b64 | 1380 | 1 | 8 KB | ram | 10 | **449.2** | 644.8 | 4.17 min | 1.0 / 2.1 / 2.1 | 28 | 3.2 | 185 |
| b64 | 1380 | 2 | 8 KB | ram | 10 | **631.4** | 906.3 | 2.97 min | 2.5 / 3.5 / 3.5 | 39 | 2.3 | 256 |
| b64 | 1380 | 4 | 8 KB | ram | 10 | **631.4** | 906.3 | 2.97 min | 7.0 / 7.6 / 7.6 | 39 | 2.4 | 247 |
| b64 | 1380 | push | 8 KB | ram | 10 | **651.6** | 935.3 | 2.88 min | 6.8 / 9.0 / 9.0 | 40 | 2.3 | 248 |
| bin | 1380 | 4 | 4 KB | ram | 10 | **793.2** | 875.8 | 2.36 min | 3.4 / 4.1 / 4.1 | 37 | 1.9 | 140 |
| bin | 4096 | 1 | 8 KB | ram | 10 | **702.4** | 727.4 | 2.67 min | 1.4 / 4.7 / 4.7 | 28 | 6.1 | 241 |
| bin | 4096 | 4 | 8 KB | ram | 10 | **846.2** | 876.1 | 2.22 min | 5.7 / 8.1 / 8.1 | 34 | 4.9 | 299 |
| bin | 4096 | push | 8 KB | ram | 10 | **848.6** | 878.6 | 2.21 min | 6.9 / 8.3 / 8.3 | 34 | 4.9 | 300 |
| bin | 4096 | 1 | 16 KB | ram | 10 | **668.4** | 692.1 | 2.81 min | 2.4 / 4.7 / 4.7 | 27 | 6.2 | 241 |
| bin | 4096 | 4 | 16 KB | ram | 10 | **846.4** | 876.4 | 2.22 min | 15.9 / 18.3 / 18.3 | 33 | 4.9 | 330 |
| bin | 4096 | push | 16 KB | ram | 10 | **835.9** | 865.5 | 2.24 min | 15.3 / 17.2 / 17.2 | 33 | 38.7 | 333 |
| b64 | 1380 | 1 | 4 KB | sd | 10 | **203.5** | 292.1 | 9.22 min | 1.1 / 2.1 / 2.1 | 69 | 11.2 | 3790 |
| b64 | 1380 | 2 | 4 KB | sd | 10 | **321.6** | 461.6 | 5.83 min | 5.8 / 6.8 / 6.8 | 100 | 8.4 | 4100 |
| b64 | 1380 | 4 | 4 KB | sd | 10 | **322.2** | 462.6 | 5.82 min | 4.8 / 5.9 / 5.9 | 100 | 7.6 | 4088 |
| b64 | 1380 | push | 4 KB | sd | 10 | **326.6** | 468.8 | 5.74 min | 6.2 / 7.0 / 7.0 | 100 | 6.2 | 4048 |
| b64 | 1380 | 1 | 8 KB | sd | 10 | **201.9** | 289.9 | 9.29 min | 2.0 / 2.6 / 2.6 | 68 | 20.8 | 3789 |
| b64 | 1380 | 2 | 8 KB | sd | 10 | **318.0** | 456.5 | 5.9 min | 5.5 / 6.3 / 6.3 | 98 | 101.8 | 4080 |
| b64 | 1380 | 4 | 8 KB | sd | 10 | **322.4** | 462.8 | 5.82 min | 5.8 / 6.8 / 6.8 | 100 | 6.4 | 4087 |
| b64 | 1380 | push | 8 KB | sd | 10 | **326.8** | 469.1 | 5.74 min | 5.6 / 6.0 / 6.0 | 100 | 8.9 | 4046 |
| bin | 1380 | 4 | 4 KB | sd | 10 | **332.7** | 367.4 | 5.64 min | 5.3 / 6.4 / 6.4 | 100 | 6.5 | 3961 |
| bin | 4096 | 1 | 8 KB | sd | 10 | **400.1** | 414.5 | 4.69 min | 4.6 / 4.7 / 4.7 | 60 | 10.6 | 4626 |
| bin | 4096 | 4 | 8 KB | sd | 10 | **759.7** | 786.7 | 2.47 min | 7.8 / 10.0 / 10.0 | 100 | 5.6 | 5169 |
| bin | 4096 | push | 8 KB | sd | 10 | **767.5** | 794.8 | 2.44 min | 7.4 / 10.0 / 10.0 | 100 | 5.5 | 5130 |
| bin | 4096 | 1 | 16 KB | sd | 10 | **400.0** | 414.4 | 4.69 min | 4.5 / 4.7 / 4.7 | 60 | 10.4 | 4623 |
| bin | 4096 | 4 | 16 KB | sd | 10 | **757.0** | 784.0 | 2.48 min | 8.2 / 9.9 / 9.9 | 100 | 5.6 | 5188 |
| bin | 4096 | push | 16 KB | sd | 10 | **766.4** | 793.7 | 2.45 min | 9.0 / 10.0 / 10.0 | 100 | 5.6 | 5136 |
| b64 | 1380 | 4 | 4 KB | sd | 60 | **321.1** | 461.3 | 5.84 min | 6.0 / 6.8 / 7.0 | 100 | 16.3 | 4104 |
| bin | 4096 | 4 | 8 KB | sd | 60 | **758.1** | 785.1 | 2.47 min | 7.9 / 10.0 / 10.1 | 100 | 5.6 | 5179 |

The sd source in the table above is a 2 MiB file. Card read style comparison (4 MiB file, 20 s per setting; the last column is the longest single card read in µs):

| Encoding | Bytes per frame | In flight | TX ring | Read style | Effective KB/s | Wire KB/s | 1-hour recording | ping RTT p50 / p95 / max | core1 busy % | Time per frame µs | Longest card read µs |
|---|---|---|---|---|---|---|---|---|---|---|---|
| b64 | 1380 | 4 | 4 KB | open/close per chunk | **302.0** | 433.6 | 6.21 min | 6.3 / 7.0 / 7.0 | 100 | 4361 | 5263 |
| bin | 4096 | 4 | 8 KB | open/close per chunk | **709.2** | 734.4 | 2.64 min | 8.5 / 10.3 / 10.5 | 100 | 5517 | 5535 |
| b64 | 1380 | 4 | 4 KB | kept open | **631.8** | 907.0 | 2.97 min | 3.6 / 5.4 / 5.6 | 90 | 1769 | 2369 |
| bin | 4096 | 4 | 8 KB | kept open | **846.1** | 876.2 | 2.22 min | 5.8 / 8.7 / 8.8 | 78 | 3197 | 3406 |

**Reading the results**:
1. **HWCDC wire speed is about 880–935 KB/s**, 4× higher than the 185–225 KB/s previously derived from sd-card §7. b64 1380 B tops out at about 633 KB/s effective (69% efficiency), bin 4096 at about 846 KB/s.
2. **In flight**: 1 in flight gives 449 KB/s (about 3 ms round trip per chunk); 2 already reach the limit. The app's 4 in flight is enough.
3. **TX ring**: no effect on throughput; the bigger the ring, the longer replies queued behind frames wait under load (4 KB: p95 ≤4 ms; 8 KB: ≤9 ms; 16 KB: ≤18 ms). Choose 4 KB.
4. **Card reads are the app's bottleneck**: open→lseek→read→close per chunk takes about 3.8–4.4 ms at 1380 B (slower for bigger files, since lseek walks the cluster chain), so a single core only gets 302–322 KB/s; with the file kept open, a sequential 1380 B read takes only about 1.5 ms, back to the link limit. The app reads the card on core 0 and encodes and sends on core 1, which can run in parallel; but as long as the audio task reopens the file per chunk, the ceiling is around 1380 B / 4 ms ≈ 330 KB/s.
5. **Binary**: 1.34× faster on the link (RAM), but with the app's card reads bin 1380 is only 1.03× faster than b64 1380. Per the §7.3 rule, b64 ≥320 KB/s → **no binary in v0.3.x**.
6. **CPU**: with the link saturated core 1 is about 39% (framing about 12%; the rest is mostly HWCDC interrupts: at most 64 B moved per interrupt, about 14,000 per second at 900 KB/s [calculated]); core 0 is 0.
7. **Withholding**: the host kept writing during load (`!g`, `!ping`), and the roughly 1 s withholding of ADR-0001 §5.2 did not appear; the longest gap between frames was 101.8 ms (once). Under load the 20 ms re-arm found data in the ring every time (about 500 times in 10 s); this only shows it runs, not whether it is needed.

## 4. G-A5: cue timing, `lead_ms`, I2C concurrency ✅ (volume 👤)

> From 2026-10-21: recording is silent, for personal use; from v0.3.1 there is no sound during recording at all; the meeting confirm box stays; the screen is the only indicator. The measurements in this section are kept as a record: the I2C lock conclusion (§4.2) is unchanged; `lead_ms` (§4.3) was only measured on the start path with a cue, and v0.3.1's silent start was not re-measured (3 silent starts with `!sdrec` gave 130–150 ms, so the margin on 150 is gone); it needs re-measuring with `cue-silent`; volume (§4.5) does not apply.

### 4.1 Method and timing

`!cue <ms> <vol> [memo|meet|silent] [off|nolock|lock|lockyield] [rec_ms]` (`silent` was added on 2026-10-21, see the end of §4.3): starting from idle (microphone and speaker both off), in a task identical to rec_task (core 0, priority 1, 6 KB stack), walk the start path of 11 §4.2: `Speaker.begin` → `setVolume` → cue (memo 1320 Hz 80 ms; meeting 660/880/1320 Hz 100 ms each, 30 ms apart) → wait for `isPlaying()` to go false → `Speaker.end` → `Mic.begin` → first 10 ms buffer → record on to 1 s. After every begin, read back the ES8311's 8 registers and compare with M5Unified's table. Meanwhile loop (core 1) records its own longest step; in hammer mode loop reads the TCA8418 nonstop (CFG must keep its configured value, the event count must be 0, the key table must be empty: nobody is pressing keys).

20 runs per mode (`off` and meeting `nolock` 5 each), volume 64; plus 1 memo lock run each at volume 128 / 255. Medians per mode (within ≤1 ms of each other, except `Speaker.end` for bare lock):

| Step | Memo | Meeting |
|---|---|---|
| `Speaker.begin` | 3.4 ms | 3.4 ms |
| Cue (until `isPlaying` goes false) | 80.8 ms | 358.8 ms |
| `Speaker.end` | 5.2–6.2 ms | 5.1–5.2 ms |
| `Mic.begin` | 3.8 ms | 3.8 ms |
| `Mic.begin` → first 10 ms buffer | 48 ms | 48 ms |
| **Start → first chunk** (lockyield / nolock / off) | **147 ms** | **424 ms** |
| Key → first chunk | 500 + 147 = **647 ms** (≤1.0 s ✅) | **424 ms** (≤0.8 s ✅) |

ES8311 read-back: all 112 runs matched the table (speaker `80B518010010BF08`, microphone `80BA18010210BF6A`), and every begin returned true. Note that M5Unified's ADV callbacks do not check whether I2C writes succeed and always return true (📚 `M5Unified.inl:1045,1537`), so "config failure" can only be detected by read-back, and the app has to do the same if it wants to check.

### 4.2 I2C concurrency

| Mode (20 runs each) | Wrong keys | Read errors / I2C errors | Codec config failures | Longest loop step | Longest lock wait in audio task | Longest start → first chunk |
|---|---|---|---|---|---|---|
| Memo nolock | 0 | 0 / 0 | 0 | 5.2 ms | — | 148 ms |
| Memo lock (bare mutex) | 0 | 0 / 0 | 0 | 14.1 ms | **3,662 ms** | **3,804 ms** ❌ |
| Memo lockyield | 0 | 0 / 0 | 0 | 10.0 ms | 0.6 ms | 147 ms ✅ |
| Meeting lock (bare mutex) | 356 (1 run, see below) | 0 / 0 | 0 | 9.6 ms | **1,180 ms** | **1,599 ms** ❌ |
| Meeting lockyield | 0 | 0 / 0 | 0 | 10.5 ms | 0.6 ms | 424 ms ✅ |

- **A bare mutex starves the audio task**: loop on core 1 retakes the lock right after releasing it, so when the task waiting on core 0 wakes up the lock is already taken again (FreeRTOS mutexes do not guarantee handoff across cores). The longest wait was 3.7 s. The real app's loop does not read I2C this densely (the keyboard is only read on interrupt, 📚 `TCA8418.cpp`), but 11 §4.2 requires calling `M5Cardputer.update()` inside the lock on every loop, so as long as loop spins fast, the same thing can happen.
- **lockyield (recommended for M3-A)**: before taking the lock the audio task sets an atomic `want` flag and clears it once it has the lock; after releasing the lock, if `want` is true, loop calls `vTaskDelay(1)`. It only triggers on the few begin/end calls at the start and end of a recording; otherwise loop never yields. An equivalent variant is "when `want` is true, loop skips the keyboard read for this round", so loop never delays at all, but that variant **was not tested on the device** ⏳; using it requires an added HIL test.
- **No app lock** also gave 0 errors (25 runs with continuous keyboard reads): every M5GFX I2C transaction is inside a port mutex (📚 `M5GFX/src/lgfx/v1/platforms/esp32/common.inl:2278`), so register-level accesses do not interleave. The point of the app lock is to make the whole begin/end sequence mutually exclusive with keyboard reads; with yielding it costs nothing.
- **The one meeting-lock run with 356 "wrong keys"**: it appeared in only one run and lasted about 356 loop rounds, while CFG read-back was all correct and there were 0 I2C errors; the recordings of the runs before it had a very loud room sound (RMS up to 839). Most likely someone touched the keyboard; it does not look like a bus error, but it **cannot be ruled out completely** ⏳. The other 111 runs (including 40 lockyield and 25 nolock) were all 0.

### 4.3 `lead_ms`

RMS (int16) per 10 ms slot after `Mic.begin`, a typical run: `126 144 18 23 26 26 25 21 17 14 16 13 8 11 9 7 …` (peaks 232, 296, then ≤64). That is, the first 20 ms are the codec thump, followed by a tail of about 20–35, back to the noise floor (7–10 when quiet) by 60–130 ms.

The `lead_ms` algorithm (`gate_cue.lead_ms`): take the median of the slots from 500–1000 ms as the noise floor, threshold = max(3 × floor, floor + 8); starting from slot 0, the first place where 5 consecutive slots (50 ms) are all at or below the threshold is the end of the thump; any later room sound does not count. 112 runs: **p50 60 / p95 90 / max 130 ms**; plus 1 run at 200 ms where the thump tail joined a loud room sound (RMS 839).

**Recommend `lead_ms = 150`** (covers every pure-codec case). Not the design default of 200: sample 0 of the recording comes about 60 ms after the cue ends (`Speaker.end` + `Mic.begin` + first chunk), and people start speaking about 200–300 ms after hearing the beep; a `lead_ms` that is too large makes WhisperWoof skip the first word.

> **2026-10-21: v0.3.1's silent start was not re-measured** ⏳. Each of the 112 runs above was `Speaker.begin` (ES8311 0x0D=0x01, analog stage powered up) → cue → `Speaker.end` → `Mic.begin`, so the analog stage was already on before `Mic.begin` (one 80 ms tone for a memo, three tones of about 360 ms for a meeting); split by mode it shows: memo p50 70 / p95 90 ms, the longer-powered meeting p50 50 / p95 80 ms. v0.3.1 goes straight to `Mic.begin` from the idle state after `Mic.end` (0x0D=0xFC, analog stage off), so the analog stage powers up together with recording. The existing side evidence is 3 WAVs from hwtest `!sdrec` (`Mic.begin` right after `Mic.end`, speaker untouched) (`captures/sd-rec*-rec-*.wav`); the same `gate_cue.lead_ms` gives **130 / 150 / 140 ms**: the 150 from p95 90 + margin has no margin left with a silent start. In v0.3.1 the analog stage stays off longer between two recordings than that, which can only make it worse, not better.
>
> Re-measure: `$H cue-silent` (`!cue … silent lockyield`, speaker untouched, 20 runs, quiet room, → `captures/g-a5-silent-*.json`, prints `lead_ms` p50 / p95 / max). Keep 150 only if p95 plus margin is still within 150; otherwise change to the design default of 200 (`kLeadMs` in `lib/rec/rec_meta.h`). `!cue` only powers off for 300 ms before each run, so you can also record a few real memos on the device (no speech for the first 1 s) and check them with the same algorithm.
>
> **2026-10-21 follow-up measurement ✅** (the lead phase of `mic_cal`, §5.4): at `kMicGain {9, 0xC4, 2}`, 5 silent 2.5 s meetings with the same `gate_cue.lead_ms` algorithm gave **90 / 80 / 70 / 80 / 90 ms**, `recommend_lead` 100 ≤ 150, so `kLeadMs` stays 150; the `mic_verify` segment (the first after a reboot) was 100 ms. `cue-silent` was not run.
> The thump is much larger than recorded above: at `kMicGain` the peak in the first 150 ms is about 4,500 (−17 dBFS), and after 150 ms the maximum does not exceed 81. It does not change with PGA (same 0 dB digital gain: U0 3,604, S10 3,500, N27 3,283), but does with REG17 (S6 with +12 dB more digital gain: 13,468); at v0.3.0's ×8 the peaks were 28,200 / 28,680 (about 3,500 before ×8), lead 260 ms. So the thump appears after the PGA. The "peak about 300" above was for the path that plays a cue first. If REG17 or `magnification` is raised later, re-measure `lead_ms`.
>
> **2026-10-21 (v0.3.2, #31)**: all the measurements above are starts with the codec already "warm". After the codec loses power and powers up again (or after a soft reset with REG00 = 0x1F), it first mutes its output to 0 for about 2^14 clock frames (933–995 ms at 16 kHz) before any sound comes out, and **there is no thump** (peak 642, steady state 628). v0.3.2 warms the codec up at boot, and when recording starts it drops the muted chunks and writes from the first sample with sound, so `lead_ms` is counted from the first sample written: with the codec warm, the first chunk is the thump and 150 is still enough; with it cold there is no thump and 150 just skips a little extra. `kLeadMs` is unchanged.

### 4.4 Verdict

| Condition | lockyield | Bare lock |
|---|---|---|
| Single loop step ≤50 ms | 10.5 ms ✅ | 14.1 ms ✅ |
| 0 wrong keys and 0 codec config failures in 20 runs | ✅ | 1 meeting run with wrong keys (see above) |
| Lock waits do not starve (≤50 ms) | 0.6 ms ✅ | 3,662 ms ❌ |
| Memo start ≤1.0 s | 647 ms ✅ | 4,304 ms ❌ |
| Meeting start ≤0.8 s | 424 ms ✅ | 1,599 ms ❌ |
| **Result** | **Pass** | Fail |

### 4.5 👤 What is left: pick the volume by ear (about 2 minutes)

> **Not applicable** (2026-10-21): from v0.3.1 recording makes no sound at all, there is no cue to listen to, so this is not needed. The steps below are kept as a record.

1. `cpa pause 900`; `cd firmware/hwtest && ~/.local/bin/pio run -e cardputer-adv -t upload`.
2. `uv run --no-project --with pyserial==3.5 python host/hwtest_host.py cue-listen --vols 32,64,128,192,255`: for each volume it plays the memo start cue and the meeting start cue in turn; the terminal first prints "now playing …", with 2 s between them.
3. Listen once with the device in a pocket and once on a desk, and choose the lowest setting that is **audible in a pocket and not harsh on a desk** (you can refine with `--vols 48,64,96`). Also confirm the meeting's three tones are not impolite in a quiet meeting room.
4. Write the chosen volume into this section as M3-A's compile-time constant; then flash v0.2 back as in §7 and `cpa pause 1`.

## 5. G-A10: levels ✅ and mixed Chinese-English accuracy 👤 (steps)

### 5.1 Reference measured by the agent: quiet-room noise floor (5 s each, numbers only)

| magnification / PGA | 100 ms window RMS p10 / p50 / p90 | Whole-segment RMS (float, including the opening thump) | Peak |
|---|---|---|---|
| 16 / 0 dB (M5Unified default) | 6.7 / 7.4 / 8.5 | 0.00040 (−68.0 dBFS) | 488 (opening thump) |
| 32 / 0 dB | 13.0 / 14.7 / 17.8 | 0.00081 | 1,040 |
| 16 / +18 dB (PGA 6) | 34.2 / 43.8 / 59.5 | 0.00304 | 3,504 |
| 16 / +30 dB (PGA 10) | 135.6 / 163.6 / 262.5 | 0.00919 | 11,072 |

- Software magnification ×2 doubles the noise floor exactly; PGA +18 dB raises it ×5.9 (+15.4 dB); at +30 dB ×22 (+26.9 dB). So PGA has about 3 dB better SNR than magnification, and **when adding gain, add PGA first**.
- At PGA +18 dB, a room with nobody speaking already has a whole-segment RMS of 0.0030, equal to WhisperWoof's small-chunk threshold: silent segments would be treated as voiced and sent for transcription. This is exactly why `CARDPUTER_SILENCE_RMS` has to be set separately.

### 5.2 For the user: HIL `mic_cal` calibration and `mic_verify` check (from v0.3.1, about 25 minutes)

> From 2026-10-21 this replaces the old hwtest steps. The old method only tuned magnification and PGA, did not touch REG17, and wrote the result into `kMicMagnification` in `audio_io.h`; that constant was removed in v0.3.1. hwtest's `mic-capture` is kept only as a fallback. The basis is the mic gain brief §5–§7.

**What it does**: the Mac's built-in speakers play reference sounds (two 1 kHz tones at −20 / −32 dBFS, one English sentence and one Chinese sentence read by `say`), and the Cardputer next to them records. The script first uses `debug.mic` to step through 11 gain settings (B1…B2 of mic gain brief §5), records a meeting of about 27 s at each, which the HIL's own temporary cpad pulls into a temporary directory for measurement, then picks the gain to ship following brief §7. Finally, at the chosen gain, it records 5 silent 2.5 s meetings to measure the opening thump (`lead_ms`), then reboots the device to remove `debug.mic`. Recordings live only in the temporary directory, the report has only numbers, and everything is deleted along with the temporary directory at the end; **nothing goes into `~/CardputerNotes` or WhisperWoof**.

**Preparation**
- A quiet room. The Cardputer has the card inserted and lies flat next to the MacBook's built-in speakers; do not move it during the run.
- The Mac's default output is the built-in speakers, not muted. No need to change the volume: the script only reads it and never changes it; if it is wrong the script refuses to run. About 7 minutes of sound will play out loud.
- Your own cpad is running and **not paused** (reason in step 3).

**Steps** (run every command from the repo root)
1. `cpa status --json`: `recordings.on_device` must be 0 and `pulling` must be 0. If not, wait for cpad to finish pulling. If anything is still on the card, the HIL's temporary cpad pulls it into the temporary directory and then deletes it from the card.
2. Flash the test firmware: `cd firmware/app && ~/.local/bin/pio run -e cardputer-adv-test -t upload && cd ../..`. `pre_upload` runs `cpa pause 120` itself.
3. `cpa pause 1`, wait until `cpa status` shows `link: ready` and `on_device` is still 0. **cpad must be resumed before the run**: preflight relies on it to confirm the card is empty; a paused cpad cannot see the card, and preflight will refuse to run. So the usual habit of "`cpa pause` before touching the device" is reversed here.
4. Calibrate: `uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_cal --report "$TMPDIR/mic-cal.json"`, about 10–15 minutes (timeout 1200 s).
   - preflight first checks the output device and `cpa status`, then runs `cpa pause 1800` itself and waits for the serial port to be free.
   - If a condition is not met, the exit code is 2 and the reason is printed.
5. Look at the results:
   - `$TMPDIR/mic-cal.mic_cal.md`: a table per setting and the conclusion;
   - `$TMPDIR/mic-cal.mic_cal.data.json`: all the numbers;
   - `$TMPDIR/mic-cal.mic_cal.json`: the individual checks.
   - Pass condition: every check is ok, including `sweep_valid`, `decision`, `headroom_n33_n36_clip_free`, `lead_measured`, and the card-emptying checks such as `card_empty`. If `decision.ok` is false, do not change the constants; write down `decision.problems`.
6. Set the constants (one commit):
   - Write `pga`, `vol_hex` and `mag` from `decision` into `kMicGain` in `firmware/app/lib/core/mic_gain.h`.
   - In the same commit change `test_the_placeholder_keeps_v030` in `test/core/test_mic_gain`: by design it pins the placeholder value `{0, 0xBF, 16}` and fails as soon as the constant changes. Then run `~/.local/bin/pio test -e native -f core/test_mic_gain`.
   - If `lead.keep_150` is false, change `kLeadMs` in `lib/rec/rec_meta.h` to `lead.recommended`.
   - Fill these numbers into the TBDs in `docs/releases/v0.3.1.md`.
   - Use `decision.silence_rms` for WhisperWoof's `CARDPUTER_SILENCE_RMS`.
7. Check (brief §6.7):
   - First flash the test firmware again as in steps 2 and 3, `cpa pause 1`, then wait for `link: ready` and `on_device` 0.
   - Then run: `uv run --project daemon python tools/hil/run_e2e.py --scenarios mic_verify --report "$TMPDIR/mic-verify.json"`, about 3 minutes.
   - It first reads `kMicGain` from `mic_gain.h`. Then it records B1 with `debug.mic` to set the reference level; reboots to remove `debug.mic`; and finally records one segment at the firmware's own `kMicGain`.
   - Pass condition: `kmicgain_verified` and the card-emptying checks are all ok. `kmicgain_verified` requires:
     - gain relative to B1 within ±1.5 dB of nominal;
     - gain measured with the tone and with the voice differ by no more than 1 dB;
     - no clipping;
     - sample step 1 at `mag` 2 (all 16 bits used);
     - extrapolated handheld peak between −14 and −10 dBFS.
   - The numbers are under `verify` in `$TMPDIR/mic-verify.mic_verify.json`.
8. Flash the release back: `cd firmware/app && ~/.local/bin/pio run -e cardputer-adv -t upload && cd ../..`, then `cpa pause 1`, and wait until `cpa status` shows `link: ready` and `fw=cpa-fw/0.3.1`. Whether it succeeded or failed, when `mic_cal` / `mic_verify` finish the device is still on the test firmware and your cpad may stay paused for up to 30 minutes; the runner's last line reminds you of this.
9. One more thing is recommended: with the release firmware, record a handheld memo at your usual speaking level and confirm the peak is around −12 dBFS (brief §8: speaking level was only measured once).

**If the run fails**
- This covers any failure: `afplay` errors, a step times out, the whole scenario times out, the device reports an error. Before reporting the error, the scenario first reboots the device (removing `debug.mic`), then lets the HIL's temporary cpad pull what is on the card into the temporary directory and delete it, waiting up to 120 s (`DRAIN_S`).
  - The scenario's `error` in the report ends with `[cleanup: rebooted … card emptied …]`: the card is empty and `debug.mic` is gone. After fixing the cause you can rerun straight from step 3; if you do not rerun, do step 8.
- `error` says `cleanup FAILED`: a calibration recording may still be on the card, and the device may still have been on one of the sweep gain settings before the reboot. **Do not `cpa pause 1` yet**, or your cpad will pull that recording into `~/CardputerNotes/recordings` and WhisperWoof will transcribe it. Do this in order:
  1. Run `cpa pause 3600` right away. The preflight pause expires 30 minutes after the run started.
  2. Power-cycle the device: side switch to OFF, unplug USB, wait 5 s, plug back in, switch to ON. The reboot removes `debug.mic`; if it was recording, that segment is recovered at boot.
  3. Run `uv run --project daemon python tools/hil/run_e2e.py --scenarios rec_memo --report "$TMPDIR/drain.json"`. The first step of `rec_memo` waits until the HIL's temporary cpad has emptied the card, and what it pulls stays only in its temporary directory.
  4. Confirm that `card_empty` in `$TMPDIR/drain.rec_memo.json` is ok (the card is empty), then do step 8.
- Mixed Chinese-English accuracy (MER, the second half of the original G-A10) is not in `mic_cal`. Whether to do it, and whether it blocks, is your call (brief §8 suggests making it a non-blocking manual item). To do it, record a handheld memo with the release firmware reading the passage in §5.3, transcribe it with WhisperWoof, then compare it with `firmware/hwtest/host/hwtest_host.py mer` against the copy the Mac recorded at the same time.

### 5.3 Reading passage (about 60 s, mixed Chinese and English, written for this project)

The passage is the test input itself, so it stays in its original mixed Chinese and English. It is a made-up meeting summary about transfer speed, battery life and action items.

> 今天下午我们和 design team 开了一个 sync meeting，主要讨论 recording pipeline 的进度。第一个问题是 transfer speed：在 USB 上用 base64，一个小时的录音大概几分钟就能传完，所以先不做 binary mode。第二个问题是 battery life，大家希望录音的时候屏幕只是调暗，不要完全关掉，这样随时能看到 REC 的标志。Action items 有三个：小王负责更新 WhisperWoof 的 pull request，Alex 在下周一之前把 SD card 的 bug 查清楚，我来写一份 summary 发给大家。最后提醒一下，下次 meeting 改到星期四上午，地点还是 Zoom，请大家提前看一下 design doc。

### 5.4 Results: `mic_cal` and `mic_verify` on 2026-10-21 ✅

Run by an agent with exclusive use of the device (test firmware `cpa-fw/0.3.1`, card inserted), following the steps in §5.2. Mac output was MacBook Pro Speakers, volume 69, not muted (read only, not changed). The B1 probe landed in the window on the first try (tone A −44.57 dBFS, no reference scaling needed), reference sound 14.1 s. Recordings stayed only in the HIL's temporary directory; the report has only numbers. Reruns and regressions are recorded in doc 07 §6.9.

**`mic_cal`** (12:21:43–12:26:55, 8/8 checks ok). ΔG is always relative to B1; expected peak = −23.7 + ΔG, expected RMS = −47.0 + ΔG (from the two reference recordings, brief §0); SNR = expected RMS − noise floor.

| Setting | PGA | REG17 | mag | Nominal ΔG | Tone A (dBFS) | ΔG | Voice ΔG | Noise floor (high-pass, dBFS) | Clipped | Step | Opening thump peak | lead (ms) | Expected peak | Expected RMS | SNR (dB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B1 | 0 | 0xBF | 16 | 0.00 | −44.57 | +0.01 | +0.01 | −76.5 | 0 | 8 | 28,200 | 260 | −23.69 | −46.99 | 29.56 |
| U0 | 0 | 0xBF | 2 | −18.06 | −62.49 | −17.91 | −18.04 | −94.7 | 0 | 1 | 3,604 | 190 | −41.61 | −64.91 | 29.82 |
| S6 | 6 | 0xD7 | 2 | +11.94 | −32.10 | +12.48 | +12.42 | −73.1 | 0 | 1 | 13,468 | 240 | −11.22 | −34.52 | 38.61 |
| S7 | 7 | 0xD1 | 2 | +11.94 | −31.88 | +12.70 | +12.55 | −73.2 | 0 | 1 | 9,580 | 200 | −11.00 | −34.30 | 38.92 |
| S8 | 8 | 0xCB | 2 | +11.94 | −32.01 | +12.58 | +12.48 | −73.4 | 0 | 1 | 6,786 | 100 | −11.12 | −34.42 | 39.02 |
| S9 | 9 | 0xC5 | 2 | +11.94 | −32.30 | +12.28 | +12.26 | −72.1 | 0 | 1 | 4,920 | 80 | −11.42 | −34.72 | 37.42 |
| S10 | 10 | 0xBF | 2 | +11.94 | −32.31 | +12.27 | +12.27 | −73.2 | 0 | 1 | 3,500 | 90 | −11.43 | −34.73 | 38.51 |
| N27 | 9 | 0xBF | 2 | +8.94 | −35.33 | +9.25 | +9.26 | −73.1 | 0 | 1 | 3,283 | 100 | −14.45 | −37.75 | 35.31 |
| N33 | 10 | 0xC5 | 2 | +14.94 | −29.24 | +15.34 | +15.35 | −59.9 | 0 | 1 | 4,903 | 60 | −8.36 | −31.66 | 28.26 |
| N36 | 10 | 0xCB | 2 | +17.94 | −26.34 | +18.24 | +18.28 | −64.5 | 0 | 1 | 6,757 | 90 | −5.46 | −28.76 | 35.75 |
| B2 | 0 | 0xBF | 16 | 0.00 | −44.59 | −0.01 | −0.01 | −76.5 | 0 | 8 | 28,680 | 260 | −23.71 | −47.01 | 29.47 |

- **Validity** (brief §7.1, all passed): B1 and B2 differ by 0.02 dB (≤1); U0 −17.91 dB, should be −18.06 ± 0.5; each setting is +0.30 to +0.76 dB above nominal (≤1.5); tone and voice ΔG differ by at most 0.15 dB (≤1); N33 and N36 did not clip a single sample either.
- **Split** (§7.3): the 5 ways to split +30 dB have SNR 37.4–39.0 dB, best is S8 (39.02). S10's 38.51 is within 1 dB, so S10 is chosen (PGA 10, no REG17 boost). The splits differ by only about 1.6 dB, confirming that "with the same total gain, how it is split barely affects quality".
- **Fine-tune** (§7.4): S10 expected peak −11.43, round(2 × (−12 + 11.43)) = −1, needs to go down, so drop one PGA step; at PGA 9 the expected peak is −14.43, round(2 × 2.43) = +5, REG17 = 0xBF + 5 = **0xC4**, expected peak **−11.93 dBFS** (window −14 to −10). Measured PGA 9 → 10 is 3.02 dB (N27 vs S10), and S9 and S10 differ by only 0.01 dB, so the extrapolation is reliable. The untuned `{10, 0xBF, 2}` (−11.43) is also in the window, with the same SNR.
- **Result**: `kMicGain {9, 0xC4, 2}`, expected peak −11.93, voiced RMS −35.23, noise floor −73.74 dBFS (S10's noise floor minus 0.5 dB), `CARDPUTER_SILENCE_RMS` 0.000843. Two independent recalculations (not using repo code) arrived at the same setting.
- **Lead phase** (§7.5): 5 silent 2.5 s meetings at `{9, 0xC4, 2}`, lead 90 / 80 / 70 / 80 / 90 ms → `recommend_lead` 100 → `kLeadMs` stays 150 (§4.3). The noise floor of these 5 segments (high-pass) was −73.8 to −74.2 dBFS, unfiltered −67.3 to −68.7 dBFS (12–14, about 0.0004).
- Note: N33 had a higher noise floor at the start (high-pass p50 33, −59.9 dBFS), and N36 a little (19.5); that was sound in the room at the time, not caused by the gain; they took no part in the decision. This room's noise floor is about 10 dB lower than the brief expected (about −64 dBFS, 0.0006).

**`mic_verify`** (after changing the constants and reflashing the test firmware, 12:34:16–12:35:17, 5/5 checks ok): B1 probe tone A −44.52 dBFS; after a reboot, without sending `debug.mic`, one segment recorded at the firmware's own `kMicGain`: ΔG **+11.74 dB** (nominal +11.44), voice +11.83 dB; extrapolated peak **−11.96 dBFS**, voiced RMS −35.26, noise floor (high-pass) −74.26 dBFS; 0 clipped samples; step 1; lead 100 ms.

**Not done yet**: §5.2 step 9 (a handheld memo at usual speaking level with the release firmware to confirm the peak), and mixed Chinese-English accuracy (MER).

> **2026-10-21 (v0.3.2, #31)**: every segment of `mic_cal` and `mic_verify` was recorded with the codec already "warm": the device stayed plugged into USB the whole time, and neither ESP32 reboots nor flashing make the codec cold (the #31 reproduction); doc 07 §6.9 also records that none of these segments start with all zeros. So the gain, noise floor and lead above are not affected by the 1 second of zeros after boot, and need not be redone. The zeros after boot are handled in v0.3.2 by warming up at boot and "waiting for sound before starting" (doc 11 §4.2), and reproduced by HIL `rec_cold_start` with the test firmware's `debug.codec`. When `mic_cal` is run again later, no special warm-up is needed before the first segment: the boot warm-up already does it.


## 6. ⚠️ New finding: a single file on the SD card hits EIO at about 8 MB (M3-A's 9.6 MB segments will hit it) ✅

`!xfermk <KB> [pace_ms] [sync_KB]` writes 16 KB of pattern data per `write` to `/sd/cpa_hwtest/xfer.bin` (20 MHz, the patched SD library, the v0.2 card):

| Requested size | How written | Result |
|---|---|---|
| 2,048 KB | Full speed | 2/2 OK (1.3 s) |
| 4,096 KB | Full speed | 3/4 OK (2.6 s); the last one (re-check after the code refactor, fsync every 1 MB) hit EIO at **245,760 B** |
| 9,375 KB (= 9,600,000 B, one 5-minute segment) | Full speed, `O_TRUNC` overwriting the old file | 4/4 **EIO**, at 999,424 / 2,080,768 / 8,372,224 / 8,372,224 B |
| 8,192 KB | Full speed, `O_TRUNC` | 2/2 **EIO**, 8,372,224 / 8,355,840 B |
| 9,375 KB | 50 ms pause after every 16 KB (about 280 KB/s, 30 s) | **EIO**, 8,372,224 B |
| 9,375 / 8,176 KB | Delete the file first, then write | **EIO**, 6,914,048 / 8,355,840 B |
| 9,375 KB | fsync every 1 MB / every 32 KB | **EIO**, 8,339,456 / 8,372,224 B |
| `!sdbench` (3 files of 4 MiB, 512 B / 4 KB / 32 KB blocks, read back and compared after writing) | Right after a 4 MiB file | All written and read correctly |

- `errno = 5` (EIO), meaning FatFs reported `FR_DISK_ERR` or `FR_INT_ERR`. It does not depend on write speed or fsync rhythm; the failure position moves with cluster allocation (the earliest two at 1 MB and 2 MB were the old `!xfermk`, which did not report errno yet). When overwriting with `O_TRUNC`, FatFs reallocates from the clusters it just freed (📚 `remove_chain` moves `last_clst` back), so all 4 stopped at the same 8,372,224 B; deleting the file first or changing where the file starts moves the failure position too.
- Guess: some area of the card cannot be written, or the patched driver fails on certain LBAs / certain multi-block writes. But the region `!sdbench` wrote right afterwards was fine, so it may also depend on the write pattern (CMD25 at 16 KB at a time). ⏳ Not located yet. Needed: log the failing LBA, block count and the card's response in `sd_diskio` of `third_party/SD` (outside this package's scope); run "Disk Utility → First Aid" on this card on a Mac; retest with another card.
- Card state: at the end `!sdinfo` reported 15,350,775 free clusters (only 9 used: v0.2's files and directories), and FSINFO bookkeeping has no leaked clusters; but this number comes from FSINFO, not a full table scan, so the FAT itself can only be confirmed with First Aid. Only v0.2's content is left on the card (§0).
- **Impact on M3-A / M3-H**: 5-minute segments (9.6 MB) will almost certainly hit EIO on this card, go down the "SD locked up" path of 11 §4.3, and the recording gets cut short; shorter segments **cannot** avoid it (4 MB has failed too). HIL `rec_meeting` (30 s segments) and `rec_rotation` (1-minute segments) may not hit it. Whether v0.2's outbox, which writes only one small file per item, can hit it is untested ⏳; v0.2 currently works fine on this card (`cpa status`: `store=sd`).
- Reproduce: after flashing hwtest, send `printf '!xfermk 9375\n'` to the device (or `host/hwtest_host.py xfer-sd --file-kb 9375`), and look for `{"t":"xfermk",…,"wrote":…,"errno":5}`; then `gate-clean`.

## 7. Reproduction

```bash
cpa pause 5400                                   # lsof /dev/cu.usbmodem* should be empty
# G-A0 (v0.2 test firmware, card inserted)
cd firmware/app && ~/.local/bin/pio test -v -e cardputer-adv-test -f 'device/test_runtime_budget' | grep CPA_BUDGET
# hwtest gate firmware
cd ../hwtest && ~/.local/bin/pio test -e native && ~/.local/bin/pio run && ~/.local/bin/pio run -e cardputer-adv -t upload
H="uv run --no-project --with pyserial==3.5 python host/hwtest_host.py"
$H xfer-sweep                 # G-A1 main table, about 8 minutes -> captures/g-a1-*.json
$H xfer-sd --file-kb 4096     # G-A1 card read styles -> captures/g-a1-sd-*.json
$H cue-sweep                  # G-A5, 112 runs, about 6 minutes, plays the cue 112 times -> captures/g-a5-*.json
$H cue-listen                 # G-A5 volume (a person); not applicable from v0.3.1, no need to run
$H cue-silent                 # G-A5 lead_ms, v0.3.1 silent start, 20 runs -> captures/g-a5-silent-*.json (§4.3)
$H mic-capture --mag 16 --pga 0 --secs 60 --label 30cm   # old G-A10 method, only a fallback from v0.3.1; use HIL mic_cal now (§5.2)
$H mer --ref ref.txt --hyp dev30.txt mac30.txt           # G-A10 MER, no device needed
$H gate-clean                 # only removes files the gates wrote
# host-side unit tests
uv run --no-project --with pytest --with pyserial==3.5 python -m pytest host/tests -q
# wrap up: flash the v0.2 release back
cd ../app && ~/.local/bin/pio run -e cardputer-adv -t upload && cpa pause 1 && cpa status
```

Raw data (`firmware/hwtest/captures/`, gitignored; copy to `docs/gates/captures/` to commit it):

| File | Content |
|---|---|
| `g-a0-test_runtime_budget-20261020.log` | G-A0 `pio test -v` output |
| `g-a1-20261020-232719.json` | G-A1 main table (32 settings, each with per-second `xs` data and every ping RTT sample) |
| `g-a1-sd-20261020-233705.json` | Card read style comparison (4 MiB file); `g-a1-sd-20261020-233027.json` is the same comparison, but the file only got 0.95 MB written (EIO, §6) |
| `g-a1-enc-*-20261020.json` | The three framing implementations of §3.2 |
| `g-a5-20261020-234339.json` | All 112 G-A5 runs (each with 100 slots of RMS / peak), group summaries, verdict for both lock types |
| `g-a10-noise-m*-p*-20261020.json` | §5.1 noise floor (numbers only) |
