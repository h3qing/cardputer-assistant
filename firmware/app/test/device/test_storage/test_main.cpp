// On-device storage tests (06 WP4, G10 part 1; 05 §5.9; docs/09-m2-design.md §9.2):
//   - partitions: LittleFS lives on "cpafs" (1.5 MB on a full flash, 32 shards), there
//     is no "spiffs" partition, and the part facts of a direct boot say so;
//   - LittleFS: write a .tmp, close, rename to .json, read back, remount, read again;
//     rename replaces atomically, open_write truncates, remove and list behave like
//     FakeStore (the native tests' model);
//   - NVS: the boot counter is advanced and persisted before a ticket is issued;
//   - NVS (docs/21 §6.8): English is saved and loaded back, a byte other than 0 or 1 in
//     cpa/lang loads as 中文, and the value found is put back as it was;
//   - NVS (v0.5.2): on an NVS with "boot", a missing cpa/lang is written 0 (Chinese) once,
//     and a stored one is left alone;
//   - 200 outbox saves (tmp -> close -> rename): p95 reported against the 100 ms target,
//     asserted <= 200 ms (LittleFS misses the target, a known issue: see kSaveTargetUs).
// The 200 timing files use a line that is NOT a valid outbox.item: should the suite
// die half way, the firmware's boot scan moves leftovers to bad/ and never sends them.
#include <Arduino.h>
#include <unity.h>

#include <esp_partition.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Preferences.h>

#include <cstring>

#include "usb_transport.h"  // listed here so the dependency finder sees it
#include "device_harness.h"
#include "lfs_store.h"
#include "nvs_boot.h"
#include "nvs_settings.h"
#include "outbox_io.h"
#include "part_facts.h"

