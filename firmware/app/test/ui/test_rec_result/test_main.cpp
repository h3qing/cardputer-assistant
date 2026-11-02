// How a recording ends, on the runtime (docs/20 U6, U15, S1): the memo cap screen waits
// for G0 to go up; an automatic stop while locked keeps its reason (RecLast) and its
// toast until the screen is seen lit; a lost end event no longer leaves the device
// stuck in "Saving…"; the 3.55 V warning line.
#include <unity.h>

#include "core/support/lang_pin.h"
#include "home_logic.h"
#include "rec_result.h"
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() { set_lang(Lang::Zh); }  // a failed English test skips its LangScope

static const KeySet kG0 = KeySet{}.with(key::kG0);

static void press(RecRig& x, const KeyEvent& ev) {
  x.r.now += 10;
  if (x.r.rt.shell().lock_gate(ev, x.r.now)) x.r.rt.shell().on_key(ev, x.r.now);
}

static void meeting(RecRig& x) {
  x.r.step(10, KeySet{}.with(key::kOpt).with(layout_index(4, 1)));  // Opt+R
  x.r.step(500);
  x.r.step(10, KeySet{}.with(key::kEnter));
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
}

// U15: at 5:00 the memo ends while G0 is still held; the screen says so until the release.
static void test_memo_cap_screen_waits_for_the_release() {
  RecRig x;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.feed(301000);  // past the cap: the audio task stops by itself
  for (int i = 0; i < 1000 && x.r.rt.state().rec.phase != RecPhase::Idle; ++i) x.r.step(10, kG0);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.held);
  x.r.step(100, kG0);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("松开 G0"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_TRUE(x.r.disp.canvas_.has("已到 5 分钟"));
  // what to do instead, in order: Opt+R does nothing while G0 is held (review)
  TEST_ASSERT_TRUE(x.r.disp.canvas_.has("要录更久：松开 G0，再按 Opt+R 开会"));
  x.release();
  x.steps();
  TEST_ASSERT_FALSE(x.r.rt.state().rec.held);
  x.r.step(100);
  TEST_ASSERT_FALSE(x.r.disp.canvas_.has("松开 G0"));  // back to the page
}

// Mando's hop for a saved memo (docs/19 §7.2) is seen on home: after the held cap screen,
// not used up behind it while G0 is still down.
static void test_mando_hops_after_the_held_screen() {
  RecRig x;
  x.hold_g0(600);
  x.port.feed(301000);
  for (int i = 0; i < 1000 && x.r.rt.state().rec.phase != RecPhase::Idle; ++i) x.r.step(10, kG0);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.held);
  for (int i = 0; i < 20; ++i) x.r.step(100, kG0);  // two seconds on the held screen
  TEST_ASSERT_TRUE(x.r.rt.state().rec.held);
  x.release();
  TEST_ASSERT_FALSE(x.r.rt.state().rec.held);
  TEST_ASSERT_TRUE(home_mando(x.r.rt.state(), x.r.now) == theme::MandoAct::Hop);
  x.r.step(kHopMs);
  TEST_ASSERT_TRUE(home_mando(x.r.rt.state(), x.r.now) != theme::MandoAct::Hop);  // once
}

// Review (U15 on battery): the G0 press was 5 minutes ago, so the first tick after the cap
// turned the backlight off and the held screen was never seen ("Recording stopped", no
// tip). Held, it stays lit as the recording was (dimmed), with the cap's text and tip.
static void test_memo_cap_screen_on_battery() {
  RecRig x;
  x.r.unplug();
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  for (int s = 0; s < 320 && x.r.rt.state().rec.phase != RecPhase::Idle; ++s) {
    x.port.feed(1000);
    x.r.step(1000, kG0);
  }
  TEST_ASSERT_TRUE(x.r.rt.state().rec.held);
  for (int i = 0; i < 10; ++i) x.r.step(1000, kG0);  // ten seconds later, still held
  TEST_ASSERT_FALSE(x.r.plat.levels.empty());
  TEST_ASSERT_TRUE(x.r.plat.levels.back() != Backlight::Off);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("已到 5 分钟，已保存"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_TRUE(x.r.disp.canvas_.has("松开 G0，再按 Opt+R"));
  x.release();
  x.steps();
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::On);  // the release is a key: lit
  TEST_ASSERT_TRUE(x.r.rt.state().rec.last.seen);
}

