// The microphone gain on the shipping MicRing path (v0.3.1): start(gain) pins over_sampling
// to 1, sets the magnification and writes the ES8311 PGA (REG14) and ADC volume (REG17)
// right after Mic.begin, inside the I2C hold, before the first block is requested.
// Pass: {10, 0xC5, 2} reads back 0x1A / 0xC5 and its samples have a GCD of 1 (unity
// magnification: no factor after the 16-bit word); after a stop, M5Unified's own values
// {0, 0xBF, 16} read back 0x10 / 0xBF (nothing of the first gain is left) and every sample
// is a multiple of 8 (16 / 2: over_sampling is 1). No write ever needed a fallback.
// Prints CPA_MIC with the numbers. No card needed.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <esp_heap_caps.h>
#include <unity.h>

#include <cstdlib>

#include "audio_io.h"
#include "device_harness.h"
#include "i2c_lock.h"
#include "mic_gain.h"
#include "usb_transport.h"

using namespace cpa;

namespace {

constexpr uint8_t kEs8311 = 0x18;
constexpr uint32_t kCodecHz = 100000;
constexpr int kBlocks = 4;                   // 512 ms of samples
constexpr MicGain kSweep{10, 0xC5, 2};       // +30 dB PGA, +3 dB ADC volume, unity
constexpr MicGain kM5Defaults{0, 0xBF, 16};  // what M5Unified writes on every begin (v0.3.0)

cpa::UsbTransport g_usb;
MicRing g_ring;
bool g_attached = false;

int read_reg(uint8_t reg) {
  I2cAudioHold hold;
  uint8_t v = 0;
  return M5.In_I2C.readRegister(kEs8311, reg, &v, 1, kCodecHz) ? v : -1;
}

uint32_t gcd(uint32_t a, uint32_t b) {
  while (b != 0) {
    const uint32_t t = a % b;
    a = b;
    b = t;
  }
  return a;
}

// GCD of |sample| over kBlocks full blocks; 0 when every sample is 0, -1 on a timeout.
int32_t samples_gcd() {
  uint32_t g = 0;
  for (int n = 0; n < kBlocks; ++n) {
    const int idx = g_ring.next_full(pdMS_TO_TICKS(1000));
    if (idx < 0) return -1;
    const int16_t* s = reinterpret_cast<const int16_t*>(g_ring.block(idx));
    for (size_t i = 0; i < kBlockSamples; ++i) g = gcd(g, static_cast<uint32_t>(std::abs(s[i])));
    g_ring.release(idx);
  }
  return static_cast<int32_t>(g);
}

struct Capture {
  int reg14 = -1;
  int reg17 = -1;
  int32_t gcd = -1;
};

Capture capture(const MicGain& gain) {
  Capture c;
  if (!g_ring.start(gain)) return c;
  c.reg14 = read_reg(0x14);
  c.reg17 = read_reg(0x17);
  c.gcd = samples_gcd();
  g_ring.stop();
  Serial.printf("CPA_MIC pga=%u vol=0x%02X mag=%u reg14=0x%02X reg17=0x%02X gcd=%ld gain_failures=%lu\n", gain.pga,
                gain.vol, gain.mag, c.reg14, c.reg17, static_cast<long>(c.gcd),
                static_cast<unsigned long>(g_ring.gain_failures()));
  return c;
}

}  // namespace

void setUp() {}
void tearDown() { g_ring.stop(); }

static void test_the_ring_attaches() {
  auto* mem = static_cast<uint8_t*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  g_attached = mem && g_ring.attach(mem);
  TEST_ASSERT_TRUE(g_attached);
}

static void test_the_gain_reads_back_and_unity_passes_the_word() {
  const Capture c = capture(kSweep);
  TEST_ASSERT_EQUAL_HEX8(0x1A, c.reg14);
  TEST_ASSERT_EQUAL_HEX8(0xC5, c.reg17);
  TEST_ASSERT_EQUAL_INT32(1, c.gcd);
  TEST_ASSERT_EQUAL_UINT32(0, g_ring.gain_failures());
}

static void test_the_next_start_writes_its_own_gain() {
  const Capture c = capture(kM5Defaults);
  TEST_ASSERT_EQUAL_HEX8(0x10, c.reg14);
  TEST_ASSERT_EQUAL_HEX8(0xBF, c.reg17);
  TEST_ASSERT_TRUE(c.gcd > 0);
  TEST_ASSERT_EQUAL_INT32(0, c.gcd % 8);
  TEST_ASSERT_EQUAL_UINT32(0, g_ring.gain_failures());
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
    RUN_TEST(test_the_gain_reads_back_and_unity_passes_the_word);
    RUN_TEST(test_the_next_start_writes_its_own_gain);
  }
  UNITY_END();
}

void loop() { devtest::idle(); }
