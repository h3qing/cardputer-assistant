// The lock after docs/20 (A2, D2, D7, D8):
//   U3a  only a G0 tap arms the lit card: a swallowed hold lights it, but a second hold
//        inside those 3 s is swallowed too (two pocket presses never record);
//   U3b  whether the card was lit counts at the press, not 500 ms later at G0Long;
//   U3c  the card stays lit while G0 is held (a swallowed hold, or one that went down on
//        the lit card), so it never goes dark under a held G0;
//   D2   on battery the screen going dark locks (no card); with a USB host never;
//   D7   the card lights 2 s when locking, 2 s when a recording ends while locked (its
//        result), 3 s when the count of sessions needing you rises (counts only); not for
//        pocket keys (Enter, Space);
//   D8   while G0 is down and a memo would start, AppState says so ("● Wait") at once;
//        not where the hold would be refused (any refusal of rec_refusal).
// And after the review: a held G0 lights the card kHoldLitMs past its time at most (a
// pocket); the card that answered a swallowed hold keeps saying so until it goes dark or a
// key lights it; an unlocked meeting that stops by itself on battery lights its end.
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "lock.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

namespace {

constexpr uint8_t kL = 38;  // 'l'
constexpr uint8_t kR = 18;  // 'r'

KeySet keys(std::initializer_list<uint8_t> ks) {
  KeySet s;
  for (uint8_t k : ks) s = s.with(k);
  return s;
}

const KeySet kOptL = keys({key::kOpt, kL});
const KeySet kOptR = keys({key::kOpt, kR});
const KeySet kEnterKey = keys({key::kEnter});
const KeySet kG0 = keys({key::kG0});

void press_at(RuntimeRig& r, KeySet ks, uint32_t at) {
  r.step(at - r.now, ks);
  r.step(10);
}

Backlight level(const RuntimeRig& r) { return r.plat.levels.empty() ? Backlight::On : r.plat.levels.back(); }

const char* kWaiting =
    "\"rev\":7,\"total\":1,\"sessions\":[{\"sid\":\"8d3e7a19\",\"slot\":1,\"name\":\"架构设计\",\"kind\":\"tty\","
    "\"st\":\"waiting\",\"need\":true,\"age_s\":5}]";

}  // namespace

// -- the pure gate (U3) ------------------------------------------------------------------

static void test_a_swallowed_hold_never_arms() {
  const LockStep held = lock_key(lock_engage(), g0long(), 100, false);
  const LockStep up = lock_key(held.state, g0longup(), 900, false);
  TEST_ASSERT_TRUE(lock_lit(up.state, 1500));
  const LockStep again = lock_key(up.state, g0long(), 1500, false);  // the card is lit: still no
  TEST_ASSERT_TRUE(again.out == LockOut::Swallow && again.state.eat_up);
  const LockStep up2 = lock_key(again.state, g0longup(), 2000, false);
  TEST_ASSERT_TRUE(up2.out == LockOut::Swallow);
  const LockStep tap = lock_key(up2.state, g0tap(), 2100, false);  // the deliberate tap
  TEST_ASSERT_TRUE(tap.out == LockOut::Swallow);
  TEST_ASSERT_TRUE(lock_key(tap.state, g0long(), 2700, false).out == LockOut::Pass);
}

static void test_only_a_g0_tap_arms_the_card() {
  const LockState by_opt_l = lock_key(lock_engage(), opt('l'), 1000, false).state;
  TEST_ASSERT_TRUE(lock_lit(by_opt_l, 1600));
  TEST_ASSERT_TRUE(lock_key(by_opt_l, g0long(), 1600, false).out == LockOut::Swallow);
  const LockState flashed = lock_flash(lock_engage(), 1000, kLockFlashMs);
  TEST_ASSERT_TRUE(lock_lit(flashed, 1600) && !flashed.window);
  TEST_ASSERT_TRUE(lock_key(flashed, g0long(), 1600, false).out == LockOut::Swallow);
  TEST_ASSERT_TRUE(lock_key(flashed, enter(), 1600, false).out == LockOut::Swallow);  // no unlock window
}