// Review (U6), then the review of D7: a recording on battery dims the screen. A stop there
// lights the screen as a key would (the page and its toast), so the reason is seen before
// the screen goes dark and locks (D2). Before, the screen went from dimmed to dark and
// locked with the reason unseen, until some later G0 tap.
static void test_a_stop_on_the_dimmed_screen_lights_it() {
  RecRig x;
  x.r.unplug();
  meeting(x);
  x.port.feed(2000);
  for (int i = 0; i < 61; ++i) x.r.step(1000);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Dim);
  x.r.plat.next.bat_mv = 3440;
  for (int i = 0; i < 20 && x.r.rt.state().rec.phase != RecPhase::Idle; ++i) x.r.step(1000);  // the stop, the end
  x.r.step(60);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::On);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("会议已停：电量低"), x.r.disp.canvas_.dump().c_str());
  x.r.step(10000);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::On);  // as after a key: 50 s, then dimmed
  TEST_ASSERT_FALSE(x.r.rt.shell().locked());
  const RecLast& l = x.r.rt.state().rec.last;
  TEST_ASSERT_TRUE(l.has && l.seen && l.why == static_cast<uint8_t>(rec::RecRefusal::Battery));
}

// U6: the battery stops a meeting while locked; nothing is lost while the screen is dark,
// the reason is kept until the screen is seen lit, and gone once it is dark again.
static void test_locked_auto_stop_keeps_the_reason() {
  RecRig x;
  meeting(x);
  press(x, opt('l'));
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  x.port.feed(2000);
  x.r.plat.next.bat_mv = 3440;
  for (int i = 0; i < 14; ++i) x.r.step(1000);  // 10 s at 3.44 V: the stop, then the end
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  const RecLast& l = x.r.rt.state().rec.last;
  TEST_ASSERT_TRUE(l.has && l.saved && l.why == static_cast<uint8_t>(rec::RecRefusal::Battery));
  TEST_ASSERT_TRUE(l.mode == RecMode::Meeting && !l.seen);
  for (int i = 0; i < 30; ++i) x.r.step(1000);  // locked and dark: kept
  TEST_ASSERT_TRUE(x.r.rt.state().rec.last.has);
  press(x, opt('l'));
  press(x, enter());  // unlocked: lit, and the stop's toast is still there
  x.r.step(100);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.last.seen);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("会议已停：电量低"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_FALSE(x.r.disp.canvas_.has("不能录音"));  // a stop is not a refusal
  press(x, opt('l'));  // dark again (docs/20 D7: after "Locked" for 2 s): seen once, now gone
  x.r.step(kLockFlashMs + 100);
  TEST_ASSERT_FALSE(x.r.rt.state().rec.last.has);
}

// S1: the audio task's end event is lost; within 5-10 s the device is usable again.
static void test_lost_end_event_returns_to_idle() {
  RecRig x;
  x.hold_g0(600);
  x.port.feed(2000);
  x.port.drop_ends = 1;
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Finishing);
  const size_t stops = x.port.count(rec::CmdKind::Stop);
  for (int i = 0; i < 6; ++i) x.r.step(1000);
  TEST_ASSERT_EQUAL_UINT(stops + 1, x.port.count(rec::CmdKind::Stop));  // the Stop again at 5 s
  for (int i = 0; i < 5; ++i) x.r.step(1000);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("录音收尾超时"), x.r.disp.canvas_.dump().c_str());
  x.memo(2000);  // and it records again
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
}

// docs/11 §4.3: at or below 3.55 V while recording, a warning on the screen, once.
static void test_battery_warning_line() {
  RecRig x;
  meeting(x);
  x.r.plat.next.bat_mv = 3540;
  x.r.step(1100);
  x.r.step(10);
  x.r.step(100);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.bat_low);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("电量低"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);  // a warning, not a stop
}

// The lock card's slot, in English: each line starts with a capital, as "Not recording"
// beside it does (the review of docs/21's English renders).
static void test_lock_card_lines_start_with_a_capital_in_english() {
  LangScope en(Lang::En);
  RecLast l;
  l.has = true;
  l.saved = true;
  l.ms = 15000;
  TEST_ASSERT_EQUAL_STRING("\xE2\x88\x9A Saved 0:15", rec_card_result(l).line.c_str());
  l.saved = false;
  l.ms = 300;
  TEST_ASSERT_EQUAL_STRING("\xC3\x97 Too short", rec_card_result(l).line.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_memo_cap_screen_waits_for_the_release);
  RUN_TEST(test_mando_hops_after_the_held_screen);
  RUN_TEST(test_locked_auto_stop_keeps_the_reason);
  RUN_TEST(test_lost_end_event_returns_to_idle);
  RUN_TEST(test_battery_warning_line);
  RUN_TEST(test_memo_cap_screen_on_battery);
  RUN_TEST(test_a_stop_on_the_dimmed_screen_lights_it);
  RUN_TEST(test_lock_card_lines_start_with_a_capital_in_english);
  return UNITY_END();
}
