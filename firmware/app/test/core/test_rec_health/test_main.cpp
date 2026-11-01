// Recording health counters (gh #21, docs/07 §6.7 #6, test firmware): the audio task keeps
// one RecHealth, starts it with each recording and replaces it after every written block.
// Highs keep the highest, heap minima the lowest, rot_ms_max only counts blocks whose write
// changed segment; the values stay after the recording until the next one starts.
// A re-arm the capture task could not make is a miss while the capture runs, and nothing once
// the stop has begun (v0.4.1 known issue: memos held exactly 3,000 ms counted one at the stop).
#include <unity.h>

#include "rec_health.h"

using namespace cpa::rec;

void setUp() {}
void tearDown() {}

static BlockHealth block(uint8_t ring, uint32_t missing, uint32_t heap, uint32_t largest) {
  BlockHealth b;
  b.ring_high = ring;
  b.mic_missing = missing;
  b.heap_free = heap;
  b.heap_block = largest;
  return b;
}

static void test_nothing_is_measured_before_the_first_recording() {
  const RecHealth h;
  TEST_ASSERT_FALSE(h.measured);
}

static void test_a_recording_starts_from_the_heap_it_begins_with() {
  const RecHealth h = health_begin(180000, 160000);
  TEST_ASSERT_TRUE(h.measured);
  TEST_ASSERT_EQUAL_UINT16(0, h.ring_high);
  TEST_ASSERT_EQUAL_UINT32(0, h.mic_missing);
  TEST_ASSERT_EQUAL_UINT32(0, h.rot_ms_max);
  TEST_ASSERT_EQUAL_UINT32(180000, h.heap_free);
  TEST_ASSERT_EQUAL_UINT32(160000, h.heap_block);
}

static void test_blocks_keep_the_highs_and_the_heap_minima() {
  RecHealth h = health_begin(180000, 160000);
  h = health_block(h, block(2, 0, 175000, 161000));
  h = health_block(h, block(1, 1, 178000, 150000));  // the ring's mark never goes down; this is the max
  h = health_block(h, block(2, 1, 179000, 159000));
  TEST_ASSERT_EQUAL_UINT16(2, h.ring_high);
  TEST_ASSERT_EQUAL_UINT32(1, h.mic_missing);
  TEST_ASSERT_EQUAL_UINT32(175000, h.heap_free);
  TEST_ASSERT_EQUAL_UINT32(150000, h.heap_block);
  TEST_ASSERT_TRUE(h.measured);
}

static void test_only_a_segment_change_counts_as_a_rotation() {
  RecHealth h = health_begin(180000, 160000);
  BlockHealth plain = block(1, 0, 180000, 160000);
  plain.write_ms = 90;  // a slow ordinary write (an fsync): not a rotation
  h = health_block(h, plain);
  TEST_ASSERT_EQUAL_UINT32(0, h.rot_ms_max);
  BlockHealth rot = plain;
  rot.rotated = true;
  rot.write_ms = 214;
  h = health_block(h, rot);
  rot.write_ms = 120;
  h = health_block(h, rot);
  TEST_ASSERT_EQUAL_UINT32(214, h.rot_ms_max);
}

static void test_the_next_recording_starts_again() {
  BlockHealth rot = block(5, 3, 90000, 40000);
  rot.rotated = true;
  rot.write_ms = 300;
  const RecHealth first = health_block(health_begin(180000, 160000), rot);
  const RecHealth second = health_begin(181000, 162000);
  TEST_ASSERT_EQUAL_UINT16(5, first.ring_high);  // the old value is untouched
  TEST_ASSERT_EQUAL_UINT16(0, second.ring_high);
  TEST_ASSERT_EQUAL_UINT32(0, second.mic_missing);
  TEST_ASSERT_EQUAL_UINT32(0, second.rot_ms_max);
  TEST_ASSERT_EQUAL_UINT32(181000, second.heap_free);
}

// MicRing's release callback found no free block, or Mic.record refused it, while the
// microphone records: a miss, and a block refill() arms later.
static void test_a_rearm_that_fails_while_capturing_is_a_miss() {
  TEST_ASSERT_TRUE(rearm_missed(false));
}

// The stop has begun (MicRing::stop, before Mic.end): Mic.end refuses the callback's record()
// while it winds the capture down, and nothing more is captured. Neither a miss nor owed.
static void test_a_rearm_that_fails_once_the_stop_began_is_not_a_miss() {
  TEST_ASSERT_FALSE(rearm_missed(true));
}

// The device's 3,000 ms memo (missprobe3.json): 24 blocks written, the stop, then the
// capture task's last release fails to re-arm and the ring still holds a block, which
// end_session writes; the recording's mic_missing stays 0. A failure before the stop counts.
static void test_a_memo_stopped_on_a_block_edge_reports_no_miss() {
  uint32_t ring = 0;  // MicRing::missed()
  RecHealth h = health_begin(180000, 160000);
  for (int i = 0; i < 24; ++i) h = health_block(h, block(1, ring, 180000, 160000));
  ring += rearm_missed(true) ? 1 : 0;  // the release during Mic.end
  h = health_block(h, block(1, ring, 180000, 160000));
  TEST_ASSERT_EQUAL_UINT32(0, h.mic_missing);
  ring += rearm_missed(false) ? 1 : 0;  // a real one, mid-recording
  h = health_block(h, block(1, ring, 180000, 160000));
  TEST_ASSERT_EQUAL_UINT32(1, h.mic_missing);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_measured_before_the_first_recording);
  RUN_TEST(test_a_recording_starts_from_the_heap_it_begins_with);
  RUN_TEST(test_blocks_keep_the_highs_and_the_heap_minima);
  RUN_TEST(test_only_a_segment_change_counts_as_a_rotation);
  RUN_TEST(test_the_next_recording_starts_again);
  RUN_TEST(test_a_rearm_that_fails_while_capturing_is_a_miss);
  RUN_TEST(test_a_rearm_that_fails_once_the_stop_began_is_not_a_miss);
  RUN_TEST(test_a_memo_stopped_on_a_block_edge_reports_no_miss);
  return UNITY_END();
}
