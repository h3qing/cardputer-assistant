#include "fsinfo.h"

#include <cstring>

namespace cpa {
namespace rec {
namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return le16(p) | (static_cast<uint32_t>(le16(p + 2)) << 16); }

bool signed_55aa(const uint8_t* s) { return s[510] == 0x55 && s[511] == 0xAA; }

bool is_fat32_boot(const uint8_t* s) {
  return signed_55aa(s) && (s[0] == 0xEB || s[0] == 0xE9) && std::memcmp(s + 82, "FAT32   ", 8) == 0;
}

}  // namespace

Opt<uint32_t> fat32_boot_lba(const uint8_t* s) {
  if (is_fat32_boot(s)) return some(uint32_t{0});  // no partition table ("superfloppy")
  if (!signed_55aa(s)) return {};
  const uint8_t type = s[446 + 4];
  if (type != 0x0B && type != 0x0C) return {};
  const uint32_t lba = le32(s + 446 + 8);
  return lba > 0 ? some(lba) : Opt<uint32_t>{};
}

Opt<Fat32Boot> fat32_boot(const uint8_t* s, uint32_t boot_lba) {
  if (!is_fat32_boot(s) || le16(s + 11) != kSectorBytes) return {};
  const uint8_t per_cluster = s[13];
  const uint32_t reserved = le16(s + 14);
  const uint32_t fats = s[16];
  const uint32_t total = le32(s + 32);
  const uint32_t fat_size = le32(s + 36);
  const uint32_t used = reserved + fats * fat_size;
  if (per_cluster == 0 || le16(s + 48) == 0 || total <= used) return {};
  Fat32Boot b;
  b.fsinfo_lba = boot_lba + le16(s + 48);
  b.cluster_bytes = per_cluster * kSectorBytes;
  b.clusters = (total - used) / per_cluster;
  return some(b);
}

Opt<uint64_t> fsinfo_free_bytes(const uint8_t* s, const Fat32Boot& b) {
  const bool sig = le32(s) == 0x41615252u && le32(s + 484) == 0x61417272u && le32(s + 508) == 0xAA550000u;
  const uint32_t free = le32(s + 488);
  if (!sig || free == 0xFFFFFFFFu || free > b.clusters) return {};
  return some(static_cast<uint64_t>(free) * b.cluster_bytes);
}

}  // namespace rec
}  // namespace cpa
