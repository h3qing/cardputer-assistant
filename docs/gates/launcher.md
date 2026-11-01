**English** · [中文](launcher.zh-CN.md)

# Launcher compatibility research (v0.2 gate: install and switch with bmorcelli Launcher + microSD storage)

> 2026-10-20, source and docs only, **no device connected**. Goal: from v0.2 on, this firmware can be installed and switched by [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher), and its main storage moves to microSD.
> Marks: **[src]** from reading source or official docs · **[calc]** numbers derived from source rules · **[todo]** needs real-device verification (listed as gates in section 8).
> Versions read: Launcher `main` = `e8cc4df` (2026-10-16, includes PR #448), latest release `2.9.1` (2026-09-04). Launcher's bootloader comes from the `launcher_keyboot` branch (`cf56f6a`) of [bmorcelli/myLibBuilder](https://github.com/bmorcelli/myLibBuilder); 2.9.1 uses the `launcher` branch (`386ffd3`). In links below, `L/` means `https://github.com/bmorcelli/Launcher/blob/e8cc4df/`.

## 0. Summary of conclusions

| Question | Conclusion |
|---|---|
| 1 Partition table | Launcher itself only takes `nvs 0x9000/0x5000`, `otadata 0xE000/0x2000`, `app0` (**test** subtype) `0x10000/0x150000`, `coredump 0x160000/0x10000`. The rest, **0x170000–0x800000 (6,881,280 B, about 6.56 MiB), is all dynamic space**: each installed firmware gets a new `ota_N` partition, sized to the image size rounded up to 64KB. Data partitions (SPIFFS/LittleFS/FAT) are created by the **label** in the firmware's own partition table; an empty data partition gets only **0x70000 (448KB)** by default on 8MB models |
| 2 What files it installs | Both app-only `.bin` and merged images are accepted. A merged image is **read only as metadata**: from the partition table at file offset 0x8000 it finds the first app entry and data entries, and writes only the app and data partition contents; **the bootloader, the partition table itself, nvs/otadata/phy are not written**. After writing it checks with `esp_image_verify`. No extra header or manifest is needed; the display name and partition label come from the **file name** |
| 3 How to get back to Launcher | Launcher uses **its own modified second-stage bootloader**: power-on reset (power switch, RST key, chip-level brownout reset) and deep-sleep wake go to Launcher; `esp_restart()`, panic, watchdog and USB-triggered resets go straight back to the currently selected firmware. The ADV has no "hold a key to enter Launcher" (the keyboard is on I2C, which the bootloader cannot read; G0 is excluded). The firmware **needs to do nothing**, as long as it does not touch otadata or the partition table, and does not erase the whole NVS |
| 4 Install Launcher / go back to full flashing | Web flasher or esptool, writing `Launcher-m5stack-cardputer.bin` to **0x0**. Back to our own full flashing: `pio run -t upload` as usual, or write our `-full.bin` to 0x0. This overwrites the bootloader, partition table and Launcher itself; NVS stays as is |
| 5 SD | Same pins as ours: CS12 / SCK40 / MISO39 / MOSI14. Launcher normally uses Arduino `SD`'s default **4 MHz**; in USB drive mode it goes up to 40 MHz (falls back to 20 MHz if the check fails, then back to 4 MHz). The card must be FAT32 + MBR. Launcher uses `/config.conf`, `/downloads/`, `/bkp/`, `/Launcher_error.log` on the card; putting our things under `/cpa/` avoids conflicts. **Our toolchain (IDF 4.4) does not support exFAT**; FAT32 only |
| 6 Plan | **No separate firmware env**: one binary, detected at runtime. Rename the LittleFS partition from `spiffs` to a dedicated label `cpafs` (same offset); the released `-full.bin` can be flashed whole or handed to Launcher to install. Main storage on SD, `cpafs` only as a fallback. Biggest trap: in Launcher the `spiffs` label is **shared by several firmwares** (Bruce uses it too), and our current `LfsStore` formats a partition it cannot mount |

## 1. Partition table (8MB ADV)

**Launcher's own partition table** [src] `L/support_files/custom_8Mb.csv#L3-L6`, selected by `L/boards/m5stack-cardputer/platformio.ini#L14`. The same env covers both Cardputer and ADV: it detects the TCA8418 at runtime, see `L/boards/m5stack-cardputer/interface.cpp#L98-L120`.

```
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xE000,   0x2000
app0,     app,  test,     0x10000,  0x150000   ← Launcher itself (test subtype on ESP32-S3)
coredump, data, coredump, 0x160000, 0x10000
(0x170000–0x800000 free, allocated dynamically on demand)
```

- **Where installed firmware goes**: `launcherPartitionCreateOtaApp` takes the next free `ota_N` subtype and finds a slot in free space, sized `alignUp(image size, 0x10000)` (`L/src/partition_table_model.cpp#L666-L697`). So **the partition fits the image exactly, with no headroom for OTA self-update**.
- **Largest possible install**: the theoretical limit is the remaining free space. With nothing installed it is 6,881,280 B [calc]. The install layout tries these in order and stops at the first success: create directly in free space → create the data partition first, then the app → delete orphaned data partitions. If none works, the user chooses between "reuse a partition / repartition / delete a firmware and its data" (`L/src/partition_install_layout.cpp#L73-L137`, `#L139-L411`).
- **Constraints**: the generated table must keep a FACTORY or TEST partition and otadata, and apps are aligned to 64KB (`L/src/partition_table_model.cpp#L274-L352`). `nvs 0x9000/0x5000` and `otadata 0xE000/0x2000` are hard-coded as "required system partitions" (`L/src/idf/idf_update.cpp#L25-L28`).
- **Headroom note**: the beta Launcher's app is 1,363,536 B, already 99% of 0x150000; it may switch to `custom_8Mb2.csv` (app0 0x170000) later. Free space would then shrink by 128KB [calc].

**Data partitions**: only created when the firmware's merged image carries its own partition table; see `L/src/sd_functions.cpp#L851-L917`.

| Entry in the image | Size of the partition Launcher creates (8MB: `DEFAULT_SPIFFS_SIZE = 0x70000`, `THRESHOLD = 0x500000`) | Source |
|---|---|---|
| SPIFFS/LittleFS, **empty content** (the file already ends at that offset, or the first 16 bytes are all FF or all 00), declared size ≤5MB | **0x70000** (whatever the label) | `#L858-L862`, `partition_table_model.cpp#L188-L198`, board ini `#L29` |
| Non-empty, label ≠ `spiffs`, declared > 0x70000 | Created at the declared size, contents copied | `#L863-L864` |
| Non-empty, label = `spiffs`, declared > 5MB | Uses the largest remaining free segment | `#L865-L866` |
| FAT, label `sys`/`system` | Copied whole by content; other FAT labels default to 0x70000 | `partition_install_layout.cpp#L36-L46` |
| A copied LittleFS image smaller than the original | block_count in the superblock is rewritten | `L/src/littlefs_patch.cpp#L155` |

**Who owns a data partition** (the most important finding this time) [src]:
- Found and reused by **label**: an existing partition with the same name, same subtype and enough size is used directly (`partition_table_model.cpp#L778-L792`). So **a partition labelled `spiffs` is shared by all firmwares**. Bruce uses `LittleFS.begin(false, "/littlefs", n)`, whose default label is exactly `spiffs` ([Bruce `src/core/sd_functions.cpp#L33-L36`](https://github.com/BruceDevices/firmware/blob/a59213f/src/core/sd_functions.cpp#L33-L36)).
- When a firmware is deleted, Launcher **erases and deletes its associated data partitions** (asking first whether to back up), **except `spiffs`** (`L/src/app_registry.cpp#L404-L519`, see `#L427` and `#L471-L480`).
- When deleting or compacting, Launcher **moves** other partitions and carries their data along (`launcherPartitionCompact` / `MigrateMovedData`). So a firmware **must never cache flash offsets**; look up by label at every boot.

**Can other installed firmwares use these data partitions**: yes, if they know the label. There is only one NVS, shared by all firmwares.

## 2. What files Launcher installs

The entry point is `updateFromSD` (`L/src/sd_functions.cpp#L801-L958`). Files uploaded through the WebUI are saved to SD first, then go through this entry (`L/src/webInterface.cpp#L2229`). Online install uses the same logic (`L/src/onlineLauncher.cpp#L500-L600`).

1. **File offset 0x8000 is not `AA 50 01`** → treated as an app-only image. The image length is measured from the start of the file by segment headers (`#L593-L621`, including the appended SHA-256) and written into a new `ota_N`. **No data partition is created**.
2. **There is a partition table at 0x8000** → treated as a merged image ("flash image" layout: bootloader at 0x0). Entries are read one by one:
   - the first app entry with subtype factory, ota_0 or test (`#L829-L849`); its **file offset** is where the app starts;
   - data entries are handled by the rules in section 1;
   - nvs, otadata, phy, coredump entries and the bootloader are all **ignored**;
   - later entries such as ota_1 are ignored too (only the first app is taken).
   - **Conclusion**: Launcher neither accepts nor writes our partition table; the table only tells it "where the app is, which data partitions are needed, and their labels".
3. **Check after writing**: the first byte must be 0xE9, and the whole image must pass IDF 5.5's `esp_image_verify` (chip ID, segment checksums, SHA-256, chip revision range) (`L/src/idf/idf_update.cpp#L72-L80`, `#L310-L330`). The `firmware.bin` PlatformIO produces by default already satisfies this; **no extra header or metadata is needed**.
4. **Names**:
   - partition label = the first 6 lowercase letters or digits in the file name, padded with `0` if fewer than 6, with the last character replaced by a digit on a name clash (`partition_table_model.cpp#L728-L764`);
   - display name = the file name without extension, at most 20 characters, stored in NVS `l_apps` (`L/src/app_registry.cpp#L326-L345`).
   - Example: the current `cpa-fw-v0.1.0-cardputer-adv-full.bin` becomes label `cpafwv` and display name `cpa-fw-v0.1.0-cardpu`.
5. **Upgrade semantics**: as long as there is space, installing again **creates a new partition**, and the old version stays in the list (`partition_install_layout.cpp#L89-L101`). Only when space runs out do overwrite options like "Use <label> partition" appear. Another way is the serial command `flash overwrite <name> <size>`, which reuses the original slot (`L/src/serial_console.cpp#L36-L41`; the tool is `L/tools/serial_flasher.py`).
6. **Online catalog** (optional, later): the LauncherHub manifest supports `install.sources.{firmware,bootloader,partitions,data}`; Launcher downloads them, merges them into one file on SD, then goes through the flow above (`L/src/onlineLauncher.cpp#L1197-L1215`).

## 3. How to get back to Launcher

**Mechanism** [src]: Launcher's merged image carries a second-stage bootloader with a modified `bootloader_start.c`. After IDF has picked a partition as usual, it adds one more decision:

| Reset reason (ESP32-S3 code) | 2.9.1 (`launcher` branch `#L105-L117`) | main/beta (`launcher_keyboot` branch `#L103-L165`) |
|---|---|---|
| Power-on `0x01` (power switch, RST key pulling CHIP_PU; **chip-level brownout reset and super WDT are also 0x01**) | Enter Launcher | Enter Launcher |
| Deep-sleep wake `0x05` | Enter Launcher | Enter Launcher; if NVS has `launcher/DDLB=1`, go straight back to the firmware |
| Software reset `0x03`/`0x0C`, watchdog, panic, USB-UART/JTAG reset `0x15`/`0x16` | Go straight to the firmware selected in otadata | Same |
| Holding the GPIO set by `LauncherOnKey` at boot | — | Force Launcher. But this menu is only shown on boards that define `LAUNCHER_GPIO_KEYS`; Cardputer does not; G0 is excluded |

Reset codes: IDF [`soc/esp32s3/include/soc/reset_reasons.h#L38-L61`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/soc/esp32s3/include/soc/reset_reasons.h#L38-L61). The bootloader reads settings from NVS with `nvs_bootloader_read`, from the `launcher` namespace in the `nvs` partition, keys `DDLB`/`LauncherOnKey`/`LauncherKeyLvl`.

**Once in Launcher** (`L/src/main.cpp#L363-L451`):
- It shows a splash screen: on main for 1 + `bootTimer` seconds, default 5, adjustable 1–5; on 2.9.1 fixed at 5 seconds.
- Press **Enter or G0** on the splash → Launcher menu;
- press a digit 1–9, 0 → boot the Nth installed firmware directly;
- press any other key → boot an installed firmware (a picker appears when more than one is installed);
- press nothing → `ESP.restart()` (`L/boards/m5stack-cardputer/interface.cpp#L393-L404`) into the currently selected firmware.
- With "Boot to Launcher" on in settings, it always stays in Launcher.

**What this means for this project**:
- Every cold boot waits about 5 seconds more, and because Launcher uses `ESP.restart()`, the reset reason our firmware reads is always `software`, never `poweron`. `hello.rst` is display-only anyway; reboot detection relies on the boot id and is not affected.
- The daemon's `debug.reboot` and `esp_restart()` both **stay in our firmware**, which is good.
- Pressing G0 (our quick-note key) at boot enters the Launcher menu. The user manual must say so.
- **Deep sleep means going back to Launcher**. Power saving in v0.2/v0.3 can only use light sleep (PRD §5.2 says light sleep too), or accept an extra Launcher splash after waking.
- The firmware **needs no cooperation at all**. Just follow: do not write otadata, do not call `esp_ota_set_boot_partition`, do not change the partition table, do not call `nvs_flash_erase`. M5Apps does the same (everything except software reset goes back to factory, see [M5Apps `bootloader_components/main/bootloader_start.c#L49-L55`](https://github.com/d4rkmen/M5Apps/blob/5f9f4aa/bootloader_components/main/bootloader_start.c#L49-L55)). Bruce does nothing either: its "power off" is `esp_deep_sleep_start()`, and the next wake naturally goes back to Launcher ([`power_commands.cpp#L5-L9`](https://github.com/BruceDevices/firmware/blob/a59213f/src/core/serial_commands/power_commands.cpp#L5-L9)).
- Optional: add "Back to Launcher" to the system page, implemented as a 1-second timer deep sleep; shown only when Launcher is detected (6.3). [todo] Test once each on 2.9.1 and beta.

## 4. Installing Launcher, and going back to our own full flashing

**Installing Launcher**:
- Web flasher <https://bmorcelli.github.io/Launcher/>, device "Cardputer and Cardputer ADV" (id `m5stack-cardputer`). It uses ESP Web Tools 10.1.1 to write the merged image at **offset 0**, with `eraseAll:false`; the "Erase before flashing" checkbox is unchecked by default (`WebPage` branch `src/webflasher.ts#L1030-L1033`, `#L589-L615`).
- The release is 2.9.1 (`Launcher-m5stack-cardputer.bin`, 1,422,720 B). Only the beta (`releases/download/beta/…`, 1,429,072 B, built 2026-10-15 from `e8cc4df`) has PR #448's macOS USB drive fix and DDLB.
- Command line (local toolchain, no need to press G0: USB-JTAG enters download mode automatically, see 03 §2):
  ```bash
  ~/.local/share/uv/tools/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/cu.usbmodem1101 -b 460800 write_flash 0x0 Launcher-m5stack-cardputer.bin
  ```
  The hard reset esptool does after flashing is a USB reset (0x15), not a power-on reset. At that point the table has no ota partition yet, so IDF falls back to the test partition, and it still enters Launcher [src] ([IDF `bootloader_utility.c#L622-L623`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/bootloader_support/src/bootloader_utility.c#L622-L623)).
- Before switching: confirm the pending-sync count is 0 (v0.1's outbox is in LittleFS and cannot be found after the table changes); optionally back up the whole flash first: `read_flash 0 0x800000`, with `-b 2000000`.

**Back to full flashing**: `pio run -e cardputer-adv -t upload`, or write our `-full.bin` to 0x0. This writes the bootloader (0x0), partition table (0x8000), boot_app0 (0xE000) and app0 (0x10000); Launcher's bootloader, partition table and body are all overwritten.
- **NVS (0x9000) is kept as is**; both layouts are the same. It will still hold Launcher's `launcher`, `l_apps`, `l_wifi` (encrypted WiFi passwords), `uiflow` and NimBLE bond namespaces. To clean it, run `erase_region 0x9000 0x5000` first. That regenerates our epoch, which is safe by the design in 05 §5.9.
- The data area may hold old app bytes. Our `LfsStore::begin()` formats when it cannot mount, provided the label is already our own (see 6.2).
- From full flashing back to Launcher: web-flash again, then install our firmware from SD.

**Development loop**:
- `pio run -t upload` wipes out Launcher. Keep using full flashing for daily development; test Launcher mode only in the Launcher gates and HIL.
- To swap firmware quickly under Launcher, use Launcher's serial `flash overwrite`. Entering Launcher only works by pressing the RST key (power-on reset), because a USB reset goes straight to the firmware. Step 1 of `serial_flasher.py` cannot do this on S3 [calc].

## 5. SD card

- **Pins and speed** [src]:
  - `SDCARD_CS=12 SCK=40 MISO=39 MOSI=14` (`L/boards/m5stack-cardputer/platformio.ini#L54-L57`), matching hwtest's `kSd*`. The screen is on a different SPI bus (MOSI35/SCLK36), not shared.
  - Mounted with `SD.begin(cs, sdcardSPI)`, default 4 MHz (`L/src/sd_functions.cpp#L33-L81`).
  - In USB drive mode it tries 40 MHz, then 20 MHz: it reads 16 sectors from the card, and the CRC must match what was read at 4 MHz; otherwise it falls back to 4 MHz (`L/src/massStorage.cpp#L330-L400`).
  - Measured (by the author and the PR author on an ADV): USB drive read about 277 KB/s at 4 MHz; after PR #448 read about 630 KB/s, and macOS took 13.2 seconds to mount.
- **ADV special handling** [src]:
  - Before initializing SD, Launcher sets GPIO 3/4/5/6/13/15 to output high (`interface.cpp#L78-L95`), with the comment "GPIO5 HIGH for SD card compatibility". These are the ADV expansion port GPIOs (Bruce's ini lists them as "ADV 3…15").
  - Probably to keep the chip selects of the same SPI bus on an expansion cap deselected. Not needed on a bare device. [todo] Verify with an expansion cap attached.
- **Card format**:
  - Launcher recommends SDHC, ≤32GB, FAT32, MBR, not GPT (`L/README.md#L22-L27`); since 2.8.0 it also supports exFAT.
  - But **our toolchain A's FatFs setting is `FF_FS_EXFAT 0`** (`~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/include/fatfs/src/ffconf.h:255`), so **FAT32 only**; cards over 32GB must also be formatted FAT32.
- **Paths Launcher uses on the card** [src]:
  - `/config.conf` and `/config.conf.old`;
  - `/downloads/` (download folder `dwn_path`, configurable; contains `downloaded.json`);
  - `/bkp/` (`backupData.json` plus `{appNum}-{appName}/` backup folders);
  - `/Launcher_error.log` (log during USB drive mode, written to the card only after leaving USB drive mode).
  - The file browser shows only `.bin` by default.
- **Our suggested layout**:
  - Everything under `/cpa/`: `/cpa/outbox/`, `/cpa/rec/` (v0.3 recordings), `/cpa/archive/`, `/cpa/fw/` (`.bin` files for Launcher to install);
  - Do not touch Launcher's paths. The two firmwares never run at the same time, so there are no concurrency issues.
- **Formatting this card (authorized: the card is dedicated to this project and may be wiped)**: on the Mac, first identify the card's disk number with `diskutil list`, then run `diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN`. Check the disk number twice; a wrong one wipes another disk. v0.3's "plug in and transfer to the Mac" has two paths:
  - **Main path**: our firmware transfers over the CDC serial port itself. [todo] Measure HWCDC sustained throughput.
  - **Fallback**: enter Launcher by hand → USB (SD->USB) USB drive mode. This needs Launcher ≥ `09ffc59` (PR #448, merged into main 2026-10-15), currently only in beta. Issue #424 is still open.
- For on-board SD measurements, see the parallel `docs/gates/sd-card.md`.

## 6. `cardputer-adv-launcher`: requirements and suggested plan

### 6.1 Suggestion: one binary, runtime detection; no separate firmware env

One firmware, one partition table, for both full flashing and Launcher install (Bruce's Cardputer build does exactly this: one merged image for both uses, see [`build.py`](https://github.com/BruceDevices/firmware/blob/a59213f/build.py), [`custom_8Mb.csv`](https://github.com/BruceDevices/firmware/blob/a59213f/custom_8Mb.csv)). Consider plan B (6.6) only if `cpafs`'s 448KB proves too small in practice.

### 6.2 Hard requirements

| # | Requirement | Reason |
|---|---|---|
| R1 | Rename the LittleFS partition to the dedicated label **`cpafs`** (≤15 characters), subtype still `spiffs` (0x82): IDF 4.4's `gen_esp32part.py` does not know `littlefs` (0x83), and Launcher accepts both. The full-flash partition table is `default_8MB.csv` with only this one label changed, **same offset and size** (0x670000/0x180000), so v0.1's data still mounts directly after the upgrade | `spiffs` is shared by several firmwares (section 1). Today `kLabel = "spiffs"` at `lfs_store.cpp:19` plus `begin(true)` would mount Bruce's filesystem, or even format it |
| R2 | Mount and format only `cpafs`. If the partition does not exist, disable internal storage (`E_STORAGE`); **never fall back to `spiffs`** | Same as above |
| R3 | Do not cache any flash offset; look up by label with `esp_partition_find_first` at boot | Launcher moves partitions |
| R4 | Do not write otadata or the partition table, do not call `nvs_flash_erase`, use NVS only in the `cpa` namespace, and as little as possible | Section 3. Also, core 2.0.17 **erases the whole NVS partition** when NVS is full (`NO_FREE_PAGES`) (`esp32-hal-misc.c:249-262`), wiping Launcher's settings too |
| R5 | What goes to Launcher must be the **merged image** (`-full.bin`, with the partition table inside), so Launcher creates `cpafs`. App-only also runs, but with no internal storage, SD only | Section 2 |
| R6 | Storage is SD first, `cpafs` as fallback; fallback capacity is computed from the actual partition size | The capacity problem in 6.4 |
| R7 | SD is FAT32 only; on mount try 20–25 MHz first, read a block of data and compare checksums, fall back to 4 MHz on mismatch (as Launcher does) | Section 5 |
| R8 | No deep sleep; always reboot with `esp_restart()` | Section 3 |

### 6.3 Runtime detection (IDF 4.4 APIs called only in `hal/`; decision logic written as pure functions in core, developed with TDD in the native env)

```cpp
// hal：收集事实，不做判断
PartFacts hal_part_facts() {
  const esp_partition_t* run  = esp_ota_get_running_partition();
  const esp_partition_t* test = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                         ESP_PARTITION_SUBTYPE_APP_TEST, nullptr);
  const esp_partition_t* fs   = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                         ESP_PARTITION_SUBTYPE_ANY, "cpafs");
  return PartFacts{test && run && run->address != test->address,  // 在 Launcher 下运行
                   fs ? fs->size : 0u,                              // 0 表示没有内部存储
                   run ? run->size : 0u};
}
// core（纯函数，不可变）：StoragePlan plan_storage(const PartFacts&, const SdFacts&);
//   → primary(Sd|Lfs|None)、fallback、lfs_item_cap、show_back_to_launcher
```

(Code comments: `hal` collects facts and makes no decisions; the first field means "running under Launcher"; `0` means no internal storage; `core` is a pure, immutable function.)

Basis:
- Launcher's table must have a FACTORY or TEST partition (`partition_table_model.cpp#L342-L345`); on ESP32-S3 it uses TEST (`L/src/partitioner.cpp#L1042-L1046`). Our own table has no test partition, so "there is a test partition, and the running one is not it" is enough to conclude "under Launcher".
- All these symbols exist in IDF 4.4 (`esp_partition.h:82,96`, `esp_ota_ops.h:214`).
- The system page shows: boot mode (launcher or direct), this app partition's label and size, `cpafs` size, SD status. **No protocol change**.

### 6.4 What is affected by the change (item by item)

1. **`LfsStore`'s label**: see R1, R2. Must change.
2. **`cpafs` gets smaller, and the shard directories eat the space** [calc]:
   - Under Launcher an empty partition is only 0x70000, i.e. 112 blocks of 4KB.
   - `LfsStore`'s 32 shard directories each take a pair of metadata blocks, **64 blocks, 256KB** in total; plus 8 blocks for the root, `/outbox`, `/outbox/bad`, `/cfg`.
   - About 40 blocks remain. Minus the "at least 64KB left" threshold (16 blocks), **only about 20–40 large items fit**, far fewer than the 200 in 05 §5.9.
   - Remedy: SD as main storage (R6); the fallback cap is computed from `cpafs`'s actual size and output by `plan_storage`. [todo] Measure.
3. **App-only images have no `cpafs`**: see R5.
4. **NVS and the boot counter**: work as usual.
   - In Launcher's table NVS is fixed at 0x9000/0x5000, same as ours. The NVS page format version is 0xfe on both sides (IDF [v4.4.7 `nvs_page.hpp#L50`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/src/nvs_page.hpp#L50), [v5.5.4 `nvs_constants.h#L33`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/nvs_flash/private_include/nvs_constants.h#L33)), so it does not trigger a `NEW_VERSION_FOUND` erase.
   - The counter only goes up by 1 when our firmware boots. With an old and a new version both installed, they share the `cpa` namespace, and IDs stay unique.
   - Launcher only erases its own namespace (`L/src/settings.cpp#L105-L106`), but at every boot it fills in about 19 `uiflow` keys (`#L383-L413`), which takes some NVS space.
   - If Launcher is reinstalled with the web flasher and "Erase before flashing" is checked, the epoch changes. By design this is safe.
5. **Reset reason**: cold boot is always `software` (section 3). Display only.
6. **Uploading during development**: `-t upload` wipes out Launcher (section 4).
7. **Upgrades**:
   - Installing a new version creates a new partition; the old version stays.
   - When the old version is deleted, Launcher **erases `cpafs` along with it**: `cpafs` is associated with every label that installed it, and deleting any one of them deletes it.
   - User flow: first bring pending sync to zero → delete the old version in Launcher (choose "Remove Without Backup") → install the new version. Another way is `flash overwrite`.
   - Restoring a backup may bring back entries that were already acknowledged. The daemon dedups by `(dev, id)` and drops them as duplicates; no problem.
8. **Launcher's console on the same USB port**:
   - Launcher prints `Press the button to enter the Launcher!` every 500ms, and parses any line it receives as a command, without echo.
   - Our JSON lines start with `{`, so they only get `ERR unknown command…` (`L/src/serial_console.cpp#L940-L1006`). The daemon treats lines not starting with `{` as noise (`daemon/src/cpad/protocol/codec.py:109`).
   - The risk: the daemon **must never** send a line starting with words like `reboot`, `partition`, `flash`, `wifi`. The current protocol meets this; keep it that way when the protocol changes.
9. **Toolchain**: Launcher's bootloader is IDF 5.5.4, our app is IDF 4.4.7. Bruce and other Arduino 2.x firmwares have always booted this way, but our image has not been verified in this combination yet. [todo]
10. **coredump**: core 2.0.17 enables `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`. On panic it writes into Launcher's coredump partition (0x160000); harmless.
11. **Size budget** [calc]:
    - Our app is now 841,808 B, in a 0xD0000 partition; plus `cpafs` 0x70000, 1,310,720 B in total.
    - Installed together with Bruce 1.16.1: Bruce's app is 4,101,424 B in 0x3F0000, plus its `spiffs` 0x70000. Then at most **1,835,008 B (about 1.75 MiB)** is left for our app.
    - Suggestion: keep the 2.5MB hard limit in CI; add a soft budget "warn above 1.75MB", and write it into 05 §5.13.

### 6.5 Work items (TDD, failing tests first)

| Item | Content | Tests |
|---|---|---|
| L1 | core `storage_plan`: pure function `plan_storage(PartFacts, SdFacts)` computing main storage, fallback storage and the LittleFS item cap | native: combinations of under Launcher with/without `cpafs`, with/without SD, different partition sizes, and the minimum 0x70000 |
| L2 | hal: implement `hal_part_facts()`; `LfsStore` switches to `cpafs`, returns false if the partition does not exist, does not format | device test: only `cpafs` is mounted, in both full-flash and Launcher states |
| L3 | Build: add `firmware/app/partitions/cpa_8MB.csv` (just `default_8MB.csv` with `spiffs` changed to `cpafs`); point `board_build.partitions` at it; rename the release `-full.bin` to a name that displays well in Launcher, e.g. `CPAssistant-<ver>.bin`, and note "for full flashing write to 0x0; or put it on SD and install with Launcher" | `check_budget.py` adds the soft budget; CI checks the CSV with `gen_esp32part.py` |
| L4 | SdStore: implement FileStore under `/cpa/` on SD. FAT renames are not atomic; the boot scan rules in 05 §5.9 plus a per-record checksum cover this | native: inject "non-atomic rename" with FakeStore; device: power-loss test |
| L5 | System page: show boot mode, partition label and SD status; optional "Back to Launcher" (needs G-L5 first) | ui tests |
| L6 | Docs: README describes install and upgrade (bring pending sync to zero → delete old version → install new version), and notes that pressing G0 at boot enters Launcher | — |

### 6.6 Plan B (only if 448KB proves too small)

Carry a `cpafs` image generated by mklittlefs in the merged image (`pio run -t buildfs`). Conditions: the label is not `spiffs`, the content is non-empty, and the declared size is larger than 0x70000. Then Launcher creates the partition at the declared size and copies the image in, and fixes block_count when shrinking (section 1).

Cost: either move `cpafs` right after the app (file about 4MB, and the full-flash layout changes), or accept a file of about 8MB. Install also copies 1.5MB more.

## 7. How other firmwares package for Launcher

- **Bruce** ([`a59213f`](https://github.com/BruceDevices/firmware/tree/a59213f)):
  - Publishes one merged image `Bruce-m5stack-cardputer.bin` (4,166,960 B); `build.py` merges the bootloader, partition table and app, starting at 0x0; it is used for both full flashing and Launcher install.
  - Table: nvs 0x6000, factory 0x4E0000, spiffs 0x300000. Installed in Launcher: the app is in ota_N, `spiffs` is empty and created at 0x70000, and **is shared**; NVS is Launcher's 0x5000 one.
  - Only 4MB boards have separate `LAUNCHER_*` envs (fewer features, smaller size); 8MB boards do not need them.
- **M5Apps** (d4rkmen): is itself a "Launcher-like" firmware, using its own factory partition plus a custom bootloader (everything except software reset goes back to factory). It and the bmorcelli Launcher are either/or; they are not stacked.
- **UIFlow2** (merged image on M5Burner):
  - Depends on FAT partitions `sys`/`vfs`; Launcher copies `sys` whole and gives `vfs` the default size (`partition_install_layout.cpp#L36-L46`).
  - The NVS defaults in its own image cannot be installed, so Launcher fills in the `uiflow` namespace for it at every boot (`settings.cpp#L383-L413`).
  - This shows indirectly: **a firmware's own NVS image is not written under Launcher; all NVS keys must be created by the firmware at runtime**. We already do this.

## 8. Gates awaiting real-device verification (need the device; add to 07-hil-checklist)

| Gate | Method | Pass criteria |
|---|---|---|
| G-L1 | Web-flash Launcher (beta), install the renamed `-full.bin` from SD | Boots; `hello` normal; system page shows launcher, `cpafs` = 0x70000 |
| G-L2 | Press the RST key, flip the power switch, run `debug.reboot`, run `pio`'s USB reset | The first two enter the Launcher splash; the last two go straight back to our firmware |
| G-L3 | Install Bruce 1.16.1 and switch between the two firmwares 10 times | Our outbox, epoch and boot count are intact; Bruce's `spiffs` is untouched |
| G-L4 | `cpafs` capacity: disconnect the daemon, keep saving until refused | Record the count; fill it back into 6.4 item 2 |
| G-L5 | System page "Back to Launcher" (timer deep sleep), once each on 2.9.1 and beta | Enters Launcher; with DDLB=1 returns to our firmware |
| G-L6 | Upgrade following the flow in 6.4 item 7 | No acknowledged data lost; all duplicate items the daemon receives are judged duplicate |
| G-L7 | SD mount at 20 and 25 MHz with checksum compare (bare device, and with expansion cap and GPIO5 high) | Record the clock actually used and the throughput |
| G-L8 | Launcher's USB drive mode (beta) mounts this card on macOS 26 | Mounts, within ≤30 seconds |

## 9. Evidence index

- Launcher source (`e8cc4df`): <https://github.com/bmorcelli/Launcher/tree/e8cc4dfb35b63aa3dc56eceef58da19e348dda5c>. 2.9.1's `platformio.ini#L114-L115` (platform 55.03.39 + `launcher_esp32-arduino-libs-20260807`).
- keyboot bootloader: <https://github.com/bmorcelli/myLibBuilder/blob/cf56f6a55cd512a908fa455d80b1ee4b6c122e9d/patches/esp-idf/components/bootloader/subproject/main/bootloader_start.c>. The version 2.9.1 uses: <https://github.com/bmorcelli/myLibBuilder/blob/386ffd319bdaab3e48cb24ae48c209af86d6a5db/patches/esp-idf/components/bootloader/subproject/main/bootloader_start.c>.
- Wiki (`4fb10da`): [Explaining-the-project](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project) (firmware format, dynamic partitions, empty data partition 0x70000); [Serial-interface](https://github.com/bmorcelli/Launcher/wiki/Serial-interface).
- Releases: <https://github.com/bmorcelli/Launcher/releases/tag/2.9.1>, and beta: `releases/download/beta/Launcher-m5stack-cardputer.bin`. Web flasher: `src/flasher.json` and `src/webflasher.ts` on the `WebPage` branch (`2b119bb`).
- Issue [#424](https://github.com/bmorcelli/Launcher/issues/424): the ADV cannot mount as a USB drive on macOS, still open; PR [#448](https://github.com/bmorcelli/Launcher/pull/448): merged into `massStorage.cpp` on 2026-10-15, uses CMD18 multi-block reads, and does not write logs to the card during USB drive mode.
- This repo: `firmware/app/lib/hal/lfs_store.cpp:19,53`; `firmware/app/platformio.ini` (`default_8MB.csv`); `.github/workflows/release.yml:46-51`; `daemon/src/cpad/protocol/codec.py:109`; the `kSd*` constants in `firmware/hwtest/src/main.cpp` (line 198 at HEAD; another agent is editing this file in the working tree, currently line 205).

## 10. Tooling: `tools/launcher/` (M3-L, 2026-10-21)

Turns the steps done by hand in G-L1 on 2026-10-20 (esptool flashes Launcher → press RST, Enter to open the menu → serial `flash firmware`) into a command line tool. **This tool has not run on a real device yet**; the behavior below comes from unit tests (fake serial) and the records in section 4 and G-L1; the lead does the first real-device run.

**Commands** (run at the repo root; `uv run` installs `pyserial==3.5` automatically from the PEP 723 metadata in the script header):

| Command | What it does |
|---|---|
| `uv run tools/launcher/cpa_launcher.py fetch` | Downloads from the official release (`releases/download/2.9.1/Launcher-m5stack-cardputer.bin`) into the cache `~/.cache/cardputer-assistant/launcher/2.9.1/`, checks size 1,422,720 B and SHA-256 `2adff1ad…a024`. Downloads to a temp file first and renames only if both match; the cached file is checked again before every use and re-downloaded if wrong |
| `… flash-launcher` | Checks again, then `pio pkg exec --package tool-esptoolpy -- esptool.py --chip esp32s3 --port <port> --baud 460800 write_flash 0x0 <image>` (no need to press G0). After flashing it waits for Launcher to come up by itself (the new table has no ota partition, so a USB reset goes straight to Launcher) and confirms with `version`, `partitions`. **Overwrites our full flash** (section 4) |
| `… wait` | Sends `version` every 0.6 s until the user presses RST, then Enter on the splash, and the menu opens (waits 1500 s by default). "Menu open" means: `version` gets a reply, and more than 2 s have passed since the last splash banner (one line every 500 ms) and since opening the port. When used alone, adding `--stay-paused` is advised; otherwise afterwards cpad will connect to Launcher's port (it lets go by itself after the handshake fails) |
| `… cmd version` / `partitions` / `whoami` / `help` | Only these four read-only commands. Commands that print passwords such as `wifi`, `settings get`, and commands that change the device, are all refused |
| `… install factory` | Installs `backups/factory-app-cardputer-adv-v0.9.bin` (first checks the SHA-256 in backups/README.md); called `factory-v0.9` in Launcher |
| `… install <path>/cpa-v0.2.0-app.bin` | Installs our app-only image; the name defaults to the file name minus `-app.bin` (`cpa-v0.2.0`); `--name` changes it, e.g. for an rc package `--name cpa-v0.2.0`. A merged image (partition table at 0x8000) is refused, with a hint to use `-app.bin` instead |

Global options: `--port` (default: the first 303A:1001 port), `--cache-dir`, `--stay-paused`. Exit codes: 0 done; 1 failed (device replied ERR, timeout, port gone mid-transfer, esptool failed); 2 problem in preparation (wrong image, download failed, pio or port not found, port held by another process); 3 all bytes sent but not confirmed; 4 Launcher console never showed up.

**Safety measures**:
- Only safe open (03 §2 trap 3: `dsrdtr=True` → open → `rts=False` → `dtr=False`); never toggles control lines to reset the chip;
- Checks for holders with `lsof -t <port>` before opening; if another process holds it, waits for it to let go (up to 10 s) and does not grab;
- Before touching the port, `cpa pause N` (N = wait time + transfer time estimated at 4 KB/s + 300 s, at least 600 s; an existing longer pause is not shortened, same as `pre_upload.py`); at the end `cpa pause 1`, and Ctrl-C also resumes; not resumed with `--stay-paused`. If `cpa` is not installed this is skipped, relying only on the lsof check;
- Before installing, reads `partitions` once; if it cannot read it fully, sends no bytes (it means we are not in the menu);
- Output is only progress and device facts (version, partition labels, sizes); no note content or passwords.

**Install protocol** (`flash firmware <name> <size>`; the same strings can be found in the 2.9.1 binary: `READY %u`, `ACK %u/%u`, `OK flashed, rebooting`, `Launcher 2.9.1`, the splash banner):
- Wait for `READY <size>` (30 s, size must match) → send one 2 KB block at a time, sending the next only when the bytes acknowledged by the device catch up with the bytes sent (the device reading a block in several parts and replying several ACK lines is fine) → each block must make progress within 15 s → after everything is acknowledged, wait for `OK` (60 s). Prints one progress line every 10%.
- Banner and log lines are always ignored and do not push back timeouts. An ACK going backward, exceeding the bytes sent, or a wrong total is a protocol error.
- **Five outcomes**: `ok` (Launcher printed OK: verified, table written, set as boot entry); `unconfirmed` (every byte acknowledged, but OK did not come out before the reboot, or the port just dropped); `error` (device replied ERR, printed verbatim); `timeout`, `lost` (no reply or port gone before everything was acknowledged). On failure Launcher does not change the boot entry.
- On `unconfirmed` (or explicit `--verify`) it confirms again:
  - if the name is `cpa-vX.Y.Z…` and `cpa` exists, via the **daemon**: first `cpa pause 1`, then poll `cpa status --json` until `link ready`, `launcher=yes`, `fw=cpa-fw/X.Y.Z` (at most 90 s). A reinstall of the same version cannot be told apart this way;
  - otherwise (e.g. the factory app does not enable USB) via **partitions**: ask the user to press RST, Enter again, read the new table and compare it with the table before the install: there must be exactly one more ota app partition, no smaller than the image, marked `<-- BOOT`. The label rule is the same as Launcher's: the first 6 lowercase letters or digits of the name, and on a clash the first 5 plus a digit (when `factory-v0.9` already has `factor`, the new one is `facto1`).

**Tests** (no device needed):

```bash
uv run --no-project --python 3.12 --with pytest --with pytest-cov --with pyserial==3.5 \
  pytest tools/launcher -q --cov=tools/launcher/cpalauncher --cov=cpa_launcher --cov-fail-under=80
(cd tools/launcher && uvx ruff==0.17.1 check . && uvx ruff==0.17.1 format --check .)
uvx lizard==1.24.1 -l python -L 50 -C 10 -w tools/launcher
```

`FakeLauncher` in `tests/fakes.py` simulates the console following `serial_console.cpp` (splash banner, READY/ACK/OK, partition table print format, port drop after reboot) with a virtual clock, so timeout tests do not really wait.

**Known limits**:
- Serial install writes only the app and **does not create `cpafs`** (recorded in G-L1); for `cpafs`, install the merged image from SD;
- Not run on a real device; the release URL is GitHub's standard address, and this tool has not actually downloaded it yet (tests use a pre-seeded local cache and a fake network);
- Waiting on a busy port relies on `lsof`; without `lsof` on the system, this step treats the port as "not held".
