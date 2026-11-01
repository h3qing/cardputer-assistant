**English** · [中文](09-m2-design.zh-CN.md)

# 09 · M2 (v0.2.0) design: Launcher install and switching + microSD storage

> Status: final (adversarial review handled; item-by-item record in §14) · Date: 2026-10-20 · Role: architecture
> Basis: [gates/launcher.md](gates/launcher.md) (from reading the Launcher source, no device attached), [gates/sd-card.md](gates/sd-card.md) (measured on the real device), 05 §5.9 / §5.13, 04 §5.2 / §7.7, ADR-0001, plus the `firmware/app` v0.1 code (`FileStore`, `LfsStore`, `outbox_io`, `outbox_scan`, `Runtime`) and the patched SD driver (`firmware/hwtest/lib-core2/SD/src/sd_diskio.cpp`).
> Tags: **[meas]** measured on the real device · **[src]** source code or docs · **[calc]** estimated · **[todo]** to be verified (each one maps to a gate or a test).
> Scope: v0.2.0 only. The v0.1 spec (05) still applies; the parts this document replaces are listed in §13.
> **The dev SD card**: this card is dedicated to the project, and the data on it does not matter; it may be wiped, or reformatted as needed. So device tests and HIL may clear `/cpa/` and the test directories on the card, and "reformat this card with `diskutil`" in the manual checklist is also authorised (before doing it, check the disk number twice with `diskutil list`, and touch only this card). The card is already FAT32, formatted by the device (sd-card.md §3), so it needs nothing before M2 starts.

## 0. One-page summary

1. **One firmware binary** that can be fully flashed or installed by bmorcelli Launcher, and detects at run time whether it is under Launcher. **No separate env.**
2. **The partition table changes one label only**: `spiffs` becomes `cpafs`, offset and size unchanged. After a v0.1 user upgrades with a full flash, the old data still mounts. Under Launcher, `spiffs` is shared by several firmwares, and we never touch it.
3. **Two `FileStore` back ends**: `SdStore` (FAT32, only `/cpa/` on the card) and `LfsStore` (internal `cpafs`). `SdStore`'s logic is pure code (`lib/storage`) and calls the VFS through a thin `FatPort` interface, so it can be tested on native with TDD; hal keeps only mounting and POSIX calls. At boot the pure function `plan_storage()` picks the primary store: SD if usable, otherwise `cpafs`; if neither exists, saving is disabled and the reason shown. No switching during the run.
4. **When SD is primary, pending items in `cpafs` are migrated to SD at boot**: each item is first committed on SD, and only then is the source file deleted. Safe against power loss mid-way; the next boot re-runs it and it converges. bad/ is not migrated.
5. **No rename at all on SD** (FAT rename is not atomic and cannot overwrite an existing target). Items are written under their final name directly, and "Saved" shows only after `fsync` plus `close` succeed; moving to bad/ is "copy, then delete". `FileStore` gains the feature bit `atomic_rename()` to tell the two commit protocols apart. The two new scan rules apply only when `atomic_rename()==false`; behaviour on LittleFS is identical to v0.1, word for word.
6. **8.3 short names on SD, flat per epoch**: `/cpa/outbox/<E7>/<B5><first 3 of S>.J<last 1 of S>`. A card creates a directory once per epoch, not once per boot. Measured: 200 items flat with 8.3 names, p95 29ms [meas].
7. **SD driver**: the patched core `SD` library moves to `firmware/third_party/SD`, one copy shared by app and hwtest. Fixed at 20MHz, **no slow-down retry**. On mount failure, read `sdcard_type()` to tell "no card" from "card unusable"; after mounting, do one probe write; `max_files=2`; never call `f_getfree`.
8. **A disk-level SD error at run time (card pulled, card dead) locks the store**: from then on every storage operation fails at once and the SPI bus is not touched again until a restart. That way a pulled card does not make the main loop stall for seconds over and over.
9. **The protocol only adds two optional fields**: `store` (`sd` / `int` / `none`) and `launcher` (bool) in the device's `hello`. `v` unchanged.
10. **The daemon only changes its display**: `cpa status` shows the two fields above; plus one guard test (lines sent by the host start only with `{` or are empty). "Short backoff for bound devices" is deferred until gate G-L9 shows it is needed.
11. **With no SD card, a fully flashed device behaves exactly like v0.1**: same partition, same shards, same commit protocol and scan rules. Not one line of the v0.1 outbox tests changes and they still pass; all v0.1 HIL scenarios pass on the "SD disabled" build.
12. **The SD layout for v0.3 recording** is reserved here (§12), not implemented this time.
13. **Four work packages**: A (firmware storage layer) and C (protocol + daemon) in parallel → B (firmware integration, UI, Launcher release) → D (HIL and manual checklist).

## 1. Goals and non-goals

### 1.1 Goals (definition of done for v0.2.0)

| # | Goal | How it is accepted |
|---|---|---|
| G1 | The same release image can be fully flashed (written to 0x0) and also installed and booted by Launcher from SD | G-L1; CI decodes the partition table |
| G2 | Under Launcher, only its own `cpafs` is mounted or formatted; `spiffs` is never read or written; otadata and the partition table are never written; NVS is never erased | Code review; G-L3 (switch back and forth with Bruce 10 times) |
| G3 | With a FAT32 card in, the outbox and the project cache live on SD; save p95 ≤100ms (also with a 200-item backlog), any single save ≤150ms; the first item per epoch per card (which creates the epoch directory) ≤250ms | device `test_sd_store`; HIL `sd_store` / `sd_bulk` |
| G4 | On FAT power loss, items that showed "Saved" lose 0 (they may be sent twice; the daemon dedupes) | native: power loss injected at every step proves the **commit protocol** itself correct; 10 manual power cuts are only low-strength physical evidence (see R16) |
| G5 | From "no card" to "card in": pending items in `cpafs` are migrated to SD at boot with 0 loss | native migration with power-loss injection; device; HIL `migrate_*` |
| G6 | With no SD card, a fully flashed device behaves like v0.1 | The v0.1 outbox suites pass with not one line changed (guarded with `git diff`); the v0.1 HIL scenarios pass on the nosd build |
| G7 | The user can see which store is in use, how the device booted, and why a store is unavailable | ui `test_mod_system`; `cpa status` |
| G8 | After the card is pulled at run time, keys and screen still respond: at most one stall on the storage operation that discovers the error (0.6–2s [calc]); after that the bus is not touched | native `test_sd_store` (0 calls to `FatPort` after lock-up); manual check |

### 1.2 Non-goals (not in v0.2)

