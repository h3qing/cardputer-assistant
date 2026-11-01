// LedgerTable (docs/28 §3.3-§3.4): the ledger replayed into a buffer, 16 B a segment.
#include <unity.h>

#include <vector>

#include "ledger_table.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kA = 0xAAAA0001;
constexpr uint32_t kB = 0xBBBB0002;

LedgerRec lr(LedgerOp op, uint32_t seq, uint16_t back, uint32_t host, uint8_t flags = 0, uint32_t wall = 0) {
  LedgerRec r;
  r.op = op;
  r.boot = 12;
  r.seq = seq;
  r.back = back;
  r.host = host;
  r.flags = flags;
  r.wall_s = wall;
  return r;
}

struct Rig {
  alignas(8) uint8_t mem[16 * 64] = {};
  std::vector<LedgerRec> log;

  void add(const LedgerRec& r) { log.push_back(r); }
  // the two passes of the engine
  LedgerTable replay(size_t bytes = sizeof(mem)) {
    BatchFinder f;
    for (const auto& r : log) f.record(r);
    LedgerTable t(mem, bytes, f.start, f.host);
    for (const auto& r : log) t.record(r);
    return t;
  }
};

}  // namespace

static void test_deliv_keeps_and_counts() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, 0, 1000));
  g.add(lr(LedgerOp::Deliv, 11, 1, kA, kLfLast, 1010));
  const LedgerTable t = g.replay();
  TEST_ASSERT_EQUAL_UINT16(2, t.count());
  const KeptInfo k = t.info();
  TEST_ASSERT_EQUAL_UINT16(2, k.segs);
  TEST_ASSERT_EQUAL_UINT32(1000, k.oldest_s);
  TEST_ASSERT_EQUAL_HEX32(kA, k.batch_host);
  TEST_ASSERT_EQUAL_UINT16(1, k.batch_recs);
  TEST_ASSERT_EQUAL_UINT16(2, k.batch_segs);
  const RecSummary s = rec_summary(t, rec_of(t.at(1)));
  TEST_ASSERT_TRUE(s.complete);
  TEST_ASSERT_EQUAL_UINT16(2, s.segs);
  TEST_ASSERT_EQUAL_UINT32(1010, s.newest_s);
}

static void test_batch_is_the_trailing_run_to_one_mac() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast));
  g.add(lr(LedgerOp::Deliv, 20, 0, kB, kLfLast));
  g.add(lr(LedgerOp::Deliv, 30, 0, kB));
  g.add(lr(LedgerOp::Deliv, 31, 1, kB, kLfLast));
  const LedgerTable t = g.replay();
  const KeptInfo k = t.info();
  TEST_ASSERT_EQUAL_UINT16(4, k.segs);
  TEST_ASSERT_EQUAL_HEX32(kB, k.batch_host);
  TEST_ASSERT_EQUAL_UINT16(2, k.batch_recs);
  TEST_ASSERT_EQUAL_UINT16(3, k.batch_segs);
  TEST_ASSERT_FALSE(t.at(t.find(12, 10)).has(kSlotBatch));
}

static void test_again_requeues_and_excludes() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast));
  g.add(lr(LedgerOp::Again, 10, 0, kA));
  const LedgerTable t = g.replay();
  const LedgerSlot& s = t.at(t.find(12, 10));
  TEST_ASSERT_FALSE(s.has(kSlotKept));
  TEST_ASSERT_TRUE(s.has(kSlotExcl));
  TEST_ASSERT_EQUAL_HEX32(kA, s.host);
  TEST_ASSERT_EQUAL_UINT16(0, t.info().segs);
  TEST_ASSERT_EQUAL_UINT32(0, t.info().batch_host);  // nothing of the batch is kept
}

static void test_hold_never_requeues_a_kept_one() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast));
  g.add(lr(LedgerOp::Again, 10, 0, kB, kLfHold));
  g.add(lr(LedgerOp::Again, 40, 0, kB, kLfHold));  // a waiting one
  const LedgerTable t = g.replay();
  TEST_ASSERT_TRUE(t.at(t.find(12, 10)).has(kSlotKept));
  TEST_ASSERT_TRUE(t.at(t.find(12, 40)).has(kSlotExcl));
}

static void test_redelivered_after_again_is_kept_for_the_new_mac() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast));
  g.add(lr(LedgerOp::Again, 10, 0, kA));
  g.add(lr(LedgerOp::Deliv, 10, 0, kB, kLfLast));
  const LedgerTable t = g.replay();
  const LedgerSlot& s = t.at(t.find(12, 10));
  TEST_ASSERT_TRUE(s.has(kSlotKept));
  TEST_ASSERT_FALSE(s.has(kSlotExcl));
  TEST_ASSERT_EQUAL_HEX32(kB, s.host);
  TEST_ASSERT_EQUAL_HEX32(kB, t.info().batch_host);
}

static void test_gone_takes_the_whole_recording_only() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA));
  g.add(lr(LedgerOp::Deliv, 12, 2, kA, kLfLast));  // same recording (seq 10), id 11 was a note
  g.add(lr(LedgerOp::Deliv, 13, 0, kA, kLfLast));  // another
  g.add(lr(LedgerOp::Gone, 10, 0, 0));
  const LedgerTable t = g.replay();
  TEST_ASSERT_TRUE(t.at(t.find(12, 10)).has(kSlotGone));
  TEST_ASSERT_TRUE(t.at(t.find(12, 12)).has(kSlotGone));
  TEST_ASSERT_FALSE(t.at(t.find(12, 12)).has(kSlotKept));
  TEST_ASSERT_TRUE(t.at(t.find(12, 13)).has(kSlotKept));
  TEST_ASSERT_EQUAL_UINT16(1, t.info().segs);
}

