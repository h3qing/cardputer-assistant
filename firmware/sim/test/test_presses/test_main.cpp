// Key presses as the runtime samples them: one KeySet per 10 ms loop, each press held
// for its hold time and then released for 30 ms (like debug.key injection), on top of
// the keys a script holds down.
#include <unity.h>

#include "presses.h"

using namespace cpasim;
using cpa::KeySet;

void setUp() {}
void tearDown() {}

namespace {
const KeySet kA = KeySet{}.with(30);
const KeySet kG0 = KeySet{}.with(cpa::key::kG0);
}  // namespace

static void test_one_press() {
  const auto s = press_samples({kA}, 50, KeySet{});
  TEST_ASSERT_EQUAL_size_t(8, s.size());
  for (size_t i = 0; i < 5; ++i) TEST_ASSERT_TRUE(s[i] == kA);
  for (size_t i = 5; i < 8; ++i) TEST_ASSERT_TRUE(s[i].empty());
}

static void test_presses_follow_each_other() {
  const KeySet b = KeySet{}.with(47);
  const auto s = press_samples({kA, b}, 50, KeySet{});
  TEST_ASSERT_EQUAL_size_t(16, s.size());
  TEST_ASSERT_TRUE(s[7].empty());
  TEST_ASSERT_TRUE(s[8] == b);
}

static void test_held_keys_stay_down() {
  const auto s = press_samples({kA}, 50, kG0);
  TEST_ASSERT_TRUE(s[0] == kA.with(cpa::key::kG0));
  TEST_ASSERT_TRUE(s[7] == kG0);
}

static void test_long_and_zero_holds() {
  TEST_ASSERT_EQUAL_size_t(63, press_samples({kG0}, 600, KeySet{}).size());
  TEST_ASSERT_EQUAL_size_t(4, press_samples({kA}, 0, KeySet{}).size());  // one sample at least
  TEST_ASSERT_EQUAL_size_t(9, press_samples({kA}, 55, KeySet{}).size());  // rounded up
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_press);
  RUN_TEST(test_presses_follow_each_other);
  RUN_TEST(test_held_keys_stay_down);
  RUN_TEST(test_long_and_zero_holds);
  return UNITY_END();
}
