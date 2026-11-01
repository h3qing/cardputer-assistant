// debug.state.diag and the recording health counters (gh #21, protocol README item 21, test
// firmware). The runtime reports the device's own numbers: the once-a-second stats (the
// system page's), the platform's loop timing, the audio task's stack and the keyboard
// counters in diag; the audio task's RecHealth in rec once it measures (from the first
// recording of the boot on). LoopStats is the pure loop timing behind the device's numbers.
#include <unity.h>

#include "loop_stats.h"
#include "rec_engine.h"
#include "runtime.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

// --- LoopStats -------------------------------------------------------------------------

static void test_a_pass_counts_its_time_and_no_gap_before_the_second() {
  const LoopStats s = loop_stats_add(LoopStats{}, 1000, 4000, 1);
  TEST_ASSERT_EQUAL_UINT32(1, s.n);
  TEST_ASSERT_EQUAL_UINT32(3000, s.max_us);
  TEST_ASSERT_EQUAL_UINT32(3000, loop_win_max_us(s));
  TEST_ASSERT_EQUAL_UINT32(0, s.gap_max_us);
  const LoopStats t = loop_stats_add(s, 9000, 9500, 9);
  TEST_ASSERT_EQUAL_UINT32(2, t.n);
  TEST_ASSERT_EQUAL_UINT32(3000, t.max_us);
  TEST_ASSERT_EQUAL_UINT32(8000, t.gap_max_us);  // start to start: the pass and what ran after it
  TEST_ASSERT_EQUAL_UINT32(8000, gap_win_max_us(t));
  TEST_ASSERT_EQUAL_UINT32(1, s.n);  // the old value is untouched
}

static void test_slow_passes_are_counted_above_each_threshold() {
  LoopStats s;
  const uint32_t passes[] = {20000, 20001, 50001, 100001, 500001};
  uint32_t at = 0;
  for (uint32_t us : passes) {
    s = loop_stats_add(s, at, at + us, at / 1000);
    at += us + 10;
  }
  TEST_ASSERT_EQUAL_UINT32(4, s.over_20);  // 20000 exactly is not over
  TEST_ASSERT_EQUAL_UINT32(3, s.over_50);
  TEST_ASSERT_EQUAL_UINT32(2, s.over_100);
  TEST_ASSERT_EQUAL_UINT32(1, s.over_500);
  TEST_ASSERT_EQUAL_UINT32(500001, s.max_us);
}

static void test_the_window_covers_the_last_15_to_30_s() {
  LoopStats s = loop_stats_add(LoopStats{}, 0, 40000, 0);  // 40 ms at t = 0
  s = loop_stats_add(s, 100000, 101000, 14999);
  TEST_ASSERT_EQUAL_UINT32(40000, loop_win_max_us(s));
  s = loop_stats_add(s, 200000, 201000, kLoopWindowMs);  // first bucket closes: still covered
  TEST_ASSERT_EQUAL_UINT32(40000, loop_win_max_us(s));
  s = loop_stats_add(s, 201500, 203500, 2 * kLoopWindowMs);  // the 40 ms pass leaves the window
  TEST_ASSERT_EQUAL_UINT32(2000, loop_win_max_us(s));
  TEST_ASSERT_EQUAL_UINT32(40000, s.max_us);  // since boot stays
  TEST_ASSERT_EQUAL_UINT32(100000, s.gap_max_us);
  TEST_ASSERT_EQUAL_UINT32(1500, gap_win_max_us(s));  // so do the 100 ms gaps
}

static void test_micros_wrapping_mid_pass_is_one_short_pass() {
  const LoopStats s = loop_stats_add(LoopStats{}, 0xFFFFFF00u, 0x00000100u, 5);
  TEST_ASSERT_EQUAL_UINT32(0x200, s.max_us);
  TEST_ASSERT_EQUAL_UINT32(0, s.over_20);
}

// --- the runtime's debug.state ------------------------------------------------------------

static proto::DebugState debug_state(RuntimeRig& r) {
  r.send("debug.state");
  r.step(10);
  return std::get<proto::DebugState>(r.tx.of_type("debug.state").back().msg.body);
}

