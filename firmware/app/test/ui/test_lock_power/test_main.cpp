// Opt+L lock and power (docs/13 §5.4 step 1, §7.1): the gate sits in the runtime before
// power_key, so a locked device stays dark, plugged in or not, and pocket presses light
// nothing. Opt+L lights the card once and opens a 3 s window for Enter; a G0 hold on the
// dark screen only lights the card (it and its release are swallowed); a G0 hold on the
// card a tap lit records a memo and the device is dark and locked again after it (docs/20
// D7: after the card showed the result 2 s); during a meeting G0 still stars. docs/20 D2:
// a screen gone dark on battery is locked already; locked over a draft the tapped card's
// hold still records; a locked memo says "● 准备" until the microphone is up. The lock
// card shows counts, never a project or session name. docs/20 D7: locking lights the card
// 2 s (test_lock_ux), so the tests below wait kLockFlashMs where they want it dark.
#include <unity.h>

#include <string>

#include "lock.h"
#include "lock_screen.h"
#include "power.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint8_t kL = 38;  // layout index of 'l' (row 2, x 10)
constexpr uint8_t kR = 18;  // 'r' (row 1, x 4)

KeySet keys(std::initializer_list<uint8_t> ks) {
  KeySet s;
  for (uint8_t k : ks) s = s.with(k);
  return s;
}

const KeySet kOptL = keys({key::kOpt, kL});
const KeySet kEnterKey = keys({key::kEnter});

// The matrix key that types c.
uint8_t key_of(char c) {
  for (uint8_t k = 0; k < kMatrixKeys; ++k) {
    if (key_base(k) == c && !is_modifier(k)) return k;
  }
  TEST_FAIL_MESSAGE("no key types this character");
  return 0;
}

// Presses ks at exactly `at` and releases it 10 ms later.
void press_at(RuntimeRig& r, KeySet ks, uint32_t at) {
  r.step(at - r.now, ks);
  r.step(10);
}

Backlight level(const RuntimeRig& r) { return r.plat.levels.empty() ? Backlight::On : r.plat.levels.back(); }

// Pseudo-random keys that are not unlock keys: no Opt+L, no Enter, no G0.
KeySet random_key(uint32_t& seed) {
  static const uint8_t kMods[] = {key::kFn, key::kShift, key::kCtrl, key::kOpt, key::kAlt};
  for (;;) {
    seed = seed * 1103515245u + 12345u;
    const uint8_t k = static_cast<uint8_t>((seed >> 8) % kMatrixKeys);
    if (is_modifier(k) || k == kL || k == key::kEnter) continue;
    const uint32_t m = (seed >> 20) % 7;
    return m < 5 ? keys({kMods[m], k}) : keys({k});
  }
}

}  // namespace

// -- the pure gate ---------------------------------------------------------------

static void test_not_locked_passes_everything() {
  const LockState s;
  for (const KeyEvent& k : {chr('a'), enter(), opt('l'), g0tap(), g0long(), nav(Nav::Menu)}) {
    TEST_ASSERT_TRUE(lock_key(s, k, 100, false).out == LockOut::Pass);
  }
  TEST_ASSERT_FALSE(lock_lit(s, 100));
}

static void test_opt_l_opens_a_three_second_window() {
  const LockState dark = lock_engage();
  TEST_ASSERT_TRUE(dark.locked && !lock_lit(dark, 0));
  const LockStep lit = lock_key(dark, opt('l'), 1000, false);
  TEST_ASSERT_TRUE(lit.out == LockOut::Swallow);
  TEST_ASSERT_TRUE(lock_lit(lit.state, 1000) && lock_lit(lit.state, 3999) && !lock_lit(lit.state, 4000));
  const LockStep in = lock_key(lit.state, enter(), 3999, false);
  TEST_ASSERT_TRUE(in.out == LockOut::Unlock);
  TEST_ASSERT_FALSE(in.state.locked);
  for (uint32_t late : {4000u, 4001u}) {
    const LockStep out = lock_key(lit.state, enter(), late, false);
    TEST_ASSERT_TRUE(out.out == LockOut::Swallow && out.state.locked);
  }
  TEST_ASSERT_TRUE(lock_key(dark, enter(), 1000, false).out == LockOut::Swallow);  // no window
}