static void test_the_card_counts_at_the_press() {
  const LockState armed = lock_key(lock_engage(), g0tap(), 1000, false).state;  // lit until 4000
  TEST_ASSERT_TRUE(lock_key(armed, g0long(), 4300, false).out == LockOut::Pass);     // down at 3800
  TEST_ASSERT_TRUE(lock_key(armed, g0long(), 4500, false).out == LockOut::Swallow);  // down at 4000
}

static void test_the_card_stays_lit_under_a_held_g0() {
  const LockState held = lock_key(lock_engage(), g0long(), 100, false).state;  // lit until 3100
  TEST_ASSERT_TRUE(lock_tick(held, 5000).card);
  const LockState up = lock_key(lock_tick(held, 5000), g0longup(), 5000, false).state;
  TEST_ASSERT_FALSE(lock_tick(up, 5010).card);
  const LockState armed = lock_key(lock_engage(), g0tap(), 1000, false).state;  // lit until 4000
  const LockState down = lock_g0(armed, true, 3800);
  TEST_ASSERT_TRUE(lock_tick(down, 4200).card);
  TEST_ASSERT_FALSE(lock_tick(lock_g0(down, false, 4250), 4260).card);
  const LockState dark_down = lock_g0(lock_engage(), true, 100);  // dark at the press
  TEST_ASSERT_FALSE(lock_tick(dark_down, 200).card);
}

// The review: a pocket can keep G0 pressed for minutes. The card stays lit under a held G0
// for kHoldLitMs past its time, no longer (the hold is still swallowed, its release too).
static void test_a_held_g0_lights_the_card_a_while_only() {
  const LockState held = lock_key(lock_engage(), g0long(), 100, false).state;  // lit until 3100
  TEST_ASSERT_TRUE(lock_tick(held, 3100 + kHoldLitMs - 1).card);
  const LockState out = lock_tick(held, 3100 + kHoldLitMs);
  TEST_ASSERT_FALSE(out.card);
  TEST_ASSERT_TRUE(out.eat_up);
  TEST_ASSERT_TRUE(lock_key(out, g0longup(), 60000, false).out == LockOut::Swallow);
  const LockState armed = lock_key(lock_engage(), opt('l'), 1000, false).state;  // lit until 4000
  const LockState down = lock_g0(armed, true, 3800);
  TEST_ASSERT_TRUE(lock_tick(down, 4000 + kHoldLitMs - 1).card);
  TEST_ASSERT_FALSE(lock_tick(down, 4000 + kHoldLitMs).card);
  TEST_ASSERT_EQUAL_UINT32(10000, kHoldLitMs);
}

// The review (stale result): the card that answered a swallowed hold keeps saying so after
// the release, until it goes dark or another key lights it (a tap, Opt+L).
static void test_a_swallowed_hold_is_answered_until_the_card_goes() {
  const LockState held = lock_key(lock_engage(), g0long(), 100, false).state;  // lit until 3100
  TEST_ASSERT_TRUE(held.swallowed);
  const LockState up = lock_key(held, g0longup(), 2100, false).state;
  TEST_ASSERT_TRUE(up.swallowed && !up.eat_up && lock_lit(up, 2500));
  TEST_ASSERT_FALSE(lock_tick(up, 3100).swallowed);
  TEST_ASSERT_FALSE(lock_key(up, g0tap(), 2500, false).state.swallowed);
  TEST_ASSERT_FALSE(lock_key(up, opt('l'), 2500, false).state.swallowed);
  TEST_ASSERT_FALSE(lock_dark(up).swallowed);
  TEST_ASSERT_TRUE(lock_flash(up, 2500, kNeedsFlashMs).swallowed);  // no key: still the answer
}

