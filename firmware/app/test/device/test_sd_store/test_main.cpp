// On-device SD store tests (docs/09-m2-design.md §9.2; needs the FAT32 card inserted).
// Everything lives below the test root /cpatest, removed at the start and the end: the
// real /cpa outbox is never touched. Budgets (§5, G3):
//   - hal_sd_mount + prepare() at 20 MHz <= 200 ms, with the root already on the card;
//     the mount adds <= 16 KB of heap;
//   - written -> read back -> remounted -> read again; list() restores the MonoIds;
//   - 200 saves, each acked at once and all kept: p95 <= 100 ms, max <= 150 ms; the
//     first item of a new epoch directory (cluster cleared) <= 250 ms;
//   - LittleFS -> SD migration of 50 items from 25 boot numbers: timed, none lost.
// The timing and migration items use a line that is NOT a valid outbox.item, and the
// migration moves only the test ids: leftovers in cpafs are quarantined by the firmware's
// boot scan, never sent, and the user's own pending items stay where they are.
#include <Arduino.h>
#include <SPI.h>
#include <dirent.h>
#include <unity.h>

#include <unistd.h>

#include <cstring>
#include <string>

#include "usb_transport.h"  // listed here so the dependency finder sees it
#include "device_harness.h"
#include "lfs_store.h"
#include "outbox_io.h"
#include "outbox_migrate.h"
#include "sd_card.h"
#include "sd_store.h"

namespace {

constexpr char kRoot[] = "/cpatest";
constexpr char kVfsRoot[] = "/sd/cpatest";
constexpr uint32_t kTestEpoch = 0x3E570001u;  // < 2^32 with a leading Crockford digit <= 3
constexpr int kSaves = 200;
constexpr int kMigrate = 50;

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_port;
cpa::SdStore g_sd(g_port, kRoot);
cpa::LfsStore g_lfs;
char g_buf[2100];
uint32_t g_us[kSaves];
char g_line[256];

cpa::MonoId test_id(uint32_t boot, uint32_t seq) {
  cpa::MonoId id;
  id.epoch = kTestEpoch;
  id.boot = boot;
  id.seq = seq;
  return id;
}

size_t test_line() {
  std::snprintf(g_line, sizeof g_line, "{\"v\":0,\"t\":\"cpa.devtest\",\"pad\":\"%0180d\"}\n", 0);
  return std::strlen(g_line);
}

// rm -r below a VFS path, bounded depth; returns the entries it could not delete.
int remove_tree(const std::string& path, int depth = 0) {
  DIR* d = ::opendir(path.c_str());
  if (!d) return 0;
  int failed = 0;
  while (const dirent* e = ::readdir(d)) {
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
    const std::string child = path + "/" + e->d_name;
    if (e->d_type == DT_DIR && depth < 4) failed += remove_tree(child, depth + 1);
    const int rc = e->d_type == DT_DIR ? ::rmdir(child.c_str()) : ::unlink(child.c_str());
    failed += rc == 0 ? 0 : 1;
  }
  ::closedir(d);
  return failed;
}

std::string read_sd(const char* path) {
  const long n = g_sd.read(path, g_buf, sizeof g_buf);
  return n < 0 ? std::string("<missing>") : std::string(g_buf, static_cast<size_t>(n));
}

class Names : public cpa::DirVisitor {
 public:
  std::string all;
  void entry(const char* name, bool is_dir) override { all += std::string(name) + (is_dir ? "/ " : " "); }
};

class Kick : public cpa::MigrateObserver {
 public:
  void on_item(uint16_t, uint16_t) override { (void)static_cast<bool>(Serial); }
};

}  // namespace

void setUp() {}
void tearDown() {}

static void test_mount_and_prepare() {
  const cpa::SdFacts f = cpa::hal_sd_mount(g_sd);
  Serial.printf("CPA_SD_FIRST state=%d size_mib=%lu hz=%lu\n", static_cast<int>(f.state),
                static_cast<unsigned long>(f.size_mib), static_cast<unsigned long>(spiClockDivToFrequency(SPI.getClockDivider())));
  TEST_ASSERT_TRUE(f.state == cpa::SdState::Ok);
  TEST_ASSERT_TRUE(f.size_mib > 0);
  TEST_ASSERT_EQUAL_INT(0, remove_tree(kVfsRoot));  // leftovers of an earlier run
  TEST_ASSERT_TRUE(g_sd.prepare());
  TEST_ASSERT_FALSE(g_sd.failed());
  TEST_ASSERT_EQUAL_UINT32(20000000u, spiClockDivToFrequency(SPI.getClockDivider()));
}

// The boot path: the root exists, the card is mounted once and probed.
static void test_write_read_remount_within_200ms_and_16kb() {
  const std::string item = std::string("/outbox/") + cpa::mono_format(test_id(1, 1)).c_str() + ".json";
  TEST_ASSERT_TRUE(g_sd.open_write(item.c_str()) && g_sd.write("hello", 5) && g_sd.close());
  TEST_ASSERT_EQUAL_STRING("hello", read_sd(item.c_str()).c_str());
  cpa::hal_sd_end();
  const uint32_t heap0 = ESP.getFreeHeap();
  const uint32_t t0 = micros();
  const cpa::SdFacts f = cpa::hal_sd_mount(g_sd);
  const uint32_t us = micros() - t0;
  const int32_t heap = static_cast<int32_t>(heap0) - static_cast<int32_t>(ESP.getFreeHeap());
  Serial.printf("CPA_SD_MOUNT us=%lu heap_delta=%ld\n", static_cast<unsigned long>(us), static_cast<long>(heap));
  TEST_ASSERT_TRUE(f.state == cpa::SdState::Ok);
  TEST_ASSERT_TRUE(us <= 200000u);
  TEST_ASSERT_TRUE(heap <= 16 * 1024);
  TEST_ASSERT_EQUAL_STRING("hello", read_sd(item.c_str()).c_str());
  TEST_ASSERT_TRUE(g_sd.remove(item.c_str()));
}

