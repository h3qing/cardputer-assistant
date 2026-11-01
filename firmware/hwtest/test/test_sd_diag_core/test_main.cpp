// Host tests for lib/sd_diag_core (pure logic behind !sdid, !sdf3 and !sdblk,
// docs/gates/sd-card.md §11):  pio test -e native
#include <unity.h>

#include <cstring>

#include "sd_diag_core.h"

using namespace sdd;

void setUp() {}
void tearDown() {}

// ---- CRC7 ------------------------------------------------------------------------------

static void test_crc7_matches_the_well_known_command_crcs() {
  const uint8_t cmd0[5] = {0x40, 0, 0, 0, 0};
  const uint8_t cmd8[5] = {0x48, 0, 0, 0x01, 0xAA};
  TEST_ASSERT_EQUAL_HEX8(0x95, crc7(cmd0, 5));
  TEST_ASSERT_EQUAL_HEX8(0x87, crc7(cmd8, 5));
}

// ---- CID -------------------------------------------------------------------------------

static void seal(uint8_t* r) { r[15] = crc7(r, 15); }

static void test_cid_fields_and_crc() {
  uint8_t r[16] = {0x03, 'S', 'D', 'S', 'N', '5', '1', '2', 0x85, 0x12, 0x34, 0x56, 0x78, 0x01, 0x8A, 0};
  seal(r);
  const Cid c = parse_cid(r);
  TEST_ASSERT_EQUAL_HEX8(0x03, c.mid);
  TEST_ASSERT_EQUAL_STRING("SD", c.oid);
  TEST_ASSERT_EQUAL_STRING("SN512", c.pnm);
  TEST_ASSERT_EQUAL_UINT8(8, c.prv_major);
  TEST_ASSERT_EQUAL_UINT8(5, c.prv_minor);
  TEST_ASSERT_EQUAL_HEX32(0x12345678, c.psn);
  TEST_ASSERT_EQUAL_UINT16(2024, c.year);  // 0x18 + 2000
  TEST_ASSERT_EQUAL_UINT8(10, c.month);
  TEST_ASSERT_TRUE(c.crc_ok);
  r[3] ^= 0x01;
  TEST_ASSERT_FALSE(parse_cid(r).crc_ok);
}

static void test_cid_replaces_unprintable_name_bytes() {
  uint8_t r[16] = {0x00, 0x00, 0x7F, 'A', 0x00, '"', 'B', 0xFF, 0, 0, 0, 0, 0, 0, 0, 0};
  seal(r);
  const Cid c = parse_cid(r);
  TEST_ASSERT_EQUAL_STRING("??", c.oid);
  TEST_ASSERT_EQUAL_STRING("A??B?", c.pnm);
  TEST_ASSERT_EQUAL_UINT16(2000, c.year);
  TEST_ASSERT_EQUAL_UINT8(0, c.month);
}

// ---- CSD -------------------------------------------------------------------------------

static void test_csd_v2_capacity_of_the_project_card() {
  // 982,573,056 sectors = (C_SIZE + 1) * 1024 -> C_SIZE 959,543 = 0x0EA437
  uint8_t r[16] = {0x40, 0x0E, 0x00, 0x32, 0x5B, 0x59, 0x00, 0x0E, 0xA4, 0x37, 0x7F, 0x80, 0x0A, 0x40, 0x00, 0};
  seal(r);
  const Csd c = parse_csd(r);
  TEST_ASSERT_EQUAL_UINT8(1, c.structure);
  TEST_ASSERT_EQUAL_UINT32(982573056u, (uint32_t)c.sectors);
  TEST_ASSERT_EQUAL_HEX8(0x32, c.tran_speed);
  TEST_ASSERT_EQUAL_HEX16(0x5B5, c.ccc);
  TEST_ASSERT_EQUAL_UINT8(9, c.read_bl_len);
  TEST_ASSERT_EQUAL_UINT8(9, c.write_bl_len);  // [25:22]: byte 12 bits 1:0 = 10, byte 13 bits 7:6 = 01
  TEST_ASSERT_TRUE(c.erase_blk_en);            // byte 10 bit 6
  TEST_ASSERT_EQUAL_UINT8(0x7F, c.sector_size);
  TEST_ASSERT_FALSE(c.perm_wp);
  TEST_ASSERT_FALSE(c.tmp_wp);
  TEST_ASSERT_TRUE(c.crc_ok);
}

