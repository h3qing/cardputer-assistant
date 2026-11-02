// The runtime's animation path (docs/19 §3.5, §7.4-§7.5): only while the backlight is On, a
// tick every 200 ms redraws a few small rectangles (Mando, the waveform or timeline, the big
// ●) and pushes just those, and breathes the ●. A tick never marks the screen dirty and never
// keeps it awake, so the full redraws stay where they were: on a change, at most 20 fps, and
// once a second; an audio level alone no longer redraws the screen. At Dim nothing moves: no
// partial push, the ● back at full Rec, the waveform and timeline as they were. Dark: nothing.
// The Tab sheet slides in: one full frame, then two pushes of the sheet within 150 ms.
#include <unity.h>

#include <string>

#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint8_t kL = 38;  // layout index of 'l'
constexpr uint8_t kR = 18;  // 'r'

KeySet keys(std::initializer_list<uint8_t> ks) {
  KeySet s;
  for (uint8_t k : ks) s = s.with(k);
  return s;
}

void press(RuntimeRig& r, KeySet ks) {
  r.step(10, ks);
  r.step(10);
}

// ms of the loop in 10 ms passes with `held` down, and an audio block every 128 ms.
void run(RecRig& x, uint32_t ms, KeySet held = KeySet{}) {
  for (uint32_t t = 0; t < ms; t += 10) {
    x.r.step(10, held);
    if (t % 130 == 120) x.port.feed(128);
  }
}

void start_meeting(RecRig& x) {
  press(x.r, keys({key::kOpt, kR}));
  x.r.step(500);
  press(x.r, keys({key::kEnter}));
  x.steps();
  x.port.feed(256);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
}

int area(const Rect& r) { return r.w * r.h; }

// One 128 ms block of a square wave: its RMS is amp.
std::string block(int16_t amp) {
  std::string pcm(4096, '\0');
  for (size_t i = 0; i < pcm.size(); i += 2) {
    const int16_t v = (i / 2) % 2 == 0 ? amp : static_cast<int16_t>(-amp);
    pcm[i] = static_cast<char>(v & 0xFF);
    pcm[i + 1] = static_cast<char>((v >> 8) & 0xFF);
  }
  return pcm;
}

bool inside(const Rect& in, const Rect& out) {
  return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

// On battery with no key for 61 s: a recording dims the backlight.
void dim(RecRig& x, KeySet held = KeySet{}) {
  x.r.tx.is_plugged = false;
  run(x, 61000, held);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Dim);
}

}  // namespace

static void test_a_memo_pushes_small_rects_each_tick() {
  RecRig x;
  const KeySet g0 = keys({key::kG0});
  x.hold_g0(600);
  run(x, 300, g0);
  const int frames = x.r.disp.frames;
  const size_t parts = x.r.disp.partials.size();
  run(x, 2000, g0);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.frames - frames <= 2, "a full frame once a second, no more");
  const size_t n = x.r.disp.partials.size() - parts;
  TEST_ASSERT_TRUE_MESSAGE(n >= 2 * 9 && n <= 3 * 11, std::to_string(n).c_str());  // ~10 ticks, <= 3 rects
  for (size_t i = parts; i < x.r.disp.partials.size(); ++i) {
    const Rect& r = x.r.disp.partials[i];
    TEST_ASSERT_TRUE(inside(r, kContentRect));
    TEST_ASSERT_TRUE(area(r) <= 173 * 23);  // the waveform is the largest (Mando 36 x 64)
  }
  const Rect dot{60, 38, 12, 12};
  int dots = 0;
  for (size_t i = parts; i < x.r.disp.partials.size(); ++i) {
    const Rect& r = x.r.disp.partials[i];
    dots += r.x == dot.x && r.y == dot.y && r.w == dot.w && r.h == dot.h ? 1 : 0;
  }
  TEST_ASSERT_TRUE(dots >= 9);  // the ● breathes every tick
  bool full = false, dark = false;
  for (uint8_t s : x.r.disp.breaths) {
    full = full || s == 0;
    dark = dark || s == 2;
  }
  TEST_ASSERT_TRUE(full && dark);
}

static void test_a_level_alone_does_not_redraw() {
  RecRig x;
  start_meeting(x);
  run(x, 1100);
  x.port.feed_pcm(block(3000));
  x.r.step(10);
  TEST_ASSERT_FALSE(x.r.rt.shell().dirty());
  x.port.feed_pcm(block(500));
  x.r.step(10);
  TEST_ASSERT_EQUAL_UINT16(500, x.r.rt.state().rec.level);  // the level reached AppState
  TEST_ASSERT_FALSE(x.r.rt.shell().dirty());                // and asked for no redraw
}

static void test_ticks_never_mark_the_screen_dirty() {
  RecRig x;
  start_meeting(x);
  for (int i = 0; i < 300; ++i) {
    const size_t parts = x.r.disp.partials.size();
    run(x, 10);
    if (x.r.disp.partials.size() != parts) TEST_ASSERT_FALSE(x.r.rt.shell().dirty());
  }
  TEST_ASSERT_TRUE(x.r.disp.partials.size() > 20);
}

