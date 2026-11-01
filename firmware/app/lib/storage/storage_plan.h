// Storage selection at boot (docs/09-m2-design.md §3.4): pure function of the facts the
// hal collects (partition table, SD mount, LittleFS mount). The primary store is chosen
// once per boot and never switched while running:
//   SD Ok        + cpafs mounted -> Sd, migrate cpafs -> SD
//   SD Ok        + no cpafs      -> Sd (note LegacyTable or NoInternal)
//   Absent / Unusable / Disabled + cpafs mounted -> Internal (NoCard / SdUnusable / none)
//   no SD        + no cpafs      -> None (LegacyTable or NoStorage)
// "Legacy table" = a full-flash device whose partition table is still v0.1's (only the
// -app.bin was flashed): !launcher && legacy_spiffs && cpafs_bytes == 0. Under the
// Launcher the spiffs partition belongs to other firmware: never mounted, never hinted.
// Free space plays no part in the choice.
#pragma once

#include <cstdint>

namespace cpa {

struct PartFacts {
  bool launcher = false;       // running from an ota_N slot of the bmorcelli Launcher
  uint32_t cpafs_bytes = 0;    // size of the "cpafs" data partition, 0 if there is none
  bool legacy_spiffs = false;  // a data partition labelled "spiffs" exists
};

// Unusable: the card initialised but cannot be used (exFAT, no FAT, broken file system,
// mkdir or probe write failed); the user re-formats it on a Mac or swaps it.
enum class SdState : uint8_t { Disabled, Absent, Unusable, Ok };

struct SdFacts {
  SdState state = SdState::Disabled;
  uint32_t size_mib = 0;
};

enum class Store : uint8_t { None, Internal, Sd };
enum class StoreNote : uint8_t { None, NoCard, SdUnusable, NoInternal, LegacyTable, NoStorage };

struct StoragePlan {
  Store primary = Store::None;
  bool migrate = false;  // move cpafs's pending items to the SD card before the first scan
  StoreNote note = StoreNote::None;
};

StoragePlan plan_storage(const PartFacts& part, const SdFacts& sd, bool lfs_mounted);

// LittleFS shard directories for a cpafs partition of this size: 32 from 1 MiB up (the
// v0.1 layout), 4 below (the 448 KB partition the Launcher creates).
uint8_t lfs_shards(uint32_t part_bytes);

// The shard ('0'-'9', 'A'-'Z') holding an item whose MonoId ends in `last_digit`: the
// digit itself with 32 shards, its Crockford value mod 4 with 4. 0 for a character that
// is no Crockford digit or a shard count other than 4 or 32.
char lfs_shard(char last_digit, uint8_t shards);

}  // namespace cpa
