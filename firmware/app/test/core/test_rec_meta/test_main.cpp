// Segment files on the card and their .M commit marker (docs/11 §4.4):
//   /cpa/rec/<E7>/<B5><S first 3>.W<S last>   the WAV
//   /cpa/rec/<E7>/<B5><S first 3>.M<S last>   its metadata, JSON <= 512 B
// A .M commits its segment only when it parses and names the same id as its file name.
#include <unity.h>

#include <cstring>
#include <string>

#include "fsinfo.h"
#include "rec_meta.h"
#include "rec_paths.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

static MonoId mid(const char* s) { return mono_parse(s, 18).value; }

static void test_paths_are_8_3_names_per_epoch() {
  const MonoId id = mid("3QZK1M7-00042-0A1C");
  TEST_ASSERT_EQUAL_STRING("/cpa/rec", rec_dir("/cpa").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/rec/3QZK1M7", rec_epoch_dir("/cpa", id).c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/rec/3QZK1M7/000420A1.WC", rec_wav_path("/cpa", id).c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/rec/3QZK1M7/000420A1.MC", rec_meta_path("/cpa", id).c_str());
  TEST_ASSERT_EQUAL_STRING("/cpatest/rec/3QZK1M7/000420A1.WC", rec_wav_path("/cpatest", id).c_str());
}

static void test_names_parse_back() {
  const Opt<RecName> w = rec_parse_name("3QZK1M7", "000420A1.WC");
  TEST_ASSERT_TRUE(w.has && w.value.wav);
  TEST_ASSERT_TRUE(w.value.id == mid("3QZK1M7-00042-0A1C"));
  const Opt<RecName> m = rec_parse_name("3QZK1M7", "000420A1.MC");
  TEST_ASSERT_TRUE(m.has && !m.value.wav);
  TEST_ASSERT_FALSE(rec_parse_name("3QZK1M7", "000420a1.WC").has);   // lower case
  TEST_ASSERT_FALSE(rec_parse_name("3QZK1M7", "000420A1.JC").has);   // an outbox item
  TEST_ASSERT_FALSE(rec_parse_name("3QZK1M7", "000420A1.W").has);
  TEST_ASSERT_FALSE(rec_parse_name("3QZK1M7", "000420A1.WCX").has);
  TEST_ASSERT_FALSE(rec_parse_name("ZZZZZZZ", "000420A1.WC").has);   // epoch over 32 bits
  TEST_ASSERT_FALSE(rec_parse_name("3QZK1M", "000420A1.WC").has);
  TEST_ASSERT_TRUE(rec_is_epoch_dir("3QZK1M7"));
  TEST_ASSERT_FALSE(rec_is_epoch_dir("BAD"));
}

static RecMeta meeting_seg0() {
  RecMeta m;
  m.id = mid("3QZK1M7-00042-0A1C");
  m.rec = m.id;
  m.seg = 0;
  m.last = false;
  m.next = some(mid("3QZK1M7-00042-0A1D"));
  m.mode = RecMode::Meeting;
  m.bytes = 9600000;
  m.sha256 = some(Sha256Hex::from("15fcf5fe0175f8bc4181c75ef77e9cefa15c87406a9eee55fc98eb4a21fbbd5f"));
  proto::RecAt at;
  at.up_ms = 735000;
  at.wall_ms = some(int64_t{1792440345000LL});
  m.at = some(at);
  m.project = some(ProjectKey::from("cpa"));
  m.n_stars = 1;
  m.stars[0] = 12800;
  m.lead_ms = some(uint16_t{150});
  m.consent = true;
  m.fw = FixedStr<31>::from("cpa-fw/0.3.0");
  return m;
}

static void test_encode_matches_the_design_example() {
  const MetaText t = rec_meta_encode(meeting_seg0());
  TEST_ASSERT_EQUAL_STRING(
      "{\"v\":1,\"id\":\"3QZK1M7-00042-0A1C\",\"rec\":\"3QZK1M7-00042-0A1C\",\"seg\":0,\"last\":false,"
      "\"next\":\"3QZK1M7-00042-0A1D\",\"mode\":\"meeting\",\"bytes\":9600000,"
      "\"sha256\":\"15fcf5fe0175f8bc4181c75ef77e9cefa15c87406a9eee55fc98eb4a21fbbd5f\","
      "\"at\":{\"up_ms\":735000,\"wall_ms\":1792440345000},\"project\":\"cpa\",\"stars\":[12800],\"lead_ms\":150,"
      "\"consent\":\"ack\",\"fw\":\"cpa-fw/0.3.0\"}",
      t.c_str());
}

static void test_round_trip_and_optional_fields() {
  const RecMeta a = meeting_seg0();
  const Opt<RecMeta> b = rec_meta_decode(rec_meta_encode(a).c_str(), rec_meta_encode(a).size(), a.id);
  TEST_ASSERT_TRUE(b.has);
  TEST_ASSERT_TRUE(rec_meta_encode(b.value) == rec_meta_encode(a));
  RecMeta r;  // a recovered memo segment: no next, no sha, no at, slot only
  r.id = mid("3QZK1M7-00042-0A1E");
  r.rec = r.id;
  r.last = true;
  r.bytes = 1953792;
  r.slot = 3;
  r.rcv = true;
  r.fw = FixedStr<31>::from("cpa-fw/0.3.0");
  const MetaText t = rec_meta_encode(r);
  TEST_ASSERT_TRUE(std::string(t.c_str()).find("\"at\":null") != std::string::npos);
  TEST_ASSERT_TRUE(std::string(t.c_str()).find("\"slot\":3") != std::string::npos);
  TEST_ASSERT_TRUE(std::string(t.c_str()).find("\"rcv\":true") != std::string::npos);
  TEST_ASSERT_TRUE(std::string(t.c_str()).find("sha256") == std::string::npos);
  const Opt<RecMeta> back = rec_meta_decode(t.c_str(), t.size(), r.id);
  TEST_ASSERT_TRUE(back.has && back.value.rcv && !back.value.at.has && back.value.slot == 3);
  TEST_ASSERT_TRUE(rec_meta_encode(back.value) == t);
}

static void test_worst_case_fits_512_bytes() {
  RecMeta m = meeting_seg0();
  m.seg = 9999;
  m.bytes = 19200000;
  m.at.value.up_ms = 4294967295u;
  m.at.value.wall_ms = some(int64_t{9007199254740991LL});
  m.project = some(ProjectKey::from("abcdefghijkl"));
  m.n_stars = 16;
  for (int i = 0; i < 16; ++i) m.stars[i] = 599999;
  m.lead_ms = some(uint16_t{2000});
  m.rcv = true;
  m.fw = FixedStr<31>::from("cpa-fw/0123456789012345678901");
  const MetaText t = rec_meta_encode(m);
  TEST_ASSERT_TRUE(t.size() <= kMetaMax);
  TEST_ASSERT_TRUE(rec_meta_decode(t.c_str(), t.size(), m.id).has);
}

static void test_decode_rejects() {
  const RecMeta a = meeting_seg0();
  const std::string good = rec_meta_encode(a).c_str();
  TEST_ASSERT_FALSE(rec_meta_decode(good.c_str(), good.size(), mid("3QZK1M7-00042-0A1D")).has);  // id != file
  TEST_ASSERT_FALSE(rec_meta_decode("", 0, a.id).has);
  TEST_ASSERT_FALSE(rec_meta_decode(good.c_str(), good.size() - 1, a.id).has);  // cut short
  auto with = [&](const std::string& from, const std::string& to) {
    std::string s = good;
    s.replace(s.find(from), from.size(), to);
    return rec_meta_decode(s.c_str(), s.size(), a.id).has;
  };
  TEST_ASSERT_FALSE(with("\"v\":1", "\"v\":2"));
  TEST_ASSERT_FALSE(with("\"mode\":\"meeting\"", "\"mode\":\"call\""));
  TEST_ASSERT_FALSE(with("\"seg\":0", "\"seg\":10000"));
  TEST_ASSERT_FALSE(with("\"bytes\":9600000", "\"bytes\":-2"));
  TEST_ASSERT_FALSE(with("15fcf5fe", "15FCF5FE"));
  TEST_ASSERT_FALSE(with("\"rec\":\"3QZK1M7-00042-0A1C\"", "\"rec\":\"nope\""));
  TEST_ASSERT_FALSE(with("[12800]", "[600000]"));
  TEST_ASSERT_TRUE(with("\"fw\":", "\"x_future\":1,\"fw\":"));  // unknown fields are ignored
}

static void test_item_of_a_segment() {
  const RecMeta a = meeting_seg0();
  const proto::FileItem it = rec_item_of(a, 44 + 9600000);
  TEST_ASSERT_TRUE(it.id.equals("3QZK1M7-00042-0A1C") && it.rec.equals("3QZK1M7-00042-0A1C"));
  TEST_ASSERT_EQUAL_UINT32(9600044, it.size);
  TEST_ASSERT_EQUAL_UINT32(300000, it.dur_ms);
  TEST_ASSERT_TRUE(it.sha256.has && !it.rcv.has && it.consent && it.lead_ms.value == 150);
  TEST_ASSERT_TRUE(it.has_stars && it.n_stars == 1 && it.project.value.equals("cpa"));
  // the file is longer than the .M says (§4.6 last row): actual size, no sha, rcv
  const proto::FileItem odd = rec_item_of(a, 44 + 9604096);
  TEST_ASSERT_EQUAL_UINT32(44 + 9604096, odd.size);
  TEST_ASSERT_EQUAL_UINT32(300128, odd.dur_ms);
  TEST_ASSERT_TRUE(!odd.sha256.has && odd.rcv.has && odd.rcv.value);
}

// The bounded free-space read (docs/11 §4.3): MBR -> FAT32 boot sector -> FSINFO.
static void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
static void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v));
  put16(p + 2, static_cast<uint16_t>(v >> 16));
}

