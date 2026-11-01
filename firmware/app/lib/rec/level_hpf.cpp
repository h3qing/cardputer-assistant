#include "level_hpf.h"

namespace cpa {
namespace rec {
namespace {

struct Biquad {
  float b0, b1, b2, a1, a2;  // a0 = 1
};

// RBJ high-pass sections at f0 130 Hz, fs 16 kHz, Q 0.5412 and 1.3066 (1 / (2 cos(pi/8)) and
// 1 / (2 cos(3pi/8))): together the 4th-order Butterworth, each with a gain of 1 at 8 kHz.
constexpr Biquad kHpf[2] = {
    {0.9543560686f, -1.908712137f, 0.9543560686f, -1.907467979f, 0.9099562954f},
    {0.9802072614f, -1.960414523f, 0.9802072614f, -1.959136663f, 0.9616923822f},
};

struct Step {
  float y, z1, z2;
};

// One sample through one section, transposed direct form II.
Step step(const Biquad& c, float x, float z1, float z2) {
  const float y = c.b0 * x + z1;
  return {y, c.b1 * x - c.a1 * y + z2, c.b2 * x - c.a2 * y};
}

// A filtered sample as a 16-bit one, rounded: its square fits 32 bits.
int32_t sample16(float y) {
  if (y >= 32767.0f) return 32767;
  if (y <= -32768.0f) return -32768;
  return static_cast<int32_t>(y < 0.0f ? y - 0.5f : y + 0.5f);
}

uint32_t isqrt(uint64_t v) {
  uint64_t r = 0;
  for (uint64_t bit = uint64_t{1} << 62; bit; bit >>= 2) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
  }
  return static_cast<uint32_t>(r);
}

}  // namespace

// The RMS of the filtered samples, summed as the unfiltered ones were (docs/11 §4.2). The state
// is a local while it runs, so it stays in registers (pcm could alias the result).
LevelOut level_of(const LevelHpf& h, const uint8_t* pcm, size_t n) {
  LevelOut out;
  out.hpf = h;
  const size_t samples = n / 2;
  if (samples == 0) return out;
  float z0 = h.z[0], z1 = h.z[1], z2 = h.z[2], z3 = h.z[3];
  uint64_t sum = 0;
  for (size_t i = 0; i < samples; ++i) {
    const int16_t s = static_cast<int16_t>(pcm[2 * i] | (pcm[2 * i + 1] << 8));
    const Step a = step(kHpf[0], static_cast<float>(s), z0, z1);
    const Step b = step(kHpf[1], a.y, z2, z3);
    z0 = a.z1;
    z1 = a.z2;
    z2 = b.z1;
    z3 = b.z2;
    const int32_t y = sample16(b.y);
    sum += static_cast<uint32_t>(y * y);
  }
  out.hpf = LevelHpf{{z0, z1, z2, z3}};
  out.rms = static_cast<uint16_t>(isqrt(sum / samples));
  return out;
}

}  // namespace rec
}  // namespace cpa
