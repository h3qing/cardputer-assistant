// Pure logic behind the SD diagnostics in hwtest (docs/gates/sd-card.md §11): card
// register decoding (CID, CSD, SD Status), the F3-style capacity probe (which LBAs, what
// each sector holds, what a read-back means) and argument parsing. No Arduino or esp_*
// headers; C++11; unit-tested on the host with `pio test -e native`.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sdd {

// SD CRC7 (x^7 + x^3 + 1) over n bytes, returned as the frame byte (crc << 1) | 1.
uint8_t crc7(const uint8_t* p, size_t n);

struct Cid {
  uint8_t mid;            // manufacturer id (SD-3C assigned)
  char oid[3];            // OEM / application id, printable or '?'
  char pnm[6];            // product name
  uint8_t prv_major, prv_minor;
  uint32_t psn;           // serial number: a device identifier, keep it out of the docs
  uint16_t year;
  uint8_t month;
  bool crc_ok;
};
Cid parse_cid(const uint8_t r[16]);

struct Csd {
  uint8_t structure;      // 0 v1 (SDSC), 1 v2 (SDHC/SDXC), 2 v3 (SDUC)
  uint8_t tran_speed;     // 0x32 = 25 MHz, 0x5A = 50 MHz
  uint16_t ccc;           // command classes
  uint8_t read_bl_len, write_bl_len;
  uint64_t sectors;       // 512 B sectors
  bool erase_blk_en;
  uint8_t sector_size;    // erase sector, in write blocks - 1
  bool perm_wp, tmp_wp;
  bool crc_ok;
};
Csd parse_csd(const uint8_t r[16]);

struct SdStatus {
  uint8_t bus_width;      // 0 = 1 bit (SPI), 2 = 4 bit
  uint16_t card_type;
  uint8_t speed_class;    // 0 2 4 6 10, 0xFF reserved code
  uint32_t au_kb;         // allocation unit, 0 = not defined
  uint16_t erase_size;    // AUs per erase
  uint8_t erase_timeout, erase_offset;
  uint8_t uhs_grade;      // U1 / U3
  uint32_t uhs_au_kb;
  uint8_t video_class;    // V6..V90
  uint8_t app_perf;       // A1 / A2
};
SdStatus parse_sd_status(const uint8_t s[64]);
uint32_t au_kb(uint8_t code);  // AU_SIZE / UHS_AU_SIZE code -> KB, 0 when not defined

// F3-style probe: up to min(n, cap) ascending, distinct LBAs in [first, sectors): every
// 2^k - 1 and 2^k from 1 MiB up, 16 spread over the last GiB (sectors - 1 included),
// the rest spread evenly. Returns how many were written to out.
size_t f3_plan(uint32_t sectors, uint32_t first, size_t n, uint32_t* out, size_t cap);

// One 512 B sector tagged with its LBA and the run's seed, so a read-back tells where
// the data came from (an alias of another LBA, an earlier run, or nowhere).
void f3_fill(uint8_t* b, uint32_t lba, uint32_t seed);

enum class F3 : uint8_t { Ok, OtherLba, OtherSeed, Zeros, Ones, Damaged, Foreign };
struct F3Check {
  F3 kind;
  uint32_t found_lba;  // the LBA in a readable tag (Ok, OtherLba, OtherSeed), else 0
  uint16_t bad_bytes;  // bytes that differ from what the tag says (Damaged, OtherLba)
};
F3Check f3_check(const uint8_t* b, uint32_t lba, uint32_t seed);
const char* f3_name(F3 k);  // ok alias stale zeros ones damaged foreign

// Up to max unsigned decimal numbers separated by spaces; stops at the first token that
// is not one (or overflows 32 bits). Returns how many were parsed.
size_t parse_uints(const char* s, uint32_t* out, size_t max);

constexpr uint16_t kBlkMax = 64;  // sectors per !sdblk (32 KB scratch)
struct BlkArgs {
  uint32_t lba;
  uint16_t count;  // 1..kBlkMax
  bool multi;      // m: one CMD25 / CMD18; s: CMD24 / CMD17 per sector
  uint32_t khz;    // 400..40000, 0 = keep the current clock
};
bool parse_blk(const char* s, BlkArgs* out);  // "<lba> <count> <s|m> [khz]"

// FAT32 table scan: entries [first_index, first_index + n) of the FAT in b (4 bytes
// each, little-endian), counting only clusters 2..n_fatent-1; the top 4 bits are masked.
struct FatCount {
  uint32_t free, bad, used;
};
void fat32_count(const uint8_t* b, size_t n, uint32_t first_index, uint32_t n_fatent, FatCount* c);

}  // namespace sdd
