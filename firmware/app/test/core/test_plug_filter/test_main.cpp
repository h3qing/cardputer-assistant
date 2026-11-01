// PlugFilter (05 §5.8; HIL 2026-10-19): plugged() goes to LinkSession debounced. A
// plug counts at once; an unplug only after plugged() read false on every sample for
// kUnplugConfirmMs, so a one-tick HWCDC::isPlugged() dropout never leaves Ready.
#include <unity.h>

#include "plug_filter.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static PlugFilter plugged_at(uint32_t now) { return plug_filter(PlugFilter{}, true, now); }

static void test_starts_unplugged_and_plug_counts_at_once() {
  TEST_ASSERT_FALSE(PlugFilter{}.plugged);
  TEST_ASSERT_FALSE(plug_filter(PlugFilter{}, false, 0).plugged);  // charger only at boot
  TEST_ASSERT_TRUE(plugged_at(0).plugged);
}

static void test_single_false_sample_is_ignored() {
  const PlugFilter dip = plug_filter(plugged_at(100), false, 110);
  TEST_ASSERT_TRUE(dip.plugged);
  const PlugFilter back = plug_filter(dip, true, 120);
  TEST_ASSERT_TRUE(back.plugged);
  TEST_ASSERT_FALSE(back.falling);
  // the dropout is forgotten: a later one starts its own window
  const PlugFilter later = plug_filter(plug_filter(back, false, 5000), false, 5000 + kUnplugConfirmMs - 1);
  TEST_ASSERT_TRUE(later.plugged);
}

static void test_unplug_after_confirm_period_of_false_samples() {
  PlugFilter f = plugged_at(0);
  for (uint32_t t = 1000; t < 1000 + kUnplugConfirmMs; t += 10) {
    f = plug_filter(f, false, t);
    TEST_ASSERT_TRUE(f.plugged);
  }
  f = plug_filter(f, false, 1000 + kUnplugConfirmMs);
  TEST_ASSERT_FALSE(f.plugged);
  TEST_ASSERT_FALSE(f.falling);
  TEST_ASSERT_FALSE(plug_filter(f, false, 9000).plugged);  // stays unplugged
  TEST_ASSERT_TRUE(plug_filter(f, true, 9001).plugged);    // replug counts at once
}

static void test_true_sample_restarts_the_window() {
  PlugFilter f = plug_filter(plugged_at(0), false, 100);
  f = plug_filter(f, true, 100 + kUnplugConfirmMs - 1);
  f = plug_filter(f, false, 100 + kUnplugConfirmMs);
  TEST_ASSERT_TRUE(f.plugged);  // the window began again at the last false sample
  TEST_ASSERT_TRUE(plug_filter(f, false, 100 + kUnplugConfirmMs - 1 + kUnplugConfirmMs).plugged);
  TEST_ASSERT_FALSE(plug_filter(f, false, 100 + 2 * kUnplugConfirmMs).plugged);
}

static void test_millis_wraparound() {
  const uint32_t near_wrap = 0xFFFFFFFFu - 100;
  const PlugFilter f = plug_filter(plugged_at(near_wrap - 10), false, near_wrap);
  TEST_ASSERT_TRUE(plug_filter(f, false, near_wrap + 200).plugged);  // now wrapped to 99
  TEST_ASSERT_FALSE(plug_filter(f, false, near_wrap + kUnplugConfirmMs).plugged);
}

static void test_input_is_not_changed() {
  const PlugFilter f = plugged_at(0);
  const PlugFilter g = plug_filter(f, false, 10);
  TEST_ASSERT_FALSE(f.falling);
  TEST_ASSERT_TRUE(g.falling);
  TEST_ASSERT_EQUAL_UINT32(10, g.since_ms);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_unplugged_and_plug_counts_at_once);
  RUN_TEST(test_single_false_sample_is_ignored);
  RUN_TEST(test_unplug_after_confirm_period_of_false_samples);
  RUN_TEST(test_true_sample_restarts_the_window);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_input_is_not_changed);
  return UNITY_END();
}