static void test_pocket_keys_change_nothing() {
  const LockState dark = lock_engage();
  for (const KeyEvent& k : {chr('a'), chr(' '), chr('`'), opt('h'), opt('r'), opt('m'), esc(), bksp(), nav(Nav::Menu),
                            opt('1')}) {
    const LockStep st = lock_key(dark, k, 500, false);
    TEST_ASSERT_TRUE(st.out == LockOut::Swallow);
    TEST_ASSERT_TRUE(st.state.locked && !st.state.card && !st.state.window && !st.state.eat_up);
  }
}

static void test_g0_hold_on_the_dark_screen_only_lights_the_card() {
  const LockStep held = lock_key(lock_engage(), g0long(), 100, false);
  TEST_ASSERT_TRUE(held.out == LockOut::Swallow);
  TEST_ASSERT_TRUE(lock_lit(held.state, 100) && held.state.eat_up && !held.state.window);
  const LockStep up = lock_key(held.state, g0longup(), 900, false);
  TEST_ASSERT_TRUE(up.out == LockOut::Swallow);
  TEST_ASSERT_FALSE(up.state.eat_up);
  // docs/20 U3a: the next hold on that card is swallowed too (two pocket holds never
  // record, test_lock_ux); a tap arms it, then a hold passes, and so does its release
  const LockStep again = lock_key(up.state, g0long(), 1500, false);
  TEST_ASSERT_TRUE(again.out == LockOut::Swallow);
  const LockState tapped = lock_key(lock_key(again.state, g0longup(), 1600, false).state, g0tap(), 1700, false).state;
  const LockStep memo = lock_key(tapped, g0long(), 2300, false);
  TEST_ASSERT_TRUE(memo.out == LockOut::Pass);
  TEST_ASSERT_TRUE(lock_key(memo.state, g0longup(), 4000, false).out == LockOut::Pass);
  TEST_ASSERT_FALSE(lock_lit(lock_dark(memo.state), 1500));
  TEST_ASSERT_TRUE(lock_dark(memo.state).locked);
}

static void test_g0_tap_lights_the_card_without_a_window() {
  const LockStep tap = lock_key(lock_engage(), g0tap(), 100, false);
  TEST_ASSERT_TRUE(tap.out == LockOut::Swallow);
  TEST_ASSERT_TRUE(lock_lit(tap.state, 3099) && !lock_lit(tap.state, 3100));
  TEST_ASSERT_TRUE(lock_key(tap.state, enter(), 200, false).out == LockOut::Swallow);
}

static void test_meeting_g0_always_passes() {
  const LockState dark = lock_engage();
  for (const KeyEvent& k : {g0tap(), g0long(), g0longup()}) {
    const LockStep st = lock_key(dark, k, 100, true);
    TEST_ASSERT_TRUE(st.out == LockOut::Pass);
    TEST_ASSERT_FALSE(lock_lit(st.state, 100));
  }
  TEST_ASSERT_TRUE(lock_key(dark, chr(' '), 100, true).out == LockOut::Swallow);  // Space is not G0
}

static void test_tick_closes_the_card_and_the_window() {
  const LockState lit = lock_key(lock_engage(), opt('l'), 0, false).state;
  const LockState still = lock_tick(lit, 2999);
  TEST_ASSERT_TRUE(still.card && still.window);
  const LockState done = lock_tick(lit, 3000);
  TEST_ASSERT_TRUE(done.locked && !done.card && !done.window);
}

// -- power ----------------------------------------------------------------------

static void test_power_lock_rules() {
  PowerState p = power_lock(PowerState{}, true, false, 0);
  p = power_tick(p, true, 100);  // plugged in: locked still wins
  TEST_ASSERT_TRUE(backlight_of(p) == Backlight::Off);
  TEST_ASSERT_TRUE(backlight_of(power_lock(p, true, true, 200)) == Backlight::On);
  const PowerState rec = power_recording(p, true);
  TEST_ASSERT_TRUE(backlight_of(rec) == Backlight::Dim);  // the ● rule: never dark while recording
  p = power_tick(p, false, 70000);  // idle on battery while locked
  const PowerState open = power_lock(p, false, false, 80000);
  TEST_ASSERT_TRUE(backlight_of(open) == Backlight::On && open.last_key_ms == 80000);
  PowerState stale;  // dark from before a recording started: still at least dimmed
  stale.dark = true;
  TEST_ASSERT_TRUE(backlight_of(power_recording(stale, true)) == Backlight::Dim);
}

