// Physical keys through the runtime (05 §5.7, §5.11): ghost frames are dropped and
// counted, a stuck key set re-initialises the keyboard after 10 s, the backlight goes
// off after 60 s on battery and the waking key is swallowed (G0 opens the quick
// note). Test hooks (CPA_TEST_HOOKS): debug.key, debug.state, debug.reboot.
#include <unity.h>

#include "kb_watch.h"
#include "power.h"
#include "runtime.h"
#include "support/fixtures.h"
#include "support/runtime_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

static KeySet keys(std::initializer_list<uint8_t> ks) {
  KeySet s;
  for (uint8_t k : ks) s = s.with(k);
  return s;
}

constexpr uint8_t kS = 31;  // layout index of 's' (row 2, x 3)
constexpr uint8_t kQ = 15;  // 'q'

static void test_physical_opt_s_opens_spawn_compose() {
  RuntimeRig r;
  r.step(10, keys({key::kOpt}));
  r.step(10, keys({key::kOpt, kS}));
  r.step(10, KeySet{});
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::Compose);
}

static void test_ghost_frames_are_counted() {
  RuntimeRig r;
  // rectangle in the matrix: layout 0 (row 0,col 0), 2 (row 1,col 0), 14 (row 0,col 1), 16 (row 1,col 1)
  r.step(10, keys({0, 2, 14, 16}));
  TEST_ASSERT_EQUAL_UINT16(1, r.rt.state().device.ghost_n);
}

static void test_stuck_keys_reinit_the_keyboard_once() {
  KbWatch w;
  KbWatchStep s = kb_watch_step(w, keys({kQ}), 0);
  s = kb_watch_step(s.state, keys({kQ}), 9999);
  TEST_ASSERT_FALSE(s.reinit);
  s = kb_watch_step(s.state, keys({kQ}), 10000);
  TEST_ASSERT_TRUE(s.reinit);
  s = kb_watch_step(s.state, keys({kQ}), 30000);
  TEST_ASSERT_FALSE(s.reinit);  // once per stuck set
  s = kb_watch_step(s.state, keys({key::kG0}), 30001);
  s = kb_watch_step(s.state, keys({key::kG0}), 60000);
  TEST_ASSERT_FALSE(s.reinit);  // G0 alone is not the matrix
  RuntimeRig r;
  for (int i = 0; i < 12; ++i) r.step(1000, keys({kQ}));
  TEST_ASSERT_EQUAL_INT(1, r.plat.reinits);
  TEST_ASSERT_EQUAL_UINT16(1, r.rt.state().device.kb_reinit_n);
}

static void test_power_policy() {
  PowerState p;
  p = power_tick(p, false, 59999);
  TEST_ASSERT_FALSE(p.dark);
  p = power_tick(p, false, 60000);
  TEST_ASSERT_TRUE(p.dark);
  PowerStep k = power_key(p, chr('a'), 60100);
  TEST_ASSERT_TRUE(k.swallow);
  TEST_ASSERT_FALSE(k.state.dark);
  TEST_ASSERT_FALSE(power_key(k.state, chr('a'), 60200).swallow);
  k = power_key(p, g0tap(), 60100);
  TEST_ASSERT_FALSE(k.swallow);  // G0 wakes into Compose
  // v0.3 (docs/11 §4.1): G0 held starts a memo straight from the dark screen
  TEST_ASSERT_FALSE(power_key(p, g0long(), 60100).swallow);
  TEST_ASSERT_FALSE(power_key(p, g0longup(), 60100).swallow);
  p = power_tick(p, true, 70000);
  TEST_ASSERT_FALSE(p.dark);  // USB host: always on
  // v0.3 (docs/11 §4.2): while recording, 60 s idle dims instead of going dark, and a
  // key on the dimmed screen is not swallowed
  PowerState rec = power_recording(PowerState{}, true);
  rec = power_tick(rec, false, 60000);
  TEST_ASSERT_TRUE(rec.dim && !rec.dark);
  TEST_ASSERT_TRUE(backlight_of(rec) == Backlight::Dim);
  const PowerStep dimmed = power_key(rec, chr('a'), 60100);
  TEST_ASSERT_FALSE(dimmed.swallow);
  TEST_ASSERT_TRUE(backlight_of(dimmed.state) == Backlight::On);
  rec = power_recording(rec, false);  // recording ended while dim: the usual rule
  rec = power_tick(rec, false, 60200);
  TEST_ASSERT_TRUE(rec.dark && backlight_of(rec) == Backlight::Off);
}