static void test_at_dim_nothing_moves() {
  RecRig x;
  start_meeting(x);
  dim(x);
  TEST_ASSERT_EQUAL_UINT8(0, x.r.disp.breaths.back());  // the ● at full Rec
  const size_t parts = x.r.disp.partials.size();
  run(x, 1500);
  const FakeCanvas a = x.r.disp.canvas_;
  run(x, 3000);
  const FakeCanvas& b = x.r.disp.canvas_;
  TEST_ASSERT_EQUAL_UINT(parts, x.r.disp.partials.size());
  TEST_ASSERT_EQUAL_UINT8(0, x.r.disp.breaths.back());
  TEST_ASSERT_EQUAL_UINT(a.arts.size(), b.arts.size());
  for (size_t i = 0; i < a.arts.size(); ++i) TEST_ASSERT_TRUE(a.arts[i].id == b.arts[i].id);  // Mando still
  TEST_ASSERT_EQUAL_UINT(a.fills.size(), b.fills.size());  // the timeline as it was
  for (size_t i = 0; i < a.fills.size(); ++i) {
    TEST_ASSERT_TRUE(a.fills[i].second == b.fills[i].second);
    TEST_ASSERT_EQUAL_INT16(a.fills[i].first.h, b.fills[i].first.h);
  }
  TEST_ASSERT_FALSE(a.row(0) == b.row(0));  // only the clock moved
}

static void test_dark_pushes_nothing() {
  RecRig x;
  x.r.tx.is_plugged = false;
  run(x, 61000);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Off);
  const size_t parts = x.r.disp.partials.size();
  const int frames = x.r.disp.frames;
  run(x, 3000);
  TEST_ASSERT_EQUAL_UINT(parts, x.r.disp.partials.size());
  TEST_ASSERT_TRUE(x.r.disp.frames - frames <= 3);  // what was there: the stats' once a second
}

// The lit lock card's z's move while it is lit, and never keep it lit.
static void test_the_lock_card_dozes_then_goes_dark() {
  RecRig x;
  x.r.tx.is_plugged = false;
  press(x.r, keys({key::kOpt, kL}));
  run(x, kLockFlashMs);  // docs/20 D7: the card said "Locked"; dark again
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Off);
  press(x.r, keys({key::kG0}));  // a tap lights the card
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::On);
  const size_t parts = x.r.disp.partials.size();
  run(x, 2500);
  TEST_ASSERT_TRUE(x.r.disp.partials.size() - parts >= 2);  // a z every 600 ms
  for (size_t i = parts; i < x.r.disp.partials.size(); ++i) TEST_ASSERT_TRUE(area(x.r.disp.partials[i]) <= 24 * 30);
  run(x, 2000);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Off);
  const size_t dark = x.r.disp.partials.size();
  run(x, 2000);
  TEST_ASSERT_EQUAL_UINT(dark, x.r.disp.partials.size());
}

static void test_the_sheet_slides_in() {
  RecRig x;
  x.r.step(1000);
  x.r.step(100);  // a while after the last full frame: the key draws at once
  const int frames = x.r.disp.frames;
  const size_t parts = x.r.disp.partials.size();
  x.r.step(10, keys({key::kTab}));
  x.r.step(10);
  TEST_ASSERT_TRUE(x.r.rt.shell().menu_open());
  TEST_ASSERT_EQUAL(frames + 1, x.r.disp.frames);  // frame 0: the dimmed page and the sheet
  run(x, 150);
  TEST_ASSERT_EQUAL_UINT(parts + 2, x.r.disp.partials.size());  // frames 1 and 2: the sheet only
  const Rect a = x.r.disp.partials[parts];
  const Rect b = x.r.disp.partials[parts + 1];
  TEST_ASSERT_TRUE(b.x < a.x);  // it moves left
  TEST_ASSERT_TRUE(a.y == 13 && a.h == 109 && a.x + a.w == kScreenW && b.x + b.w == kScreenW);
  run(x, 500);
  TEST_ASSERT_EQUAL_UINT(parts + 2, x.r.disp.partials.size());  // then it rests
  TEST_ASSERT_EQUAL(frames + 1, x.r.disp.frames);
}

static void test_at_dim_the_sheet_does_not_slide() {
  RecRig x;
  start_meeting(x);
  dim(x);
  const size_t parts = x.r.disp.partials.size();
  x.r.step(10, keys({key::kTab}));
  x.r.step(10);
  TEST_ASSERT_TRUE(x.r.rt.shell().menu_open());
  run(x, 500);
  TEST_ASSERT_EQUAL_UINT(parts, x.r.disp.partials.size());
}

// The device's timing check (docs/19 §10): under the test hooks the runtime keeps its longest
// full redraw and its longest animation tick by the platform's clock; test_runtime_budget
// prints them on the CPA_BUDGET line and holds them under 50 ms.
static void test_render_times_are_kept() {
  RecRig x;
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.render_times().max_render_us);
  x.r.plat.step_us = 7000;
  x.hold_g0(600);
  run(x, 1000, keys({key::kG0}));
  TEST_ASSERT_EQUAL_UINT32(7000, x.r.rt.render_times().max_render_us);
  TEST_ASSERT_EQUAL_UINT32(7000, x.r.rt.render_times().max_tick_us);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_memo_pushes_small_rects_each_tick);
  RUN_TEST(test_a_level_alone_does_not_redraw);
  RUN_TEST(test_ticks_never_mark_the_screen_dirty);
  RUN_TEST(test_at_dim_nothing_moves);
  RUN_TEST(test_dark_pushes_nothing);
  RUN_TEST(test_the_lock_card_dozes_then_goes_dark);
  RUN_TEST(test_the_sheet_slides_in);
  RUN_TEST(test_at_dim_the_sheet_does_not_slide);
  RUN_TEST(test_render_times_are_kept);
  return UNITY_END();
}