// -- through the runtime ----------------------------------------------------------

static void test_locked_and_plugged_is_dark() {
  RuntimeRig r;
  TEST_ASSERT_TRUE(r.tx.is_plugged);
  press_at(r, kOptL, 100);
  TEST_ASSERT_TRUE(r.rt.shell().locked());
  r.step(kLockFlashMs);  // docs/20 D7: the card said "已锁屏"
  TEST_ASSERT_TRUE(level(r) == Backlight::Off);
  TEST_ASSERT_TRUE(r.rt.state().ui.locked);
  r.step(120000);
  TEST_ASSERT_TRUE(level(r) == Backlight::Off);
}

static void test_fifty_pocket_presses_change_nothing() {
  RuntimeRig r;
  press_at(r, kOptL, 100);
  r.step(kLockFlashMs);  // docs/20 D7: the card said "已锁屏"
  const size_t levels = r.plat.levels.size();
  const uint16_t pending = r.rt.state().outbox.pending;
  uint32_t seed = 7;
  for (int i = 0; i < 50; ++i) press_at(r, random_key(seed), r.now + 50);
  TEST_ASSERT_TRUE(r.rt.shell().locked());
  TEST_ASSERT_EQUAL_UINT32(levels, r.plat.levels.size());
  TEST_ASSERT_TRUE(level(r) == Backlight::Off);
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
  TEST_ASSERT_EQUAL_STRING("board", r.rt.shell().top_id());
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::None);
  TEST_ASSERT_FALSE(r.rt.shell().menu_open());
  TEST_ASSERT_FALSE(r.rt.shell().lock_lit(r.now));
  TEST_ASSERT_EQUAL_UINT16(pending, r.rt.state().outbox.pending);
}

static void test_opt_l_from_dark_lights_once_and_enter_unlocks_in_time() {
  for (uint32_t late : {2999u, 3001u}) {
    RuntimeRig r;
    r.board.slot = 2;
    press_at(r, keys({key::kOpt, 31}), 50);  // Opt+S: a draft under the lock
    press_at(r, kOptL, 100);
    TEST_ASSERT_TRUE(level(r) == Backlight::On);  // docs/20 D7: "已锁屏" for 2 s, no window
    press_at(r, kEnterKey, 600);
    TEST_ASSERT_TRUE(r.rt.shell().locked());
    r.step(kLockFlashMs);
    TEST_ASSERT_TRUE(level(r) == Backlight::Off);
    const uint32_t t0 = r.now + 1000;
    press_at(r, kOptL, t0);
    TEST_ASSERT_TRUE(level(r) == Backlight::On);  // one press lights the card
    press_at(r, kEnterKey, t0 + late);
    TEST_ASSERT_EQUAL(late < 3000, !r.rt.shell().locked());
    TEST_ASSERT_TRUE(level(r) == (late < 3000 ? Backlight::On : Backlight::Off));
    if (late < 3000) TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::Compose);  // the draft is back
    TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
  }
}

static void test_dark_g0_hold_records_nothing_and_lit_hold_records_a_memo() {
  RecRig x;
  press_at(x.r, kOptL, x.r.now + 100);
  x.r.step(kLockFlashMs);  // docs/20 D7: the card said "已锁屏"
  x.hold_g0(600);  // dark: only lights the card
  x.release();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.port.cmds.size());
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE(x.r.rt.shell().lock_lit(x.r.now));
  TEST_ASSERT_TRUE(level(x.r) == Backlight::On);
  press_at(x.r, keys({key::kG0}), x.r.now + 10);  // docs/20 U3a: a tap arms the card
  x.hold_g0(600);  // on the tapped card: a memo
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Dim);
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  x.r.step(kLockFlashMs);  // docs/20 D7: the card showed the result
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Off);
}

