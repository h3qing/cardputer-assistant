#include "mic_start.h"

namespace cpa {
namespace rec {
namespace {

int16_t sample(const uint8_t* pcm, size_t i) { return static_cast<int16_t>(pcm[2 * i] | (pcm[2 * i + 1] << 8)); }

bool quiet(int16_t s) { return s >= -kMutedMax && s <= kMutedMax; }

int32_t magnitude(int16_t s) { return s < 0 ? -static_cast<int32_t>(s) : s; }

// The correction's tail never grows and falls by at most 1/16 of itself and 1 per sample
// (at any DC); live audio falls faster on its way to a zero.
bool decays(int16_t prev, int16_t s) {
  const int32_t a = magnitude(prev);
  const int32_t b = magnitude(s);
  return b <= a && a - b <= a / 16 + 1;
}

// Samples of the DC offset correction's tail at the block's start: louder than kMutedMax,
// of one sign, decaying into a quiet sample. 0 when the block does not start that way.
size_t tail(const uint8_t* pcm, size_t samples) {
  size_t i = 0;
  for (; i < samples && !quiet(sample(pcm, i)); ++i) {
    if (i == 0) continue;
    const int16_t prev = sample(pcm, i - 1);
    const int16_t s = sample(pcm, i);
    if ((s < 0) != (prev < 0) || !decays(prev, s)) return 0;
  }
  return i > 0 && i < samples && decays(sample(pcm, i - 1), sample(pcm, i)) ? i : 0;
}

}  // namespace

bool block_muted(const uint8_t* pcm, size_t n) {
  const size_t samples = n / 2;
  if (samples == 0) return false;
  const size_t from = tail(pcm, samples);  // always short of samples
  size_t count[2 * kMutedMax + 1] = {};
  for (size_t i = from; i < samples; ++i) {
    const int16_t s = sample(pcm, i);
    if (quiet(s)) ++count[s + kMutedMax];
  }
  for (const size_t c : count) {
    if (c * 10 >= (samples - from) * 9) return true;
  }
  return false;
}

size_t muted_lead(const uint8_t* pcm, size_t n) {
  const size_t samples = n / 2;
  for (size_t i = tail(pcm, samples); i < samples; ++i) {
    if (!quiet(sample(pcm, i))) return 2 * i;
  }
  return 0;
}

}  // namespace rec
}  // namespace cpa
