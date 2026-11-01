// The kept copies' policy (docs/28 §3.3, D4, D15, D16): how many may be kept, when a wall
// clock may age them out, and which recording goes first.
#include <unity.h>

#include <vector>

#include "ledger_policy.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kA = 0xA1;
constexpr uint32_t kDay = 86400;
constexpr uint32_t kNow = 1800000000;

LedgerRec deliv(uint32_t seq, uint16_t back, bool last, uint32_t wall) {
  LedgerRec r;
  r.op = LedgerOp::Deliv;
  r.boot = 3;
  r.seq = seq;
  r.back = back;
  r.flags = last ? kLfLast : 0;
  r.wall_s = wall;
  r.host = kA;
  return r;
}

struct Rig {
  alignas(8) uint8_t mem[16 * 64] = {};
  std::vector<LedgerRec> log;
  LedgerTable table() {
    LedgerTable t(mem, sizeof mem);
    for (const auto& r : log) t.record(r);
    return t;
  }
};

TidyCtx ctx(uint32_t now, uint32_t kept_total = 0, uint16_t kept_max = 1264, bool low = false) {
  TidyCtx c;
  c.now_s = now;
  c.kept_total = kept_total;
  c.kept_max = kept_max;
  c.space_low = low;
  return c;
}

}  // namespace

static void test_kept_max_is_the_scan_table_less_256_and_16() {
  TEST_ASSERT_EQUAL_UINT16(1264, kept_max_for(1536));  // the default build's 24 KB ring
  TEST_ASSERT_EQUAL_UINT16(752, kept_max_for(1024));   // record-at-start: 16 KB
  TEST_ASSERT_EQUAL_UINT16(0, kept_max_for(272));
  TEST_ASSERT_EQUAL_UINT16(0, kept_max_for(100));
}

static void test_too_long_a_recording_is_not_kept() {
  TEST_ASSERT_TRUE(keep_segment(0, 1264));
  TEST_ASSERT_TRUE(keep_segment(631, 1264));
  TEST_ASSERT_FALSE(keep_segment(632, 1264));
  TEST_ASSERT_FALSE(keep_segment(0, 0));  // no room at all: delete as before S1a
}

static void test_clock_without_a_reference_is_trusted() {
  const ClockStep s = clock_check(ClockRef{}, kNow, 10);
  TEST_ASSERT_TRUE(s.trusted);
  TEST_ASSERT_TRUE(s.ref.has);
}

// D16: a Mac a year ahead ages nothing; a second time close to it does.
static void test_a_year_ahead_is_not_used_until_confirmed() {
  ClockRef ref;
  ref.has = true;
  ref.wall_s = kNow;
  ref.up_s = 0;
  const ClockStep a = clock_check(ref, kNow + 365 * kDay, 100);
  TEST_ASSERT_FALSE(a.trusted);
  const ClockStep b = clock_check(a.ref, kNow + 365 * kDay + 300, 400);
  TEST_ASSERT_TRUE(b.trusted);
}

static void test_within_a_day_plus_uptime_is_trusted() {
  ClockRef ref;
  ref.has = true;
  ref.wall_s = kNow;
  ref.up_s = 1000;
  TEST_ASSERT_TRUE(clock_check(ref, kNow + kDay + 5000, 6000).trusted);
  TEST_ASSERT_FALSE(clock_check(ref, kNow + kDay + 5001, 6000).trusted);
  TEST_ASSERT_TRUE(clock_check(ref, kNow + 5000 - kDay, 6000).trusted);  // back within a day
}

// Review 8: a Mac a year behind would stamp kept copies a year old (they would go at the
// next good time.set); a jump back past a day is not used either until confirmed.
static void test_a_year_behind_is_not_used_until_confirmed() {
  ClockRef ref;
  ref.has = true;
  ref.wall_s = kNow;
  ref.up_s = 1000;
  const ClockStep a = clock_check(ref, kNow - 365 * kDay, 6000);
  TEST_ASSERT_FALSE(a.trusted);
  TEST_ASSERT_FALSE(clock_check(ref, kNow - kDay - 1, 1000).trusted);
  TEST_ASSERT_TRUE(clock_check(a.ref, kNow - 365 * kDay + 600, 6600).trusted);
}