// gh #51: the lock-screen memo has the same grace. G0 slips and is pressed again inside
// it: the same memo goes on, and that press is the memo's (it lights no card); let go for
// good, the grace ends it, saved, dark and locked again (docs/20 D7: after the card showed
// the result).
static void test_locked_memo_has_the_grace() {
  RecRig x;
  press_at(x.r, kOptL, x.r.now + 100);
  press_at(x.r, keys({key::kG0}), x.r.now + 100);  // a tap lights the card
  x.hold_g0(600);
  x.port.feed(1500);
  x.release();
  for (int i = 0; i < 20; ++i) x.r.step(10);  // 0.2 s let go
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording && x.r.rt.state().rec.grace);
  x.hold_g0(300);  // pressed again
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording && !x.r.rt.state().rec.grace);
  x.port.feed(1000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(rec::CmdKind::Start));
  x.r.step(kLockFlashMs);  // docs/20 D7: the card showed the result
  TEST_ASSERT_TRUE(x.r.rt.shell().locked() && !x.r.rt.shell().lock_lit(x.r.now));
}

// On battery the screen goes dark after 60 s, and docs/20 D2 locks it then (v0.4 needed
// Opt+L, which locked from the dark screen at once). Opt+L on it is the lock's own key now:
// it lights the card and opens the unlock window.
static void test_opt_l_on_a_dark_screen_locks_at_once() {
  for (bool draft : {false, true}) {
    RuntimeRig r;
    r.unplug();
    if (draft) press_at(r, keys({key::kOpt, key_of('n')}), r.now + 10);
    r.step(61000);
    TEST_ASSERT_TRUE(level(r) == Backlight::Off);
    TEST_ASSERT_TRUE_MESSAGE(r.rt.shell().locked(), draft ? "over a draft" : "on a page");
    TEST_ASSERT_TRUE(r.rt.shell().overlay() == (draft ? proto::Overlay::Compose : proto::Overlay::None));
    press_at(r, keys({key_of('a')}), r.now + 10);  // a pocket press: nothing
    TEST_ASSERT_TRUE(level(r) == Backlight::Off);
    press_at(r, kOptL, r.now + 10);
    TEST_ASSERT_TRUE(r.rt.shell().locked());
    TEST_ASSERT_TRUE(level(r) == Backlight::On);
    press_at(r, kEnterKey, r.now + 100);
    TEST_ASSERT_FALSE(r.rt.shell().locked());
  }
}

// Locked over a draft (docs/13 §5.3), the lit card's hold still records a memo, plugged
// in or dark on battery; the draft is still there after unlocking.
static void test_lit_hold_over_a_draft_records_and_keeps_the_draft() {
  for (bool battery : {false, true}) {
    const char* what = battery ? "battery" : "plugged";
    RecRig x;
    if (battery) x.r.unplug();
    press_at(x.r, keys({key::kOpt, key_of('n')}), x.r.now + 10);
    press_at(x.r, keys({key_of('a')}), x.r.now + 10);
    press_at(x.r, kOptL, x.r.now + 10);
    if (battery) x.r.step(61000);
    press_at(x.r, keys({key::kG0}), x.r.now + 10);  // a tap lights the card
    x.hold_g0(600);
    TEST_ASSERT_TRUE_MESSAGE(x.r.rt.state().rec.phase == RecPhase::Recording, what);
    x.port.feed(2000);
    x.let_go();
    x.steps();
    TEST_ASSERT_TRUE_MESSAGE(x.port.heard.back() == rec::EvtKind::Saved, what);
    TEST_ASSERT_TRUE(x.r.rt.shell().locked());
    x.r.step(kLockFlashMs);  // docs/20 D7: the card showed the result
    TEST_ASSERT_TRUE(level(x.r) == Backlight::Off);
    press_at(x.r, kOptL, x.r.now + 10);
    press_at(x.r, kEnterKey, x.r.now + 100);
    TEST_ASSERT_FALSE(x.r.rt.shell().locked());
    TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::Compose);
    x.r.step(10);
    TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("a_"), x.r.disp.canvas_.dump().c_str());
  }
}

