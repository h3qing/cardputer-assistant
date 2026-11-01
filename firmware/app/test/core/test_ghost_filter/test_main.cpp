// GhostFilter (05 §5.7 rule 3, 04 US-13): matrix coordinates as in hwtest
// (row = x / 2, col = y + 4 * (x % 2)). A frame whose pressed set holds all four
// corners of a matrix rectangle is ambiguous: dropped whole, ghost_n counted once.
#include <unity.h>

#include "ghost_filter.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static uint8_t at(uint8_t x, uint8_t y) { return layout_index(x, y); }

static void test_matrix_mapping_matches_hwtest() {
  const MatrixPos fn = matrix_of(key::kFn);  // layout (0, 2)
  TEST_ASSERT_EQUAL_UINT8(0, fn.row);
  TEST_ASSERT_EQUAL_UINT8(2, fn.col);
  const MatrixPos shift = matrix_of(key::kShift);  // layout (1, 2)
  TEST_ASSERT_EQUAL_UINT8(0, shift.row);
  TEST_ASSERT_EQUAL_UINT8(6, shift.col);
  const MatrixPos bksp = matrix_of(key::kBksp);  // layout (13, 0)
  TEST_ASSERT_EQUAL_UINT8(6, bksp.row);
  TEST_ASSERT_EQUAL_UINT8(4, bksp.col);
  for (uint8_t i = 0; i < kMatrixKeys; ++i) {
    const MatrixPos p = matrix_of(i);
    TEST_ASSERT_TRUE(p.row < 7 && p.col < 8);
    TEST_ASSERT_EQUAL_UINT8(i, layout_of(p));
  }
}

static void test_keyset_is_a_value() {
  const KeySet a = KeySet{}.with(key::kOpt);
  const KeySet b = a.with(at(1, 1));
  TEST_ASSERT_EQUAL_UINT8(1, a.count());
  TEST_ASSERT_EQUAL_UINT8(2, b.count());
  TEST_ASSERT_TRUE(b.has(key::kOpt));
  TEST_ASSERT_FALSE(a.has(at(1, 1)));
  TEST_ASSERT_TRUE(b.minus(a) == KeySet{}.with(at(1, 1)));
  TEST_ASSERT_TRUE(b.without(key::kOpt) == KeySet{}.with(at(1, 1)));
  TEST_ASSERT_TRUE(KeySet{}.with(key::kG0).has(key::kG0));
  TEST_ASSERT_TRUE(KeySet{}.empty());
}

static void test_rectangle_detection() {
  // fn (0,2) shift (0,6) a (1,2) s (1,6): Fn+Shift+A shows s as a ghost (02 §6)
  const KeySet three = KeySet{}.with(key::kFn).with(key::kShift).with(at(2, 2));
  TEST_ASSERT_TRUE(ghost_risk(three));
  TEST_ASSERT_FALSE(has_rectangle(three));
  TEST_ASSERT_TRUE(has_rectangle(three.with(at(3, 2))));
  TEST_ASSERT_FALSE(ghost_risk(KeySet{}.with(key::kOpt).with(at(3, 2))));
  // three keys in one matrix row: no rectangle, no risk
  TEST_ASSERT_FALSE(ghost_risk(KeySet{}.with(at(0, 0)).with(at(0, 1)).with(at(0, 2))));
}

static void test_ambiguous_frame_is_dropped_and_counted_once() {
  const KeySet good = KeySet{}.with(key::kFn).with(key::kShift).with(at(2, 2));
  const KeySet ghosted = good.with(at(3, 2));
  const GhostStep s1 = ghost_filter(GhostState{}, good);
  TEST_ASSERT_TRUE(s1.keys == good);
  const GhostStep s2 = ghost_filter(s1.state, ghosted);
  TEST_ASSERT_TRUE(s2.keys == good);  // the whole frame is dropped
  TEST_ASSERT_EQUAL_UINT16(1, s2.state.ghost_n);
  const GhostStep s3 = ghost_filter(s2.state, ghosted);  // same frame again
  TEST_ASSERT_EQUAL_UINT16(1, s3.state.ghost_n);
  TEST_ASSERT_TRUE(s3.keys == good);
  const GhostStep s4 = ghost_filter(s3.state, KeySet{});
  TEST_ASSERT_TRUE(s4.keys.empty());
  TEST_ASSERT_EQUAL_UINT16(1, s4.state.ghost_n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_matrix_mapping_matches_hwtest);
  RUN_TEST(test_keyset_is_a_value);
  RUN_TEST(test_rectangle_detection);
  RUN_TEST(test_ambiguous_frame_is_dropped_and_counted_once);
  return UNITY_END();
}
