// Commit protocol (docs/09-m2-design.md §3.6), per FileStore kind:
//                atomic_rename() = true (LittleFS, v0.1)    false (FAT)
//   commit_file  open(tmp) write close rename(tmp, path)     open(path) write close;
//                                                            any failure: remove(path)
//   move_file    rename(from, to)                            read (2 KB) -> commit_file(to)
//                                                            -> remove(from)
// On FAT neither ever calls rename, and every failure keeps the source.
#include <unity.h>

#include <string>

#include "protocol_limits.h"
#include "store_commit.h"
#include "support/fake_store.h"

using namespace cpa;
using testsupport::FakeStore;

void setUp() {}
void tearDown() {}

static const char kPath[] = "/outbox/1K3M9Q2-00042-0A1C.json";
static const char kTmp[] = "/outbox/1K3M9Q2-00042-0A1C.tmp";
static const char kBad[] = "/outbox/bad/1K3M9Q2-00042-0A1C.json";

static FakeStore fat() {
  FakeStore s;
  s.set_fat(true);
  return s;
}

// A store whose write (or close) reports failure without crashing: the cleanup after
// a failed step can still run.
class FlakyStore : public FakeStore {
 public:
  bool fail_write = false;
  bool fail_close = false;
  bool write(const char* data, size_t n) override { return FakeStore::write(data, n) && !fail_write; }
  bool close() override { return FakeStore::close() && !fail_close; }
};

static void test_atomic_commit_writes_tmp_then_renames() {
  FakeStore s;
  TEST_ASSERT_TRUE(commit_file(s, kPath, kTmp, "abc", 3));
  TEST_ASSERT_EQUAL_STRING("abc", s.get(kPath).c_str());
  TEST_ASSERT_FALSE(s.has(kTmp));
  TEST_ASSERT_EQUAL_INT(1, s.renames());
  TEST_ASSERT_EQUAL_INT(4, s.ops());  // open, write, close, rename: as in v0.1
}

static void test_atomic_commit_failure_removes_only_the_tmp() {
  FlakyStore s;
  s.put(kPath, "old");
  s.fail_close = true;
  TEST_ASSERT_FALSE(commit_file(s, kPath, kTmp, "new", 3));
  TEST_ASSERT_FALSE(s.has(kTmp));
  TEST_ASSERT_EQUAL_STRING("old", s.get(kPath).c_str());
  TEST_ASSERT_EQUAL_INT(0, s.renames());
}

static void test_fat_commit_writes_the_final_name() {
  FakeStore s = fat();
  s.put(kPath, "stale and longer");  // a half-migrated copy: truncated and rewritten
  TEST_ASSERT_TRUE(commit_file(s, kPath, kTmp, "abc", 3));
  TEST_ASSERT_EQUAL_STRING("abc", s.get(kPath).c_str());
  TEST_ASSERT_FALSE(s.has(kTmp));
  TEST_ASSERT_EQUAL_INT(0, s.renames());
  TEST_ASSERT_EQUAL_INT(3, s.ops());  // open, write, close
}

static void test_fat_commit_failure_removes_the_file() {
  for (int which = 0; which < 2; ++which) {
    FlakyStore s;
    s.set_fat(true);
    s.fail_write = which == 0;
    s.fail_close = which == 1;
    TEST_ASSERT_FALSE(commit_file(s, kPath, kTmp, "abc", 3));
    TEST_ASSERT_FALSE(s.has(kPath));  // never shown as saved, never left behind
    TEST_ASSERT_EQUAL_INT(0, s.renames());
  }
}

// FatFs must never unlink an open file: after a failed write the file is closed first.
class LogStore : public FlakyStore {
 public:
  std::string log;
  bool open_write(const char* p) override { return note("open", FlakyStore::open_write(p)); }
  bool write(const char* d, size_t n) override { return note("write", FlakyStore::write(d, n)); }
  bool close() override { return note("close", FlakyStore::close()); }
  bool remove(const char* p) override { return note("remove", FlakyStore::remove(p)); }

 private:
  bool note(const char* op, bool ok) {
    log += std::string(op) + (ok ? " " : "! ");
    return ok;
  }
};

static void test_fat_commit_closes_before_it_removes() {
  LogStore s;
  s.set_fat(true);
  s.fail_write = true;
  TEST_ASSERT_FALSE(commit_file(s, kPath, kTmp, "abc", 3));
  TEST_ASSERT_EQUAL_STRING("open write! close remove ", s.log.c_str());
  LogStore m;
  m.set_fat(true);
  m.put(kPath, "item");
  m.fail_write = true;
  TEST_ASSERT_FALSE(move_file(m, kPath, kBad));
  TEST_ASSERT_EQUAL_STRING("open write! close remove ", m.log.c_str());  // the target, not the source
  TEST_ASSERT_TRUE(m.has(kPath));
}