static void test_csd_v1_capacity_and_write_protect_bits() {
  // READ_BL_LEN 9, C_SIZE 4095, C_SIZE_MULT 7: 4096 * 512 * 512 B = 1 GiB
  uint8_t r[16] = {0x00, 0, 0, 0x32, 0x5F, 0x59, 0x03, 0xFF, 0xC0, 0x03, 0x80, 0, 0, 0, 0x30, 0};
  seal(r);
  const Csd c = parse_csd(r);
  TEST_ASSERT_EQUAL_UINT8(0, c.structure);
  TEST_ASSERT_EQUAL_UINT32(2097152u, (uint32_t)c.sectors);
  TEST_ASSERT_TRUE(c.perm_wp);
  TEST_ASSERT_TRUE(c.tmp_wp);
}

static void test_csd_v3_capacity() {
  // SDUC: C_SIZE is 28 bits [75:48]; 0x0100000 -> (0x100000 + 1) * 1024 sectors
  uint8_t r[16] = {0x80, 0, 0, 0x32, 0x5B, 0x59, 0x00, 0x10, 0x00, 0x00, 0, 0, 0, 0, 0, 0};
  seal(r);
  TEST_ASSERT_EQUAL_UINT64(0x100001ull * 1024u, parse_csd(r).sectors);
}

// ---- SD Status (ACMD13) ----------------------------------------------------------------

static void test_sd_status_speed_and_allocation_unit() {
  uint8_t s[64] = {0};
  s[0] = 0x80;               // DAT_BUS_WIDTH 2 (4-bit)
  s[8] = 0x04;               // SPEED_CLASS 4 -> class 10
  s[10] = 0x90;              // AU_SIZE 9 -> 4 MB
  s[11] = 0x00, s[12] = 0x10;  // ERASE_SIZE 16 AUs
  s[13] = (0x05 << 2) | 0x01;  // ERASE_TIMEOUT 5 s, ERASE_OFFSET 1
  s[14] = 0x13;              // UHS_SPEED_GRADE 1, UHS_AU_SIZE 3
  s[15] = 0x1E;              // VIDEO_SPEED_CLASS 30
  s[21] = 0x10;              // APP_PERF_CLASS 1
  const SdStatus st = parse_sd_status(s);
  TEST_ASSERT_EQUAL_UINT8(2, st.bus_width);
  TEST_ASSERT_EQUAL_UINT8(10, st.speed_class);
  TEST_ASSERT_EQUAL_UINT32(4096, st.au_kb);
  TEST_ASSERT_EQUAL_UINT16(16, st.erase_size);
  TEST_ASSERT_EQUAL_UINT8(5, st.erase_timeout);
  TEST_ASSERT_EQUAL_UINT8(1, st.erase_offset);
  TEST_ASSERT_EQUAL_UINT8(1, st.uhs_grade);
  TEST_ASSERT_EQUAL_UINT32(64, st.uhs_au_kb);
  TEST_ASSERT_EQUAL_UINT8(30, st.video_class);
  TEST_ASSERT_EQUAL_UINT8(1, st.app_perf);
}

static void test_sd_status_unknown_codes() {
  uint8_t s[64] = {0};
  s[8] = 0x09;  // reserved speed class code
  const SdStatus st = parse_sd_status(s);
  TEST_ASSERT_EQUAL_UINT8(0xFF, st.speed_class);
  TEST_ASSERT_EQUAL_UINT32(0, st.au_kb);  // AU_SIZE 0: not defined
  TEST_ASSERT_EQUAL_UINT32(65536, au_kb(0x0F));
  TEST_ASSERT_EQUAL_UINT32(12288, au_kb(0x0B));
}

// ---- F3-style capacity probe -----------------------------------------------------------

static bool contains(const uint32_t* v, size_t n, uint32_t x) {
  for (size_t i = 0; i < n; ++i) if (v[i] == x) return true;
  return false;
}