static void test_diag_reports_the_device_numbers() {
  RecRig x;
  x.r.plat.next.heap_free = 183028;
  x.r.plat.next.heap_min = 171520;
  x.r.plat.next.heap_max_block = 163828;
  x.r.plat.next.stack_hwm = 5100;
  x.r.plat.loop = loop_stats_add(loop_stats_add(LoopStats{}, 0, 52000, 0), 60000, 61000, 0);
  x.port.stack = 2256;
  x.r.step(10, KeySet{}.with(0).with(2).with(14).with(16));  // a ghost frame
  x.r.step(1000);  // the once-a-second stats
  const proto::DebugState s = debug_state(x.r);
  TEST_ASSERT_TRUE(s.diag.has);
  const proto::DebugDiag& d = s.diag.value;
  TEST_ASSERT_EQUAL_UINT32(183028, d.heap_free);
  TEST_ASSERT_EQUAL_UINT32(171520, d.heap_min);
  TEST_ASSERT_EQUAL_UINT32(163828, d.heap_block);
  TEST_ASSERT_EQUAL_UINT32(5100, d.stack_left);
  TEST_ASSERT_EQUAL_UINT32(2256, d.audio_stack_left);
  TEST_ASSERT_EQUAL_UINT32(2, d.loop_n);
  TEST_ASSERT_EQUAL_UINT32(52000, d.loop_max_us);
  TEST_ASSERT_EQUAL_UINT32(52000, d.loop_win_max_us);
  TEST_ASSERT_EQUAL_UINT32(60000, d.gap_max_us);
  TEST_ASSERT_EQUAL_UINT32(60000, d.gap_win_max_us);
  TEST_ASSERT_EQUAL_UINT32(1, d.loop_over_20);
  TEST_ASSERT_EQUAL_UINT32(1, d.loop_over_50);
  TEST_ASSERT_EQUAL_UINT32(0, d.loop_over_100);
  TEST_ASSERT_EQUAL_UINT32(0, d.loop_over_500);
  TEST_ASSERT_EQUAL_UINT32(1, d.ghost_n);
  TEST_ASSERT_EQUAL_UINT32(0, d.kb_reinit_n);
}

static void test_diag_without_an_audio_task_or_loop_timing_is_zeros() {
  RuntimeRig r;  // no attach_rec: no SD card, no audio task; the fake platform times nothing
  r.handshake();
  const proto::DebugState s = debug_state(r);
  TEST_ASSERT_TRUE(s.diag.has);
  TEST_ASSERT_EQUAL_UINT32(0, s.diag.value.audio_stack_left);
  TEST_ASSERT_EQUAL_UINT32(0, s.diag.value.loop_n);
  TEST_ASSERT_FALSE(s.rec.has);
}

static void test_rec_health_is_sent_once_the_audio_task_measures() {
  RecRig x;
  proto::DebugState s = debug_state(x.r);
  TEST_ASSERT_TRUE(s.rec.has);
  TEST_ASSERT_FALSE(s.rec.value.ring_high.has || s.rec.value.mic_missing.has || s.rec.value.rot_ms_max.has);
  TEST_ASSERT_FALSE(s.rec.value.heap_free.has || s.rec.value.heap_block.has);
  rec::BlockHealth b;
  b.ring_high = 2;
  b.rotated = true;
  b.write_ms = 214;
  b.heap_free = 171520;
  b.heap_block = 155648;
  x.port.health_now = rec::health_block(rec::health_begin(180000, 160000), b);
  s = debug_state(x.r);
  TEST_ASSERT_TRUE(s.rec.value.ring_high.has && s.rec.value.mic_missing.has && s.rec.value.rot_ms_max.has);
  TEST_ASSERT_EQUAL_UINT16(2, s.rec.value.ring_high.value);
  TEST_ASSERT_EQUAL_UINT32(0, s.rec.value.mic_missing.value);
  TEST_ASSERT_EQUAL_UINT32(214, s.rec.value.rot_ms_max.value);
  TEST_ASSERT_EQUAL_UINT32(171520, s.rec.value.heap_free.value);
  TEST_ASSERT_EQUAL_UINT32(155648, s.rec.value.heap_block.value);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_pass_counts_its_time_and_no_gap_before_the_second);
  RUN_TEST(test_slow_passes_are_counted_above_each_threshold);
  RUN_TEST(test_the_window_covers_the_last_15_to_30_s);
  RUN_TEST(test_micros_wrapping_mid_pass_is_one_short_pass);
  RUN_TEST(test_diag_reports_the_device_numbers);
  RUN_TEST(test_diag_without_an_audio_task_or_loop_timing_is_zeros);
  RUN_TEST(test_rec_health_is_sent_once_the_audio_task_measures);
  return UNITY_END();
}