// Power cut before any step: the file is absent, empty (opened, never synced) or
// complete; the scan deletes the empty one on FAT.
static void test_fat_commit_power_loss_never_leaves_a_partial_file() {
  for (long k = 0; k <= 3; ++k) {
    FakeStore s = fat();
    s.crash_before(k);
    const bool ok = commit_file(s, kPath, kTmp, "abc", 3);
    const std::string got = s.has(kPath) ? s.get(kPath) : "<absent>";
    TEST_ASSERT_EQUAL(k == 3, ok);
    TEST_ASSERT_TRUE(got == "<absent>" || got.empty() || got == "abc");
    if (ok) TEST_ASSERT_EQUAL_STRING("abc", got.c_str());
  }
}

static void test_atomic_move_is_one_rename() {
  FakeStore s;
  s.put(kPath, "item");
  TEST_ASSERT_TRUE(move_file(s, kPath, kBad));
  TEST_ASSERT_EQUAL_STRING("item", s.get(kBad).c_str());
  TEST_ASSERT_FALSE(s.has(kPath));
  TEST_ASSERT_EQUAL_INT(1, s.renames());
  TEST_ASSERT_EQUAL_INT(1, s.ops());
}

static void test_fat_move_copies_then_deletes() {
  FakeStore s = fat();
  s.put(kPath, "item");
  s.put(kBad, "an older copy");  // left by an interrupted move: replaced
  TEST_ASSERT_TRUE(move_file(s, kPath, kBad));
  TEST_ASSERT_EQUAL_STRING("item", s.get(kBad).c_str());
  TEST_ASSERT_FALSE(s.has(kPath));
  TEST_ASSERT_EQUAL_INT(0, s.renames());
}

static void test_fat_move_failures_keep_the_source() {
  FakeStore missing = fat();
  TEST_ASSERT_FALSE(move_file(missing, kPath, kBad));
  TEST_ASSERT_FALSE(missing.has(kBad));

  FakeStore big = fat();  // more than one outbox line: never truncated into bad/
  big.put(kPath, std::string(limits::kLineMax + 1, 'x'));
  TEST_ASSERT_FALSE(move_file(big, kPath, kBad));
  TEST_ASSERT_TRUE(big.has(kPath));
  TEST_ASSERT_FALSE(big.has(kBad));

  FakeStore max = fat();
  max.put(kPath, std::string(limits::kLineMax, 'x'));
  TEST_ASSERT_TRUE(move_file(max, kPath, kBad));
  TEST_ASSERT_EQUAL_UINT(limits::kLineMax, max.get(kBad).size());

  FlakyStore flaky;
  flaky.set_fat(true);
  flaky.put(kPath, "item");
  flaky.fail_write = true;
  TEST_ASSERT_FALSE(move_file(flaky, kPath, kBad));
  TEST_ASSERT_EQUAL_STRING("item", flaky.get(kPath).c_str());
  TEST_ASSERT_FALSE(flaky.has(kBad));
}

// Wherever the power goes, the item is still at its source or complete at the target.
static void test_fat_move_power_loss_keeps_one_complete_copy() {
  for (long k = 0; k <= 4; ++k) {
    FakeStore s = fat();
    s.put(kPath, "item");
    s.crash_before(k);
    const bool ok = move_file(s, kPath, kBad);
    const bool src = s.get(kPath) == "item";
    const bool dst = s.get(kBad) == "item";
    TEST_ASSERT_TRUE_MESSAGE(src || dst, ("crash before op " + std::to_string(k)).c_str());
    TEST_ASSERT_EQUAL(k == 4, ok);
    TEST_ASSERT_EQUAL_INT(0, s.renames());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_atomic_commit_writes_tmp_then_renames);
  RUN_TEST(test_atomic_commit_failure_removes_only_the_tmp);
  RUN_TEST(test_fat_commit_writes_the_final_name);
  RUN_TEST(test_fat_commit_failure_removes_the_file);
  RUN_TEST(test_fat_commit_closes_before_it_removes);
  RUN_TEST(test_fat_commit_power_loss_never_leaves_a_partial_file);
  RUN_TEST(test_atomic_move_is_one_rename);
  RUN_TEST(test_fat_move_copies_then_deletes);
  RUN_TEST(test_fat_move_failures_keep_the_source);
  RUN_TEST(test_fat_move_power_loss_keeps_one_complete_copy);
  return UNITY_END();
}
