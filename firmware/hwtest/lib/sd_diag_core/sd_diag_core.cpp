#include "sd_diag_core.h"

#include <algorithm>
#include <cstring>

namespace sdd {

namespace {

constexpr uint32_t kMagic = 0x33465043u;  // "CPF3" as little-endian bytes
constexpr uint16_t kDamagedMax = 64;      // more differing bytes than this: not ours
constexpr uint32_t kTailSectors = 1u << 21;  // 1 GiB
constexpr size_t kTailPoints = 16;

char printable(uint8_t c) { return (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') ? static_cast<char>(c) : '?'; }

void copy_name(char* out, const uint8_t* in, size_t n) {
  for (size_t i = 0; i < n; ++i) out[i] = printable(in[i]);
  out[n] = '\0';
}

uint32_t be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

uint32_t rd32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

void wr32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }

uint32_t xorshift(uint32_t x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

uint64_t csd_sectors(const uint8_t* r, uint8_t structure, uint8_t read_bl_len) {
  if (structure == 1) {
    const uint32_t c_size = (r[7] & 0x3Fu) << 16 | r[8] << 8 | r[9];
    return (static_cast<uint64_t>(c_size) + 1) * 1024u;
  }
  if (structure == 2) {
    const uint32_t c_size = (r[6] & 0x0Fu) << 24 | r[7] << 16 | r[8] << 8 | r[9];
    return (static_cast<uint64_t>(c_size) + 1) * 1024u;
  }
  const uint32_t c_size = (r[6] & 0x03u) << 10 | r[7] << 2 | r[8] >> 6;
  const uint32_t mult = (r[9] & 0x03u) << 1 | r[10] >> 7;
  const uint64_t bytes = (static_cast<uint64_t>(c_size) + 1) << (mult + 2) << read_bl_len;
  return bytes / 512u;
}

// Appends v when it is in range and there is room; returns the new count.
size_t push(uint32_t* out, size_t k, size_t limit, uint32_t v, uint32_t first, uint32_t sectors) {
  if (k < limit && v >= first && v < sectors) out[k++] = v;
  return k;
}

uint16_t diff_bytes(const uint8_t* b, uint32_t lba, uint32_t seed) {
  uint8_t want[512];
  f3_fill(want, lba, seed);
  uint16_t bad = 0;
  for (size_t i = 0; i < sizeof(want); ++i) bad += b[i] != want[i];
  return bad;
}

bool all_bytes(const uint8_t* b, uint8_t v) {
  for (size_t i = 0; i < 512; ++i) if (b[i] != v) return false;
  return true;
}

}  // namespace

uint8_t crc7(const uint8_t* p, size_t n) {
  uint8_t crc = 0;
  for (size_t i = 0; i < n; ++i) {
    uint8_t d = p[i];
    for (int b = 0; b < 8; ++b, d <<= 1) {
      crc <<= 1;
      if ((d ^ crc) & 0x80) crc ^= 0x09;
    }
  }
  return static_cast<uint8_t>((crc << 1) | 1);
}

Cid parse_cid(const uint8_t r[16]) {
  Cid c;
  c.mid = r[0];
  copy_name(c.oid, r + 1, 2);
  copy_name(c.pnm, r + 3, 5);
  c.prv_major = r[8] >> 4;
  c.prv_minor = r[8] & 0x0F;
  c.psn = be32(r + 9);
  c.year = static_cast<uint16_t>(2000 + ((r[13] & 0x0F) << 4 | r[14] >> 4));
  c.month = r[14] & 0x0F;
  c.crc_ok = crc7(r, 15) == r[15];
  return c;
}

Csd parse_csd(const uint8_t r[16]) {
  Csd c;
  c.structure = r[0] >> 6;
  c.tran_speed = r[3];
  c.ccc = static_cast<uint16_t>(r[4] << 4 | r[5] >> 4);
  c.read_bl_len = r[5] & 0x0F;
  c.write_bl_len = static_cast<uint8_t>((r[12] & 0x03) << 2 | r[13] >> 6);
  c.sectors = csd_sectors(r, c.structure, c.read_bl_len);
  c.erase_blk_en = (r[10] & 0x40) != 0;
  c.sector_size = static_cast<uint8_t>((r[10] & 0x3F) << 1 | r[11] >> 7);
  c.perm_wp = (r[14] & 0x20) != 0;
  c.tmp_wp = (r[14] & 0x10) != 0;
  c.crc_ok = crc7(r, 15) == r[15];
  return c;
}

uint32_t au_kb(uint8_t code) {
  static const uint32_t kKb[16] = {0, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 12288, 16384, 24576, 32768, 65536};
  return kKb[code & 0x0F];
}

SdStatus parse_sd_status(const uint8_t s[64]) {
  static const uint8_t kClass[5] = {0, 2, 4, 6, 10};
  SdStatus st;
  st.bus_width = s[0] >> 6;
  st.card_type = static_cast<uint16_t>(s[2] << 8 | s[3]);
  st.speed_class = s[8] < 5 ? kClass[s[8]] : 0xFF;
  st.au_kb = au_kb(s[10] >> 4);
  st.erase_size = static_cast<uint16_t>(s[11] << 8 | s[12]);
  st.erase_timeout = s[13] >> 2;
  st.erase_offset = s[13] & 0x03;
  st.uhs_grade = s[14] >> 4;
  st.uhs_au_kb = au_kb(s[14] & 0x0F);
  st.video_class = s[15];
  st.app_perf = s[21] >> 4;
  return st;
}

size_t f3_plan(uint32_t sectors, uint32_t first, size_t n, uint32_t* out, size_t cap) {
  const size_t limit = std::min(n, cap);
  if (first >= sectors || limit == 0) return 0;
  size_t k = 0;
  for (uint32_t bit = 11; bit < 32; ++bit) {  // 2^11 sectors = 1 MiB
    k = push(out, k, limit, (1u << bit) - 1, first, sectors);
    k = push(out, k, limit, 1u << bit, first, sectors);
  }
  const uint32_t tail = std::min(kTailSectors, sectors - first);
  for (size_t i = 0; i < kTailPoints; ++i) {
    k = push(out, k, limit, sectors - 1 - static_cast<uint32_t>(i * (tail / kTailPoints)), first, sectors);
  }
  const size_t spread = limit - k;
  for (size_t i = 0; i < spread; ++i) {
    k = push(out, k, limit, first + static_cast<uint32_t>(static_cast<uint64_t>(sectors - first) * i / spread),
             first, sectors);
  }
  std::sort(out, out + k);
  return static_cast<size_t>(std::unique(out, out + k) - out);
}

void f3_fill(uint8_t* b, uint32_t lba, uint32_t seed) {
  wr32(b, kMagic);
  wr32(b + 4, lba);
  wr32(b + 8, seed);
  wr32(b + 12, ~lba ^ seed);
  uint32_t x = (lba * 0x9E3779B1u) ^ seed ^ 0xA5A5A5A5u;
  if (x == 0) x = 1;
  for (size_t i = 16; i < 512; i += 4) {
    x = xorshift(x);
    wr32(b + i, x);
  }
}

F3Check f3_check(const uint8_t* b, uint32_t lba, uint32_t seed) {
  F3Check c = {F3::Foreign, 0, 0};
  if (all_bytes(b, 0x00)) { c.kind = F3::Zeros; return c; }
  if (all_bytes(b, 0xFF)) { c.kind = F3::Ones; return c; }
  const uint32_t tag_lba = rd32(b + 4), tag_seed = rd32(b + 8);
  const bool tagged = rd32(b) == kMagic && rd32(b + 12) == (~tag_lba ^ tag_seed);
  if (tagged && tag_seed != seed) {
    c.kind = F3::OtherSeed;
    c.found_lba = tag_lba;
    return c;
  }
  if (tagged && tag_lba != lba) {
    c.kind = F3::OtherLba;
    c.found_lba = tag_lba;
    c.bad_bytes = diff_bytes(b, tag_lba, seed);
    return c;
  }
  c.bad_bytes = diff_bytes(b, lba, seed);
  if (c.bad_bytes == 0) c.kind = F3::Ok;
  else if (c.bad_bytes <= kDamagedMax) c.kind = F3::Damaged;
  c.found_lba = tagged ? tag_lba : 0;
  return c;
}

const char* f3_name(F3 k) {
  switch (k) {
    case F3::Ok: return "ok";
    case F3::OtherLba: return "alias";
    case F3::OtherSeed: return "stale";
    case F3::Zeros: return "zeros";
    case F3::Ones: return "ones";
    case F3::Damaged: return "damaged";
    default: return "foreign";
  }
}

size_t parse_uints(const char* s, uint32_t* out, size_t max) {
  size_t k = 0;
  while (k < max) {
    while (*s == ' ') ++s;
    if (*s < '0' || *s > '9') break;
    uint32_t v = 0;
    for (; *s >= '0' && *s <= '9'; ++s) {
      const uint32_t d = static_cast<uint32_t>(*s - '0');
      if (v > (UINT32_MAX - d) / 10) return k;
      v = v * 10 + d;
    }
    if (*s != ' ' && *s != '\0') break;
    out[k++] = v;
  }
  return k;
}

void fat32_count(const uint8_t* b, size_t n, uint32_t first_index, uint32_t n_fatent, FatCount* c) {
  for (size_t i = 0; i < n; ++i) {
    const uint32_t idx = first_index + static_cast<uint32_t>(i);
    if (idx < 2 || idx >= n_fatent) continue;
    const uint32_t v = rd32(b + i * 4) & 0x0FFFFFFFu;
    if (v == 0) c->free++;
    else if (v == 0x0FFFFFF7u) c->bad++;
    else c->used++;
  }
}

static const char* skip_tokens(const char* s, int n) {
  for (int i = 0; i < n; ++i) {
    while (*s == ' ') ++s;
    while (*s && *s != ' ') ++s;
  }
  while (*s == ' ') ++s;
  return s;
}

bool parse_blk(const char* s, BlkArgs* out) {
  uint32_t v[2];
  if (parse_uints(s, v, 2) != 2 || v[1] == 0 || v[1] > kBlkMax) return false;
  const char* mode = skip_tokens(s, 2);
  const bool one_letter = mode[0] != '\0' && (mode[1] == ' ' || mode[1] == '\0');
  if (!one_letter || (mode[0] != 's' && mode[0] != 'm')) return false;
  uint32_t khz = 0;
  const bool has_khz = parse_uints(mode + 1, &khz, 1) == 1;
  if (has_khz && (khz < 400 || khz > 40000)) return false;
  out->lba = v[0];
  out->count = static_cast<uint16_t>(v[1]);
  out->multi = mode[0] == 'm';
  out->khz = has_khz ? khz : 0;
  return true;
}

}  // namespace sdd
