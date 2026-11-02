// docs/29 P1 "暗屏少转" on the device: the firmware's loop -- the real keyboard scan under
// the I2C lock, the Runtime with the modules of src/module_list.cpp on the real display, and
// the yield of src/main.cpp after each pass -- counted for 3 s in three states:
//   lit    on battery, the screen on (no yield):            the pass rate today;
//   dark   on battery, dark and locked, the card out:       <= 1000 / kIdleYieldMs passes a
//          second, and the longest start-to-start gap at most one yield (plus a tick and
//          spread) over the lit loop's longest (docs/29 P1: key sampling <= 10 ms later);
//   host   a Mac on the cable, still locked and dark:       no yield, the full rate again.
// "On battery" is the transport's: the test talks over USB, so the Runtime gets a transport
// whose plugged() the test sets. The clock it is given runs ahead of millis() to skip the
// 60 s to dark and the lock card's 2 s. No storage (Store::None) and no audio task: neither
// is a gate here (test_idle_yield_runtime covers them). The numbers go out on CPA_IDLE.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <unity.h>

#include <esp_timer.h>

#include <cstdio>

#include "board_module.h"
#include "device_harness.h"
#include "hal_platform.h"
#include "home_module.h"
#include "i2c_lock.h"
#include "idle_yield.h"
#include "inbox_module.h"
#include "keyboard.h"
#include "lock.h"
#include "m5_display.h"
#include "null_store.h"
#include "power.h"
#include "runtime.h"
#include "system_module.h"
#include "usb_transport.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

constexpr uint32_t kWindowMs = 3000;
// The lit loop already has ~20 ms passes (a full redraw, 20.8 ms on the device), so the gate is what the
// yield adds to that: kIdleYieldMs, plus 2 ms for the tick vTaskDelay rounds to and run-to-run spread.
constexpr uint32_t kAddedGapUs = (cpa::kIdleYieldMs + 2) * 1000;

// A link with nobody on it; plugged() is what the test says.
class QuietTransport : public cpa::Transport {
 public:
  bool host = false;
  bool plugged() const override { return host; }
  size_t read(char*, size_t) override { return 0; }
  bool send(const char*, size_t, cpa::TxClass) override { return true; }
  void tick(uint32_t) override {}
};

cpa::UsbTransport g_usb;
cpa::NullStore g_fs;
cpa::M5Display g_display;
cpa::HalPlatform g_platform(g_display);
QuietTransport g_link;
cpa::HomeModule g_home;
cpa::BoardModule g_board;
cpa::InboxModule g_inbox;
cpa::SystemModule g_system;
cpa::Module* const g_mods[] = {&g_home, &g_board, &g_inbox, &g_system};  // as src/module_list.cpp
cpa::Runtime g_rt(g_link, g_fs, g_platform, g_display, g_mods, sizeof(g_mods) / sizeof(g_mods[0]));
uint32_t g_ahead = 0;  // the Runtime's clock minus millis()

struct Rate {
  uint32_t passes = 0;
  uint32_t yields = 0;
  uint32_t gap_max_us = 0;
};

uint32_t runtime_now() { return millis() + g_ahead; }

// One pass of src/main.cpp's loop(), the yield included; true when it yielded.
bool pass() {
  const cpa::KeySet keys = cpa::hal_update_and_keys();
  const uint32_t now = runtime_now();
  g_rt.loop(now, keys);
  g_platform.tick(now);
  const uint32_t rest = g_rt.idle_yield_ms();
  if (rest > 0) vTaskDelay(pdMS_TO_TICKS(rest));
  return rest > 0;
}

Rate measure(uint32_t ms) {
  Rate r;
  const uint32_t t0 = millis();
  int64_t last = esp_timer_get_time();
  while (millis() - t0 < ms) {
    r.yields += pass() ? 1 : 0;
    ++r.passes;
    const int64_t t = esp_timer_get_time();
    const uint32_t gap = static_cast<uint32_t>(t - last);
    r.gap_max_us = gap > r.gap_max_us ? gap : r.gap_max_us;
    last = t;
  }
  return r;
}

uint32_t hz(const Rate& r) { return r.passes * 1000u / kWindowMs; }

void boot_runtime() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  M5Cardputer.begin(cfg, true);
  TEST_ASSERT_TRUE(g_display.begin());
  cpa::hal_i2c_lock_begin();
  cpa::StorageBoot storage;
  storage.plan = cpa::StoragePlan{cpa::Store::None, false, cpa::StoreNote::None};
  cpa::BootInfo boot;
  boot.dev = cpa::hal_dev_id();
  boot.rst = cpa::hal_reset_reason();
  g_rt.begin(boot, runtime_now(), storage);
}

// Skips the clock past the 60 s to dark, then past the lock card's flash.
void go_dark() {
  g_ahead += cpa::kBacklightIdleMs + 100;
  for (int i = 0; i < 3; ++i) pass();
  g_ahead += cpa::kLockFlashMs + 100;
  for (int i = 0; i < 3; ++i) pass();
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_dark_loop_yields_lit_and_host_do_not() {
  boot_runtime();
  for (int i = 0; i < 50; ++i) pass();  // the first frames
  const Rate lit = measure(kWindowMs);
  go_dark();
  TEST_ASSERT_TRUE(g_rt.shell().locked());
  const Rate dark = measure(kWindowMs);
  g_link.host = true;
  for (int i = 0; i < 3; ++i) pass();
  const Rate host = measure(kWindowMs);
  std::printf("CPA_IDLE lit_hz=%u lit_gap_max_us=%u dark_hz=%u dark_yields=%u dark_gap_max_us=%u host_hz=%u\n",
              static_cast<unsigned>(hz(lit)), static_cast<unsigned>(lit.gap_max_us), static_cast<unsigned>(hz(dark)),
              static_cast<unsigned>(dark.yields), static_cast<unsigned>(dark.gap_max_us),
              static_cast<unsigned>(hz(host)));
  TEST_ASSERT_EQUAL_UINT32(0, lit.yields);
  TEST_ASSERT_EQUAL_UINT32(0, host.yields);
  TEST_ASSERT_TRUE(dark.yields + 2 >= dark.passes);  // every pass but a timer's odd one
  TEST_ASSERT_TRUE(hz(dark) <= 1000u / cpa::kIdleYieldMs + 5);
  TEST_ASSERT_TRUE(dark.gap_max_us <= lit.gap_max_us + kAddedGapUs);
  TEST_ASSERT_TRUE(hz(lit) >= 2 * hz(dark));
  TEST_ASSERT_TRUE(hz(host) >= 2 * hz(dark));
}

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_dark_loop_yields_lit_and_host_do_not);
  UNITY_END();
}

void loop() { devtest::idle(); }