- Recording, transcription, and moving files to the Mac: v0.3; layout in §12.
- Formatting SD on the device. By default format with `diskutil` on the Mac; waiting for your confirmation (open question 3).
- Switching stores automatically after the SD card is pulled or inserted at run time, or recovering without a restart after a pull. v0.2 chooses once, at boot.
- A dedicated "storage full" notice for a full SD card. A full card shows the generic "Save failed (E_STORAGE)" (§3.8).
- Retrying at 4MHz after a failed mount.
- Migrating `cpafs`'s bad/ to SD.
- An encrypted wrapper and PIN lock on SD. The security items the PRD originally put in v0.2 are deferred (open question 9).
- Archiving synced items to SD (the PRD's original v0.2 "SD card history"): deferred.
- A "Back to Launcher" item on the system page. It needs deep sleep, which breaks R8; flipping the power switch or pressing RST already returns to Launcher. So launcher.md's G-L5 is not tested.
- Short daemon backoff for bound devices: only if G-L9 fails (§7).
- Deep sleep, power saving, USB drive mode (MSC), OTA, the LauncherHub online catalogue.
- A separate `cardputer-adv-launcher` env. Option B (carrying a 1.5MB LittleFS image inside the merged image) is only reopened if G-L4 measurements fall short.

## 2. Input facts

| Fact | Source | Effect on the design |
|---|---|---|
| Launcher writes only the app and data partitions; data partitions are created or reused by **label**; `spiffs` is shared by several firmwares and kept when a firmware is deleted; partitions with other labels are erased when their app is deleted | [src] launcher.md §1–2 | Label becomes `cpafs`; never touch `spiffs`; upgrade flow in §4.4 |
| On 8MB models an empty data partition is always created as 0x70000 (448KB) | [src] | `cpafs` is small under Launcher, so SD is primary; internal storage drops to 4 shards |
| Power-on, RST and deep-sleep wake all go to Launcher; `esp_restart`, the watchdog and USB reset stay in this firmware; the reset reason read on a cold start is always `software` | [src] | No deep sleep; `rst` is for display only |
| The merged image is only read as metadata; the display name comes from the file name, at most 20 characters | [src] | Release file names must be shorter (§4.4) |
| Launcher's serial console treats every received line as a command; a line starting with `{` only gets `ERR` | [src] | Host lines must start with `{` or be empty |
| The SD driver bundled with core cannot initialise this 512GB card; with the ACMD41 argument changed to HCS only, it initialises in 3–6ms | [meas] sd-card.md §2 | Use the patched driver; Launcher itself may also be unable to read this card [todo] |
| In the patched driver `sdcard_init` only allocates structures and registers diskio, **without touching the card**; the card is initialised in `f_mount(…, 1)` inside `sdcard_mount`, and both "no card" and "no FAT" return `false`. But on successful init `card->type` is set to SD/SDHC/MMC and can be read with `sdcard_type()` | [src] `sd_diskio.cpp` | After a mount failure, use `sdcard_type()` to tell Absent from Unusable (§3.9) |
| With no card, `GO_IDLE_STATE` retries 3 times with `delay(100)` each; `sdSelectCard` waits up to 500ms; `sdcard_uninit` sends one more CMD0 | [src] | With no card, do not uninit and do not retry; the cost of a cardless boot depends on the MISO idle level [todo] |
| After the card is pulled, every FatFs call first goes through `disk_status` (CMD13, same retries) and `disk_initialize` (CMD0), 0.6–2s each time | [src] + [calc] | Lock up after a disk-level SD error (§3.8) |
| Toolchain A's FatFs does not support exFAT | [src] | FAT32 / FAT16 only; exFAT cards must be formatted on the Mac first |
| Five clock settings (4 / 20 / 25→20 / 26.7 / 40MHz) all pass mount and write-read verification 5/5; the driver checks CRC16 on every block | [meas] | Fixed at 20MHz, no slow-down retry |
| At 20MHz, 32KB block writes run at about 1.6MB/s; occasional write stalls of 30–65ms | [meas] | SD writes may exceed the 50ms single-step rule (§5) |
| Long file names flat, 200 items: p95 93ms, and it slows down linearly; 8.3 names flat, 200 items: p95 29.1ms, max 101.7ms; creating a directory zeroes a 32KB cluster, about 70–90ms each (inferred from the 89.5ms average of the first 10 items in the sharded layout) | [meas] + [calc] | Use 8.3 names, flat per epoch, no per-boot directories |
| rename returns EEXIST if the target exists; FatFs rename first registers the new directory entry, then deletes the old one, so a power cut in between leaves two names pointing to the same cluster chain | [meas] + [src] | No rename on SD, and never delete `.tmp` |
| `f_getfree` takes 0ms when FSINFO is valid; when it is invalid, an estimated ~35s scan | [meas] / [calc] | v0.2 never calls `f_getfree` |
| All performance figures come from cards formatted by FatFs itself (1 FAT, data area aligned to 4MiB); a `diskutil`-formatted card has 2 FATs and unknown alignment | [meas] + [src] | Re-run the device tests on a `diskutil`-formatted card (R17) |
| `esptool merge_bin` fills gaps between segments with 0xFF; writing the merged image to 0x0 wipes the NVS at 0x9000 | [src] release.yml | After writing 0x0 the epoch is regenerated; docs must not promise NVS is kept (§4.4) |
| Recording 60s at 32KB/s, 3 runs, 0 dropped frames; about 27% of replies held back during recording | [meas] | Basis for v0.3; no recording in v0.2 |

## 3. Storage architecture

### 3.1 Boot flow overview

```
setup()
  g_usb.begin(); M5Cardputer.begin(); g_display.begin()          (unchanged from v0.1)
  part = hal_part_facts()            -> PartFacts{launcher, cpafs_bytes, legacy_spiffs}
  sd   = hal_sd_mount(g_sd)          -> SdFacts{state, size_mib}
                                        state==Ok means mounted, and g_sd.prepare() succeeded (creates /cpa/outbox, probe write)
  lfs  = g_lfs.begin()               signature unchanged: finds cpafs by label itself, false if absent; shard count from partition size
  plan = plan_storage(part, sd, lfs) -> StoragePlan{primary, migrate, note}           pure function
  boot = BootInfo{nvs_boot_ticket(), dev, rst}                    (unchanged from v0.1)
  fs   = primary==Sd ? g_sd : primary==Internal ? g_lfs : g_null
  rt   = function-local static Runtime(…, fs, …) in setup(), pointer handed to loop()
  rt->begin(boot, millis(), StorageBoot{plan, sd, part, plan.migrate ? &g_lfs : nullptr})
        1. with a migration source: outbox_boot_scan(source) → outbox_migrate(source → primary), one callback per item (kick USB, refresh progress)
        2. outbox_boot_scan(primary)
        3. after that, same as v0.1
  SD primary and lfs mounted: after migration, call g_lfs.end() to free the heap LittleFS uses
```

The `Runtime` constructor is unchanged, and `begin()`'s third parameter has a default (no migration, internal primary), so the v0.1 ui tests need no change.

### 3.2 Changes to `FileStore`

```cpp
class FileStore {             // lib/outbox/file_store.h
  // ...v0.1 methods unchanged...
  // true: rename is atomic and overwrites the target (LittleFS). false: FAT, commit protocol in §3.6
  virtual bool atomic_rename() const { return true; }
};
```

- The default is `true`, so existing v0.1 implementations and tests need no change.
- `NullStore` (`lib/storage/null_store.h`, pure code): every operation fails and `free_bytes()` returns 0. Used when no store is available at all; in that case `Runtime` never even tries to write (`NoStorage` in §8).

### 3.3 The two back ends

| | `LfsStore` (internal) | `SdStore` (SD) |
|---|---|---|
| Code location | `lib/hal/lfs_store` (device-only, as in v0.1) | `lib/storage/sd_store` (pure code, testable on native) + `lib/hal/sd_card` (mounting and `VfsFatPort`) |
| Medium | `cpafs` on internal flash (LittleFS) | microSD, FAT32 or FAT16, MBR; on FSPI: SCK40 / MISO39 / MOSI14 / CS12 |
| Mount point | `/littlefs` | `/sd`, using only `/sd/cpa/` |
| `atomic_rename()` | true | false; `rename()` returns false straight away without touching the card |
| Commit protocol | As in v0.1: write `.tmp` → close → rename | Write the final file name directly → fsync → close (§3.6) |
| Physical layout | v0.1's shards `/outbox/_<last MonoId char>/`; with a partition under 1MiB only 4 shards `_0`–`_3` | 8.3 names, flat per epoch (§3.5) |
| `free_bytes()` | As in v0.1: walks the metadata once at boot | Does not scan the FAT: 0xFFFFFFFF normally, 0 after lock-up |
| Errors | Fails call by call, as in v0.1 | Locks up after a disk-level error (§3.8) |
| When it formats | Only formats `cpafs` itself when it cannot be mounted (e.g. an empty partition just created by Launcher) | Never formats |
| When it mounts | Mounts if `cpafs` exists; unmounted after migration when SD is primary | Tried once per boot, no retry |

**`LfsStore::begin()` keeps its signature** (no arguments, returns bool). Internally it finds the partition with `esp_partition_find_first(DATA, ANY, "cpafs")`: if absent it returns false without mounting or formatting; if present it sets the shard count from `lfs_shards(size)` and calls `LittleFS.begin(true, "/littlefs", 4, "cpafs")`. That way the existing `g_fs.begin()` calls in `src/main.cpp` and `device/test_runtime_budget` still compile after M2-A is merged (§14 B2).

**Shard count** `lfs_shards(part_bytes)` (pure function): 32 shards when the partition is ≥1MiB, exactly the v0.1 layout; 4 when under 1MiB, with the shard number being the Crockford value of the last MonoId character modulo 4.

[calc] A 448KB partition has 112 blocks in all. Subtract 8 blocks for the root, `/outbox`, bad and `/cfg`, and the 64KB reserve (16 blocks):
- with 32 shards, the shard directories take 64 blocks, leaving room for only about 25 items;
- with 4 shards, the shard directories take 8 blocks, leaving room for about 80 items.

The real count is measured by G-L4.

**`SdStore` and `FatPort`** (`lib/storage/fat_port.h`, `lib/storage/sd_store.{h,cpp}`):

```cpp
enum class FatErr : uint8_t { None, NotFound, Exists, Disk, Other };  // Disk = errno EIO / ENODEV / ENXIO
class FatPort {        // POSIX calls under the mount point; paths are physical ("/cpa/..."); one write file open at a time
 public:
  virtual ~FatPort() = default;
  virtual FatErr open_write(const char* path) = 0;     // O_CREAT | O_TRUNC
  virtual FatErr write(const char* data, size_t n) = 0;
  virtual FatErr sync_close() = 0;                     // fsync + close
  virtual long read(const char* path, char* buf, size_t cap, FatErr& err) = 0;
  virtual FatErr unlink(const char* path) = 0;
  virtual FatErr mkdir(const char* path) = 0;          // already exists counts as success
  virtual FatErr list(const char* dir, DirVisitor& visit) = 0;
};

class SdStore : public FileStore {                      // pure code
 public:
  explicit SdStore(FatPort& port, const char* root = "/cpa");  // device tests use another root, never the real outbox
  bool prepare();       // mkdir root, root/outbox; probe: write 1 byte to root/PROBE.BIN → fsync → delete
  bool atomic_rename() const override { return false; }
  bool failed() const;  // locked up (§3.8)
  // other FileStore methods: map the path per §3.5, then call port
};
```

- Only two things in hal touch the SD library and the VFS: `SdFacts hal_sd_mount(SdStore&)` (§3.9) and `VfsFatPort` (`::open`, `::fsync` and so on, mapping errno to `FatErr`).
- The probe write lives in `prepare()` because `mkdir` alone cannot detect a read-only (worn-out) or full card: `mkdir` simply succeeds when the directory exists. If such a card were chosen as primary, every save would fail while internal storage works fine (§14 risk 3).

### 3.4 Selection rules: `plan_storage` (pure function, `lib/storage/storage_plan.h`)

```cpp
struct PartFacts { bool launcher; uint32_t cpafs_bytes; bool legacy_spiffs; };
enum class SdState : uint8_t { Disabled, Absent, Unusable, Ok };
struct SdFacts { SdState state; uint32_t size_mib; };
enum class Store : uint8_t { None, Internal, Sd };
enum class StoreNote : uint8_t { None, NoCard, SdUnusable, NoInternal, LegacyTable, NoStorage };
struct StoragePlan { Store primary; bool migrate; StoreNote note; };
StoragePlan plan_storage(const PartFacts& part, const SdFacts& sd, bool lfs_mounted);
uint8_t lfs_shards(uint32_t part_bytes);
```

`SdState::Unusable` means the card initialised but cannot be used: exFAT, unformatted, a broken file system, or a failed mkdir or probe write. The draft's Unmountable and Unwritable are merged into this one, because the user does the same thing for both: reformat on the Mac or use another card (§14 S5).

"Old table" in the table below means `!launcher && legacy_spiffs && cpafs_bytes == 0`, i.e. a fully flashed device that was flashed with `-app.bin` only, so its partition table is still v0.1's.

| SD state | `cpafs` mounted | Primary | Migrate | Reason (`note`) |
|---|---|---|---|---|
| Ok | yes | Sd | yes | None |
| Ok | no | Sd | no | LegacyTable with the old table, otherwise NoInternal (e.g. Launcher installed an app-only image) |
| Absent | yes | Internal | no | NoCard |
| Unusable | yes | Internal | no | SdUnusable |
| Disabled | yes | Internal | no | None (the nosd build shows no notice) |
| not Ok | no | None | no | LegacyTable with the old table, otherwise NoStorage |

Notes:
- `launcher` does not affect the choice; it only affects display and how `legacy_spiffs` is read. Under Launcher, `spiffs` belongs to other firmware: never mounted, never mentioned.
- Free space is not considered when choosing.
- When `migrate` is true but `cpafs` has no pending items, migration does nothing.
- With the old table, v0.1's pending items are still in the partition named `spiffs` and are not migrated; the notice tells the user that a full flash brings them back.

### 3.5 SD card layout

```
/                                     Launcher's files: /config.conf  /downloads/  /bkp/  /Launcher_error.log (we never touch them)
/cpa/
  PROBE.BIN                           probe written at mount (deleted right after; may survive a power cut, overwritten next boot)
  outbox/<E7>/<B5><S3>.J<S1>          pending items (MonoId = E7-B5-S4; S3 = first 3 of S, S1 = last 1 of S)
  outbox/BAD/<E7>/<B5><S3>.J<S1>      items the daemon rejected or that failed to parse (only the newest 20 kept)
  cfg/projects.json                   cache of slot to project key/name
  rec/                                reserved for v0.3 (§12)
  logs/                               reserved
  fw/                                 suggested place for .bin files for Launcher to install (the firmware does not read it)
```

Example: `K3M9Q2T-00042-0A1C` is stored as `/cpa/outbox/K3M9Q2T/000420A1.JC`.

**Path mapping** (pure function, `lib/storage/sd_paths`). Logical paths are the ones `outbox_io` uses today, unchanged. Physical paths are relative to the mount point `/sd`; `/cpa` is `SdStore`'s root:

| Logical path | Physical path on SD |
|---|---|
| `/outbox` | `/cpa/outbox` |
| `/outbox/<E>-<B>-<S>.json` | `/cpa/outbox/<E>/<B><first 3 of S>.J<last 1 of S>` |
| `/outbox/<id>.tmp` | No mapping: `open_write` fails outright (the FAT commit protocol never writes `.tmp`) |
| `/outbox/bad` | `/cpa/outbox/BAD` |
| `/outbox/bad/<E>-<B>-<S>.json` | `/cpa/outbox/BAD/<E>/<B><first 3 of S>.J<last 1 of S>` |
| any other path `/x/y` | `/cpa/x/y` (e.g. `/cpa/cfg/projects.json`; a single file with a long-name directory entry is fine) |

- **Why 8.3 names work**: Crockford base32 characters (0-9, A-Z, minus I, L, O, U) are all valid 8.3 characters. For an all-uppercase name that fits 8.3, FatFs creates no long-name directory entry.
- **Why flat per epoch, not a directory per boot**:
  - Without the epoch, a MonoId has only 9 significant characters (B5 + S4); add a type letter `J` and it fits exactly into 8+3.
  - The epoch changes only when NVS is re-initialised, so a card usually has only one or two epoch directories, each created once.
  - One directory holds at most 200 pending items (bad is kept separately). Measured: 200 items flat with 8.3 names, p95 29.1ms, max 101.7ms [meas]; that run also included a rename, so writing directly is only faster.
  - With a directory per boot, every boot would create a directory (zeroing a 32KB cluster, about 70–90ms [calc]) and empty directories would need cleaning up; migrating items from many boots would create a directory per boot number (§14 B4 and risk 1).
- **Directories do not get slower over time**: FAT does not shrink a directory when files are deleted, and freed entries are reused, so a directory's size stays at the historical maximum of concurrent items (≤200 entries; one 32KB cluster holds 1024), and lookup cost is bounded.
- **Listing**: `list("/outbox")` first lists `/cpa/outbox`:
  - `BAD` is reported as a directory named `bad`;
  - directories whose name is 7 Crockford characters are noted (at most 8 epochs; extras are not listed) and, after the top level is done, listed one by one (only one directory open at a time, as with `LfsStore`'s shards);
  - files in an epoch directory matching "8 characters + `.J` + 1 character" are joined back into `<MonoId>.json` and reported;
  - other names are reported as they are; the boot scan counts them as other and leaves them alone.
- **Directories created on demand**: when `open_write` gets NotFound, it creates `/cpa/outbox/<E7>` (or `BAD`, `BAD/<E7>`) level by level and tries once more. This happens once per epoch per card, and that save may take ≤250ms.
- Empty directories are not cleaned up: there are very few epoch directories.

### 3.6 Power-loss safety on FAT

**Commit protocol**. Implemented in `lib/outbox/store_commit.{h,cpp}` (`commit_file`, `move_file`) as pure code on top of the `FileStore` interface; `outbox_persist`, `outbox_apply`, the scan actions and the project cache all call them:

| Operation | `atomic_rename()=true` (LittleFS, unchanged from v0.1) | `atomic_rename()=false` (FAT) |
|---|---|---|
| Save an item | `open(<id>.tmp)` → write → close → `rename(<id>.json)` | `open(<id>.json)` → write → close (with fsync); if any step fails, `remove(<id>.json)` |
| Ack received (stored / duplicate) | `remove(<id>.json)` | Same |
| Move to bad/ | `rename(<id>.json, bad/<id>.json)` | Read the content (static 2KB buffer) → write `bad/<id>.json` as in "Save an item" → `remove(<id>.json)` |
| Project cache | Write `projects.tmp` → close → `rename(projects.json)` | Overwrite `projects.json` directly. If the write is corrupted, treat it as no cache; the daemon pushes it again |

**Invariant**: on a store with `atomic_rename()=false`, the outbox code and the project cache never call rename, not once. Tests assert this with FakeStore's call counts.

**Boot scan rules** (the two bold ones are new and **apply only when `atomic_rename()==false`**; on LittleFS the rules are identical to v0.1, word for word, so the v0.1 test "a `.json` of length 0 is moved to bad/" still holds, §14 B3):
- **A `.json` of length 0: delete.** It was never committed: on FAT the directory entry is registered at open, but power was lost before fsync. The user never saw "Saved".
- A `.json` that fails to parse, or whose id does not match the file name: move to bad/ (unchanged).
- A `.json` that cannot be read (I/O error): leave it for the next boot (unchanged).
- **A `.tmp` is always only counted, never deleted.** We never write `.tmp` on FAT (and `SdStore` never even reports `.tmp`), so if one shows up we did not write it; and deleting a directory entry that may share a cluster chain with another file would destroy that file too. This rule is a safeguard for FakeStore's FAT mode and any future FAT back end.
- bad/ keeps only the newest 20 (unchanged).

**Power-loss analysis** (FAT):

| Power lost | Seen after boot | Result |
|---|---|---|
| After open, before fsync | No file, or a file of length 0 | Deleted. The user never saw "Saved", so nothing is lost |
| While fsync updates the directory entry (sector half written) | Right length but bad content, or a broken directory entry | Fails to parse, moved to bad/. Up to 15 neighbouring directory entries in the same sector may be damaged with it; see R16 [todo] |
| After fsync | The complete file | Pending as normal |
| While deleting the file after an ack | The file is still there | Sent once more; the daemon judges it duplicate |
| Moving to bad/: copy written, original not yet deleted | Both exist | If the original fails to parse, it is moved again (overwriting the copy); if it is a valid item that was rejected, it is sent again and rejected again. Nothing is lost |

**Residual risks**:
- A power cut may leave a "lost cluster": marked as used in the FAT but referenced by no file. At most one 32KB cluster leaks per power cut. The device has no fsck; Disk Utility → First Aid on the Mac reclaims it.
- A power cut halfway through writing a FAT or directory sector may cause cross-linking or damage neighbouring directory entries. Very unlikely, and it depends on whether the card's FTL writes sectors atomically [todo]. Ten manual power cuts are almost certain to miss this window of a few milliseconds, so G4's physical evidence is weak (R16).

### 3.7 Migration: internal storage → SD

The interface is in `lib/outbox/outbox_migrate.{h,cpp}`:

```cpp
struct MigrateResult { uint16_t moved, failed; };
class MigrateObserver {
 public:
  virtual ~MigrateObserver() = default;
  virtual void on_item(uint16_t done, uint16_t total) = 0;
};
MigrateResult outbox_migrate(FileStore& from, FileStore& to, const OutboxCore& pending, MigrateObserver& obs);
```

**Steps** (run in `Runtime::begin`, before scanning the primary store):
1. First run `outbox_boot_scan(from, codec)` on the source (LittleFS, v0.1 rules): delete orphan `.tmp` files, move bad items to the source's bad/, and get the ids of the valid pending items. The list holds at most 200, in ascending id order; any beyond that stay in the source until the next boot.
2. For each id: read the source file (static 2KB buffer) → write it to the target with `commit_file` (FAT protocol: truncate and rewrite, so it overwrites a partial copy left by an interrupted migration) → delete the source. If any step fails, keep the source file, count it in `failed`, and move on to the next.
3. bad/ is not migrated and stays in `cpafs` (those items are never sent; they are only evidence, §14 S3).
4. `/cfg/projects.json` is not migrated; the daemon pushes it again.
5. After each item, `obs.on_item` is called once. The shell kicks USB in the callback and refreshes "Moving to the SD card i/n".

**Invariant**: an item in the source is deleted only after it has been committed on the target (fsync + close both succeeded). That is the same bar as showing "Saved" when the user saves. So whenever power is lost, each item is in the source, on the target, or both; the next boot runs it again and it converges.

**Nothing is lost because of this**:
- Power lost after the target is committed but before the source is deleted, and the next boot has no card: the item is sent from internal storage and deleted once acknowledged; when the card is inserted later, the copy on SD is sent once more and the daemon judges it duplicate.
- Sending only reads from the primary store; even if duplicates appear, the daemon dedupes by `(dev, id)`.

**Time** [calc]: about 40ms per item (about 5ms to read internal flash, 20–30ms to write SD, about 10ms to delete the internal file). 200 items take about 8s, and it only happens once. The target is a flat per-epoch directory, so no directory is created per boot number.

**Tests**: two FakeStores share one "power" object; power is lost before the k-th operation on either side, with k running over every step; after the power loss, migration and scan run again. Requirement: 0 lost, each item ends up exactly once on the target, and the source is empty. The device test times 50 items from ≥20 different boot numbers.

### 3.8 Faults at run time

- **The primary store is chosen at boot and not switched during the run.** Simple and easy to test; besides, `OutboxCore` only tracks ids, not which store an item lives in.
- **A disk-level SD error locks the store**: the first time `SdStore` gets `FatErr::Disk` from `FatPort` (errno EIO / ENODEV / ENXIO, i.e. FatFs's `FR_DISK_ERR`, `FR_INT_ERR`, `FR_NOT_READY` and the like), it marks itself failed. From then on every method returns failure at once and never calls `FatPort` again, until a restart.
  - Why: after the card is pulled, every FatFs call first goes through `disk_status` (CMD13, 3 retries with `delay(100)` each) and `disk_initialize` (CMD0, same retries), and `sdSelectCard` may wait another 500ms, 0.6–2s each time [src/calc]. `pump_outbox` reads up to 4 in-flight items on every resend; without the lock the main loop would stall every few seconds, more than ten times over the single-step budget.
  - The one operation before the lock still runs through its retries, so pulling the card causes one 0.6–2s stall [calc]; after that keys and screen respond normally (G8).
  - After the lock: saving shows "Save failed (E_STORAGE)"; the system page shows "Store errors N: restart". Pending items on the card stay on the card and are not lost.
  - After reinserting the card, press RST or flip the power switch to recover; under Launcher, a restart first passes through Launcher's 5-second splash.
- **Pending items still on the card when it is pulled**: they stay on the card and are sent as normal after the next boot with the card in. Meanwhile the status bar's ↑N does not include them. The README says "do not pull the card while ↑N is not 0".
- **Full card**: not a disk-level error, so no lock; the save fails and shows the generic "Save failed (E_STORAGE)". No dedicated "storage full" notice: `Runtime` reads `free_bytes()` only once at boot, and adding one would mean checking on every save (§14 risk 2). The outbox itself takes at most 200 clusters of 32KB (about 6.4MB), so a full card can only mean other files filled it.

### 3.9 SD driver and mounting

- **Driver**: a patched copy of the core 2.0.17 `SD` library, moved from `firmware/hwtest/lib-core2/SD` to `firmware/third_party/SD`, shared by app and hwtest.
  - Patch 1 (ACMD41 argument with HCS only) is kept.
  - Patch 2 (`GET_BLOCK_SIZE` reports 4MiB so `f_mkfs` aligns) is kept. The app never formats; this patch stays for hwtest and later use.
  - Patch 3 (removing the 25MHz cap) now only applies when `CPA_SD_UNCAPPED_CLOCK` is defined; only hwtest defines it.
  - NOTICE gains an Apache-2.0 entry with the path and the three patches.
  - This code lives under `firmware/third_party/` and is outside check_sizes, lizard and clang-tidy: it is third-party code, kept as is except where marked `CPA patch`.
  - The app only uses the `sdcard_*` functions in `sd_diskio.h`, not the `SDFS` class (`SD.begin()`).
  - The `cardputer-adv-test` env lists `lib_deps` explicitly and must add `storage`; the build log must show `third_party/SD` being used, not the framework's own `SD`.
- **Mount steps** (`hal_sd_mount(SdStore&)`):
  1. `CPA_STORAGE_NO_SD` defined: report Disabled straight away, do not touch the bus.
  2. `SPI.begin(40, 39, 14, 12)`; `sdcard_init(12, &SPI, 20000000)`. This only registers the driver; it does not touch the card.
  3. `sdcard_mount(pdrv, "/sd", 2, false)`. The card is initialised only here (`disk_initialize` inside `f_mount`; during init the driver always uses 400kHz). `format_if_empty` is always false: never format.
  4. Mount failed: read `sdcard_type(pdrv)`.
     - `CARD_NONE` / `CARD_UNKNOWN`: Absent.
     - `CARD_SD` / `CARD_SDHC` / `CARD_MMC`: Unusable (the card initialised, but has no usable FAT, e.g. exFAT).
     - In both cases **do not call `sdcard_uninit`** (it sends another CMD0, another 300ms+ with no card) and **do not retry**; the driver stays registered and unused (`sdcard_mount` already unregistered the VFS path on failure).
  5. Mount succeeded: `size_mib = sdcard_num_sectors(pdrv) / 2048`; if `store.prepare()` fails report Unusable, otherwise Ok.
- **No 4MHz slow-down retry**: all five clock settings (20MHz included) pass 5/5 [meas], and the driver checks CRC16 on every block (§14 S1). This replaces launcher.md R7's "verify by comparison, fall back to 4MHz".
- **Cost of a cardless boot**: `GO_IDLE_STATE` retries 3 times with `delay(100)` each, about 300ms; if MISO reads 0x00 when idle, `sdWait` and `sdSelectCard` each wait another 500ms, possibly over 2s [src/calc]. The budget is ≤500ms, measured by device `test_sd_absent`; if over, set GPIO39 to the internal pull-up after `SPI.begin` and measure again [todo].
- **File slots**: `max_files = 2`, one for writing, one for reading. This SDK's `FF_MAX_SS` is 4096, so each slot costs about 4KB of heap. v0.3 recording raises it to 3.
- **Free space**: v0.2 never calls `f_getfree`. Capacity is bounded by the 200-item limit; when v0.3 needs to show how long it can still record, add an FSINFO read with a bounded time.
- **Card format requirements**: FAT32 (or FAT16), MBR partition table.
  - exFAT cards (cards of 32GB and up ship as exFAT) are Unusable, and the system page says "SD unusable: format FAT32 on a Mac".
  - The README gives the format command: `diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN`. Check the disk number twice with `diskutil list` first.
- **Expansion port GPIOs**: v0.2 does not touch GPIO 3/4/5/6/13/15. Launcher pulls these pins high for expansion-cap compatibility; the case with a cap fitted is tested in G-L7, and v0.2 does not support using SD with an expansion cap fitted.
- **Test build `cardputer-adv-test-nosd`**: defines `CPA_STORAGE_NO_SD=1`. SD then reports Disabled and internal is primary. It is used to regress v0.1 behaviour on a HIL device with a card in, and to seed data for the migration scenarios. If the power-cut gate shows saved items lost on SD, the release can also use this switch to fall back to "internal as primary" (R16).

## 4. Launcher: build approach and partition detection

### 4.1 Decision: one binary, no separate env

Reasons:
- Launcher reads the partition table in the merged image and creates `cpafs` from it.
- Our app runs from `ota_N`, which makes no difference compared with `app0`.
- The differences between the two cases (under Launcher or not, partition size, reset reason) can all be detected at run time.
- Bruce's 8MB build does the same.

**Development flow**: `pio run -t upload` wipes Launcher. Day-to-day development keeps using full flashing; Launcher mode is only tested in the G-L gates and HIL.

### 4.2 Partition table `firmware/app/partitions/cpa_8MB.csv`

```
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  ota_0,    0x10000,  0x330000,
app1,     app,  ota_1,    0x340000, 0x330000,
cpafs,    data, spiffs,   0x670000, 0x180000,
coredump, data, coredump, 0x7F0000, 0x10000,
```

- Compared with `default_8MB.csv`, only one label changes.
- The subtype is still `spiffs` (0x82): IDF 4.4's `gen_esp32part.py` does not know `littlefs`, and Launcher accepts both.
- CI decodes the built `partitions.bin` and asserts three things:
  - `cpafs` exists, at 0x670000 with size 0x180000;
  - no partition is labelled `spiffs`;
  - no app partition has subtype test (otherwise Launcher detection would be fooled).

### 4.3 Run-time detection (`lib/hal/part_facts`)

```cpp
PartFacts hal_part_facts();   // only collects facts; the decisions are in plan_storage
//  launcher      = an APP/TEST partition exists and is not the one running (our own table has no test partition)
//  cpafs_bytes   = size of esp_partition_find_first(DATA, ANY, "cpafs"); 0 if absent
//  legacy_spiffs = a data partition labelled "spiffs" exists (only used for the notice, never mounted)
```

Every boot looks partitions up by label again and caches no flash offset, because Launcher moves partitions around.

### 4.4 Release artifacts, install and upgrade

| File | Purpose |
|---|---|
| `cpa-<tag>.bin` | Merged image with bootloader, partition table, boot_app0 and app. Written to 0x0 for a full flash; can also be put on SD for Launcher to install, where it shows as `cpa-<tag>` (20 characters at most) |
| `cpa-<tag>-app.bin` | App only, written to 0x10000, for re-flashing without changing the partition table. Launcher can install it too, but then no `cpafs` is created and only SD can be used |
| `SHA256SUMS` | Checksums |

The merged-image logic moves from `release.yml` to `scripts/make_image.py`, which HIL also uses to build test merged images.

**About NVS**: `merge_bin` fills the gaps between segments with 0xFF, so writing `cpa-<tag>.bin` to 0x0 wipes the NVS at 0x9000, and the epoch and boot counter are regenerated. Per the design in 05 §5.9 this is safe (ids from a new epoch never collide with old ones), but neither docs nor HIL **may promise** that the epoch or boot counter survive a write to 0x0. `pio run -t upload` writes segment by segment and does not touch NVS. This corrects launcher.md §4's claim that "NVS is kept as is": that only holds for `pio run -t upload`.

**Step 0: before switching, empty internal pending items** (in both directions; written into the README, G-L6 and the 07 checklist):
- Get the status bar's ↑N to 0 (connect to the Mac and finish syncing); or boot once with the SD card in so migration empties `cpafs`, then confirm on the system page that internal pending is 0.
- Why: on a fully flashed device `cpafs` is at 0x670000; web-flashing Launcher replaces the partition table, and that region is then either unclaimed or later overwritten by an `ota_N` that Launcher places dynamically. In the other direction, going from Launcher back to a full flash abandons the 448KB `cpafs` from Launcher in the same way.

**Installing the Launcher build** (written into the README):
0. Empty internal pending items per step 0 above.
1. Install Launcher with the web flasher or esptool (the beta is suggested; see open question 4).
2. Put `cpa-<tag>.bin` on the SD card, preferably in `/cpa/fw/`.
3. In Launcher choose SD → choose the file → install.

**Upgrading under Launcher**:
- Overwrite install is recommended (the "Use <label> partition" option Launcher offers when space runs out, or serial `flash overwrite`), or delete the old version and reinstall the new one right away. **Do not** delete the old version and leave other versions lying around: deleting any one of our versions erases `cpafs` with it, and the remaining version has no storage when no card is in (NoStorage).
- With an SD card in, pending items are all on the card and unaffected; without a card, do step 0 first.

**Upgrading from v0.1 (full flash)**:
- Run `pio run -t upload` (keeps NVS), or write `cpa-<tag>.bin` to 0x0 (wipes NVS, the epoch is regenerated). Neither touches 0x670000; the new `cpafs` is the same region as v0.1's `spiffs`, so the data is all still there; with an SD card in, boot migrates automatically.
- If only `-app.bin` was written, the partition table is not updated. The system page then says "v0.1 partition table: flash it all". No data is lost; a full flash brings it back.

**What users should know**:
- Holding G0 at power-on enters the Launcher menu instead of opening our quick note.
- A cold start adds a Launcher splash of about 5 seconds.
- Flipping the power switch off and on goes to Launcher first.

### 4.5 Rules the firmware must follow (from launcher.md R1–R8)

- Mount and format only `cpafs`. If the partition does not exist, disable internal storage; never fall back to `spiffs`.
- Look partitions up by label at every boot; never cache offsets.
- Never write otadata or the partition table, never call `esp_ota_set_boot_partition`, never call `nvs_flash_erase`; v0.2 adds no NVS key.
- No deep sleep; always restart with `esp_restart()`.
- Launcher must be given the merged image, so that it creates `cpafs`.
- R7 (SD slow-down retry) is replaced by §3.9.

## 5. Budgets (updates 05 §5.13)

| Item | v0.1 | v0.2 | How it is checked |
|---|---|---|---|
| Static RAM | ≤96KB (59KB actual) | Same limit; new static buffers add at most 4KB (`move_file` 2KB, migration 2KB) | `check_budget.py` |
| Free heap / largest free block | ≥150KB / ≥64KB | Same targets, but measured **with SD mounted**; mounting SD adds at most 16KB of heap | device `test_runtime_budget`, `test_sd_store` |
| Loop stack headroom | ≥4KB | Unchanged. Call chains through `SdStore` get an extra 2KB FatFs margin [calc] (`FF_USE_LFN=2` puts a ~512B long-name buffer on the stack, plus VFS and SPI frames); new chains: ack received → `apply_outbox` → `move_file` → `SdStore`, and `setup` → `Runtime::begin` → `outbox_migrate` → `commit_file` → `SdStore` | `stack_budget.py`, `stack_gate.py`, device `test_runtime_budget` (including one rejected ack on SD) |
| App image | ≤2.5MB | Hard limit still 2.5MB; **warn above 1.75MiB (1,835,008B)**, the headroom for installing alongside Bruce 1.16.1 in Launcher | `check_budget.py` |
| Save | p95 ≤100ms (LittleFS with a 200-item backlog: 108–158ms, a known issue) | SD: p95 ≤100ms, also with a 200-item backlog; any single save ≤150ms; the first item per epoch per card (which creates the epoch directory) ≤250ms | device `test_sd_store` |
| Single-step blocking | ≤50ms | Exception for SD writes: the card pauses 30–65ms while tidying internally; a single save within 150ms passes. The one operation that discovers the error after a card pull stalls 0.6–2s [calc]; after that the store locks and the bus is not touched | device; manual |
| Preparing storage at boot | — | With a card: init, mount, `prepare()` (including the probe write) ≤200ms; without a card ≤500ms [todo]; scanning about 5ms per pending item [meas]; migration about 40ms per item, once only | device `test_sd_store`, `test_sd_absent` (timed) |
| Outbox capacity | 200 items, and ≥64KB free | SD: 200 items (limited by the size of the in-memory id array); `cpafs` 1.5MB: 200 items; `cpafs` 448KB: about 80 items [calc] | G-L4 |

## 6. Protocol changes (additive fields only, `v` stays 0, per 05 §6.9)

The device's `hello` reply gains two optional fields:

| Field | Values | Meaning |
|---|---|---|
| `store` | `"sd"` / `"int"` / `"none"` | The primary store chosen at this boot |
| `launcher` | bool | Whether it runs under Launcher |

- **Why they are needed**:
  - HIL must be able to confirm a scenario really went through SD. Otherwise, when SD is unusable, the firmware quietly falls back to internal storage and the scenario passes anyway.
  - When saving fails, the user can see why in `cpa status` (for example `none`).
- **Compatibility**: the device part of `hello.schema.json` has no `additionalProperties: false`, so the v0.1 daemon ignores both fields; the v0.2 daemon shows `-` for a v0.1 device (without them).
- **Not done**: no new message types, outbox format unchanged, `debug.state` unchanged.
- **Contract**:
  - Add two optional properties to the device part of `hello.schema.json`.
  - Test vectors: with both fields, valid; without them, also valid (v0.1 devices); `store:"fat"` invalid; `launcher:"yes"` invalid.
  - When the firmware sets neither field, the encoding is byte-identical to v0.1.
- **Guard rule for the Launcher console**: every line the host sends either starts with `{` or is empty (nudge). A daemon test guards this; `protocol/README` documents it.
- **Reserved for v0.3 (not implemented now)**:
  - The outbox gains `kind:"rec"` to register a recording (rec id, bytes, sha256, duration). It carries no execution semantics, so it fits the rule that "the outbox holds no execution items".
  - Messages for transferring recordings (the `voice.*` names are already reserved, see 05 §6.10).

## 7. daemon changes

1. Parse and keep `hello.store` and `hello.launcher`.
   - `cpa status` prints `store=sd launcher=yes`; in JSON output they sit under `device`.
   - When old firmware lacks both fields, it shows `-`.
2. Add a guard test: every host-initiated message, once serialised, starts with `{`; nudge is an empty line; no line ever starts with a letter.
3. **Short backoff for bound devices: deferred; G-L9 decides whether to do it.**
   - The worry: the user boots while the Mac is connected and the daemon is running. During Launcher's 5-second splash, the daemon's hello only gets `ERR …` (judged unrelated); with no valid reply to 3 hellos over 6 seconds, the result is `no_reply` and the port is suspended for 30 seconds. Our firmware is up after 5 seconds but then waits up to another 30 seconds to connect.
   - In the current code the suspension is recorded by serial number (`port_finder.Backoff` is keyed by serial), and re-enumerating the port does not lift it [src]. However, if Launcher's `ESP.restart()` makes the port disappear, the link ends for a different reason (not in the suspension list) and reconnects at once; and if Launcher does not use 303A:1001, PortFinder never touches it. Which case applies has to be measured [todo].
   - G-L9: with the daemon running and the device plugged into the Mac, boot under Launcher 10 times; each time our firmware must reach link ready within 12 seconds. If it fails, M2-C makes a small change: a port with a bound serial number that ends in `no_reply` is suspended for only 3 seconds; `foreign`, `version` and `open_failed` stay at 30 seconds.
4. Everything else unchanged: events, notes, dedupe by `(dev, id)`, credit, nudge.

## 8. UI changes

- **The system page gains two rows.** Raise the total row limit to match; if needed, merge the daemon row into another row.
  - The first row is the boot method: `Boot Launcher` or `Boot direct` (`启动 Launcher` / `启动 直接`).
  - The second row is storage: `Store SD 476G`, `Store Flash 1.5M` or `Store none` (`存储 SD 476G` / `存储 内部 1.5M` / `存储 无`), followed by the reason (`StoreNote`):
    - NoCard: `SD: no card` (`SD：未检测到卡`)
    - SdUnusable: `SD unusable: format FAT32 on a Mac` (`SD 卡无法使用：请在 Mac 上格式化为 FAT32`)
    - NoInternal: `No flash store: SD only` (`无内部存储，只用 SD`)
    - LegacyTable: `v0.1 partition table: flash it all` (`分区表是 v0.1 的，请整片刷写`)
    - NoStorage: `No storage: insert a FAT32 SD card` (`没有可用存储：请插入 FAT32 SD 卡`)
  - After a migration it shows `moved N` (`已迁移 N`); with failures, `failed N` (`迁移失败 N`); when the storage error count is not 0, `Store errors N: restart` (`存储错误 N，请重启`).
- **Notices when Compose submits**:
  - Primary store is None: "No storage: insert a FAT32 SD card". This needs a new `SaveResult::NoStorage`, checked **before Full** (NullStore's `free_bytes()` is 0, so otherwise Full would be reported first).
  - Write failure (including SD lock-up and a full card): "Save failed (E_STORAGE)", and the storage error count goes up by 1.
- **Migration at boot**: when it takes longer than 0.5 seconds, the screen shows "Moving to the SD card i/n".
- **Status bar**: unchanged.

## 9. Test strategy

### 9.1 native tests (in CI, written first per TDD)

| Suite | What it tests |
|---|---|
| `core/test_sd_paths` | Two-way mapping of logical and physical paths; invalid ids, over-long paths, lowercase and long names, the BAD directory; every generated name is a valid 8.3 name; `.tmp` has no mapping |
| `core/test_storage_plan` | Every row of the table in §3.4; combinations of launcher, legacy_spiffs and cpafs_bytes; the `lfs_shards` boundary (1MiB−1 and 1MiB) |
| `core/test_sd_store` | With FakeFatPort: path mapping; list joining back MonoIds, the epoch directory limit, other names reported as is; parent directories created on demand; `prepare()`'s probe write and its failures; `rename()` returns false without calling port; the root prefix; **after any `Disk` error, 0 calls to port**, and `free_bytes()` becomes 0 |
| `core/test_store_commit` | `commit_file` / `move_file` under both semantics; 0 rename calls in FAT mode; cleanup on failure |
| `core/test_outbox_fat` | FakeStore's FAT mode, with power loss injected at every step of save, ack, move to bad/ and project-cache write: 0 lost, every item that showed "Saved" is still there; a `.json` of length 0 is deleted; a `.tmp` is only counted, not deleted |
| `core/test_outbox_migrate` | Two FakeStores share one power source; re-run after power loss at every step: the result converges, 0 lost, the source ends empty; also covers a partial copy with the same id already on the target, a source with more than 200 items, and the source's bad/ left untouched |
| All v0.1 suites | Unchanged, and must still pass (goal G6); `test_outbox_boot_scan` and `test_outbox_core` are guarded with `git diff` so not one line changes |
| `ui/test_mod_system`, `ui/test_runtime_storage` | System page text for each `StoreNote`; NoStorage before Full; with a migration source, `Runtime::begin` migrates first, then scans; `hello` carries store and launcher; the project cache does not call rename on FAT; the storage error count goes up by 1 on a write failure |
| Protocol vectors and daemon unit tests | See §6, §7 |

**FakeStore's FAT mode** (only simulates what is needed, §14 S2):
- `atomic_rename()` returns false;
- rename fails when the target exists (like EEXIST);
- rename calls are counted, and tests assert 0;
- open creates a visible entry of length 0 right away (the existing FakeStore already does this), and it remains after power loss.

It does not simulate "rename split into two steps, leaving two names sharing a cluster chain": per the invariant, code on FAT never calls rename at all, so asserting 0 calls is enough.

**FakeFatPort** (`test/core/support/fake_fat_port.h`): physical paths and directories in memory; can inject `Disk`, `NotFound` and other errors on the k-th call; counts calls of each kind.

### 9.2 Real-device tests (`pio test -e cardputer-adv-test -f 'device/*'`)

- **`test_sd_store`** (card in; uses root `/cpatest`, deleted afterwards, never the real outbox):
  - `hal_sd_mount` plus `prepare()` takes ≤200ms, clock 20MHz;
  - write directly → read back → remount → read again;
  - list restores the MonoIds;
  - p95 and max of 200 saves, both "ack each item at once" and "full backlog"; the first item in a new epoch directory timed separately (≤250ms);
  - heap difference before and after mounting is at most 16KB;
  - LFS → SD migration of 50 items (from ≥20 different boot numbers), timed, 0 lost.
- **`test_sd_absent`** (card pulled, run by hand): `hal_sd_mount` returns Absent within ≤500ms.
- **`test_storage` (updated)**:
  - the partition label is `cpafs`; with a full flash its size is 1.5MB, using 32 shards;
  - part facts: on a direct boot, launcher is false and legacy_spiffs is false.
- **`test_runtime_budget` (updated, M2-B)**: measures heap, largest free block and stack headroom with SD mounted and primary; the workload includes one rejected ack (which goes through `move_file` on SD).

### 9.3 HIL (`tools/hil`, run by the agent that has the device to itself)

| Scenario | Firmware used | What it checks |
|---|---|---|
| `sd_empty` | `cardputer-adv-test`, card in | `hello.store=="sd"`; pending drops to 0 after connecting. Precondition for the migration scenarios |
| `sd_store` | Same | `hello.store=="sd"`; record 3 items offline → `debug.reboot` → each delivered exactly once |
| `sd_bulk` | Same | Record 100 items offline; after reconnecting, all delivered within 5 seconds, no duplicates |
| All v0.1 scenarios | The one above, and `cardputer-adv-test-nosd` | All pass; under nosd `hello.store=="int"` |
| `migrate_seed` → `migrate_check` → `migrate_drained` | First run `sd_empty` on test; then nosd → test → nosd in turn, switching firmware with `pio run -t upload` in between (keeps NVS and `cpafs`) | First seed N items in internal storage; after switching to the test firmware `store=="sd"`, every seeded item is delivered exactly once and nothing else appears; after switching back to nosd, `pending==0` |
| `launcher_boot` | A test merged image installed by Launcher | `hello.launcher==true`, `rst=="software"`; then run handshake, capture, reboot |

### 9.4 Manual checklist (added to 07)

- G-L1–G-L4 and G-L6–G-L8 from launcher.md §8. G-L5 is not tested (v0.2 has no "Back to Launcher"). G-L6's upgrade flow follows §4.4 (step 0, overwrite install).
- **G-L9 (new)**: with the Mac connected and the daemon running, boot under Launcher 10 times. Each time our firmware must reach link ready within 12 seconds; note whether a 30-second suspension appears.
- SD items:
  - 10 power cuts: right after "Saved" appears, flip the switch to OFF and unplug; 0 loss required. Results are recorded as low-strength evidence (R16).
  - Pull the card while running: at most one stall (≤2s), keys work normally afterwards; the system page shows a storage error; after reinserting the card and restarting, it works normally again.
  - Boot without a card: run device `test_sd_absent` and record the time.
  - Insert an exFAT card: the system page shows the "unusable, please format" notice.
  - **A `diskutil`-formatted card**: put the dev card in the Mac's card reader, reformat it with `diskutil eraseDisk FAT32 CPA MBRFormat /dev/diskN` (authorised; check the disk number twice), then run device `test_sd_store` and record p95, directory-creation time and write stalls (2 FATs, unknown alignment, R17).
  - Test 2–3 more cards, at least one under 32GB that ships as FAT32.
  - Use Disk Utility → First Aid on the Mac to check the card for lost clusters.

## 10. Work packages and dependencies

```
M2-A firmware storage layer ──┐
                              ├──► M2-B firmware integration, UI, Launcher release ──► M2-D HIL and checklist
M2-C protocol and daemon ─────┘                                                         ▲
        └───────────────────────────────────────────────────────────────────────────────┘
```

- A and C can run in parallel.
- B depends on A and C. But B's scripts, CI, README and the pure logic of the system page can start early against the interfaces fixed in §3.4 and §6.
- D depends on B and C.
- The paths each package owns do not overlap (see the work package list). A does not change `src/main.cpp`: the `LfsStore::begin()` signature is unchanged, so the existing wiring still compiles after A is merged.
- Rules as in 08: tests first (RED), then implement (GREEN), then refactor (REFACTOR). Changes stay in the working tree, uncommitted; the Lead commits everything.
- Precondition: the Lead commits the hwtest SD work first (`lib-core2/SD`, `sd_*.cpp`, the two gate reports) before A can move the driver.

## 11. Risks

| # | Risk | Impact | Mitigation / gate |
|---|---|---|---|
| R1 | Launcher's bootloader (IDF 5.5) cannot boot our IDF 4.4 app | The Launcher goal fails | Run G-L1 first; if it really fails, ship only the full image |
| R2 | Launcher's own SD driver cannot read this card (the same ACMD41 problem) | Cannot install from SD | G-L1; use another card, use the serial flasher, or propose a fix upstream (open question 5) |
| R3 | Lost clusters or cross-links after a FAT power cut | Leaked space; in rare cases a corrupted file | No rename, never delete `.tmp`; reclaim with Mac First Aid; 10 manual power cuts |
| R4 | Only one card tested | Other cards may fail to initialise | HCS only conforms to the SPI-mode spec [src]; test 2–3 more cards |
| R5 | With invalid FSINFO, `f_getfree` scans for about 35 seconds | Boot hangs | v0.2 never calls it |
| R6 | Card pulled while running | One 0.6–2s stall; after that all saves fail until a restart; pending items on the card are not counted in ↑N | SdStore locks up (§3.8); a notice; documented in the README |
| R7 | Under Launcher without SD, `cpafs` is only 448KB | Full at about 80 items [calc] | Use 4 shards; measure with G-L4; if not enough, reopen option B |
| R8 | Deleting any one of our versions in Launcher erases `cpafs` | Without SD, pending items are lost and the remaining version has no internal storage | README: step 0, prefer overwrite install; unaffected when SD is primary |
| R9 | core 2.0.17 erases the whole NVS when NVS is full | Launcher's settings are wiped too | v0.2 adds no NVS key |
| R10 | SD write stalls of 30–65ms exceed the 50ms single-step rule; HWCDC replies are held back | Occasional link latency | A single save ≤150ms counts as passing; the daemon already has nudge; HIL `sd_bulk` verifies 100 items in ≤5 seconds |
| R11 | Migrating 200 items at boot takes about 8 seconds, during which setup() does not reply | The daemon's 3 hellos may all time out and the port is suspended for 30 seconds | Happens only once; progress is shown on screen; acceptable |
| R12 | Expansion caps share the same SPI bus as SD (GPIO5 and others not pulled high) | SD unstable | G-L7; v0.2 does not support SD with an expansion cap fitted |
| R13 | Third-party code (Apache-2.0, about 870 lines) we maintain ourselves | — | Every patch marked `CPA patch`; listed in NOTICE; consider submitting upstream (open question 5) |
| R14 | Launcher's serial console treats host lines as commands; its console task starts before the splash | Launcher commands triggered by mistake; handshake slowed at boot | Host lines start only with `{` or are empty, guarded by a daemon test; G-L9 |
| R15 | Holding G0 at boot enters the Launcher menu; a cold start takes 5 seconds longer | User confusion | Documented in the README and PRD |
| R16 | A half-written directory sector, or a damaged paired page in the card's FTL, may destroy the directory entries of up to 15 saved items in the same sector | G4 is not truly proven at the physical level: 10 manual power cuts almost never hit this window of a few milliseconds | G4 only claims 0 loss at the protocol level; power-cut results are recorded as low-strength evidence; if the power-cut gate shows any loss, the release falls back to internal as primary with `CPA_STORAGE_NO_SD` |
| R17 | All performance figures come from FatFs-formatted cards (1 FAT, 4MiB aligned); `diskutil` formats with 2 FATs (every FAT change written twice) and unknown alignment | G3's p95 and directory-creation time may get worse | Re-run `test_sd_store` on a `diskutil`-formatted card (§9.4, authorised) |
| R18 | Cardless boot time depends on the MISO idle level | May exceed 500ms, worst case over 2s | Measure with `test_sd_absent`; if over, enable the internal pull-up on GPIO39 |
| R19 | Writing the merged image to 0x0 wipes NVS | Epoch and boot counter are regenerated | Harmless by design; docs and HIL do not promise they are kept (§4.4) |
| R20 | FatFs's long-name buffer and VFS and SPI frames are all on the loop stack; `stack_budget.py`'s current 1KB frame margin is not enough | Stack overflow | Chains through SdStore get an extra 2KB margin; two new chains; `test_runtime_budget` runs one rejected ack on SD |

## 12. Reserved for v0.3: recording (design only, not implemented now)

- **Layout**: `/cpa/rec/<E7>/<B5><first 3 of S>.W<last 1 of S>`, flat per epoch like the outbox; one MonoId per recording segment, a new segment every 5 minutes.
- **No "write `.part` first, then rename"** (rename is unsafe on FAT). Instead:
  1. Write the final file name directly, with a placeholder WAV header first, and fsync once a second;
  2. when a segment is finished, fill in the header, then fsync;
  3. then write an outbox `rec` item to register the segment. This registration is the "commit".
- **Half a segment left by a power cut**: the length in the header is 0, and the real size is taken from the directory entry (that is, the size at the last fsync). If the boot scan finds a segment that was never registered, it registers it; the Mac side repairs the header from the real size.
- **How it writes**: refill buffers in the mic's release callback, 4 buffers of 4KB in a ring, write each block as it fills, fsync once a second (sd-card.md §7 measured 0 dropped frames). `max_files` goes up to 3.
- **Free space**: add an FSINFO read with a bounded time, to show how much recording time is left.
- **Moving to the Mac**: base64 is only 138KB/s, so 1 hour of recording takes 14 minutes to transfer, too slow. v0.3 switches to binary framing with a sliding window, or USB drive mode; measure sustained HWCDC throughput before starting.
- **About 27% of replies held back while recording**: move card writes to a separate task, or rely on the daemon's nudge.
- **SD lock-up** (§3.8) applies to recording too: pulling the card while recording ends that segment.
- **Toolchain**: recording stays on toolchain A (ADR-0001; IDF 5.5 has the #18621 all-zero recording problem).

## 13. Effect on existing docs (changed by the Lead)

- **05 §5.9**: the "Outbox (LittleFS only)" statement, and the budget table in §5.13, are replaced by this document. Add a note in 05 pointing here.
- **04 §5.2**: what was in v0.2 moves, per the new roadmap, to v0.3 (offline recording) and v0.4 (hooks, approvals, replies). Also add two items: holding G0 at boot enters the Launcher menu; no deep sleep under Launcher.
- **launcher.md**: R7 (SD slow-down retry) is replaced by §3.9; §4's "NVS is kept as is" only holds for `pio run -t upload` (§4.4); G-L5 is not tested.
- **sd-card.md §8**: the two suggestions "keep the v0.2 outbox on LittleFS" and "directories per boot counter" are replaced by this document's "SD as primary, flat per epoch" (open question 1).
- **07**: M2-D adds G-L1–G-L4, G-L6–G-L9 and the manual SD items.
- **ADR**: if you accept "SD as primary" (open question 1), write it up as ADR-0003.

## 14. Review record

### 14.1 Blocking issues (all accepted)

| # | Issue | Resolution | Where |
|---|---|---|---|
| B1 | Calling `sdcard_init` and `sdcard_mount` separately cannot tell Absent from Unmountable: `sdcard_init` does not touch the card; "slow-down retry only when init succeeded" cannot be decided; uninit plus retry with no card exceeds 500ms | After a mount failure read `sdcard_type()`: NONE/UNKNOWN is Absent, SD/SDHC/MMC is Unusable; neither uninits nor retries (the slow-down retry itself is also cut, see S1); add device `test_sd_absent` with timing | §3.9 |
| B2 | `LfsStore::begin(shards)` changed the signature, but the caller belongs to M2-B, so M2-A's own build acceptance would fail to compile | `begin()` keeps its signature and finds `cpafs` and computes the shard count internally | §3.3 |
| B3 | "Delete a `.json` of length 0" was written as common to both stores, conflicting with the v0.1 test (move to bad/) | The two new rules apply only when `atomic_rename()==false`; not one line of the v0.1 tests changes, guarded with `git diff` | §3.6 |
| B4 | The first save of each boot creates two directories, `<E>` and `<B>`, 70–90ms each, exceeding 150ms | Simplified further: flat per epoch, no per-boot directories; the first item per epoch per card gets its own ≤250ms. `prepare_boot` was not adopted: it also needs empty-directory cleanup, and migrating items from many boots would still create a directory per boot number | §3.5 |
| B5 | Pulling the card while running makes the main loop stall 0.6–2s every time, not just fail saves | SdStore locks up on a disk-level error and never touches the bus again; the lock logic is in pure code and FakeFatPort asserts "0 calls afterwards"; a manual check confirms keys still respond | §3.8 |
| B6 | The install flow in the docs missed "empty pending first", so switching between full flash and Launcher would lose internal pending items | Step 0 added for both directions; written into the README, G-L6 and 07 | §4.4 |

### 14.2 Scope cuts (all accepted)

| # | Cut | Note |
|---|---|---|
| S1 | Drop the 4MHz slow-down retry | All five clock settings pass 5/5; the driver checks CRC16 on every block; cardless boot is faster too |
| S2 | FakeStore's FAT mode does not simulate the two-step rename | Assert 0 rename calls; only simulate "fails when the target exists" and "an entry of length 0 exists from open" |
| S3 | Do not migrate `cpafs`'s bad/ | Those items are never sent and are only evidence; one branch and one set of power-loss tests fewer |
| S4 | Defer the daemon's short backoff for bound devices | Decided by G-L9 measurements; one addition: the existing suspension is recorded by serial number and re-enumeration does not lift it, but if the port really disappears, the link's end reason is not in the suspension list, so it reconnects at once |
| S5 | Merge Unwritable into Unmountable | Merged into `SdState::Unusable` with one notice |

### 14.3 Risks raised by the review

| Risk | Resolution |
|---|---|
| 1. Migrating items from many boots would create a directory per boot number, possibly 20–25 seconds | Gone with flat per-epoch directories; the device test still times items from ≥20 boot numbers |
| 2. "ENOSPC → storage full" is not possible (`Runtime` reads free space only once at boot); NullStore would report Full first | Drop "storage full"; `NoStorage` is checked before Full (M2-B) |
| 3. `mkdir` alone cannot detect a read-only or full card | Add a probe write in `prepare()` |
| 4. A half-written directory sector may destroy neighbouring items; 10 power cuts cannot prove G4 | R16: G4 becomes a protocol-level claim; power-cut results are recorded as low-strength evidence; on any loss, fall back to internal as primary with the nosd switch |
| 5. Performance figures come only from FatFs-formatted cards | R17: re-run on a `diskutil`-formatted card (authorised) |
| 6. Stack: FatFs's long-name buffer is on the loop stack | R20: add a 2KB margin and two new chains; `test_runtime_budget` runs one rejected ack on SD (M2-B) |
| 7. Writing the merged image to 0x0 wipes NVS | R19; §4.4 corrects the statement |
| 8. Launcher's console is open during the splash, and no gate covers "boot with the Mac connected" | New G-L9 |
| 9. Deleting an old version in Launcher erases the shared `cpafs` | README recommends overwrite install (§4.4) |
| 10. HIL `migrate_check`'s "exactly N items" requires no pending items on SD beforehand | Run `sd_empty` first; `migrate_check` only accepts the seeded ids |
| 11. Launcher itself may be unable to read this card (R2) | Still open (open question 5) |
| 12. `cardputer-adv-test` lists its libraries explicitly; we must confirm the patched `SD` is linked | M2-A adds `storage` and checks the build log for `third_party/SD` |