// docs/20 U24: on battery the screen dims kPreDimMs before it goes dark; a key on the
// dimmed screen works at once (only a dark screen's first key just wakes it).
static void test_screen_dims_before_it_goes_dark() {
  TEST_ASSERT_TRUE(kPreDimMs >= 10000 && kPreDimMs <= 15000);
  PowerState p;
  p = power_tick(p, false, kBacklightIdleMs - kPreDimMs - 1);
  TEST_ASSERT_TRUE(backlight_of(p) == Backlight::On);
  p = power_tick(p, false, kBacklightIdleMs - kPreDimMs);
  TEST_ASSERT_TRUE(backlight_of(p) == Backlight::Dim && !p.dark);
  const PowerStep k = power_key(p, chr('a'), kBacklightIdleMs - 1000);
  TEST_ASSERT_FALSE(k.swallow);
  TEST_ASSERT_TRUE(backlight_of(k.state) == Backlight::On);
  p = power_tick(p, false, kBacklightIdleMs);
  TEST_ASSERT_TRUE(backlight_of(p) == Backlight::Off && p.dark);
  PowerState plugged;
  plugged = power_tick(plugged, true, kBacklightIdleMs - 1);
  TEST_ASSERT_TRUE(backlight_of(plugged) == Backlight::On);  // with a USB host: always on
  RuntimeRig r;
  r.tx.is_plugged = false;
  r.step(cpa::kUnplugConfirmMs);
  r.step(kBacklightIdleMs - kPreDimMs + 10);
  TEST_ASSERT_TRUE(r.plat.levels.back() == Backlight::Dim);
  r.step(10, keys({kQ}));  // the key works: no wake-only swallow
  r.step(10, KeySet{});
  TEST_ASSERT_TRUE(r.plat.levels.back() == Backlight::On);
}

// docs/20 D2: on battery the dark screen is locked. A key no longer wakes it (the lock
// swallows it and lights nothing); a G0 tap lights the lock card instead of opening the
// editor; Opt+L, then Enter, unlock and light the page. gh #51: a G0 tap on the page opens
// the quick note once the double tap's window is out; one on the lock card never does.
static void test_backlight_off_on_battery_and_waking_key_swallowed() {
  RuntimeRig r;
  r.tx.is_plugged = false;
  r.step(61000);
  TEST_ASSERT_FALSE(r.plat.backlight.empty());
  TEST_ASSERT_FALSE(r.plat.backlight.back());
  TEST_ASSERT_TRUE(r.rt.shell().locked());
  r.step(10, keys({key::kOpt}));
  r.step(10, keys({key::kOpt, kS}));  // swallowed: nothing wakes
  r.step(10, KeySet{});
  TEST_ASSERT_FALSE(r.plat.backlight.back());
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::None);
  r.step(10, keys({key::kG0}));
  r.step(100, KeySet{});  // G0 tap: the lock card, not the editor
  TEST_ASSERT_TRUE(r.plat.backlight.back());
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::None);
  r.step(kDoubleTapMs);  // gh #51: no note once the double tap's window is out either
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::None);
  r.step(10, keys({key::kOpt, 38}));  // Opt+L
  r.step(10, KeySet{});
  r.step(100, keys({key::kEnter}));
  r.step(10, KeySet{});
  TEST_ASSERT_FALSE(r.rt.shell().locked());
  TEST_ASSERT_TRUE(r.plat.backlight.back());
  r.step(10, keys({key::kG0}));
  r.step(100, KeySet{});  // G0 tap on the page
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::None);
  r.step(kDoubleTapMs);  // gh #51: the note opens once no second tap came
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::Compose);
}

