// plan_storage (docs/09-m2-design.md §3.4): every row of the selection table, the
// launcher / legacy_spiffs / cpafs_bytes combinations that decide LegacyTable, and the
// LittleFS shard count (32 from 1 MiB up, 4 below) with its shard mapping.
#include <unity.h>

#include "storage_plan.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static PartFacts part(bool launcher, uint32_t cpafs_bytes, bool legacy_spiffs) {
  PartFacts p;
  p.launcher = launcher;
  p.cpafs_bytes = cpafs_bytes;
  p.legacy_spiffs = legacy_spiffs;
  return p;
}

static SdFacts sd(SdState state, uint32_t size_mib = 0) {
  SdFacts s;
  s.state = state;
  s.size_mib = size_mib;
  return s;
}

static const PartFacts kFlashed = part(false, 0x180000, false);  // full flash, v0.2 table
static const PartFacts kLegacy = part(false, 0, true);           // only -app.bin flashed over v0.1
static const PartFacts kLauncherApp = part(true, 0, true);      // app-only image under the Launcher

static void assert_plan(Store primary, bool migrate, StoreNote note, const StoragePlan& p) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(primary), static_cast<int>(p.primary));
  TEST_ASSERT_EQUAL(migrate, p.migrate);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(note), static_cast<int>(p.note));
}

static void test_defaults_are_nothing_known() {
  const PartFacts p;
  const SdFacts s;
  const StoragePlan plan;
  TEST_ASSERT_FALSE(p.launcher);
  TEST_ASSERT_EQUAL_UINT32(0, p.cpafs_bytes);
  TEST_ASSERT_FALSE(p.legacy_spiffs);
  TEST_ASSERT_TRUE(s.state == SdState::Disabled);
  TEST_ASSERT_EQUAL_UINT32(0, s.size_mib);
  assert_plan(Store::None, false, StoreNote::None, plan);
}

static void test_sd_ok_with_cpafs_mounted_migrates() {
  assert_plan(Store::Sd, true, StoreNote::None, plan_storage(kFlashed, sd(SdState::Ok, 476000), true));
  assert_plan(Store::Sd, true, StoreNote::None, plan_storage(part(true, 0x70000, true), sd(SdState::Ok), true));
}

static void test_sd_ok_without_cpafs() {
  assert_plan(Store::Sd, false, StoreNote::LegacyTable, plan_storage(kLegacy, sd(SdState::Ok), false));
  assert_plan(Store::Sd, false, StoreNote::NoInternal, plan_storage(kLauncherApp, sd(SdState::Ok), false));
  assert_plan(Store::Sd, false, StoreNote::NoInternal, plan_storage(kFlashed, sd(SdState::Ok), false));  // mount failed
}

static void test_internal_when_sd_is_not_ok() {
  assert_plan(Store::Internal, false, StoreNote::NoCard, plan_storage(kFlashed, sd(SdState::Absent), true));
  assert_plan(Store::Internal, false, StoreNote::SdUnusable, plan_storage(kFlashed, sd(SdState::Unusable), true));
  assert_plan(Store::Internal, false, StoreNote::None, plan_storage(kFlashed, sd(SdState::Disabled), true));
}

static void test_no_storage_at_all() {
  const SdState states[] = {SdState::Disabled, SdState::Absent, SdState::Unusable};
  for (SdState st : states) {
    assert_plan(Store::None, false, StoreNote::LegacyTable, plan_storage(kLegacy, sd(st), false));
    assert_plan(Store::None, false, StoreNote::NoStorage, plan_storage(kLauncherApp, sd(st), false));
    assert_plan(Store::None, false, StoreNote::NoStorage, plan_storage(kFlashed, sd(st), false));
  }
}

// "Legacy table" = !launcher && legacy_spiffs && cpafs_bytes == 0; under the Launcher the
// spiffs partition belongs to other firmware and is never hinted at.
static void test_legacy_table_needs_all_three_facts() {
  const SdFacts none = sd(SdState::Absent);
  assert_plan(Store::None, false, StoreNote::LegacyTable, plan_storage(part(false, 0, true), none, false));
  assert_plan(Store::None, false, StoreNote::NoStorage, plan_storage(part(true, 0, true), none, false));
  assert_plan(Store::None, false, StoreNote::NoStorage, plan_storage(part(false, 0, false), none, false));
  assert_plan(Store::None, false, StoreNote::NoStorage, plan_storage(part(false, 0x70000, true), none, false));
}

// The choice ignores free space and the launcher flag otherwise.
static void test_launcher_does_not_change_the_choice() {
  for (int l = 0; l < 2; ++l) {
    const PartFacts p = part(l == 1, 0x70000, false);
    assert_plan(Store::Internal, false, StoreNote::NoCard, plan_storage(p, sd(SdState::Absent), true));
    assert_plan(Store::Sd, true, StoreNote::None, plan_storage(p, sd(SdState::Ok, 30000), true));
  }
}

static void test_lfs_shards_boundary() {
  TEST_ASSERT_EQUAL_UINT8(4, lfs_shards(0));
  TEST_ASSERT_EQUAL_UINT8(4, lfs_shards(0x70000));  // 448 KB under the Launcher
  TEST_ASSERT_EQUAL_UINT8(4, lfs_shards(1024u * 1024u - 1u));
  TEST_ASSERT_EQUAL_UINT8(32, lfs_shards(1024u * 1024u));
  TEST_ASSERT_EQUAL_UINT8(32, lfs_shards(0x180000));  // 1.5 MB, full flash: the v0.1 layout
  TEST_ASSERT_EQUAL_UINT8(32, lfs_shards(0xFFFFFFFFu));
}

// 32 shards keep v0.1's "/outbox/_<last MonoId digit>/"; 4 shards use the digit's
// Crockford value mod 4, so "_0".."_3".
static void test_shard_of_the_last_digit() {
  TEST_ASSERT_EQUAL_CHAR('C', lfs_shard('C', 32));
  TEST_ASSERT_EQUAL_CHAR('Z', lfs_shard('Z', 32));
  TEST_ASSERT_EQUAL_CHAR('0', lfs_shard('0', 4));
  TEST_ASSERT_EQUAL_CHAR('3', lfs_shard('3', 4));
  TEST_ASSERT_EQUAL_CHAR('0', lfs_shard('4', 4));
  TEST_ASSERT_EQUAL_CHAR('0', lfs_shard('C', 4));  // C = 12
  TEST_ASSERT_EQUAL_CHAR('3', lfs_shard('Z', 4));  // Z = 31
  TEST_ASSERT_EQUAL_CHAR('1', lfs_shard('H', 4));  // H = 17 (I is skipped)
  TEST_ASSERT_EQUAL_CHAR(0, lfs_shard('I', 4));    // not a Crockford digit
  TEST_ASSERT_EQUAL_CHAR(0, lfs_shard('c', 32));
  TEST_ASSERT_EQUAL_CHAR(0, lfs_shard('C', 8));  // only 4 and 32 exist
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_nothing_known);
  RUN_TEST(test_sd_ok_with_cpafs_mounted_migrates);
  RUN_TEST(test_sd_ok_without_cpafs);
  RUN_TEST(test_internal_when_sd_is_not_ok);
  RUN_TEST(test_no_storage_at_all);
  RUN_TEST(test_legacy_table_needs_all_three_facts);
  RUN_TEST(test_launcher_does_not_change_the_choice);
  RUN_TEST(test_lfs_shards_boundary);
  RUN_TEST(test_shard_of_the_last_digit);
  return UNITY_END();
}
