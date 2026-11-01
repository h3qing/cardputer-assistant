// The microphone gain (v0.3.1, docs/releases/v0.3.1.md): where the level comes from.
//   pga  ES8311 REG14 analog PGA, 0..10 = 0..+30 dB in 3 dB steps (REG14 = 0x10 | pga: Mic1)
//   vol  ES8311 REG17 ADC volume, 0.5 dB a step, 0xBF = 0 dB, 0xFF = +32 dB
//   mag  M5Unified's magnification, applied after the 16-bit word: with over_sampling pinned
//        to 1 the factor is mag / 2, so 2 is unity (bit-exact) and 16 is v0.3.0's x8
// The two registers act before the word, so they add level without losing resolution;
// magnification only scales the word (and its noise). M5Unified writes 0x10 / 0xBF on every
// Mic.begin(), so MicRing writes these after each begin. Pure.
#pragma once

#include <cmath>
#include <cstdint>

namespace cpa {

struct MicGain {
  uint8_t pga = 0;
  uint8_t vol = 0xBF;
  uint8_t mag = 2;
  bool operator==(const MicGain& o) const { return pga == o.pga && vol == o.vol && mag == o.mag; }
};

constexpr uint8_t kMicPgaMax = 10;
constexpr uint8_t kMicMagMin = 1;
constexpr uint8_t kMicMagMax = 64;
constexpr uint8_t kMicVolUnity = 0xBF;

// The HIL calibration (mic_cal, 2026-10-21; docs/gates/m3-device-gates.md §5.4): PGA +27 dB,
// ADC volume +2.5 dB, unity magnification, so +29.5 dB in the codec. The user's handheld
// speech at its normal level is predicted to peak at -11.9 dBFS (target -12, window -14..-10).
// v0.3.0 was {0, kMicVolUnity, 16}.
constexpr MicGain kMicGain{9, 0xC4, 2};

constexpr bool mic_gain_valid(const MicGain& g) {
  return g.pga <= kMicPgaMax && g.mag >= kMicMagMin && g.mag <= kMicMagMax;
}

constexpr uint8_t es8311_reg14(uint8_t pga) { return static_cast<uint8_t>(0x10 | pga); }

// The gain over the codec's 0 dB with unity magnification, in dB.
inline float nominal_db(const MicGain& g) {
  const float vol_db = (static_cast<float>(g.vol) - static_cast<float>(kMicVolUnity)) / 2.0f;
  return 3.0f * static_cast<float>(g.pga) + vol_db + 20.0f * std::log10(static_cast<float>(g.mag) / 2.0f);
}

static_assert(mic_gain_valid(kMicGain), "kMicGain out of range");

}  // namespace cpa
