// The microphone gain (v0.3.1): the ES8311 PGA (REG14) and ADC volume (REG17) before the
// 16-bit word, M5Unified's magnification after it (over_sampling pinned to 1, so mag 2 is
// unity). kMicGain is what the HIL calibration chose (mic_cal, 2026-10-21): PGA +27 dB and
// +2.5 dB ADC volume at unity, for a -12 dBFS handheld speech peak.
#include <unity.h>

#include "mic_gain.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static void test_reg14_selects_mic1_and_sets_the_pga() {
  TEST_ASSERT_EQUAL_HEX8(0x10, es8311_reg14(0));   // M5Unified's own value: 0 dB
  TEST_ASSERT_EQUAL_HEX8(0x17, es8311_reg14(7));   // +21 dB
  TEST_ASSERT_EQUAL_HEX8(0x1A, es8311_reg14(10));  // +30 dB, the maximum
}

static void test_validity_bounds() {
  TEST_ASSERT_TRUE(mic_gain_valid(MicGain{0, 0x00, 1}));
  TEST_ASSERT_TRUE(mic_gain_valid(MicGain{10, 0xFF, 64}));
  TEST_ASSERT_FALSE(mic_gain_valid(MicGain{11, 0xBF, 2}));  // PGA above +30 dB
  TEST_ASSERT_FALSE(mic_gain_valid(MicGain{0, 0xBF, 0}));   // mag 0 would silence the samples
  TEST_ASSERT_FALSE(mic_gain_valid(MicGain{0, 0xBF, 65}));
}

static void test_nominal_db() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.06f, nominal_db(MicGain{0, 0xBF, 16}));  // v0.3.0: x8 after the word
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, nominal_db(MicGain{0, 0xBF, 2}));    // unity
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, nominal_db(MicGain{10, 0xBF, 2}));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, nominal_db(MicGain{7, 0xD1, 2}));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -95.5f, nominal_db(MicGain{0, 0x00, 2}));  // REG17 below 0 dB
}

static void test_the_calibrated_gain() {
  TEST_ASSERT_TRUE(mic_gain_valid(kMicGain));
  TEST_ASSERT_EQUAL_UINT8(9, kMicGain.pga);     // +27 dB
  TEST_ASSERT_EQUAL_HEX8(0xC4, kMicGain.vol);   // +2.5 dB
  TEST_ASSERT_EQUAL_UINT8(2, kMicGain.mag);     // unity
  TEST_ASSERT_TRUE(kMicGain == (MicGain{9, 0xC4, 2}));
  TEST_ASSERT_FALSE(kMicGain == (MicGain{0, 0xBF, 16}));  // no longer v0.3.0's gain
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 29.5f, nominal_db(kMicGain));
  TEST_ASSERT_EQUAL_HEX8(0x19, es8311_reg14(kMicGain.pga));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reg14_selects_mic1_and_sets_the_pga);
  RUN_TEST(test_validity_bounds);
  RUN_TEST(test_nominal_db);
  RUN_TEST(test_the_calibrated_gain);
  return UNITY_END();
}