// A memo started from the lock card shows "● 准备" until the microphone is up, as the
// unlocked memo does, so nobody starts talking too early; then "● REC" and the clock.
static void test_locked_memo_says_wait_during_the_cue() {
  AppState s;
  s.rec.phase = RecPhase::Cue;
  s.rec.mode = RecMode::Memo;
  FakeCanvas c;
  render_lock_screen(c, s, false, 5000);
  TEST_ASSERT_TRUE_MESSAGE(c.has("● 准备"), c.dump().c_str());
  TEST_ASSERT_FALSE(c.has("REC"));
  s.rec.phase = RecPhase::Recording;
  s.rec.started_ms = 0;
  c.clear();
  render_lock_screen(c, s, false, 12000);
  TEST_ASSERT_TRUE_MESSAGE(c.has("● REC 0:12"), c.dump().c_str());
}

// gh #51: a hands-free memo goes on under the lock; the lit card says how it ends (a G0
// press while the card is lit), not how to start a memo, and it fits the screen.
static void test_locked_hands_free_memo_says_how_it_ends() {
  AppState s;
  s.rec.phase = RecPhase::Recording;
  s.rec.mode = RecMode::Memo;
  s.rec.latched = true;
  FakeCanvas c;
  render_lock_screen(c, s, true, 12000);
  const TextOp* t = c.find("免提录音中：按");
  TEST_ASSERT_NOT_NULL_MESSAGE(t, c.dump().c_str());
  TEST_ASSERT_TRUE(c.has("结束") && !c.has("再按住说 · 点两下免提"));
  TEST_ASSERT_TRUE(t->x >= 0 && c.line(t->y).find("免提录音中：按 G0 结束") != std::string::npos);
  c.clear();
  render_lock_screen(c, s, false, 12000);  // dark: the badge alone says hands free (review of gh #51)
  TEST_ASSERT_TRUE_MESSAGE(c.has("● 免提 0:12"), c.dump().c_str());
  s.rec.latched = false;  // held: as before
  c.clear();
  render_lock_screen(c, s, true, 12000);
  TEST_ASSERT_TRUE(c.has("再按住说 · 点两下免提"));
}

// gh #51 review: a locked end makes no sound, so the lit card says how the last memo ended
// until the screen is dark again (RecLast goes then): 保存中… in place of the G0 line while it
// ends; then, merged with docs/20 B1, the result in the clock's place (test_lock_card), and the
// G0 line says how to record again. A refusal is the card's too (B1: × 没录上).
static void test_lock_card_says_how_the_last_memo_ended() {
  AppState s;
  s.rec.phase = RecPhase::Finishing;
  FakeCanvas c;
  render_lock_screen(c, s, true, 9000);
  TEST_ASSERT_TRUE_MESSAGE(c.has("保存中…") && !c.has("点两下免提"), c.dump().c_str());
  s.rec.phase = RecPhase::Idle;
  s.rec.last = RecLast{true, true, 0, RecMode::Memo, 6000, false, true};
  c.clear();
  render_lock_screen(c, s, true, 9000);
  const TextOp* t = c.find("\xE2\x88\x9A 已存 0:06");
  TEST_ASSERT_NOT_NULL_MESSAGE(t, c.dump().c_str());
  TEST_ASSERT_TRUE(t->style.fg == Color::Ok && c.has("点两下免提") && !c.has("保存中…"));
  s.rec.last.saved = false;  // too short
  c.clear();
  render_lock_screen(c, s, true, 9000);
  TEST_ASSERT_TRUE(c.find("\xC3\x97 太短，未存") && c.find("\xC3\x97 太短，未存")->style.fg == Color::Warn);
  s.rec.last.why = static_cast<uint8_t>(rec::RecRefusal::NoSd);  // refused
  s.rec.last.ms = 0;
  c.clear();
  render_lock_screen(c, s, true, 9000);
  TEST_ASSERT_TRUE_MESSAGE(c.has("\xC3\x97 没录上") && c.has("点两下免提"), c.dump().c_str());
}

static void test_meeting_g0_stars_while_locked() {
  RecRig x;
  press_at(x.r, keys({key::kOpt, kR}), x.r.now + 10);
  press_at(x.r, kEnterKey, x.r.now + 500);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.feed(2000);
  press_at(x.r, kOptL, x.r.now + 10);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  x.r.step(kLockFlashMs);  // docs/20 D7: the card said "已锁屏"
  TEST_ASSERT_TRUE(level(x.r) == Backlight::Dim);  // ● stays visible
  press_at(x.r, keys({key::kG0}), x.r.now + 10);   // a tap
  x.hold_g0(600);
  x.release();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(2, x.port.count(CmdKind::Mark));
  press_at(x.r, keys({key::kSpace}), x.r.now + 10);  // Space does not pass the gate
  x.steps();
  TEST_ASSERT_EQUAL_UINT(2, x.port.count(CmdKind::Mark));
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
}

