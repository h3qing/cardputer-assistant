// docs/29 P1 in the runtime (Runtime::idle_yield_ms, idle_yield.h): on battery the loop may
// sleep only once the screen went dark and locked and the lock card went out; never with a
// Mac on the cable, a key down (or within the grace after it), a recording, or the audio
// task's boot scan unfinished; a key on the dark screen brings the full rate back at once.
#include <unity.h>

#include "idle_yield.h"
#include "lock.h"
#include "power.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

// The matrix key that types c.
uint8_t key_of(char c) {
  for (uint8_t k = 0; k < kMatrixKeys; ++k) {
    if (key_base(k) == c && !is_modifier(k)) return k;
  }
  TEST_FAIL_MESSAGE("no key types this character");
  return 0;
}

// Steps of 100 ms for ms milliseconds, no key.
void wait(RuntimeRig& r, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 100) r.step(100);
}

// On battery, dark, locked, the lock card out (docs/20 D2, D7).
void go_dark(RecRig& x) {
  x.r.unplug();
  wait(x.r, kBacklightIdleMs + kLockFlashMs + 200);
}

}  // namespace

static void test_lit_on_battery_runs_at_full_rate() {
  RecRig x;
  x.r.unplug();
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
  wait(x.r, kBacklightIdleMs - kPreDimMs + 100);  // dimmed: still read
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
}

static void test_dark_locked_on_battery_yields() {
  RecRig x;
  go_dark(x);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
  TEST_ASSERT_EQUAL_UINT32(kIdleYieldMs, x.r.rt.idle_yield_ms());
}

static void test_a_mac_on_the_cable_never_yields() {
  RecRig x;  // plugged, handshake done
  wait(x.r, kBacklightIdleMs + kLockFlashMs + 200);
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
}

static void test_a_key_on_the_dark_screen_brings_the_full_rate_back() {
  RecRig x;
  go_dark(x);
  const KeySet a = KeySet{}.with(key_of('a'));
  x.r.step(10, a);  // swallowed by the lock: the screen stays dark
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
  x.r.step(10);
  wait(x.r, kYieldGraceMs - 100);
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
  wait(x.r, 200);
  TEST_ASSERT_EQUAL_UINT32(kIdleYieldMs, x.r.rt.idle_yield_ms());
}

static void test_g0_held_on_the_dark_screen_never_yields() {
  RecRig x;
  go_dark(x);
  x.hold_g0(3000);  // a pocket hold: swallowed, the card lights
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
}

static void test_a_memo_never_yields() {
  RecRig x;
  go_dark(x);
  x.r.step(10, KeySet{}.with(key::kG0));  // the tap arms the card
  x.r.step(10);
  x.hold_g0(600);  // ... the hold records
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
  x.port.feed(400);
  x.let_go();
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
}

static void test_an_unfinished_boot_scan_never_yields() {
  RecRig x(true, false);  // the audio task has not reported IndexReady
  go_dark(x);
  TEST_ASSERT_EQUAL_UINT32(0, x.r.rt.idle_yield_ms());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lit_on_battery_runs_at_full_rate);
  RUN_TEST(test_dark_locked_on_battery_yields);
  RUN_TEST(test_a_mac_on_the_cable_never_yields);
  RUN_TEST(test_a_key_on_the_dark_screen_brings_the_full_rate_back);
  RUN_TEST(test_g0_held_on_the_dark_screen_never_yields);
  RUN_TEST(test_a_memo_never_yields);
  RUN_TEST(test_an_unfinished_boot_scan_never_yields);
  return UNITY_END();
}