static void boot_sector(uint8_t* s) {
  std::memset(s, 0, 512);
  s[0] = 0xEB;
  put16(s + 11, 512);
  s[13] = 64;          // 32 KB clusters
  put16(s + 14, 32);   // reserved
  s[16] = 2;           // FATs
  put32(s + 32, 1000000032u);
  put32(s + 36, 100000);
  put16(s + 48, 1);    // FSINFO right after the boot sector
  std::memcpy(s + 82, "FAT32   ", 8);
  s[510] = 0x55;
  s[511] = 0xAA;
}

static void test_fsinfo_free_space() {
  uint8_t mbr[512] = {};
  mbr[446 + 4] = 0x0C;
  put32(mbr + 446 + 8, 32768);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;
  TEST_ASSERT_EQUAL_UINT32(32768, fat32_boot_lba(mbr).value);
  uint8_t boot[512];
  boot_sector(boot);
  TEST_ASSERT_EQUAL_UINT32(0, fat32_boot_lba(boot).value);  // no partition table
  const Opt<Fat32Boot> b = fat32_boot(boot, 32768);
  TEST_ASSERT_TRUE(b.has && b.value.fsinfo_lba == 32769 && b.value.cluster_bytes == 32768);
  uint8_t fsi[512] = {};
  put32(fsi, 0x41615252u);
  put32(fsi + 484, 0x61417272u);
  put32(fsi + 488, 1000);
  put32(fsi + 508, 0xAA550000u);
  TEST_ASSERT_TRUE(fsinfo_free_bytes(fsi, b.value).value == 1000ull * 32768);
  put32(fsi + 488, 0xFFFFFFFFu);
  TEST_ASSERT_FALSE(fsinfo_free_bytes(fsi, b.value).has);  // not maintained: unknown
  put32(fsi + 488, b.value.clusters + 1);
  TEST_ASSERT_FALSE(fsinfo_free_bytes(fsi, b.value).has);  // impossible: unknown
  put32(fsi + 488, 5);
  fsi[0] = 0;
  TEST_ASSERT_FALSE(fsinfo_free_bytes(fsi, b.value).has);
  mbr[446 + 4] = 0x07;  // exFAT / NTFS
  TEST_ASSERT_FALSE(fat32_boot_lba(mbr).has);
  mbr[510] = 0;
  TEST_ASSERT_FALSE(fat32_boot_lba(mbr).has);
  boot[13] = 0;
  TEST_ASSERT_FALSE(fat32_boot(boot, 0).has);
  boot_sector(boot);
  boot[82] = 'X';
  TEST_ASSERT_FALSE(fat32_boot(boot, 0).has);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_paths_are_8_3_names_per_epoch);
  RUN_TEST(test_names_parse_back);
  RUN_TEST(test_encode_matches_the_design_example);
  RUN_TEST(test_round_trip_and_optional_fields);
  RUN_TEST(test_worst_case_fits_512_bytes);
  RUN_TEST(test_decode_rejects);
  RUN_TEST(test_item_of_a_segment);
  RUN_TEST(test_fsinfo_free_space);
  return UNITY_END();
}
