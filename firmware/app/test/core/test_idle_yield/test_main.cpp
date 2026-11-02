// docs/29 P1 (idle_yield.h): the loop gives the CPU away only on a dark screen on battery
// with nothing going on; each gate alone keeps it at the full rate; the key grace survives
// millis() wrapping.
#include <unity.h>

#include "idle_yield.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kNow = 100000;

// Everything quiet: dark, idle, on battery, no key for a long time.
IdleFacts quiet() {
  IdleFacts f;
  f.dark = true;
  f.rec_idle = true;
  f.host = false;
  f.audio_busy = false;
  f.keys_down = false;
  f.key_seen = true;
  f.key_ms = kNow - 60000;
  f.writes_due = false;
  return f;
}

}  // namespace

static void test_quiet_dark_battery_yields() { TEST_ASSERT_EQUAL_UINT32(kIdleYieldMs, idle_yield_ms(quiet(), kNow)); }

static void test_defaults_never_yield() { TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(IdleFacts{}, kNow)); }

static void test_the_budget_is_under_20_ms() { TEST_ASSERT_TRUE(kIdleYieldMs < 20); }

static void test_lit_or_dimmed_never_yields() {
  IdleFacts f = quiet();
  f.dark = false;  // On, or Dim (● REC on battery)
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, kNow));
}

static void test_each_gate_alone_keeps_full_rate() {
  IdleFacts f = quiet();
  f.rec_idle = false;  // a memo, a meeting, a box, a held result
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, kNow));
  f = quiet();
  f.host = true;  // a Mac on the cable
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, kNow));
  f = quiet();
  f.audio_busy = true;  // the scan, a file request, a Ledger command
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, kNow));
  f = quiet();
  f.keys_down = true;  // G0 or any key held
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, kNow));
  f = quiet();
  f.writes_due = true;
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, kNow));
}

static void test_grace_after_the_last_key() {
  IdleFacts f = quiet();
  f.key_ms = kNow - kYieldGraceMs + 1;
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, kNow));
  f.key_ms = kNow - kYieldGraceMs;
  TEST_ASSERT_EQUAL_UINT32(kIdleYieldMs, idle_yield_ms(f, kNow));
}

static void test_no_key_since_boot_needs_no_grace() {
  IdleFacts f = quiet();
  f.key_seen = false;
  f.key_ms = kNow;  // ignored
  TEST_ASSERT_EQUAL_UINT32(kIdleYieldMs, idle_yield_ms(f, kNow));
}

static void test_grace_across_millis_wrap() {
  IdleFacts f = quiet();
  f.key_ms = 0xFFFFFF00u;  // 256 ms before the wrap
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, 0x00000100u));  // 512 ms later: still in grace
  TEST_ASSERT_EQUAL_UINT32(kIdleYieldMs, idle_yield_ms(f, 0xFFFFFF00u + kYieldGraceMs));
  TEST_ASSERT_EQUAL_UINT32(0, idle_yield_ms(f, 0xFFFFFF00u + kYieldGraceMs - 1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_quiet_dark_battery_yields);
  RUN_TEST(test_defaults_never_yield);
  RUN_TEST(test_the_budget_is_under_20_ms);
  RUN_TEST(test_lit_or_dimmed_never_yields);
  RUN_TEST(test_each_gate_alone_keeps_full_rate);
  RUN_TEST(test_grace_after_the_last_key);
  RUN_TEST(test_no_key_since_boot_needs_no_grace);
  RUN_TEST(test_grace_across_millis_wrap);
  return UNITY_END();
}