static void test_list_restores_mono_ids() {
  const size_t n = test_line();
  for (uint32_t s = 0; s < 3; ++s) TEST_ASSERT_TRUE(cpa::outbox_persist(g_sd, test_id(2, s), g_line, n));
  Names names;
  TEST_ASSERT_TRUE(g_sd.list("/outbox", names));
  Serial.printf("CPA_SD_LIST %s\n", names.all.c_str());
  for (uint32_t s = 0; s < 3; ++s) {
    const std::string name = std::string(cpa::mono_format(test_id(2, s)).c_str()) + ".json ";
    TEST_ASSERT_TRUE(names.all.find(name) != std::string::npos);
    TEST_ASSERT_TRUE(g_sd.remove(cpa::outbox_path(test_id(2, s), cpa::OutboxFile::Json).c_str()));
  }
}

// One save = open the final name -> write -> fsync + close (commit_file on FAT). The
// epoch directory is removed first, so item 0 pays for creating it and is timed apart.
static void timed_saves(bool keep, const char* label) {
  const std::string epoch_dir = std::string(kVfsRoot) + "/outbox/" + cpa::crockford_encode(kTestEpoch, 7).c_str();
  remove_tree(epoch_dir);
  ::rmdir(epoch_dir.c_str());
  const size_t n = test_line();
  uint32_t t0 = micros();
  TEST_ASSERT_TRUE(cpa::outbox_persist(g_sd, test_id(3, 0), g_line, n));
  const uint32_t first = micros() - t0;
  int ok = 0;
  uint32_t worst = 0;
  for (int i = 0; i < kSaves; ++i) {
    const cpa::MonoId id = test_id(3, static_cast<uint32_t>(i + 1));
    t0 = micros();
    ok += cpa::outbox_persist(g_sd, id, g_line, n) ? 1 : 0;
    g_us[i] = micros() - t0;
    worst = g_us[i] > worst ? g_us[i] : worst;
    if (!keep) g_sd.remove(cpa::outbox_path(id, cpa::OutboxFile::Json).c_str());
  }
  const uint32_t p95 = devtest::p95(g_us, kSaves);
  Serial.printf("CPA_SD_SAVE %s n=%d ok=%d first_us=%lu p50_us=%lu p95_us=%lu max_us=%lu\n", label, kSaves, ok,
                static_cast<unsigned long>(first), static_cast<unsigned long>(g_us[kSaves / 2]),
                static_cast<unsigned long>(p95), static_cast<unsigned long>(worst));
  remove_tree(epoch_dir);
  TEST_ASSERT_EQUAL_INT(kSaves, ok);
  TEST_ASSERT_TRUE(first <= 250000u);
  TEST_ASSERT_TRUE(p95 <= 100000u);
  TEST_ASSERT_TRUE(worst <= 150000u);
}

static void test_200_saves_acked() { timed_saves(false, "acked"); }
static void test_200_saves_accumulated() { timed_saves(true, "accumulated"); }

// 50 items from 25 boot numbers in cpafs move to the card; only the test ids migrate.
static void test_migrate_50_items_from_littlefs() {
  TEST_ASSERT_TRUE(g_lfs.begin());
  const size_t n = test_line();
  cpa::OutboxCore core;
  for (uint32_t i = 0; i < kMigrate; ++i) {
    const cpa::MonoId id = test_id(10 + i / 2, i % 2);
    TEST_ASSERT_TRUE(cpa::outbox_persist(g_lfs, id, g_line, n));
    TEST_ASSERT_TRUE(cpa::outbox_insert_pending(core, id));
  }
  Kick kick;
  const uint32_t t0 = millis();
  const cpa::MigrateResult r = cpa::outbox_migrate(g_lfs, g_sd, core, kick);
  const uint32_t ms = millis() - t0;
  Serial.printf("CPA_SD_MIGRATE n=%d moved=%u failed=%u ms=%lu per_item_ms=%lu\n", kMigrate, r.moved, r.failed,
                static_cast<unsigned long>(ms), static_cast<unsigned long>(ms / kMigrate));
  int on_sd = 0;
  int left = 0;
  for (uint16_t i = 0; i < core.n; ++i) {
    const auto path = cpa::outbox_path(core.ids[i], cpa::OutboxFile::Json);
    on_sd += read_sd(path.c_str()) == std::string(g_line, n) ? 1 : 0;
    left += g_lfs.read(path.c_str(), g_buf, sizeof g_buf) >= 0 ? 1 : 0;
    g_lfs.remove(path.c_str());
  }
  g_lfs.end();
  TEST_ASSERT_EQUAL_UINT16(kMigrate, r.moved);
  TEST_ASSERT_EQUAL_INT(kMigrate, on_sd);
  TEST_ASSERT_EQUAL_INT(0, left);
}

static void test_cleanup() {
  TEST_ASSERT_EQUAL_INT(0, remove_tree(kVfsRoot));
  TEST_ASSERT_EQUAL_INT(0, ::rmdir(kVfsRoot));
  TEST_ASSERT_FALSE(g_sd.failed());
  cpa::hal_sd_end();
}

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_mount_and_prepare);
  RUN_TEST(test_write_read_remount_within_200ms_and_16kb);
  RUN_TEST(test_list_restores_mono_ids);
  RUN_TEST(test_200_saves_acked);
  RUN_TEST(test_200_saves_accumulated);
  RUN_TEST(test_migrate_50_items_from_littlefs);
  RUN_TEST(test_cleanup);
  UNITY_END();
}

void loop() { devtest::idle(); }
