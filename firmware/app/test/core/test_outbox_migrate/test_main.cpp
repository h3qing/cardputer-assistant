// Boot migration cpafs -> SD (docs/09-m2-design.md §3.7): for each pending id of the
// source's boot scan, read it, commit it on the target (FAT protocol: truncate and
// rewrite, so a half-copied earlier attempt is replaced), then delete the source. Any
// failure keeps the source and moves on; bad/ and the project cache stay behind.
// Invariant: a source item is deleted only after the target committed it, so with power
// cut before any operation of either store, a rerun converges: every item exactly once
// on the target, the source outbox empty, the source's bad/ untouched.
#include <unity.h>

#include <string>
#include <vector>

#include "codec.h"
#include "outbox_io.h"
#include "outbox_migrate.h"
#include "support/fake_store.h"

using namespace cpa;
using testsupport::FakeStore;
using testsupport::PowerRail;

void setUp() {}
void tearDown() {}

static MonoId mid(uint32_t seq, uint32_t boot) {
  MonoId id;
  id.epoch = 7;
  id.boot = boot;
  id.seq = seq;
  return id;
}

static std::string name(const MonoId& id) { return mono_format(id).c_str(); }
static std::string json(const MonoId& id) { return "/outbox/" + name(id) + ".json"; }

static std::string item_line(const MonoId& id) {
  return "{\"v\":0,\"t\":\"outbox.item\",\"id\":\"" + name(id) +
         "\",\"kind\":\"note\",\"at\":{\"up_ms\":1,\"wall_ms\":null},\"body\":{\"text\":\"n" + name(id) + "\"}}\n";
}

class Progress : public MigrateObserver {
 public:
  std::vector<std::pair<uint16_t, uint16_t>> calls;
  void on_item(uint16_t done, uint16_t total) override { calls.emplace_back(done, total); }
};

// The source: n pending items over several boots, two items in bad/, a project cache.
static std::vector<MonoId> seed(FakeStore& src, uint32_t n) {
  std::vector<MonoId> ids;
  for (uint32_t i = 0; i < n; ++i) ids.push_back(mid(i % 7, 1 + i / 7));
  for (const MonoId& id : ids) src.put(json(id), item_line(id));
  src.put("/outbox/bad/" + name(mid(0, 0)) + ".json", "{old");
  src.put("/outbox/bad/" + name(mid(1, 0)) + ".json", "{older");
  src.put("/cfg/projects.json", "[]");
  return ids;
}

struct Boot {
  MigrateResult migrated;
  OutboxCore primary;
};

// Runtime::begin's storage steps: scan the source, migrate, scan the target.
static Boot boot(FakeStore& src, FakeStore& dst, MigrateObserver& obs) {
  proto::Codec codec;
  Boot b;
  b.migrated = outbox_migrate(src, dst, outbox_boot_scan(src, codec).core, obs);
  b.primary = outbox_boot_scan(dst, codec).core;
  return b;
}

static void assert_converged(const FakeStore& src, const FakeStore& dst, const std::vector<MonoId>& ids, const std::string& at) {
  for (const MonoId& id : ids) TEST_ASSERT_EQUAL_STRING_MESSAGE(item_line(id).c_str(), dst.get(json(id)).c_str(), at.c_str());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(ids.size(), dst.count_in("/outbox", ".json"), at.c_str());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, src.count_in("/outbox", ".json"), at.c_str());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(2, src.count_in("/outbox/bad", ".json"), at.c_str());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, dst.count_in("/outbox/bad", ".json"), at.c_str());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, dst.renames(), at.c_str());
}

static void test_moves_every_pending_item_and_reports_progress() {
  FakeStore src;
  FakeStore dst;
  dst.set_fat(true);
  const auto ids = seed(src, 10);
  Progress progress;
  const Boot b = boot(src, dst, progress);
  TEST_ASSERT_EQUAL_UINT16(10, b.migrated.moved);
  TEST_ASSERT_EQUAL_UINT16(0, b.migrated.failed);
  TEST_ASSERT_EQUAL_UINT16(10, b.primary.n);
  assert_converged(src, dst, ids, "clean run");
  TEST_ASSERT_TRUE(src.has("/cfg/projects.json") && !dst.has("/cfg/projects.json"));  // re-pushed by the daemon
  TEST_ASSERT_EQUAL_INT(10, static_cast<int>(progress.calls.size()));
  for (uint16_t i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL_UINT16(i + 1, progress.calls[i].first);
    TEST_ASSERT_EQUAL_UINT16(10, progress.calls[i].second);
  }
}

static void test_nothing_to_move() {
  FakeStore src;
  FakeStore dst;
  dst.set_fat(true);
  Progress progress;
  const Boot b = boot(src, dst, progress);
  TEST_ASSERT_EQUAL_UINT16(0, b.migrated.moved + b.migrated.failed);
  TEST_ASSERT_TRUE(progress.calls.empty());
}