static void test_age_takes_complete_recordings_past_seven_days() {
  Rig g;
  g.log.push_back(deliv(10, 0, true, kNow - 8 * kDay));
  g.log.push_back(deliv(20, 0, true, kNow - 6 * kDay));
  const LedgerTable t = g.table();
  const TidyPick p = tidy_pick(t, ctx(kNow, 2));
  TEST_ASSERT_TRUE(p.has);
  TEST_ASSERT_EQUAL_UINT32(10, p.rec.seq);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TidyWhy::Age), static_cast<uint8_t>(p.why));
}

static void test_age_never_takes_unknown_time_or_incomplete() {
  Rig g;
  g.log.push_back(deliv(10, 0, true, 0));                // handed over before the time was known
  g.log.push_back(deliv(20, 0, false, kNow - 30 * kDay));  // its last segment still waits
  const LedgerTable t = g.table();
  TEST_ASSERT_FALSE(tidy_pick(t, ctx(kNow, 2)).has);
  TEST_ASSERT_FALSE(tidy_pick(t, ctx(0, 2)).has);  // no trusted clock: nothing by age
}

static void test_age_uses_the_newest_segment_of_the_recording() {
  Rig g;
  g.log.push_back(deliv(10, 0, false, kNow - 9 * kDay));
  g.log.push_back(deliv(11, 1, true, kNow - 2 * kDay));
  TEST_ASSERT_FALSE(tidy_pick(g.table(), ctx(kNow, 2)).has);
}

static void test_count_and_space_take_the_oldest_whatever_it_is() {
  Rig g;
  g.log.push_back(deliv(10, 0, false, 0));
  g.log.push_back(deliv(20, 0, true, kNow));
  const LedgerTable t = g.table();
  const TidyPick c = tidy_pick(t, ctx(kNow, 3, 2));
  TEST_ASSERT_TRUE(c.has);
  TEST_ASSERT_EQUAL_UINT32(10, c.rec.seq);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TidyWhy::Count), static_cast<uint8_t>(c.why));
  const TidyPick s = tidy_pick(t, ctx(kNow, 2, 1264, true));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TidyWhy::Space), static_cast<uint8_t>(s.why));
  TEST_ASSERT_FALSE(tidy_pick(t, ctx(kNow, 1, 2)).has);  // at the low mark: nothing
}

// Review 5: a tidy goes down to kept_max less one waiting list (256), so a whole list can be
// handed over before the limit, where a new delivery would be deleted instead of kept.
static void test_count_goes_down_to_a_waiting_list_below_the_limit() {
  TEST_ASSERT_EQUAL_UINT16(1008, kept_low(1264));
  TEST_ASSERT_EQUAL_UINT16(496, kept_low(752));
  TEST_ASSERT_EQUAL_UINT16(1, kept_low(2));  // a tiny table: half
  Rig g;
  g.log.push_back(deliv(10, 0, true, 0));
  g.log.push_back(deliv(20, 0, true, 0));
  const LedgerTable t = g.table();
  TEST_ASSERT_TRUE(tidy_pick(t, ctx(0, 1009)).has);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TidyWhy::Count), static_cast<uint8_t>(tidy_pick(t, ctx(0, 1009)).why));
  TEST_ASSERT_FALSE(tidy_pick(t, ctx(0, 1008)).has);
}

static void test_marked_recordings_are_skipped() {
  Rig g;
  g.log.push_back(deliv(10, 0, true, kNow - 8 * kDay));
  g.log.push_back(deliv(20, 0, true, kNow - 8 * kDay));
  LedgerTable t = g.table();
  t.at(0).set(kSlotMark);
  TEST_ASSERT_EQUAL_UINT32(20, tidy_pick(t, ctx(kNow, 2)).rec.seq);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_kept_max_is_the_scan_table_less_256_and_16);
  RUN_TEST(test_too_long_a_recording_is_not_kept);
  RUN_TEST(test_clock_without_a_reference_is_trusted);
  RUN_TEST(test_a_year_ahead_is_not_used_until_confirmed);
  RUN_TEST(test_within_a_day_plus_uptime_is_trusted);
  RUN_TEST(test_a_year_behind_is_not_used_until_confirmed);
  RUN_TEST(test_count_goes_down_to_a_waiting_list_below_the_limit);
  RUN_TEST(test_age_takes_complete_recordings_past_seven_days);
  RUN_TEST(test_age_never_takes_unknown_time_or_incomplete);
  RUN_TEST(test_age_uses_the_newest_segment_of_the_recording);
  RUN_TEST(test_count_and_space_take_the_oldest_whatever_it_is);
  RUN_TEST(test_marked_recordings_are_skipped);
  return UNITY_END();
}