static void test_debug_key_types_and_state_reports_timestamps() {
  RuntimeRig r;
  r.handshake();
  r.send("debug.key", "\"keys\":[\"g0\"],\"hold_ms\":50");
  r.send("debug.key", "\"keys\":[\"h\"]");
  r.send("debug.key", "\"keys\":[\"i\"]");
  for (int i = 0; i < 40; ++i) r.step(10);
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::Compose);
  r.send("debug.state");
  r.step(10);
  const auto st = r.tx.of_type("debug.state");
  TEST_ASSERT_EQUAL_UINT32(1, st.size());
  const auto& s = std::get<proto::DebugState>(st[0].msg.body);
  TEST_ASSERT_TRUE(s.summary);
  TEST_ASSERT_TRUE(s.overlay == proto::Overlay::Compose);
  TEST_ASSERT_TRUE(s.mod.equals("board"));
  TEST_ASSERT_TRUE(s.overlay_ms.has && s.key_ms.has);
  TEST_ASSERT_TRUE(s.key_ms.value >= s.overlay_ms.value);  // h and i were pressed after G0
  TEST_ASSERT_EQUAL_UINT8(1, s.n);
  TEST_ASSERT_EQUAL_UINT16(1, s.projects[0].run);
  TEST_ASSERT_EQUAL_UINT32(3, s.revs.projects);
  TEST_ASSERT_EQUAL_UINT32(3, r.tx.count("ack") - 1);  // three debug.key acks + projects
  r.key(enter(), r.now);
  TEST_ASSERT_EQUAL_UINT16(1, r.rt.state().outbox.pending);  // "hi" saved
}

static void test_debug_key_queue_presses_repeated_keys_separately() {
  InjectQueue q;
  proto::DebugKey k;
  k.n = 1;
  k.keys[0] = 31;
  q = inject_push(q, k);
  q = inject_push(q, k);
  InjectStep st = inject_tick(q, 100);
  TEST_ASSERT_TRUE(st.keys.has(31));
  st = inject_tick(st.queue, 149);
  TEST_ASSERT_TRUE(st.keys.has(31));
  st = inject_tick(st.queue, 150);  // released
  TEST_ASSERT_TRUE(st.keys.empty());
  st = inject_tick(st.queue, 170);  // gap
  TEST_ASSERT_TRUE(st.keys.empty());
  st = inject_tick(st.queue, 180);
  TEST_ASSERT_TRUE(st.keys.has(31));
  for (int i = 0; i < kInjectMax + 2; ++i) st.queue = inject_push(st.queue, k);
  TEST_ASSERT_EQUAL_UINT8(kInjectMax, st.queue.n);
  TEST_ASSERT_TRUE(st.queue.dropped > 0);
}

static void test_debug_reboot_acks_then_restarts() {
  RuntimeRig r;
  r.handshake();
  r.send("debug.reboot");
  r.step();
  TEST_ASSERT_EQUAL_INT(1, r.plat.reboots);
  TEST_ASSERT_EQUAL_UINT32(2, r.tx.count("ack"));
}

// v0.4.0 (docs/13 §12.1): debug.state carries ui{menu, locked}; overlay still names the
// layer under the menu.
static proto::DebugState debug_state(RuntimeRig& r) {
  r.send("debug.state");
  r.step(10);
  return std::get<proto::DebugState>(r.tx.of_type("debug.state").back().msg.body);
}

static void test_debug_state_reports_menu_and_lock() {
  RuntimeRig r;
  r.handshake();
  proto::DebugState s = debug_state(r);
  TEST_ASSERT_TRUE(s.ui.has && !s.ui.value.menu && !s.ui.value.locked);
  r.step(10, keys({key::kG0}));
  r.step(10, KeySet{});  // G0 tap: Compose
  r.step(10, keys({key::kTab}));
  r.step(10, KeySet{});
  s = debug_state(r);
  TEST_ASSERT_TRUE(s.ui.value.menu && !s.ui.value.locked);
  TEST_ASSERT_TRUE(s.overlay == proto::Overlay::Compose);
  r.step(10, keys({key::kTab}));
  r.step(10, KeySet{});
  r.step(10, keys({key::kOpt, 38}));  // Opt+L
  r.step(10, KeySet{});
  s = debug_state(r);
  TEST_ASSERT_TRUE(!s.ui.value.menu && s.ui.value.locked);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_physical_opt_s_opens_spawn_compose);
  RUN_TEST(test_ghost_frames_are_counted);
  RUN_TEST(test_stuck_keys_reinit_the_keyboard_once);
  RUN_TEST(test_power_policy);
  RUN_TEST(test_screen_dims_before_it_goes_dark);
  RUN_TEST(test_backlight_off_on_battery_and_waking_key_swallowed);
  RUN_TEST(test_debug_key_types_and_state_reports_timestamps);
  RUN_TEST(test_debug_key_queue_presses_repeated_keys_separately);
  RUN_TEST(test_debug_reboot_acks_then_restarts);
  RUN_TEST(test_debug_state_reports_menu_and_lock);
  return UNITY_END();
}
