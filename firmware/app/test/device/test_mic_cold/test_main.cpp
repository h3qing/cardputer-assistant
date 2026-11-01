// The codec's muted start on the shipping MicRing path (issue #31, v0.3.2): after the soft
// reset debug.codec uses (MicRing::cold, REG00 = 0x1F) the first start at kMicGain is held
// muted for about 1 s (7-8 of its first 12 blocks, rec::block_muted); a start right after is
// not; and after cold + MicRing::warm (the boot's warm-up, 48 kHz) the next 16 kHz start is
// not muted either. Pass: at least kColdMin muted blocks cold, none warm, none after the
// warm-up. Prints CPA_COLD with the numbers. No card, no speaker, no Mac tone needed: room
// noise at kMicGain (RMS 12-14) is never a muted block.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <esp_heap_caps.h>
#include <unity.h>

#include "audio_io.h"
#include "device_harness.h"
#include "i2c_lock.h"
#include "mic_gain.h"
#include "mic_start.h"
#include "usb_transport.h"

using namespace cpa;

namespace {

constexpr int kBlocks = 12;   // 1.5 s at 16 kHz
constexpr int kColdMin = 5;   // 640 ms: the soft reset gave 933-995 ms on the bench

cpa::UsbTransport g_usb;
MicRing g_ring;
bool g_attached = false;

// The muted blocks from the first one (stops at the first live block), -1 on a timeout.
int leading_muted(const char* what) {
  if (!g_ring.start(kMicGain)) return -1;
  int muted = 0;
  bool live = false;
  for (int n = 0; n < kBlocks; ++n) {
    const int idx = g_ring.next_full(pdMS_TO_TICKS(1000));
    if (idx < 0) {
      muted = -1;
      break;
    }
    live = live || !rec::block_muted(g_ring.block(idx), kBlockBytes);
    muted += live ? 0 : 1;
    g_ring.release(idx);
  }
  g_ring.stop();
  Serial.printf("CPA_COLD %s muted_blocks=%d muted_ms=%d\n", what, muted, muted * 128);
  return muted;
}

}  // namespace

void setUp() {}
void tearDown() { g_ring.stop(); }

static void test_the_ring_attaches() {
  auto* mem = static_cast<uint8_t*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  g_attached = mem && g_ring.attach(mem);
  TEST_ASSERT_TRUE(g_attached);
}

static void test_a_soft_reset_brings_the_muted_start_back() {
  TEST_ASSERT_TRUE(g_ring.cold());
  TEST_ASSERT_GREATER_OR_EQUAL_INT(kColdMin, leading_muted("cold"));
}

static void test_a_start_right_after_is_not_muted() { TEST_ASSERT_EQUAL_INT(0, leading_muted("warm")); }

static void test_the_warm_up_removes_the_muted_start() {
  TEST_ASSERT_TRUE(g_ring.cold());
  const uint32_t t0 = millis();
  TEST_ASSERT_TRUE(g_ring.warm(kMicGain));
  Serial.printf("CPA_COLD warm_up_ms=%lu\n", static_cast<unsigned long>(millis() - t0));
  TEST_ASSERT_EQUAL_INT(0, leading_muted("after_warm_up"));
}

void setup() {
  devtest::begin(g_usb);
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  M5Cardputer.begin(cfg, true);
  hal_i2c_lock_begin();
  UNITY_BEGIN();
  RUN_TEST(test_the_ring_attaches);
  if (g_attached) {
    RUN_TEST(test_a_soft_reset_brings_the_muted_start_back);
    RUN_TEST(test_a_start_right_after_is_not_muted);
    RUN_TEST(test_the_warm_up_removes_the_muted_start);
  }
  UNITY_END();
}

void loop() { devtest::idle(); }