static void test_f3_plan_covers_boundaries_tail_and_spread() {
  const uint32_t sectors = 982573056u, first = 2048;
  uint32_t lba[160];
  const size_t n = f3_plan(sectors, first, 128, lba, 160);
  TEST_ASSERT_TRUE(n >= 120 && n <= 128);
  for (size_t i = 0; i < n; ++i) {
    TEST_ASSERT_TRUE(lba[i] >= first && lba[i] < sectors);
    if (i) TEST_ASSERT_TRUE(lba[i] > lba[i - 1]);  // ascending, no duplicates
  }
  TEST_ASSERT_TRUE(contains(lba, n, sectors - 1));
  for (uint32_t k = 12; k <= 29; ++k) {  // 2 MiB .. 256 GiB (2^29 sectors)
    TEST_ASSERT_TRUE(contains(lba, n, (1u << k) - 1));
    TEST_ASSERT_TRUE(contains(lba, n, 1u << k));
  }
  size_t tail = 0, top = 0;  // last GiB; top half of the card
  for (size_t i = 0; i < n; ++i) {
    tail += lba[i] >= sectors - (1u << 21);
    top += lba[i] >= sectors / 2;
  }
  TEST_ASSERT_TRUE(tail >= 16);
  TEST_ASSERT_TRUE(top >= 40);
}

static void test_f3_plan_small_card_and_cap() {
  uint32_t lba[64];
  const size_t n = f3_plan(1000000u, 0, 200, lba, 64);  // cap wins over the request
  TEST_ASSERT_TRUE(n > 40 && n <= 64);
  TEST_ASSERT_EQUAL_UINT32(999999u, lba[n - 1]);
  TEST_ASSERT_TRUE(contains(lba, n, (1u << 19) - 1));
  TEST_ASSERT_FALSE(contains(lba, n, 1u << 20));  // past the end
  TEST_ASSERT_EQUAL_UINT32(0, f3_plan(100, 200, 16, lba, 64));  // first past the end
}

static void test_f3_fill_round_trips_and_differs_per_lba() {
  uint8_t a[512], b[512];
  f3_fill(a, 1234, 77);
  f3_fill(b, 1235, 77);
  TEST_ASSERT_TRUE(memcmp(a, b, 512) != 0);
  const F3Check c = f3_check(a, 1234, 77);
  TEST_ASSERT_TRUE(c.kind == F3::Ok);
  TEST_ASSERT_EQUAL_UINT32(1234, c.found_lba);
  TEST_ASSERT_EQUAL_STRING("ok", f3_name(c.kind));
}

static void test_f3_check_names_aliases_and_stale_data() {
  uint8_t b[512];
  f3_fill(b, 900000000u, 5);  // what a wrapping fake returns at a low LBA
  F3Check c = f3_check(b, 12345, 5);
  TEST_ASSERT_TRUE(c.kind == F3::OtherLba);
  TEST_ASSERT_EQUAL_UINT32(900000000u, c.found_lba);
  TEST_ASSERT_EQUAL_STRING("alias", f3_name(c.kind));
  f3_fill(b, 12345, 4);  // an earlier run's data: the write did not land
  c = f3_check(b, 12345, 5);
  TEST_ASSERT_TRUE(c.kind == F3::OtherSeed);
  TEST_ASSERT_EQUAL_STRING("stale", f3_name(c.kind));
}

static void test_f3_check_blank_damaged_and_foreign() {
  uint8_t b[512];
  memset(b, 0x00, sizeof(b));
  TEST_ASSERT_TRUE(f3_check(b, 1, 1).kind == F3::Zeros);
  memset(b, 0xFF, sizeof(b));
  TEST_ASSERT_TRUE(f3_check(b, 1, 1).kind == F3::Ones);
  f3_fill(b, 42, 9);
  b[100] ^= 0x10;
  b[300] ^= 0x01;
  F3Check c = f3_check(b, 42, 9);
  TEST_ASSERT_TRUE(c.kind == F3::Damaged);
  TEST_ASSERT_EQUAL_UINT16(2, c.bad_bytes);
  b[0] ^= 0x01;  // header hit too: still recognisably this sector
  TEST_ASSERT_TRUE(f3_check(b, 42, 9).kind == F3::Damaged);
  for (size_t i = 0; i < sizeof(b); ++i) b[i] = (uint8_t)(i * 7 + 3);
  TEST_ASSERT_TRUE(f3_check(b, 42, 9).kind == F3::Foreign);
  TEST_ASSERT_EQUAL_STRING("foreign", f3_name(F3::Foreign));
}

// ---- argument parsing ------------------------------------------------------------------

static void test_parse_uints() {
  uint32_t v[4] = {0};
  TEST_ASSERT_EQUAL_UINT32(3, parse_uints(" 128  2048 7", v, 4));
  TEST_ASSERT_EQUAL_UINT32(128, v[0]);
  TEST_ASSERT_EQUAL_UINT32(2048, v[1]);
  TEST_ASSERT_EQUAL_UINT32(7, v[2]);
  TEST_ASSERT_EQUAL_UINT32(1, parse_uints("5 x 6", v, 4));  // stops at the first non-number
  TEST_ASSERT_EQUAL_UINT32(2, parse_uints("1 2 3", v, 2));  // never writes past max
  TEST_ASSERT_EQUAL_UINT32(0, parse_uints("", v, 4));
  TEST_ASSERT_EQUAL_UINT32(0, parse_uints("99999999999", v, 4));  // overflow is not a number
}

