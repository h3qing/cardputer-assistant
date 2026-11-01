**English** · [中文](sd-card.zh-CN.md)

# SD card gate, measured: init, formatting, throughput, outbox load, continuous recording

> Measured on real hardware (Cardputer ADV, MAC 02:00:00:00:00:01) on 2026-10-20. User authorisation: this card is dedicated to this project, and all existing data on it may be erased.
> Device side: `firmware/hwtest/src/sd_*.cpp` (`!sd…` commands); host side: `firmware/hwtest/host/sd_run.py`; raw JSON and WAVs: `firmware/hwtest/captures/sd-*` (gitignored).
> Marks: ✅ measured on the device · 📚 from docs or source code · ⏳ not verified.

## 0. Environment and operation log

| Item | Value |
|---|---|
| Toolchain | A: `espressif32@6.10.0`, Arduino core 2.0.17 / ESP-IDF 4.4.7 (ADR-0001) |
| Pins | SCK 40 / MISO 39 / MOSI 14 / CS 12, via Arduino `SPI` (FSPI = SPI2). The screen is on SPI3; the two do not share a bus (📚 M5GFX source) |
| SD driver | `firmware/hwtest/lib-core2/SD`: a copy of the `SD` library shipped with core 2.0.17, with 3 changes (§2.3). Used only by envs `cardputer-adv` / `-gnu17` |
| Card | SDXC, 982,573,056 sectors (503.1 GB, labelled 512GB). Originally MBR partition type `0x07`, partition starting at LBA 2048, **exFAT**, 128 KB clusters |
| Port etiquette | First `cpa pause 3000`; touch the device only once `lsof /dev/cu.usbmodem1101` is empty; the pause was extended twice along the way. Every upload (a dozen or so) was `pio run -t upload`, with no key presses and no G0; the serial port only used the safe open sequence (03 §2). At the end the v0.1 app was flashed back (HEAD `7a323d7` of `release/v0.1.0`), `cpa pause 1` resumed the daemon, and `cpa status` showed `link: ready … fw=cpa-fw/0.1.0` |
| Anomaly | After one upload, the first `hello` reported `rst:"panic"` (rst_code 4, 2.6 s after boot). It did not happen again in a dozen more uploads; cause unknown ⏳ |

**Existing data on the card**: it was exFAT, and this toolchain's FatFs is built without exFAT (`FF_FS_EXFAT 0`, 📚 the SDK's `ffconf.h`), so the directory could not be read and **there was no way to count how many files, or how much data, were deleted**. Per the authorisation, the whole card was formatted to FAT32 directly.

## 1. Summary of conclusions

| Item | Conclusion | Impact on v0.2 / v0.3 |
|---|---|---|
| Init | ❌ **Neither the core's own `SD` library nor IDF 4.4's `sdspi` driver can initialise this card**: ACMD41 keeps answering "idle" and they give up after 1 second. The root cause is the voltage bits in the ACMD41 argument (§2). With the argument changed to HCS only (`0x40000000`), init takes 3–6 ms, and all 40+ mounts since then succeeded ✅ | The SD driver must be a patched copy; the stock drivers cannot be used as is. Retest with any other card |
| File system | exFAT does not mount → formatted to FAT32 with FatFs, taking **43.7 s**. 1 FAT, 32 KB clusters, 15,350,784 clusters, 479,712 MiB usable; data area starts at sector 122,880 (= 60 MiB, 4 MiB aligned) ✅ | Cards above 32GB ship as exFAT, so the device must be able to format them itself |
| SPI clock | Requested 4 / 20 / 25 / 26.7 / 40 MHz, 5 runs each of "mount + write 256 KB + read back and verify": **all 5/5 passed**. Requesting 25 MHz actually gives only **20 MHz** (80 MHz APB divider) ✅ | **Recommend 20 MHz** (within spec, and plenty for recording). 40 MHz works but is out of spec; re-evaluate later for bulk transfer |
| Sequential throughput (20 MHz) | Write / read: 512 B blocks 483 / 1109 KB/s, 4 KB blocks 1363 / 1619 KB/s, 32 KB blocks **1634 / 1714 KB/s**. At 40 MHz, 32 KB blocks give 2513 / 2667 KB/s ✅ | Write units of at least 4 KB, ideally 32 KB (= 1 cluster) |
| outbox load (200 items × 300 B) | Flat with long file names: per-item save p95 **93 ms**, getting linearly slower with the item count; **flat with 8.3 short names: p95 29 ms**; 32 shard directories: about 20 ms once settled, but each new shard directory costs tens of milliseconds more ✅ | For small-file directories use 8.3 names or subdirectories, not flat long file names |
| rename | Fails when the target exists, `errno = EEXIST`, with source and target unchanged ✅. FatFs rename registers the new name first and deletes the old name after; it is **not atomic** (📚) | The "rename atomically overwrites" semantics of `FileStore` do not hold on SD (§6.3) |
| Continuous recording | 16 kHz / 16 bit mono, 60 s written as WAV, ring of 4 × 4 KB buffers. 3 runs: **0 dropped frames**, longest single write 45 ms, longest once-per-second fsync 78 ms, at most 1 ring buffer in use ✅ | 4 × 4 KB (16 KB of RAM) is enough |
| USB link while recording | 59/59 pings answered, p50 4.6 ms; but **16/59 replies were held for about 1 second**, until the host's next write (the HWCDC problem in ADR-0001 §5.2) ✅ | The recommendation that the daemon send an empty line after 500 ms is required in the recording case |
| Pulling files to the Mac | A 1.9 MB WAV pulled to the Mac in base64 chunks (12 KB each, with CRC32) at **138 KB/s**, 0 retransmits; header correct, `afinfo` recognises it, the test tone is visible in the RMS ✅ | One hour of recording is about 115 MB, which takes 14 minutes at this speed. v0.3 needs a faster transfer method |
| What is left on the card | Only four empty directories, `/cpa/`, `/cpa/outbox/`, `/cpa/rec/`, `/cpa/logs/`, and no files ✅ | — |

