// G0's release debounce (gh #51): M5Unified debounces BtnA for 10 ms only while update()
// runs at least every 10 ms, so one open-contact sample of a held side button read as a
// release. A press counts at once; a release only when G0 read up on two samples or more,
// kG0ReleaseMs apart at least.
#include <unity.h>

#include "g0_debounce.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static G0Debounce run(G0Debounce s, bool raw, uint32_t now) { return g0_debounce(s, raw, now); }

static void test_a_press_counts_at_once() {
  const G0Debounce s = run(G0Debounce{}, true, 100);
  TEST_ASSERT_TRUE(s.down);
  TEST_ASSERT_FALSE(run(G0Debounce{}, false, 100).down);
}

static void test_one_open_sample_is_not_a_release() {
  G0Debounce s = run(G0Debounce{}, true, 0);
  s = run(s, false, 50);  // a bounce, read once (the loop pass took 50 ms)
  TEST_ASSERT_TRUE(s.down);
  s = run(s, true, 60);
  TEST_ASSERT_TRUE(s.down);
  s = run(s, false, 200);  // the next open sample starts again
  TEST_ASSERT_TRUE(s.down);
  s = run(s, true, 210);
  TEST_ASSERT_TRUE(s.down);
}

static void test_a_release_counts_after_kG0ReleaseMs() {
  G0Debounce s = run(G0Debounce{}, true, 0);
  s = run(s, false, 1000);
  TEST_ASSERT_TRUE(s.down);
  s = run(s, false, 1000 + kG0ReleaseMs - 1);
  TEST_ASSERT_TRUE(s.down);
  s = run(s, false, 1000 + kG0ReleaseMs);
  TEST_ASSERT_FALSE(s.down);
  s = run(s, true, 2000);  // pressed again: at once
  TEST_ASSERT_TRUE(s.down);
  TEST_ASSERT_TRUE(kG0ReleaseMs >= 20 && kG0ReleaseMs <= 50);  // a tap stays a tap
}

static void test_two_samples_even_after_a_long_pass() {
  G0Debounce s = run(G0Debounce{}, true, 0);
  s = run(s, false, 500);  // the first open sample, however late, is never the release
  TEST_ASSERT_TRUE(s.down);
  s = run(s, false, 510);
  TEST_ASSERT_TRUE(s.down);  // 10 ms up: not yet
  s = run(s, false, 600);
  TEST_ASSERT_FALSE(s.down);
}

static void test_wraps_with_millis() {
  G0Debounce s = run(G0Debounce{}, true, 0xFFFFFFF0u);
  s = run(s, false, 0xFFFFFFF8u);
  s = run(s, false, 0xFFFFFFF8u + kG0ReleaseMs);
  TEST_ASSERT_FALSE(s.down);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_press_counts_at_once);
  RUN_TEST(test_one_open_sample_is_not_a_release);
  RUN_TEST(test_a_release_counts_after_kG0ReleaseMs);
  RUN_TEST(test_two_samples_even_after_a_long_pass);
  RUN_TEST(test_wraps_with_millis);
  return UNITY_END();
}