static void test_parse_blk() {
  BlkArgs a;
  TEST_ASSERT_TRUE(parse_blk("139808 32 m 4000", &a));
  TEST_ASSERT_EQUAL_UINT32(139808, a.lba);
  TEST_ASSERT_EQUAL_UINT16(32, a.count);
  TEST_ASSERT_TRUE(a.multi);
  TEST_ASSERT_EQUAL_UINT32(4000, a.khz);
  TEST_ASSERT_TRUE(parse_blk("7 1 s", &a));
  TEST_ASSERT_FALSE(a.multi);
  TEST_ASSERT_EQUAL_UINT32(0, a.khz);  // keep the current clock
  TEST_ASSERT_FALSE(parse_blk("7 0 s", &a));
  TEST_ASSERT_FALSE(parse_blk("7 65 m", &a));
  TEST_ASSERT_FALSE(parse_blk("7 4 x", &a));
  TEST_ASSERT_FALSE(parse_blk("7 4 m 300", &a));
  TEST_ASSERT_FALSE(parse_blk("7 4 m 40001", &a));
  TEST_ASSERT_FALSE(parse_blk("7", &a));
}

// ---- FAT32 table scan ------------------------------------------------------------------

static void put32(uint8_t* b, size_t i, uint32_t v) {
  b[i * 4] = (uint8_t)v, b[i * 4 + 1] = (uint8_t)(v >> 8), b[i * 4 + 2] = (uint8_t)(v >> 16),
  b[i * 4 + 3] = (uint8_t)(v >> 24);
}

static void test_fat32_count_skips_reserved_entries_and_masks_top_bits() {
  uint8_t b[8 * 4] = {0};
  put32(b, 0, 0x0FFFFFF8);  // media entry: not a cluster
  put32(b, 1, 0xFFFFFFFF);  // reserved: not a cluster
  put32(b, 2, 0x0FFFFFFF);  // end of chain: used
  put32(b, 3, 0xF0000000);  // top 4 bits are reserved: free
  put32(b, 4, 0x0FFFFFF7);  // bad cluster
  put32(b, 5, 6);           // chain link: used
  FatCount c = {0, 0, 0};
  fat32_count(b, 8, 0, 7, &c);  // n_fatent 7: entry 7 is past the table
  TEST_ASSERT_EQUAL_UINT32(2, c.free);  // entries 3 and 6
  TEST_ASSERT_EQUAL_UINT32(1, c.bad);
  TEST_ASSERT_EQUAL_UINT32(2, c.used);
  fat32_count(b, 8, 128, 200, &c);  // a later sector: every entry is a cluster; adds up
  TEST_ASSERT_EQUAL_UINT32(2 + 3, c.free);  // 3, 6, 7
  TEST_ASSERT_EQUAL_UINT32(1 + 1, c.bad);
  TEST_ASSERT_EQUAL_UINT32(2 + 4, c.used);  // 0, 1 (end-of-chain values), 2, 5
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc7_matches_the_well_known_command_crcs);
  RUN_TEST(test_cid_fields_and_crc);
  RUN_TEST(test_cid_replaces_unprintable_name_bytes);
  RUN_TEST(test_csd_v2_capacity_of_the_project_card);
  RUN_TEST(test_csd_v1_capacity_and_write_protect_bits);
  RUN_TEST(test_csd_v3_capacity);
  RUN_TEST(test_sd_status_speed_and_allocation_unit);
  RUN_TEST(test_sd_status_unknown_codes);
  RUN_TEST(test_f3_plan_covers_boundaries_tail_and_spread);
  RUN_TEST(test_f3_plan_small_card_and_cap);
  RUN_TEST(test_f3_fill_round_trips_and_differs_per_lba);
  RUN_TEST(test_f3_check_names_aliases_and_stale_data);
  RUN_TEST(test_f3_check_blank_damaged_and_foreign);
  RUN_TEST(test_parse_uints);
  RUN_TEST(test_parse_blk);
  RUN_TEST(test_fat32_count_skips_reserved_entries_and_masks_top_bits);
  return UNITY_END();
}