// D7 with U6: a card lit by itself (a recording's end, a session needing you) may light an
// empty pocket: it is not a look, so a result stays until a key lights the card. Locking
// lights it in front of the user: that is one.
static void test_a_flash_is_not_a_look() {
  const LockState flashed = lock_flash(lock_engage(), 1000, kLockFlashMs);
  TEST_ASSERT_TRUE(lock_lit(flashed, 1500) && flashed.flash);
  const LockState tapped = lock_key(flashed, g0tap(), 1500, false).state;
  TEST_ASSERT_TRUE(lock_lit(tapped, 1600) && !tapped.flash);
  TEST_ASSERT_FALSE(lock_flash(tapped, 1600, kNeedsFlashMs).flash);  // already looked at
  const LockState locked = lock_engage(1000, kLockFlashMs);
  TEST_ASSERT_TRUE(lock_lit(locked, 2999) && !lock_lit(locked, 3000) && !locked.flash && !locked.window);
}

// -- through the runtime ------------------------------------------------------------------

// D2: on battery the screen going dark locks, with no card; plugged in it never does.
static void test_battery_dark_locks_usb_never() {
  RuntimeRig bat;
  bat.unplug();
  press_at(bat, keys({key::kOpt, 31}), bat.now + 10);  // Opt+S: a draft
  bat.step(kBacklightIdleMs - 1000);
  TEST_ASSERT_FALSE(bat.rt.shell().locked());
  TEST_ASSERT_TRUE(level(bat) == Backlight::Dim);
  bat.step(1100);
  TEST_ASSERT_TRUE(bat.rt.shell().locked());
  TEST_ASSERT_TRUE(level(bat) == Backlight::Off);
  TEST_ASSERT_TRUE(bat.rt.shell().overlay() == proto::Overlay::Compose);  // the draft stays
  const uint16_t pending = bat.rt.state().outbox.pending;
  for (uint8_t k : {uint8_t{35}, uint8_t{36}, key::kEnter, key::kSpace}) press_at(bat, keys({k}), bat.now + 300);
  TEST_ASSERT_TRUE(level(bat) == Backlight::Off);  // D7: pocket keys light nothing
  TEST_ASSERT_EQUAL_UINT16(pending, bat.rt.state().outbox.pending);
  RuntimeRig usb;
  usb.step(10 * 60000);
  TEST_ASSERT_FALSE(usb.rt.shell().locked());
  TEST_ASSERT_TRUE(level(usb) == Backlight::On);
}

// D2 + U3: once it locked by itself, a pocket hold records nothing; tap, then hold does.
static void test_auto_locked_memo_needs_the_tap() {
  RecRig x;
  x.r.unplug();
  x.r.step(kBacklightIdleMs + 100);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  x.hold_g0(1500);
  x.release();
  x.r.step(800);
  x.hold_g0(600);  // a second pocket hold inside the card's 3 s
  x.release();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
  press_at(x.r, kG0, x.r.now + 500);  // tap
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
}

// D7 / U11: locking lights the card 2 s ("Locked"), without an unlock window.
static void test_locking_lights_the_card_for_2_s() {
  RuntimeRig r;
  press_at(r, kOptL, 100);
  TEST_ASSERT_TRUE(r.rt.shell().locked());
  TEST_ASSERT_TRUE(level(r) == Backlight::On);
  r.step(10);
  TEST_ASSERT_TRUE_MESSAGE(r.disp.canvas_.has("已锁屏"), r.disp.canvas_.dump().c_str());
  press_at(r, kEnterKey, 600);
  TEST_ASSERT_TRUE(r.rt.shell().locked());
  r.step(kLockFlashMs);
  TEST_ASSERT_TRUE(level(r) == Backlight::Off);
}

