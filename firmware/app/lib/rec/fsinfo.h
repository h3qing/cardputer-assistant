// Bounded free-space read (docs/11 §4.3, 09 §12): f_getfree scans the whole FAT when
// FSINFO is not trusted (about 35 s on the 512 GB card), so hal reads three sectors
// instead and these pure functions interpret them:
//   sector 0  an MBR (first partition's start) or the FAT32 boot sector itself
//   boot      bytes/sector, sectors/cluster, the FSINFO sector, the cluster count
//   FSINFO    the free cluster count (0xFFFFFFFF: unknown)
// Anything unexpected means "unknown"; the device then says so and records anyway.
#pragma once

#include <cstdint>

#include "opt.h"

namespace cpa {
namespace rec {

constexpr uint32_t kSectorBytes = 512;

struct Fat32Boot {
  uint32_t fsinfo_lba = 0;
  uint32_t cluster_bytes = 0;
  uint32_t clusters = 0;
};

Opt<uint32_t> fat32_boot_lba(const uint8_t* sector0);  // 512 bytes
Opt<Fat32Boot> fat32_boot(const uint8_t* boot, uint32_t boot_lba);
Opt<uint64_t> fsinfo_free_bytes(const uint8_t* fsinfo, const Fat32Boot& b);

}  // namespace rec
}  // namespace cpa