static void test_incomplete_recording_and_unknown_back() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA));                  // seg 0 only: the rest waits
  g.add(lr(LedgerOp::Deliv, 9000, kBackUnknown, kA, 0));  // its own recording
  const LedgerTable t = g.replay();
  TEST_ASSERT_FALSE(rec_summary(t, rec_of(t.at(0))).complete);
  const RecKey own = rec_of(t.at(1));
  TEST_ASSERT_EQUAL_UINT32(9000, own.seq);
  TEST_ASSERT_EQUAL_UINT16(2, t.info().batch_recs);
}

static void test_back_too_far_is_unknown() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 6000, 5000, kA, kLfLast));
  const LedgerTable t = g.replay();
  TEST_ASSERT_EQUAL_UINT32(6000, rec_of(t.at(0)).seq);
}

static void test_overflow_is_flagged_never_written_past() {
  Rig g;
  for (uint32_t i = 0; i < 10; ++i) g.add(lr(LedgerOp::Deliv, 100 + i, 0, kA, kLfLast));
  const LedgerTable t = g.replay(16 * 4);
  TEST_ASSERT_EQUAL_UINT16(4, t.count());
  TEST_ASSERT_TRUE(t.overflow());
}

static void test_oldest_ignores_unknown_wall() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast, 0));
  g.add(lr(LedgerOp::Deliv, 11, 0, kA, kLfLast, 5000));
  TEST_ASSERT_EQUAL_UINT32(5000, g.replay().info().oldest_s);
}

static void test_live_records_for_compaction() {
  Rig g;
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast));
  g.add(lr(LedgerOp::Deliv, 11, 0, kA, kLfLast));
  g.add(lr(LedgerOp::Gone, 11, 0, 0));
  g.add(lr(LedgerOp::Again, 50, 0, kB, kLfHold));
  LedgerTable t = g.replay();
  // DELIV 10; DELIV 11 + GONE 11 (files not known gone: the GONE needs a kept one to act on); AGAIN 50
  TEST_ASSERT_EQUAL_UINT16(4, t.live_records());
  t.at(t.find(12, 11)).set(kSlotDone);
  TEST_ASSERT_EQUAL_UINT16(2, t.live_records());
}

// A compaction's GEN carries the batch (kLfBatch: host = its Mac, wall_s = the record number
// it starts at), so an older run to the same Mac written before it never joins it.
static void test_gen_batch_sets_start_and_mac() {
  Rig g;
  g.add(lr(LedgerOp::Gen, 3, 0, kA, kLfBatch, 3));  // the batch starts at record 3
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast));    // 1: kept for A, not the batch
  g.add(lr(LedgerOp::Deliv, 20, 0, kB, kLfLast));    // 2: kept for B (written before the batch)
  g.add(lr(LedgerOp::Deliv, 30, 0, kA, kLfLast));    // 3: the batch
  const KeptInfo k = g.replay().info();
  TEST_ASSERT_EQUAL_HEX32(kA, k.batch_host);
  TEST_ASSERT_EQUAL_UINT16(1, k.batch_recs);
  TEST_ASSERT_EQUAL_UINT16(1, k.batch_segs);
  g.add(lr(LedgerOp::Deliv, 31, 0, kA, kLfLast));  // appended later to A: the batch goes on
  TEST_ASSERT_EQUAL_UINT16(2, g.replay().info().batch_recs);
  g.add(lr(LedgerOp::Deliv, 40, 0, kB, kLfLast));  // to B: a new batch
  const KeptInfo k2 = g.replay().info();
  TEST_ASSERT_EQUAL_HEX32(kB, k2.batch_host);
  TEST_ASSERT_EQUAL_UINT16(1, k2.batch_recs);
}

// A GEN that says "no batch" (host 0) keeps a stale run from becoming one.
static void test_gen_without_batch_has_none() {
  Rig g;
  g.add(lr(LedgerOp::Gen, 3, 0, 0, kLfBatch, 2));
  g.add(lr(LedgerOp::Deliv, 10, 0, kA, kLfLast));
  TEST_ASSERT_EQUAL_UINT16(0, g.replay().info().batch_recs);
  g.add(lr(LedgerOp::Deliv, 11, 0, kA, kLfLast));
  TEST_ASSERT_EQUAL_UINT16(1, g.replay().info().batch_recs);
}

static void test_gen_and_prof_change_nothing() {
  Rig g;
  g.add(lr(LedgerOp::Gen, 7, 0, 0));
  g.add(lr(LedgerOp::Prof, 10, 0, 0));
  const LedgerTable t = g.replay();
  TEST_ASSERT_EQUAL_UINT16(0, t.count());
  TEST_ASSERT_EQUAL_UINT32(2, t.records());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_deliv_keeps_and_counts);
  RUN_TEST(test_batch_is_the_trailing_run_to_one_mac);
  RUN_TEST(test_again_requeues_and_excludes);
  RUN_TEST(test_hold_never_requeues_a_kept_one);
  RUN_TEST(test_redelivered_after_again_is_kept_for_the_new_mac);
  RUN_TEST(test_gone_takes_the_whole_recording_only);
  RUN_TEST(test_incomplete_recording_and_unknown_back);
  RUN_TEST(test_back_too_far_is_unknown);
  RUN_TEST(test_overflow_is_flagged_never_written_past);
  RUN_TEST(test_oldest_ignores_unknown_wall);
  RUN_TEST(test_live_records_for_compaction);
  RUN_TEST(test_gen_and_prof_change_nothing);
  RUN_TEST(test_gen_batch_sets_start_and_mac);
  RUN_TEST(test_gen_without_batch_has_none);
  return UNITY_END();
}
