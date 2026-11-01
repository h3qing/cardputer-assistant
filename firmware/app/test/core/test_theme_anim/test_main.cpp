// Motion (docs/19 §3.5, §7.3): Mando's poses step once per 200 ms tick (four key poses per
// loop, nodding and tilting on one shared phase, a hop played once, a bubble that pops up in
// two steps, sniffing that stops after 8 s, the lock card's z's one per 600 ms), the big ●
// breathing in four steps, and the Tab sheet's three-frame slide. Between ticks a page draws
// the last tick's view, so nothing moves without one.
#include <unity.h>

#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core
#include "theme_anim.h"

using namespace cpa;
using namespace cpa::theme;

void setUp() {}
void tearDown() {}

namespace {

MandoMotion ticks(MandoMotion m, MandoAct a, int n) {
  for (int i = 0; i < n; ++i) m = mando_tick(m, a);
  return m;
}

}  // namespace

static void test_a_tick_is_due_every_200_ms() {
  TEST_ASSERT_EQUAL_UINT32(200, kAnimTickMs);
  TEST_ASSERT_FALSE(tick_due(1000, 1199));
  TEST_ASSERT_TRUE(tick_due(1000, 1200));
  TEST_ASSERT_TRUE(tick_due(0xFFFFFF00u, 0x100));  // millis() wraps
}

static void test_loops_step_through_four_poses() {
  MandoMotion m;
  uint8_t seen[8];
  for (int i = 0; i < 8; ++i) {
    m = mando_tick(m, MandoAct::Listen);
    seen[i] = m.shown.pose;
    TEST_ASSERT_EQUAL(MandoAct::Listen, m.shown.act);
  }
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_UINT8(seen[i], seen[i + 4]);
    TEST_ASSERT_TRUE(seen[i] < 4);
    TEST_ASSERT_TRUE(seen[(i + 1) % 4] == (seen[i] + 1) % 4);
  }
}

static void test_nodding_and_tilting_share_the_phase() {
  const MandoMotion a = ticks(MandoMotion{}, MandoAct::Listen, 3);
  const MandoMotion b = mando_tick(a, MandoAct::Tilt);
  TEST_ASSERT_EQUAL(MandoAct::Tilt, b.shown.act);
  TEST_ASSERT_EQUAL_UINT8((a.shown.pose + 1) % 4, b.shown.pose);
  TEST_ASSERT_EQUAL_UINT16(a.ticks + 1, b.ticks);  // the same act family: no restart
}

static void test_a_hop_plays_once() {
  MandoMotion m = ticks(MandoMotion{}, MandoAct::Idle, 2);
  const uint8_t want[] = {0, 1, 2, 3, 3, 3};
  for (uint8_t w : want) {
    m = mando_tick(m, MandoAct::Hop);
    TEST_ASSERT_EQUAL_UINT8(w, m.shown.pose);
  }
}

static void test_a_bubble_pops_up_in_two_steps() {
  MandoMotion m = ticks(MandoMotion{}, MandoAct::Idle, 5);
  m = mando_tick(m, MandoAct::NeedsYou);
  TEST_ASSERT_EQUAL_UINT8(0, m.shown.bubble);
  TEST_ASSERT_EQUAL_UINT8(0, m.shown.pose);
  m = mando_tick(m, MandoAct::NeedsYou);
  TEST_ASSERT_EQUAL_UINT8(1, m.shown.bubble);
  m = ticks(m, MandoAct::NeedsYou, 10);
  TEST_ASSERT_EQUAL_UINT8(1, m.shown.bubble);
  m = mando_tick(m, MandoAct::NoVoice);  // a new act pops again
  TEST_ASSERT_EQUAL_UINT8(0, m.shown.bubble);
}

static void test_sniffing_stops_after_eight_seconds() {
  MandoMotion m = ticks(MandoMotion{}, MandoAct::Sniff, kSniffTicks - 4);
  bool moved = false;
  for (int i = 0; i < 4; ++i) {
    const uint8_t before = m.shown.pose;
    m = mando_tick(m, MandoAct::Sniff);
    moved = moved || m.shown.pose != before;
  }
  TEST_ASSERT_TRUE(moved);
  for (int i = 0; i < 12; ++i) {
    m = mando_tick(m, MandoAct::Sniff);
    TEST_ASSERT_EQUAL_UINT8(0, m.shown.pose);
  }
}

static void test_the_zs_light_one_every_600_ms() {
  const uint8_t want[] = {3, 3, 3, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 0};
  MandoMotion m;
  for (uint8_t w : want) {
    m = mando_tick(m, MandoAct::Doze);
    TEST_ASSERT_EQUAL_UINT8(w, m.shown.z);
  }
}