static void test_lock_card_shows_counts_only() {
  RuntimeRig r;
  r.handshake();
  r.send("sessions.snapshot",
         "\"rev\":4,\"total\":1,\"sessions\":[{\"sid\":\"8d3e7a19\",\"slot\":1,\"name\":\"架构设计\",\"kind\":\"tty\","
         "\"st\":\"waiting\",\"need\":true,\"age_s\":540}]");
  r.step();
  press_at(r, kOptL, r.now + 100);
  press_at(r, kOptL, r.now + 100);
  r.step(10);
  const FakeCanvas& c = r.disp.canvas_;
  TEST_ASSERT_TRUE_MESSAGE(c.has("已锁屏"), c.dump().c_str());
  TEST_ASSERT_TRUE(c.has("等你 1"));
  TEST_ASSERT_TRUE(c.has("Opt+L"));
  for (const char* name : {"cpa", "Cardputer", "助手", "架构设计", "8d3e7a19"}) {
    TEST_ASSERT_FALSE_MESSAGE(c.has(name), name);
  }
}

// The card's date line (local time) and battery; nothing while the time is unknown.
static void test_lock_card_date_and_battery() {
  TimeInfo t;
  TEST_ASSERT_EQUAL_STRING("", lock_date_text(t, 0).c_str());
  t.known = true;
  t.offset_ms = 1792590120000LL;  // 2026-10-21 13:42 UTC, a Wednesday
  TEST_ASSERT_EQUAL_STRING("10/21 周三", lock_date_text(t, 0).c_str());
  t.tz_min = -840;  // 13 h 42 min earlier would still be the 21st; 14 h is the 20th
  TEST_ASSERT_EQUAL_STRING("10/20 周二", lock_date_text(t, 0).c_str());
  AppState s;
  s.time = t;
  s.device.bat_mv = 4000;
  FakeCanvas c;
  render_lock_screen(c, s, true, 0);
  // docs/20 B3: the battery is its own text, coloured when low (test_lock_card)
  TEST_ASSERT_TRUE_MESSAGE(c.has("10/20 周二 · ") && c.has("电 82%"), c.dump().c_str());
  TEST_ASSERT_TRUE(c.has("23:42"));
  TEST_ASSERT_TRUE(c.has("点两下免提"));
  c.clear();
  render_lock_screen(c, s, false, 0);  // dark: nothing but black
  TEST_ASSERT_EQUAL_UINT32(0, c.texts.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_not_locked_passes_everything);
  RUN_TEST(test_opt_l_opens_a_three_second_window);
  RUN_TEST(test_pocket_keys_change_nothing);
  RUN_TEST(test_g0_hold_on_the_dark_screen_only_lights_the_card);
  RUN_TEST(test_g0_tap_lights_the_card_without_a_window);
  RUN_TEST(test_meeting_g0_always_passes);
  RUN_TEST(test_tick_closes_the_card_and_the_window);
  RUN_TEST(test_power_lock_rules);
  RUN_TEST(test_locked_and_plugged_is_dark);
  RUN_TEST(test_fifty_pocket_presses_change_nothing);
  RUN_TEST(test_opt_l_from_dark_lights_once_and_enter_unlocks_in_time);
  RUN_TEST(test_dark_g0_hold_records_nothing_and_lit_hold_records_a_memo);
  RUN_TEST(test_opt_l_on_a_dark_screen_locks_at_once);
  RUN_TEST(test_lit_hold_over_a_draft_records_and_keeps_the_draft);
  RUN_TEST(test_locked_memo_says_wait_during_the_cue);
  RUN_TEST(test_locked_memo_has_the_grace);
  RUN_TEST(test_meeting_g0_stars_while_locked);
  RUN_TEST(test_locked_hands_free_memo_says_how_it_ends);
  RUN_TEST(test_lock_card_says_how_the_last_memo_ended);
  RUN_TEST(test_lock_card_shows_counts_only);
  RUN_TEST(test_lock_card_date_and_battery);
  return UNITY_END();
}