## 2. Init: why the stock drivers fail

### 2.1 Symptoms ✅
- **core 2.0.17's `SD` library**: `SD.begin()` fails every time, each taking about 1.0 s, logging `APP_OP_COND failed: 1` followed by `f_mount failed: (3)`. Tried 6+ times across 4 boots, all failed.
- **IDF 4.4.7's `sdspi` + `esp_vfs_fat_sdspi_mount`**: fails the same way, logging `sdmmc_init_ocr: send_op_cond (1) returned 0x107` (timeout). It fails even right after a successful raw init.
- **The card itself is fine**: raw SPI at 400 kHz, CMD0 returns R1 = `0x01`. The simplest init sequence (CMD0, CMD8, then a CMD55 + ACMD41(`0x40000000`) loop) is ready in 189 ms (436 polls); the OCR afterwards is `C0FF8000` (powered up, high capacity).

### 2.2 Single-variable comparison ✅ (`!sdrawinit`, see `kVariants` in `sd_rawinit.cpp`)
First let an IDF mount fail once, leaving the card in the "stuck" state, then try to rescue it with one raw init sequence; then let IDF fail again and try the next sequence. Each sequence gets at most 3 s:

| Recovery sequence (the simplest sequence with exactly one change) | Result |
|---|---|
| Turn on CRC (CMD59) | Ready in 3 ms |
| Send CMD58 before ACMD41 | Ready in 3 ms |
| Raise CS and give 8 clocks between commands (IDF's way) | Ready in 4 ms |
| **ACMD41 argument with the 3.2–3.3 V voltage bit (`0x40100000`, as the core library writes it)** | **Not ready within 3 s (6882 polls)** |
| The full core library / IDF sequence (CRC, CMD58, voltage bits, CS toggling) | Not ready within 3 s (6188 polls) |
| The simplest sequence itself | Ready in 0–3 ms; after that even the full sequence takes only 1 ms |

Two more points:
- Without CMD8 the card never becomes ready. This matches the spec: a high-capacity card should not become ready if the host never sent CMD8.
- If the card has just been ready once, every variant becomes ready quickly, including the IDF-specific ones: 10 ms polling interval, CMD5, CMD52, a 2.7–3.6 V voltage mask, no idle byte before commands.
- Only the core library's way of selecting (raising CS without giving clocks) failed all 4 times. But that round did not control the starting state, so whether it causes problems on its own cannot be said here ⏳.

**Conclusion**: once this card starts init from scratch, it stays idle as long as the ACMD41 argument carries voltage-window bits; with HCS only it finishes at once. In the SD physical layer spec's command table for SPI mode, the ACMD41 argument defines only HCS (bit 30); the other bits are reserved (📚 quoted from memory, not checked word for word). The core library hard-codes `0x40100000`. IDF 4.4's `sdmmc` writes its own voltage mask. Since "`0x40FF8000` passes from the ready state, yet IDF actually fails", there must be another factor; this was not chased down ⏳. IDF is a precompiled library and cannot be changed.

### 2.3 Patch (`firmware/hwtest/lib-core2/SD`, Apache-2.0, copied verbatim with only 3 changes, each marked `CPA patch`)
1. ACMD41 argument changed from `0x40100000` to `0x40000000`. **With only this change** the card initialises, and it keeps working afterwards.
2. `GET_BLOCK_SIZE` changed from 1 to 8192 sectors (4 MiB), so `f_mkfs` aligns the data area to 4 MiB. The SD Association's formatter aligns SDHC/SDXC the same way (📚).
3. The 25 MHz clock cap removed, only so the clock sweep can test 26.7 / 40 MHz.

`platformio.ini` uses `lib_extra_dirs = lib-core2` so that only toolchain A uses this copy. B (`cardputer-adv-p54`, core 3.2.1) has a different `f_mkfs` signature, so it keeps its own `SD`; all three envs compile. ⚠️ **The NOTICE at the repo root does not list this Apache-2.0 code yet**; it needs to be added.

## 3. Before and after formatting ✅

| | Before | After |
|---|---|---|
| MBR partition | Type `0x07`, from LBA 2048, 982,571,008 sectors | Type `0x0C` (FAT32 LBA), from LBA 63, 982,572,993 sectors |
| Boot sector | `EXFAT   `, 128 KB clusters | `MSDOS5.0` / `FAT32   `, 32 KB clusters |
| FatFs mount | `FR_NO_FILESYSTEM` (13) | Mounts in 5–6 ms; `fs_type` 3, `n_fats` 1 |
| Layout | — | FAT starts at sector 2,936, 119,944 sectors long (58.6 MiB); data area starts at sector 122,880 (4 MiB aligned) |
| Free-space query | — | `f_getfree` 0 ms (trusts the free cluster count in FSINFO) |

How: `!sdformat` first mounts without formatting. Only if that fails does it mount again with `format_if_empty=true`, and the library calls `f_mkfs(FM_ANY, auto cluster size)`. The whole process took 43.7 s. The partition still starts at LBA 63 (hard-coded by FatFs R0.13c for a single partition), but what matters for performance is whether the data area (that is, every cluster) is aligned, and it is.

`!sdclear` (when the card mounts as FAT, delete everything and report only the counts) was also run on the FAT card: it deleted 4 directories and 0 files, leaving the card empty.

## 4. SPI clock sweep ✅ (5 runs per step: mount, write 256 KB and fsync, read back, compare byte by byte)

| Requested | Actual clock | Mounts OK | Verify OK | Write KB/s | Read KB/s |
|---|---|---|---|---|---|
| 4 MHz | 4.0 MHz | 5/5 | 5/5 | 438 | 441 |
| 20 MHz | 20.0 MHz | 5/5 | 5/5 | 1610 | 1673 |
| 25 MHz | **20.0 MHz** | 5/5 | 5/5 | 1612 | 1673 |
| 26.7 MHz | 26.67 MHz | 5/5 | 5/5 | 1795 | 2029 |
| 40 MHz | 40.0 MHz | 5/5 | 5/5 | 2359 | 2569 |

- The driver checks CRC16 on data blocks (verified on read, also sent on write), and we also compared the content byte by byte, so "OK" means the data really was correct. At 40 MHz another 12 MB was written and read back (§5), all correct.
- 40 MHz exceeds the 25 MHz default-speed cap of SPI mode (the driver never sends CMD6 to switch to high speed), and MISO goes through the GPIO matrix. It works, but it is not recommended as the default.

## 5. Sequential throughput ✅ (for each block size, write 4 MB and read it back; times include the final fsync and close)

| Clock | Block | Write KB/s | Read KB/s | Longest single write | fsync+close |
|---|---|---|---|---|---|
| 20 MHz | 512 B | 483 | 1109 | 36.8 ms | 3.9 ms |
| 20 MHz | 4 KB | 1363 | 1619 | 35.0 ms | 4.0 ms |
| 20 MHz | 32 KB | **1634** | **1714** | 40.2 ms | 4.6 ms |
| 40 MHz | 512 B | 547 | 1454 | 64.6 ms | 3.2 ms |
| 40 MHz | 4 KB | 1930 | 2443 | 32.9 ms | 4.0 ms |
| 40 MHz | 32 KB | 2513 | 2667 | 36.0 ms | 4.1 ms |

Whatever the block size, there is occasionally a 30–65 ms write stall (the card doing internal housekeeping). 512 B blocks have to be committed one by one and are 3.4× slower than 32 KB blocks.

## 6. outbox-style load ✅ (20 MHz, `!sdoutbox 200`)

### 6.1 Method
About 300 B per item, saved in the order `open(tmp, O_CREAT|O_TRUNC)` → `write` → `fsync` → `close` → `rename(tmp, json)`, the same as `outbox_persist` but through the POSIX interface under `/sd`. Three layouts:
- **flat_lfn**: `/…/<MonoId>.json`, e.g. `K3M9Q2T-00042-0001.json`. IDs from the same boot share their first 14 characters.
- **shard32_lfn**: `/…/_<last character of the ID>/<MonoId>.json`, the same sharding rule as `LfsStore`.
- **flat_83**: `/…/00000001.JSN`, follows the 8.3 rule and creates no long-file-name directory entries.

### 6.2 Results

| Layout | Save p50 / p95 / max | Mean of first 10 → mean of last 10 | List directory | Read back 200 one by one | Delete p50 / p95 / max |
|---|---|---|---|---|---|
| flat_lfn | 56.7 / **93.3** / 125.1 ms | 16.1 → **94.2 ms** | 26 ms | 2029 ms | 12.6 / 19.9 / 22.1 ms |
| shard32_lfn | 19.0 / 88.1 / 137.8 ms | 89.5 → 20.7 ms | 95 ms | 606 ms | 5.8 / 7.9 / 59.6 ms |
| flat_83 | 21.4 / **29.1** / 101.7 ms | 11.8 → 29.2 ms | 14 ms | 934 ms | 7.2 / 9.7 / 10.4 ms |

`f_getfree` takes 36–39 µs each time.

**Analysis (📚 FatFs behaviour, ⏳ not measured item by item)**
- flat_lfn slows down linearly with the item count. A long file name needs a matching "short name + `~n`", and since these IDs all share a prefix, short names collide heavily. Every number tried means a linear scan of the whole directory, and rename scans it again.
- For shard32, each of the first 10 items creates a new shard directory. FAT zeroes a whole cluster (32 KB) when creating a directory, which pushes up p95 and max. Once all 32 directories exist, each item takes about 20 ms.
- For comparison: a comment in `LfsStore` records that flat LittleFS with 200 items has a p95 of 513 ms. So on speed alone, a flat directory with 8.3 names on SD is actually faster.

### 6.3 rename semantics and power loss ✅ + 📚
- **Overwriting an existing target** ✅: `rename(a.tmp, a.json)` with `a.json` already present returns -1, `errno = 17` (EEXIST); `a.tmp` is kept as is and `a.json` is unchanged. `unlink(a.json)` first, then rename, succeeds. LittleFS rename overwrites the target atomically; FAT does not.
- **Order of a rename** (📚 FatFs R0.13c `f_rename`): first register the new directory entry (copying the original start cluster and size), then delete the old entry, then sync. If the old and new entries are not in the same sector, a power loss in between leaves **two names (`.tmp` and `.json`) pointing at the same cluster chain**. This is the "a `.json` with the same name already sits next to the `.tmp`" case in 05 §5.9.
  - ⚠️ In that case **never `unlink(.tmp)`**: when FatFs deletes a file it also frees its cluster chain, which `.json` is still using, so `.json` breaks with it. The rule set in 05 §5.9, "do not delete `.tmp`, only count it", is required on SD, not just a safety margin.
- **Power loss mid-write** (📚 the "Critical section" part of the FatFs application note): a power loss halfway through a FAT write can corrupt the FAT structure. According to the note, at worst there are cross-links and the object being changed is lost; milder cases are a newly created file disappearing, appended content rolling back, or "lost clusters" (space in use but referenced by no file). Files other than those open for writing are not affected. The note's mitigation is to keep files open for writing as briefly as possible and to call `f_sync` promptly.
  - What this means for the outbox: if power is lost while a `.tmp` is half written, at the boot scan the `.tmp` is either missing or has the wrong size (the size in the directory entry is written only at close/fsync), and it can simply be deleted, consistent with the rules in 05. But clusters already allocated may be lost for good. There is no fsck on the device, so lost clusters can never be reclaimed.

## 7. Continuous recording ✅

### 7.1 Method (`!sdrec 60 2048 4 1000`, 20 MHz)
- 4 buffers of 2048 samples each (4 KB, 128 ms) form a ring. Using M5Unified's `Mic.setBufferReleaseCallback`: as soon as the mic task fills a buffer, it queues the next empty buffer immediately, without going through the loop. The loop task only takes full buffers off the queue, `write`s them to `/sd/cpa_hwtest/rec.wav`, `fsync`s every 1000 ms, and answers `!ping`.
- **Dropped-frame rule**: whenever both of the mic's request slots are empty at once, count one dropped-frame event and record how long they were empty. Also reconcile "samples actually written" against "wall clock × 16 kHz".
- The WAV header is written as a placeholder at the start and the lengths are filled in after recording.
- The host sends `!ping <id>` once per second and computes the round trip from when the reply arrives. After recording, `!sdget` pulls the WAV back to the Mac to check the header, run `afinfo` and compute the RMS.

### 7.2 Results

| Run | Buffers / samples | Wall clock | Dropped-frame events | Longest / mean single write | Longest fsync | Max ring buffers in use |
|---|---|---|---|---|---|---|
| 1 | 468 / 958,464 | 59,939 ms | 0 | 39.7 / 3.5 ms | 78.4 ms | 1/4 |
| 2 (1 kHz test tone played on the Mac midway) | 468 / 958,464 | 59,937 ms | 0 | 45.1 / 3.4 ms | 40.6 ms | 1/4 |
| 3 | 468 / 958,464 | 59,938 ms | 0 | 38.8 / 3.5 ms | 75.0 ms | 1/4 |

- Samples written are 528–560 fewer (33–35 ms) than "wall clock × 16 kHz". That is the latency of the first buffer at the start of recording, not a loss midway.
- Writing to the card takes 3.5 ms per 128 ms on average, a duty cycle of about 2.7%. The worst cases, a 45 ms write stall or a 78 ms fsync, are far below the roughly 380 ms the ring can absorb (while the loop is stuck writing one block, the other 3 keep recording).
- **WAV check**: all header fields are correct (RIFF/data lengths, PCM, 1 channel, 16 kHz, 16 bit); `afinfo` reports `WAVE, 1 ch, 16000 Hz, Int16, 59.904 s`. When quiet, after dropping the first 1.5 s, the RMS is about 6 with 7–8 distinct values: the ES8311 noise floor in a quiet room (consistent with the numbers in ADR-0001). Run 2 played a 4 s 1 kHz test tone at about 22–26 s; RMS for those seconds is **43–74**, 6 elsewhere, peak 144. So what reached the SD card is the real microphone signal.
- **USB link** (run 3): all 59/59 pings got replies, p50 4.6 ms. But **16 were over 100 ms**: 15 about 1.0 s and 1 at 580 ms, all held by HWCDC until the host's next write (ADR-0001 §5.2). The ADR's idle baseline is 0 held in 240 round trips. Whether this increase comes from card writes blocking the loop or from the mic task was not separated ⏳.
- **Pull speed**: 1,916,972 B took 13.6 s (138 KB/s), 0 retransmits, all CRC32s matching. (The 318 s and "1 s round trip" recorded in run 1 were an artefact of the host script polling in 2 s chunks at the time; that is fixed and those numbers are void.)

## 8. Recommendations

| Area | Recommendation |
|---|---|
| Driver | v0.2's `SdStore` uses the patched core `SD` library (move `lib-core2/SD` into the app, or send the ACMD41 argument fix upstream). Do not use IDF `sdspi` (precompiled, cannot be changed). Add the Apache-2.0 entry to NOTICE |
| SPI clock | **20 MHz** (requesting 20 or 25 both give 20). Init always at 400 kHz |
| Mount / format | At boot, mount without formatting first. If it fails (exFAT, blank card), **do not format automatically**; only format after the user confirms on the device (about 44 s for a 512GB card). Keep patch 2's 4 MiB alignment when formatting |
| Recording writes | Refill buffers from the mic's release callback, ring of 4 × 4 KB buffers, write each block from the loop as soon as it is full (i.e. one write per 4 KB), fsync once per 1 s. As in the 04 PRD, write `/cpa/rec/<id>.wav.part`, fill in the header when done, then rename. A `.part` left by a power loss has length 0 in its header; the Mac repairs it from the actual file size |
| Bulk writes | Blocks of at least 4 KB, ideally 32 KB (= 1 cluster); do not write in small 512 B blocks |
| Small files / outbox | v0.2's **outbox stays on LittleFS** (atomic rename, already verified). SD holds few, large files such as recordings and archives. If small files ever go on SD: use 8.3 names and split directories by boot count, e.g. `/cpa/outbox/B00042/0001.JSN`, keeping each directory to a few hundred entries |
| `FileStore` semantics | `rename` on SD returns false when the target exists, and it **must not** be changed to unlink-then-rename: a power loss between the two steps leaves only the `.tmp`, which the boot scan treats as "never committed" and deletes. When `.tmp` and `.json` both exist, never unlink the `.tmp` (§6.3) |
| Directory layout | `/cpa/outbox/`, `/cpa/rec/`, `/cpa/logs/` (already created). No conflict with Launcher's `/config.conf`, `/downloads/`, `/bkp/` (see launcher.md) |
| Transfer to the Mac | base64 with per-chunk requests only gives 138 KB/s. v0.3 should switch to binary framing with a sliding window, or USB mass storage mode (MSC). Measure the USB FS ceiling first, then decide |

## 9. Risks and open items

| Risk | Notes |
|---|---|
| Card compatibility | Only this one 512GB card was tested. Other cards may initialise with the stock driver, or may have other problems. ⏳ Test at least 2–3 more cards of different brands and sizes (including one under 32GB that ships as FAT32) |
| Launcher reading this card | Launcher also uses Arduino `SD` (launcher.md §0), so it very likely has the same ACMD41 problem and cannot read this card. ⏳ Needs a real test with Launcher |
| 40 MHz | 25/25 mounts and 12 MB of data all correct, but out of spec, and no long-duration test |
| FAT is not power-loss safe | Cross-links or lost clusters can happen, and there is no fsck on the device. ⏳ Power-loss testing (G10-style) not done yet |
| When FSINFO is not trustworthy | If the free cluster count in FSINFO is invalid, `f_getfree` has to scan the whole 58.6 MiB FAT, about 35 s at 1.7 MB/s ⏳. Do not query free space on the main thread at boot |
| 4 GB single-file limit | A FAT32 limit. At 32 KB/s that is 36 hours; with the PRD's 5-minute segments it is not a problem |
| RAM | hwtest's static RAM is 130 KB (including a 32 KB test buffer and 64 KB of mic buffers). Also, this SDK's `FF_MAX_SS` is 4096, and each open-file slot in the VFS takes about 4 KB of heap, so `max_files` should be kept small (📚, ⏳ heap use not measured) |
| Replies held while recording | The link does not drop and no replies are lost, but about 27% of replies wait for the host's next write. The daemon's empty-line wake-up, or moving card writes to a separate task, would both help ⏳ |
| The one panic reset | The one mentioned in §0 did not recur; cause unknown ⏳ |

## 10. Reproduce

```bash
cpa pause 3000                                   # make the daemon release the serial port; lsof /dev/cu.usbmodem* should be empty
cd firmware/hwtest && ~/.local/bin/pio run -e cardputer-adv -t upload
cd host
uv run --no-project --with pyserial==3.5 python sd_run.py --label probe --steps info,freq
uv run --no-project --with pyserial==3.5 python sd_run.py --label fmt --steps format,info          # formats only when FAT does not mount
uv run --no-project --with pyserial==3.5 python sd_run.py --label run \
    --steps hz:20000,bench,outbox,rename,rec:60:2048:4:1000,layout,tree
cd ../../app && ~/.local/bin/pio run -e cardputer-adv -t upload && cpa pause 1 && cpa status
```

The device also has a few diagnostic commands: `!sdrawinit <variant…> [interval ms]` (raw init; variants in `sd_rawinit.cpp`), `!sdclear`, `!sdget <path> <offset> <length>`.

## 11. 2026-10-21 addendum: the sequential-write EIO is a driver problem (not waiting for the card to finish busy after a multi-block write), not the card ✅

> Follows up on `!xfermk 9375` → `errno 5` in [m3-device-gates.md](m3-device-gates.md) §6. Same device (MAC 02:00:00:00:00:01), same card. The user authorised erasing the card, but in the end it was not erased: the file system stayed intact throughout (§11.5).
> Device side: `firmware/hwtest/src/sd_diag.cpp`, `sd_f3.cpp` (`!sddiag` `!sdid` `!sdblk` `!sdf3` `!sdfile` `!sdfatscan`); pure logic in `firmware/hwtest/lib/sd_diag_core` (`pio test -e native`); diagnostics and the fix in the driver are CPA patches 4 and 5 in `firmware/third_party/SD`; host side `firmware/hwtest/host/sd_diag.py`. Raw JSON: `firmware/hwtest/captures/sd-diag-*` (gitignored).
> Port etiquette: first `cpa pause 5400`; touch the device only once `lsof /dev/cu.usbmodem*` is empty. 5 hwtest uploads and 1 v0.2 upload, all `pio run -t upload`, G0 never pressed, the serial port only used the safe open sequence (03 §2), and the device never entered download mode.

### 11.1 Conclusions

| Question | Conclusion |
|---|---|
| Is it a capacity-faked / counterfeit card? | **No fake capacity found.** Two rounds of F3-style sampling (127 and 159 sectors, from 64 MiB up: every 2^k−1 and 2^k up to 2^29, 16 points in the last 1 GiB, the last sector 982,573,055, the rest evenly spread). Each sector was written with its own LBA and the round's random seed, the card was re-initialised with CMD0, and everything read back: **286/286 correct**. No aliasing (wrap-around), no stale data, no all-0 / all-FF ✅. ⚠️ This is sampling, not filling the whole card (503 GB at 1.6 MB/s would take about 87 hours); it only rules out the common "capacity wrap-around" kind of fake |
| Is it a bad sector? | **No.** The two failing ranges (32 sectors each from LBA 367,520 and 367,328) were written alone with CMD24 block by block and with one CMD25, then read with CMD17 block by block and with one CMD18, at 4 / 10 / 20 MHz each: **18/18 runs, all 32 sectors correct** ✅ |
| Is it FAT / cluster allocation? | **No.** What failed was a 32-sector CMD25 in the data area, and the data response for all 32 blocks was "accepted" (0x05). A scan of the whole FAT after all the EIOs: 9 clusters used (root directory, 6 directories and 2 files, 1 each), free count matching FSINFO, no lost clusters ✅ |
| Root cause | **A driver bug**, in the upstream code of core 2.0.17's `SD` library. After sending the Stop Tran token (0xFD) of a multi-block write, the driver releases CS right away and then sends CMD13 to check status. But the card stays busy for a while after 0xFD (up to 46.5 ms measured), and the busy check when CMD13 selects the card is not reliable here, so CMD13 is sent during the busy period. The "R1" read back is then 0x07, and from that the driver decides the write failed → FatFs `FR_DISK_ERR` → `errno 5`. In fact the card had accepted the data: asking CMD13 again right after returns R1 0, status 0 (4/4) |
| Fix | `third_party/SD` CPA patch 5: after sending 0xFD, give one more byte of clock and wait for the card to finish busy before releasing CS. **After the fix, a 9.6 MB single-file write + read-back comparison passed 3/3**, 16 MB × 5 was also all correct, and the driver logged 0 error events ✅ |
| This card | **Can stay in use**; the recording tests do not need a different card because of this. Provided the app uses the driver with patch 5: the app links `../third_party`, and the app in the current working tree builds with both `cardputer-adv` and `cardputer-adv-test` |

### 11.2 Evidence: before and after the fix (same firmware, first switched back to upstream behaviour with a flag, then with the wait turned on)

| Condition | Result |
|---|---|
| Upstream behaviour, `!xfermk 9375` (new file) | EIO, after writing 8,273,920 B. Driver log: CMD25 writing 32 blocks from LBA 367,328, all 32 accepted; longest busy 31 ms, no timeout; then CMD13 "R1" = **0x07** |
| Upstream behaviour, `!xfermk 9375` × 4 (overwriting with `O_TRUNC`, same clusters) | **4/4 EIO**, all stopping at 8,372,224 B (the same number as in m3-device-gates §6). Each time CMD25 writing 32 blocks from LBA **367,520**, all accepted; longest busy 64 ms; CMD13 "R1" = **0x07**; **asking CMD13 again right away returns R1 0, status 0** |
| Wait for busy to end before releasing CS, `!xfermk 9375` × 4 | 4/4 OK, 9,600,000 B, about 6.0 s each; 0 error events |
| Same, `!sdfile 9375 3` | 3/3 written and read back byte-identical |

- **Why 0x07** (inference): 0x07 means "idle + erase reset + illegal command", yet the card had not reset, and asking again was fine. It looks more like the busy signal ending right in the middle of the byte read as R1: DO still low for the first 5 clocks, high for the last 3. Under this explanation, most of the time the byte read is 0x00 (still busy), which passes as R1 = 0; it only fails when busy ends exactly inside that one byte. So failures are rare, yet deterministic for the same write sequence.
- **Why upstream's select did not wait for busy** (📚 the multi-block write timing for SPI mode in the SD spec, quoted from memory, not checked word for word): after the Stop Tran token the card waits one more byte (Nbr) before pulling DO low. Upstream releases CS right after 0xFD, so the first byte read on the next select may be that 0xFF, which is taken as "not busy". Patch 5 first adds that one byte, then waits for busy to end.
- **Why the failure position follows cluster allocation and not write speed**: the failing LBAs write perfectly well on their own (§11.1). So what matters is the card's internal state during a long run of sequential writes, for example a point where it has to tidy up or commit, after which it stays busy especially long after Stop Tran; it is not any particular sector. Overwriting with `O_TRUNC` keeps the same clusters, so the write sequence is the same and it stops at the same place every time. Every 16 KB write ends with its own Stop Tran, so writing more slowly or fsyncing more often does not avoid it.
- **Cost of the fix**: after Stop Tran the card has to finish busy anyway, and the next command's select would wait too. Measured: 9.6 MB takes 5.8–6.1 s (about 1.6 MB/s), the same as before the fix. Across 1,758 multi-block writes, the longest busy after Stop Tran was 46.5 ms.

### 11.3 Regression after the fix ✅ (final firmware: patch 5 always on, no flag)

| Command | Result |
|---|---|
| `!sdfile 9375 3` (acceptance: 9.6 MB single-file write + read-back, 3 times) | **3/3**, write 6.0–6.1 s, read 6.3 s each, byte-identical |
| `!xfermk 9375` × 3 (the original repro command) | 3/3 OK, 9,600,000 B |
| `!xfermk 8192`, `!xfermk 4096` | Both OK (before: 8 MB failed 2/2, 4 MB failed 1 of 4) |
| `!xfermk 9375 0 1024`, `!xfermk 9375 0 32` (fsync every 1 MB / every 32 KB) | Both OK (both failed before) |
| `!sdfile 16384 5` (16 MB × 5 = 80 MB) | **5/5**, write about 10.4 s, read about 11.0 s, byte-identical |
| All of these together | About 10,000 multi-block writes, **0** driver error events |

### 11.4 Card identity (`!sdid`)

| Register | Content |
|---|---|
| CID | Manufacturer ID 0x09, OEM "AP", product name "MB78B", revision 1.3, manufactured 2022-08, CRC correct. The serial number stays only in the captures and is not written into the docs. Which brand 0x09 / "AP" belongs to was not verified, but it is not the ID of a major maker such as SanDisk (0x03), Samsung (0x1B) or Kioxia (0x02) |
| CSD | Version 2.0 (SDHC/SDXC), 982,573,056 sectors (503.1 GB, labelled 512GB), TRAN_SPEED 0x32 (25 MHz), CCC 0x5B5, 512 B read/write blocks, no write protection, CRC correct |
| SD Status (ACMD13) | Class 10, UHS U3, AU 4 MB (UHS AU also 4 MB), ERASE_SIZE 1, ERASE_TIMEOUT 12, ERASE_OFFSET 3; video speed class 0, application performance class 0 (no V30 / A1 marking) |
| SCR (ACMD51) | `0205848401000000`: supports SD spec 3.0 and 4.0, bus width 1 / 4 bits, **SD_SECURITY = 0** (no CPRM). SDXC cards usually report 4, so 0 is unusual; this alone does not decide whether the card is genuine, and F3 sampling found no fake capacity |

- The card's U3 / Class 10 rating cannot be verified at SPI 20 MHz; the bus itself is the bottleneck (about 1.6 MB/s).
- **Recommendation**: no need to replace this card. The "only one card tested" risk in §9 remains, so it is still worth testing 2–3 cards of other brands, ideally including a sub-32GB card that ships as FAT32, such as a 32GB SanDisk or Samsung. This is unrelated to this bug.

### 11.5 Impact on v0.2 / v0.3, and wrap-up

- **v0.3 (M3-A recording)**: recording writes 4 KB at a time, which is an 8-sector CMD25, exactly what hits this bug, and it would be treated as the "SD locked up" case in 11 §4.3. Patch 5 is in the shared `third_party/SD`, so the M3 app picks it up when rebuilt. The line "do not run v0.3 recording tests on this card until this is understood" in m3-device-gates.md §1 and §6 can be lifted; that document does not belong to this package, so it is left for the Lead / M3-H to change.
- **v0.2 release** (currently flashed on the device, `v0.2.0` = HEAD 702c0ec): uses the upstream driver without patch 5. v0.2 writes only small files (outbox items of about 300 B, plus FAT and directory entries); each `f_write` is under one sector and goes through CMD24 single-block writes, not the broken CMD25 end path, so the risk is low. The release is not changed.
- **Single-block writes (CMD24)**: the card starts busy right after the data response, and upstream does wait for it when CMD13 selects. The 286 single-block writes in this F3 run, the 192 single-block writes of `!sdblk … s`, and the FAT / directory-entry updates of every file write produced no error events. Patch 5 changes only the end path of multi-block writes.
- **Diagnostic code went into the app**: patch 4 only records numbers (about 150 B of static RAM), and only sends one extra CMD13 after a CMD13 status check fails; there are no extra bus operations on the normal path.
- **Card contents**: the card was neither erased nor reformatted. F3 only writes free clusters above 64 MiB. The two ranges written by `!sdblk` belonged to `/cpa_hwtest/xfer.bin` at the time, and that file was finally deleted by `!gateclean`. At the end, `!sdinfo` matched the start: `/cpa/{outbox,rec,logs,cfg}` plus Launcher's `/config.conf`, 2 files, 6 directories, 892 B; a full FAT scan shows 9 clusters used, matching FSINFO.
- **End state**: v0.2 release flashed back. To avoid picking up the M3 changes in the working tree, `git archive v0.2.0 firmware` was exported to a temp directory and `pio run -e cardputer-adv -t upload` run there; the exported driver is byte-identical to v0.2's. Then `cpa pause 1`, and `cpa status` showed `link: ready … fw=cpa-fw/0.2.0 pending=0 store=sd launcher=no`.
- ⚠️ The NOTICE at the repo root lists only patches 1–3; patches 4 and 5 still need to be added (not this package's job).

### 11.6 Reproduce

```bash
cpa pause 5400                                   # lsof /dev/cu.usbmodem* should be empty
cd firmware/hwtest && ~/.local/bin/pio test -e native && ~/.local/bin/pio run -e cardputer-adv -t upload
cd host
P="uv run --no-project --with pyserial==3.5 python sd_diag.py"
$P --label id  '!sdid' '!sdf3 128' '!sdf3 160 131072'                     # identity + F3 sampling
$P --label eio '!sddiag reset' '!xfermk 9375' '!sddiag'                     # after the fix: ok=1, events=0
$P --label blk '!sdblk 367520 32 m 4000' '!sdblk 367520 32 s 20000'         # single-block / multi-block comparison
$P --label acc '!sdfile 9375 3' '!sdfile 16384 5' '!gateclean' '!sdfatscan' '!sdinfo'
# flash the v0.2 release back (export from the tag when the working tree has M3 changes)
mkdir -p "$TMPDIR/v020" && git archive v0.2.0 firmware | tar -x -C "$TMPDIR/v020" && cd "$TMPDIR/v020/firmware/app" \
  && ~/.local/bin/pio run -e cardputer-adv -t upload && cpa pause 1 && cpa status
```

`!sddiag`'s `diag` field: `first` / `last` are the first and last error events in the driver (phase, command number, token, start sector and block count of the failing read or write); `busy_hist` is the distribution of busy waits (<1, <10, <50, <100, <250, <500, ≥500 ms); `rechecks` / `rechecks_clean` are how many times CMD13 was asked again after a failure, and how many of those returned 0/0; `stop_busy_max_us` is the longest busy after Stop Tran. The before-fix comparison (upstream behaviour) used a temporary flag that has been removed from the final code.