static void test_between_ticks_the_last_view_is_drawn() {
  const MandoMotion m = ticks(MandoMotion{}, MandoAct::Listen, 2);
  TEST_ASSERT_TRUE(same_view(m.shown, mando_view(m, MandoAct::Listen)));
  TEST_ASSERT_TRUE(same_view(m.shown, mando_view(m, MandoAct::Tilt)));  // frozen: no swap until a tick
  const MandoView sniff = mando_view(m, MandoAct::Sniff);  // a new act: its rest pose
  TEST_ASSERT_EQUAL(MandoAct::Sniff, sniff.act);
  TEST_ASSERT_EQUAL_UINT8(mando_rest_pose(MandoAct::Sniff), sniff.pose);
  TEST_ASSERT_EQUAL_UINT8(0, mando_view(m, MandoAct::Hop).pose);  // a hop from its start
  TEST_ASSERT_EQUAL_UINT8(0, mando_view(m, MandoAct::NeedsYou).bubble);
  TEST_ASSERT_EQUAL_UINT8(kZs, mando_view(m, MandoAct::Doze).z);
  MandoView other = m.shown;
  other.pose = static_cast<uint8_t>(other.pose + 1);
  TEST_ASSERT_FALSE(same_view(m.shown, other));
}

// Only what shows changes the view: an idle Mando's ticks redraw nothing.
static void test_a_still_act_keeps_its_view() {
  const MandoMotion a = ticks(MandoMotion{}, MandoAct::Idle, 1);
  const MandoMotion b = ticks(a, MandoAct::Idle, 7);
  TEST_ASSERT_TRUE(same_view(a.shown, b.shown));
  TEST_ASSERT_TRUE(same_view(mando_view(MandoMotion{}, MandoAct::Idle), b.shown));
  TEST_ASSERT_EQUAL_UINT8(kZs, ticks(a, MandoAct::Listen, 5).shown.z);
}

static void test_the_breath_steps_round() {
  const uint8_t want[] = {1, 2, 3, 0, 1};
  uint8_t s = 0;
  for (uint8_t w : want) {
    s = breath_next(s);
    TEST_ASSERT_EQUAL_UINT8(w, s);
  }
}

static void test_a_slide_takes_three_frames() {
  TEST_ASSERT_EQUAL_INT16(240 - (240 - 66) * 45 / 100, slide_at(240, 66, 0));
  TEST_ASSERT_EQUAL_INT16(240 - (240 - 66) * 85 / 100, slide_at(240, 66, 1));
  TEST_ASSERT_EQUAL_INT16(66, slide_at(240, 66, 2));
  TEST_ASSERT_EQUAL_INT16(66, slide_at(240, 66, 9));
  TEST_ASSERT_EQUAL_INT16(100 + 45 * 45 / 100, slide_at(100, 145, 0));  // either way
  TEST_ASSERT_EQUAL_INT16(100 + 60 * 75 / 100, knob_at(100, 160, 0));  // most of the way at once
  TEST_ASSERT_EQUAL_INT16(100 + 60 * 90 / 100, knob_at(100, 160, 1));
  TEST_ASSERT_EQUAL_INT16(160, knob_at(100, 160, 2));
  TEST_ASSERT_EQUAL_UINT8(0, slide_frame(0));
  TEST_ASSERT_EQUAL_UINT8(0, slide_frame(kSlideMs - 1));
  TEST_ASSERT_EQUAL_UINT8(1, slide_frame(kSlideMs));
  TEST_ASSERT_EQUAL_UINT8(2, slide_frame(2 * kSlideMs));
  TEST_ASSERT_EQUAL_UINT8(kSlideFrames - 1, slide_frame(5000));
  TEST_ASSERT_TRUE(kSlideMs * kSlideFrames <= 150);  // the sheet is in within 150 ms
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_tick_is_due_every_200_ms);
  RUN_TEST(test_loops_step_through_four_poses);
  RUN_TEST(test_nodding_and_tilting_share_the_phase);
  RUN_TEST(test_a_hop_plays_once);
  RUN_TEST(test_a_bubble_pops_up_in_two_steps);
  RUN_TEST(test_sniffing_stops_after_eight_seconds);
  RUN_TEST(test_the_zs_light_one_every_600_ms);
  RUN_TEST(test_between_ticks_the_last_view_is_drawn);
  RUN_TEST(test_a_still_act_keeps_its_view);
  RUN_TEST(test_the_breath_steps_round);
  RUN_TEST(test_a_slide_takes_three_frames);
  return UNITY_END();
}