// D7 / U6: a recording that ends while locked lights the card 2 s with its result, the
// memo from the card and a meeting stopped by the battery alike.
static void test_a_recording_end_while_locked_lights_the_card() {
  RecRig x;
  press_at(x.r, kOptL, x.r.now + 100);
  x.r.step(kLockFlashMs + 100);
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Off);
  press_at(x.r, kG0, x.r.now + 10);
  x.hold_g0(600);
  x.port.feed(2000);
  x.let_go();  // gh #51: the grace runs out
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_TRUE(level(x.r) == Backlight::On);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.last.has && x.r.rt.state().rec.last.saved);
  x.r.step(kLockFlashMs + 100);
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Off);

  RecRig m;
  press_at(m.r, kOptR, m.r.now + 10);
  press_at(m.r, kEnterKey, m.r.now + 500);
  m.steps();
  TEST_ASSERT_TRUE(m.r.rt.state().rec.phase == RecPhase::Recording);
  m.port.feed(2000);
  press_at(m.r, kOptL, m.r.now + 10);
  m.r.step(kLockFlashMs + 100);
  TEST_ASSERT_TRUE(level(m.r) == Backlight::Dim);  // ● stays visible
  m.r.plat.next.bat_mv = 3400;
  for (int i = 0; i < 13 && m.r.rt.state().rec.phase != RecPhase::Idle; ++i) {
    m.port.feed(1000);
    m.r.step(1000);
  }
  m.steps();
  TEST_ASSERT_TRUE(m.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE(m.r.rt.shell().locked());
  TEST_ASSERT_TRUE(level(m.r) == Backlight::On);
}

// The review: on battery a G0 held in a pocket for a minute lights the card at most
// kHoldLitMs past its 3 s; the backlight is off for the rest of the hold. Nothing records,
// and the card says "Not recording" while it is lit.
static void test_a_pocket_hold_does_not_keep_the_backlight_on() {
  RecRig x;
  x.r.unplug();
  press_at(x.r, kOptL, x.r.now + 100);
  x.r.step(kLockFlashMs + 100);
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Off);
  x.hold_g0(2000);
  TEST_ASSERT_TRUE(level(x.r) == Backlight::On);
  TEST_ASSERT_TRUE(x.r.rt.state().ui.lock_held);
  x.hold_g0(58000);
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Off);
  TEST_ASSERT_TRUE(x.r.rt.state().ui.lock_held);  // still swallowed
  x.release();
  x.steps();
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Off);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
}