namespace {

cpa::UsbTransport g_usb;
cpa::LfsStore g_fs;
constexpr uint32_t kTestEpoch = 0x7E570000u;  // ids of the timing files
constexpr int kSaves = 200;
// Save p95 (docs/09-m2-design.md §5): the 100 ms target is met on SD (device/test_sd_store,
// G3). LittleFS, the fallback, misses it, a known issue since v0.1 (108-158 ms with 200
// items kept; 101-120 ms on 2026-10-20), so here the target is only reported. The limit
// still catches a real regression, such as losing the shards (513 ms, lfs_store.h).
constexpr uint32_t kSaveTargetUs = 100000;
constexpr uint32_t kLfsSaveP95MaxUs = 200000;
char g_buf[2100];
uint32_t g_us[kSaves];

std::string read_all(const char* path) {
  const long n = g_fs.read(path, g_buf, sizeof g_buf);
  return n < 0 ? std::string("<missing>") : std::string(g_buf, static_cast<size_t>(n));
}

bool put(const char* path, const char* text) {
  return g_fs.open_write(path) && g_fs.write(text, std::strlen(text)) && g_fs.close();
}

class Names : public cpa::DirVisitor {
 public:
  std::string all;
  void entry(const char* name, bool is_dir) override { all += std::string(name) + (is_dir ? "/ " : " "); }
};

cpa::MonoId timing_id(int i) {
  cpa::MonoId id;
  id.epoch = kTestEpoch;
  id.boot = 1;
  id.seq = static_cast<uint32_t>(i);
  return id;
}

void remove_timing_files() {
  for (int i = 0; i < kSaves; ++i) {
    g_fs.remove(cpa::outbox_path(timing_id(i), cpa::OutboxFile::Json).c_str());
    g_fs.remove(cpa::outbox_path(timing_id(i), cpa::OutboxFile::Tmp).c_str());
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_mount_and_capacity() {
  TEST_ASSERT_TRUE(g_fs.begin());
  TEST_ASSERT_TRUE(g_fs.total_bytes() >= 1024u * 1024u);
  TEST_ASSERT_TRUE(g_fs.free_bytes() >= 64u * 1024u);
  TEST_ASSERT_EQUAL_UINT8(32, g_fs.shards());  // 1.5 MB: v0.1's layout
}

// Full flash, direct boot: the v0.2 table (partitions/cpa_8MB.csv), not under the Launcher.
static void test_partition_label_and_part_facts() {
  const esp_partition_t* cpafs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "cpafs");
  TEST_ASSERT_NOT_NULL(cpafs);
  TEST_ASSERT_EQUAL_HEX32(0x670000, cpafs->address);
  TEST_ASSERT_EQUAL_HEX32(0x180000, cpafs->size);
  TEST_ASSERT_NULL(esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs"));
  const cpa::PartFacts f = cpa::hal_part_facts();
  Serial.printf("CPA_PART launcher=%d cpafs=%lu legacy_spiffs=%d\n", f.launcher ? 1 : 0,
                static_cast<unsigned long>(f.cpafs_bytes), f.legacy_spiffs ? 1 : 0);
  TEST_ASSERT_FALSE(f.launcher);
  TEST_ASSERT_FALSE(f.legacy_spiffs);
  TEST_ASSERT_EQUAL_UINT32(0x180000, f.cpafs_bytes);
}

static void test_write_close_rename_reopen() {
  g_fs.mkdir("/devtest");
  g_fs.remove("/devtest/a.json");
  TEST_ASSERT_TRUE(put("/devtest/a.tmp", "hello"));
  TEST_ASSERT_TRUE(g_fs.rename("/devtest/a.tmp", "/devtest/a.json"));
  TEST_ASSERT_EQUAL_STRING("hello", read_all("/devtest/a.json").c_str());
  TEST_ASSERT_EQUAL_STRING("<missing>", read_all("/devtest/a.tmp").c_str());
  g_fs.end();  // remount: what was renamed is still there
  TEST_ASSERT_TRUE(g_fs.begin());
  TEST_ASSERT_EQUAL_STRING("hello", read_all("/devtest/a.json").c_str());
}

static void test_rename_replaces_and_open_truncates() {
  TEST_ASSERT_TRUE(put("/devtest/b.tmp", "second"));
  TEST_ASSERT_TRUE(g_fs.rename("/devtest/b.tmp", "/devtest/a.json"));  // over an existing file
  TEST_ASSERT_EQUAL_STRING("second", read_all("/devtest/a.json").c_str());
  TEST_ASSERT_TRUE(put("/devtest/a.json", "x"));  // open_write truncates
  TEST_ASSERT_EQUAL_STRING("x", read_all("/devtest/a.json").c_str());
  TEST_ASSERT_FALSE(g_fs.rename("/devtest/none.tmp", "/devtest/c.json"));
}

static void test_list_mkdir_remove() {
  TEST_ASSERT_TRUE(g_fs.mkdir("/devtest/sub"));
  TEST_ASSERT_TRUE(g_fs.mkdir("/devtest/sub"));  // already there: fine
  Names names;
  TEST_ASSERT_TRUE(g_fs.list("/devtest", names));
  TEST_ASSERT_TRUE(names.all.find("a.json ") != std::string::npos);
  TEST_ASSERT_TRUE(names.all.find("sub/ ") != std::string::npos);
  TEST_ASSERT_TRUE(g_fs.remove("/devtest/a.json"));
  TEST_ASSERT_FALSE(g_fs.remove("/devtest/a.json"));
  TEST_ASSERT_EQUAL_INT(-1, g_fs.read("/devtest/a.json", g_buf, sizeof g_buf));
  TEST_ASSERT_FALSE(g_fs.list("/devtest/nothing", names));
  TEST_ASSERT_FALSE(g_fs.write("x", 1));  // nothing open
  TEST_ASSERT_FALSE(g_fs.close());
  ::rmdir("/littlefs/devtest/sub");  // leave nothing behind
  ::rmdir("/littlefs/devtest");
}

static void test_nvs_boot_counter_advances_and_persists() {
  const cpa::BootRecord before = cpa::nvs_boot_read();
  const cpa::BootTicket t = cpa::nvs_boot_ticket();
  TEST_ASSERT_TRUE(t.valid());
  const cpa::BootRecord after = cpa::nvs_boot_read();  // reopened from flash
  TEST_ASSERT_EQUAL_UINT32(t.boot(), after.boot);
  TEST_ASSERT_EQUAL_UINT32(t.epoch(), after.epoch);
  if (before.epoch != 0) {
    TEST_ASSERT_EQUAL_UINT32(before.epoch, after.epoch);
    TEST_ASSERT_EQUAL_UINT32(before.boot + 1, after.boot);
  }
  Serial.printf("CPA_NVS epoch=%08lx boot=%lu (was %lu)\n", static_cast<unsigned long>(after.epoch),
                static_cast<unsigned long>(after.boot), static_cast<unsigned long>(before.boot));
}

// LittleFS lookups are linear in the size of a directory, so LfsStore keeps outbox
// items in 32 small shard directories (/outbox/_<last MonoId digit>/) and still shows
// them as direct children of /outbox, exactly like FakeStore does.
namespace {

// cpa/lang as it is now: has = false when the key is missing.
struct LangByte {
  bool has = false;
  uint8_t b = 0;
};

LangByte lang_byte() {
  Preferences p;
  LangByte v;
  if (!p.begin("cpa", true)) return v;
  v.has = p.isKey("lang");
  v.b = p.getUChar("lang", 0);
  p.end();
  return v;
}

bool put_lang_byte(const LangByte& v) {
  Preferences p;
  if (!p.begin("cpa", false)) return false;
  const bool ok = v.has ? p.putUChar("lang", v.b) == 1 : (!p.isKey("lang") || p.remove("lang"));
  p.end();
  return ok;
}

}  // namespace

static void test_settings_lang_round_trip_and_a_bad_byte() {
  const LangByte was = lang_byte();
  TEST_ASSERT_TRUE(cpa::nvs_settings_save(cpa::with_lang(cpa::Settings{}, cpa::Lang::En)));
  const bool en = cpa::nvs_settings_load().lang == cpa::Lang::En;
  TEST_ASSERT_TRUE(put_lang_byte(LangByte{true, 7}));
  const bool bad_is_zh = cpa::nvs_settings_load().lang == cpa::Lang::Zh;
  TEST_ASSERT_TRUE(put_lang_byte(was));  // before the checks, so a failure leaves it as found
  const LangByte now = lang_byte();
  Serial.printf("CPA_NVS_LANG was=%s%u en=%d bad_is_zh=%d\n", was.has ? "" : "missing:", was.b, en, bad_is_zh);
  TEST_ASSERT_TRUE(en);
  TEST_ASSERT_TRUE(bad_is_zh);
  TEST_ASSERT_TRUE(now.has == was.has && (!was.has || now.b == was.b));
}

// v0.5.2 (docs/21 §6.8): nvs_lang_migrate(), which src/main.cpp runs before the ticket. This
// NVS has "boot" (test_nvs_boot_counter_advances_and_persists ran first): with no "lang" it
// writes 0 (Chinese), and nothing on a second call; a "lang" already there is left alone. The
// fresh case (no "boot") is test_settings' and an erased-NVS device run's: removing
// "boot" here would restart this NVS's ids.
static void test_settings_lang_migration_keeps_an_older_device_chinese() {
  const LangByte was = lang_byte();
  TEST_ASSERT_TRUE(put_lang_byte(LangByte{false, 0}));
  const bool wrote = cpa::nvs_lang_migrate();
  const LangByte zh = lang_byte();
  const bool again = cpa::nvs_lang_migrate();
  TEST_ASSERT_TRUE(put_lang_byte(LangByte{true, 1}));
  const bool over_en = cpa::nvs_lang_migrate();
  const LangByte en = lang_byte();
  TEST_ASSERT_TRUE(put_lang_byte(was));  // before the checks, so a failure leaves it as found
  Serial.printf("CPA_NVS_LANG_MIGRATE wrote=%d zh=%d:%u again=%d over_en=%d en=%u\n", wrote, zh.has, zh.b, again,
                over_en, en.b);
  TEST_ASSERT_TRUE(cpa::nvs_boot_read().boot != 0);
  TEST_ASSERT_TRUE(wrote);
  TEST_ASSERT_TRUE(zh.has && zh.b == 0);
  TEST_ASSERT_FALSE(again);
  TEST_ASSERT_FALSE(over_en);
  TEST_ASSERT_TRUE(en.has && en.b == 1);
}

static void test_outbox_items_are_sharded_but_listed_flat() {
  const cpa::MonoId id = timing_id(7);
  const auto json = cpa::outbox_path(id, cpa::OutboxFile::Json);
  const auto bad = cpa::outbox_path(id, cpa::OutboxFile::Bad);
  TEST_ASSERT_TRUE(cpa::outbox_persist(g_fs, id, "{}\n", 3));
  const std::string name = std::string(json.c_str() + 8);  // after "/outbox/"
  struct stat st;
  const std::string shard = std::string("/littlefs/outbox/_") + name[17] + "/" + name;
  TEST_ASSERT_EQUAL_INT(0, ::stat(shard.c_str(), &st));
  Names names;
  TEST_ASSERT_TRUE(g_fs.list("/outbox", names));
  TEST_ASSERT_TRUE(names.all.find(name + " ") != std::string::npos);
  TEST_ASSERT_TRUE(names.all.find("_") == std::string::npos);  // shard dirs never show
  TEST_ASSERT_EQUAL_STRING("{}\n", read_all(json.c_str()).c_str());
  g_fs.mkdir("/outbox/bad");
  TEST_ASSERT_TRUE(g_fs.rename(json.c_str(), bad.c_str()));  // MoveToBad leaves the shard
  TEST_ASSERT_EQUAL_STRING("{}\n", read_all(bad.c_str()).c_str());
  TEST_ASSERT_TRUE(g_fs.remove(bad.c_str()));
}

// One save = tmp -> write -> close (commit) -> rename, timed; `keep` leaves the item in
// /outbox (the directory grows to capacity), otherwise it is acked (removed) at once.
static uint32_t timed_saves(bool keep, const char* label) {
  remove_timing_files();
  char line[256];
  std::snprintf(line, sizeof line, "{\"v\":0,\"t\":\"cpa.devtest\",\"pad\":\"%0180d\"}\n", 0);
  const size_t n = std::strlen(line);
  int ok = 0;
  uint32_t worst = 0;
  for (int i = 0; i < kSaves; ++i) {
    const uint32_t t0 = micros();
    ok += cpa::outbox_persist(g_fs, timing_id(i), line, n) ? 1 : 0;
    g_us[i] = micros() - t0;
    worst = g_us[i] > worst ? g_us[i] : worst;
    if (keep && i % 50 == 0) Serial.printf("CPA_SAVE_AT n_in_dir=%d us=%lu\n", i, static_cast<unsigned long>(g_us[i]));
    if (!keep) g_fs.remove(cpa::outbox_path(timing_id(i), cpa::OutboxFile::Json).c_str());
  }
  const uint32_t p95 = devtest::p95(g_us, kSaves);
  Serial.printf("CPA_SAVE %s n=%d ok=%d p50_us=%lu p95_us=%lu max_us=%lu bytes=%u\n", label, kSaves, ok,
                static_cast<unsigned long>(g_us[kSaves / 2]), static_cast<unsigned long>(p95),
                static_cast<unsigned long>(worst), static_cast<unsigned>(n));
  remove_timing_files();
  TEST_ASSERT_EQUAL_INT(kSaves, ok);
  return p95;
}

static void check_save_p95(bool keep, const char* label) {
  const uint32_t p95 = timed_saves(keep, label);
  Serial.printf("CPA_SAVE_TARGET %s p95_us=%lu target_us=%lu %s\n", label, static_cast<unsigned long>(p95),
                static_cast<unsigned long>(kSaveTargetUs), p95 <= kSaveTargetUs ? "met" : "missed (known on LittleFS)");
  TEST_ASSERT_TRUE(p95 <= kLfsSaveP95MaxUs);
}

// Online: every item is acked before the next one (the normal path).
static void test_200_saves_acked_p95_within_200ms() { check_save_p95(false, "acked"); }

// Offline worst case (04 US-01 AC4, G10): the outbox fills to its 200-item capacity.
static void test_200_saves_accumulated_p95_within_200ms() { check_save_p95(true, "accumulated"); }

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_mount_and_capacity);
  RUN_TEST(test_partition_label_and_part_facts);
  RUN_TEST(test_write_close_rename_reopen);
  RUN_TEST(test_rename_replaces_and_open_truncates);
  RUN_TEST(test_list_mkdir_remove);
  RUN_TEST(test_nvs_boot_counter_advances_and_persists);
  RUN_TEST(test_settings_lang_round_trip_and_a_bad_byte);
  RUN_TEST(test_settings_lang_migration_keeps_an_older_device_chinese);
  RUN_TEST(test_outbox_items_are_sharded_but_listed_flat);
  RUN_TEST(test_200_saves_acked_p95_within_200ms);
  RUN_TEST(test_200_saves_accumulated_p95_within_200ms);
  UNITY_END();
}

void loop() { devtest::idle(); }