static void test_a_half_copied_target_is_rewritten() {
  FakeStore src;
  FakeStore dst;
  dst.set_fat(true);
  const auto ids = seed(src, 3);
  dst.put(json(ids[1]), "");                  // opened, power lost before fsync
  dst.put(json(ids[2]), item_line(ids[2]));  // committed, source not yet deleted
  Progress progress;
  boot(src, dst, progress);
  assert_converged(src, dst, ids, "half-copied target");
}

// More than the 200 ids a scan tracks: the oldest 200 go now, the rest next boot.
static void test_more_than_capacity_takes_two_boots() {
  FakeStore src;
  FakeStore dst;
  dst.set_fat(true);
  const auto ids = seed(src, kOutboxCapacity + 5);
  Progress progress;
  TEST_ASSERT_EQUAL_UINT16(kOutboxCapacity, boot(src, dst, progress).migrated.moved);
  TEST_ASSERT_EQUAL_UINT(5, src.count_in("/outbox", ".json"));
  TEST_ASSERT_TRUE(src.has(json(ids.back())));  // the newest waited
  TEST_ASSERT_EQUAL_UINT16(5, boot(src, dst, progress).migrated.moved);
  assert_converged(src, dst, ids, "second boot");
}

// A store whose writes fail for one path, or which cannot delete: the item stays at
// the source, counts as failed, and the others still move.
class PickyStore : public FakeStore {
 public:
  std::string refuse;
  bool no_remove = false;
  bool open_write(const char* path) override { return refuse != path && FakeStore::open_write(path); }
  bool remove(const char* path) override { return !no_remove && FakeStore::remove(path); }
};

static void test_failures_keep_the_source_and_continue() {
  FakeStore src;
  PickyStore dst;
  dst.set_fat(true);
  const auto ids = seed(src, 4);
  dst.refuse = json(ids[1]);
  src.fail_read(json(ids[2]));
  Progress progress;
  proto::Codec codec;
  const MigrateResult r = outbox_migrate(src, dst, outbox_boot_scan(src, codec).core, progress);
  TEST_ASSERT_EQUAL_UINT16(2, r.moved);
  TEST_ASSERT_EQUAL_UINT16(1, r.failed);  // the unreadable one was never pending
  TEST_ASSERT_TRUE(src.has(json(ids[1])) && !dst.has(json(ids[1])));
  TEST_ASSERT_TRUE(src.has(json(ids[2])) && !dst.has(json(ids[2])));
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(progress.calls.size()));

  PickyStore src2;
  FakeStore dst2;
  dst2.set_fat(true);
  const auto ids2 = seed(src2, 2);
  src2.no_remove = true;
  const MigrateResult r2 = outbox_migrate(src2, dst2, outbox_boot_scan(src2, codec).core, progress);
  TEST_ASSERT_EQUAL_UINT16(0, r2.moved);
  TEST_ASSERT_EQUAL_UINT16(2, r2.failed);
  TEST_ASSERT_TRUE(src2.has(json(ids2[0])) && dst2.has(json(ids2[0])));  // both: sent twice at worst
}

// Power cut before operation k of either store (one rail), then a clean boot.
static void test_power_loss_anywhere_converges() {
  long total = 0;
  {
    FakeStore src;
    FakeStore dst;
    dst.set_fat(true);
    PowerRail rail;
    src.share_power(rail);
    dst.share_power(rail);
    seed(src, 9);
    Progress progress;
    boot(src, dst, progress);
    total = rail.ops;
  }
  TEST_ASSERT_TRUE(total > 30);
  for (long k = 0; k <= total; ++k) {
    FakeStore src;
    FakeStore dst;
    dst.set_fat(true);
    PowerRail rail;
    src.share_power(rail);
    dst.share_power(rail);
    const auto ids = seed(src, 9);
    src.crash_before(k);
    Progress progress;
    boot(src, dst, progress);
    const std::string at = "power lost before op " + std::to_string(k);
    for (const MonoId& id : ids) {
      const bool somewhere = src.get(json(id)) == item_line(id) || dst.get(json(id)) == item_line(id);
      TEST_ASSERT_TRUE_MESSAGE(somewhere, (at + ": lost " + name(id)).c_str());
    }
    src.power_cycle();
    dst.power_cycle();
    boot(src, dst, progress);
    assert_converged(src, dst, ids, at);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_moves_every_pending_item_and_reports_progress);
  RUN_TEST(test_nothing_to_move);
  RUN_TEST(test_a_half_copied_target_is_rewritten);
  RUN_TEST(test_more_than_capacity_takes_two_boots);
  RUN_TEST(test_failures_keep_the_source_and_continue);
  RUN_TEST(test_power_loss_anywhere_converges);
  return UNITY_END();
}