// The review: on battery an unlocked meeting dims after a minute without a key. When it
// stops by itself (the battery) the result page and its toast light up as after a key
// (D7: a recording's end is shown), and only then does the screen dim, go dark and lock.
static void test_a_meeting_stopped_unlocked_on_battery_shows_its_end() {
  RecRig m;
  m.r.unplug();
  press_at(m.r, kOptR, m.r.now + 10);
  press_at(m.r, kEnterKey, m.r.now + 500);
  m.steps();
  TEST_ASSERT_TRUE(m.r.rt.state().rec.phase == RecPhase::Recording);
  for (int i = 0; i < 70; ++i) {
    m.port.feed(1000);
    m.r.step(1000);
  }
  TEST_ASSERT_TRUE(level(m.r) == Backlight::Dim);
  m.r.plat.next.bat_mv = 3400;
  for (int i = 0; i < 13 && m.r.rt.state().rec.phase != RecPhase::Idle; ++i) {
    m.port.feed(1000);
    m.r.step(1000);
  }
  m.steps();
  TEST_ASSERT_TRUE(m.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_FALSE(m.r.rt.shell().locked());
  TEST_ASSERT_TRUE(level(m.r) == Backlight::On);
  TEST_ASSERT_TRUE(m.r.rt.state().rec.last.has && m.r.rt.state().rec.last.seen);
  m.r.step(kBacklightIdleMs - kPreDimMs - 500);
  TEST_ASSERT_TRUE(level(m.r) == Backlight::On);
  m.r.step(kPreDimMs + 1000);
  TEST_ASSERT_TRUE(level(m.r) == Backlight::Off);
  TEST_ASSERT_TRUE(m.r.rt.shell().locked());  // D2, once it was seen
}

// D7 / U11a: locked at the desk, a session starting to need you lights the card 3 s.
static void test_needs_you_rising_lights_the_card() {
  RuntimeRig r;
  r.handshake();
  press_at(r, kOptL, r.now + 100);
  r.step(kLockFlashMs + 100);
  TEST_ASSERT_TRUE(level(r) == Backlight::Off);
  r.send("sessions.snapshot", kWaiting);
  r.step();
  TEST_ASSERT_TRUE(level(r) == Backlight::On);
  r.step(60);  // the next frame (at most 20 a second)
  TEST_ASSERT_TRUE_MESSAGE(r.disp.canvas_.has("等你 1"), r.disp.canvas_.dump().c_str());
  TEST_ASSERT_FALSE(r.disp.canvas_.has("架构设计"));
  r.step(kNeedsFlashMs);
  TEST_ASSERT_TRUE(level(r) == Backlight::Off);
  r.send("sessions.snapshot", kWaiting);  // the same count: nothing
  r.step();
  TEST_ASSERT_TRUE(level(r) == Backlight::Off);
}

// D8: "● Wait" the moment G0 goes down, where a hold would record a memo; not for a tap
// once it is up, not on a dark lock (it would be swallowed), not without the card (refused).
static void test_g0_down_says_ready() {
  RecRig x;
  x.hold_g0(100);
  TEST_ASSERT_TRUE(x.r.rt.state().ui.memo_ready);
  x.release();
  TEST_ASSERT_FALSE(x.r.rt.state().ui.memo_ready);
  x.hold_g0(600);  // the memo: the cue says "Wait" from here
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase != RecPhase::Idle);
  TEST_ASSERT_FALSE(x.r.rt.state().ui.memo_ready);
  x.port.feed(2000);
  x.let_go();  // gh #51: the grace runs out
  x.steps();
  press_at(x.r, kOptL, x.r.now + 10);
  x.r.step(kLockFlashMs + 100);
  x.hold_g0(100);
  TEST_ASSERT_FALSE(x.r.rt.state().ui.memo_ready);  // dark: this hold only lights the card
  x.hold_g0(600);
  x.release();
  x.steps();
  press_at(x.r, kG0, x.r.now + 3100);  // after the card went out: a tap arms it again
  x.hold_g0(100);
  TEST_ASSERT_TRUE(x.r.rt.state().ui.memo_ready);
  RecRig no_card(false);  // the hold would only be refused ("Needs an SD card"): no promise
  no_card.hold_g0(100);
  TEST_ASSERT_FALSE(no_card.r.rt.state().ui.memo_ready);
  RecRig low;  // the review: every refusal, as rec_refusal has them (here 3.45 V and below)
  low.r.plat.next.bat_mv = 3400;
  low.r.step(1100);  // sampled once a second
  low.hold_g0(100);
  TEST_ASSERT_FALSE(low.r.rt.state().ui.memo_ready);
  low.hold_g0(600);
  TEST_ASSERT_EQUAL_UINT(0, low.port.count(CmdKind::Start));  // refused, as "● Wait" did not promise
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_swallowed_hold_never_arms);
  RUN_TEST(test_only_a_g0_tap_arms_the_card);
  RUN_TEST(test_the_card_counts_at_the_press);
  RUN_TEST(test_the_card_stays_lit_under_a_held_g0);
  RUN_TEST(test_a_held_g0_lights_the_card_a_while_only);
  RUN_TEST(test_a_swallowed_hold_is_answered_until_the_card_goes);
  RUN_TEST(test_a_flash_is_not_a_look);
  RUN_TEST(test_battery_dark_locks_usb_never);
  RUN_TEST(test_auto_locked_memo_needs_the_tap);
  RUN_TEST(test_locking_lights_the_card_for_2_s);
  RUN_TEST(test_a_recording_end_while_locked_lights_the_card);
  RUN_TEST(test_needs_you_rising_lights_the_card);
  RUN_TEST(test_g0_down_says_ready);
  RUN_TEST(test_a_pocket_hold_does_not_keep_the_backlight_on);
  RUN_TEST(test_a_meeting_stopped_unlocked_on_battery_shows_its_end);
  return UNITY_END();
}
