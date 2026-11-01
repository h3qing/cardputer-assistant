// The muted start of the microphone (issue #31, docs/11 §4.2). After a codec power-on the
// ES8311 holds its ADC output at zero for 2^14 clock frames from its first clocked start
// (about 1 s at 16 kHz); M5Unified's DC offset correction turns the zeros into a constant
// 0 or ±1 after a decaying tail (its offset survives Mic.end: after an earlier capture the
// tail starts at about -DC and lasts 64 x ln(|DC| / 1.5) samples, under 600). A block is
// muted when, after that tail, at least 90 % of its samples are one value v with
// |v| <= kMutedMax; room noise at kMicGain (RMS 12-14) never looks like that.
// RecEngine::arm() drops muted blocks and opens the session at the first live sample, or
// after kMuteWaitMs anyway (a broken microphone must not block recording).
// Little-endian int16 samples, as the ring holds them. Pure.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cpa {
namespace rec {

constexpr int16_t kMutedMax = 2;
constexpr uint32_t kMuteWaitMs = 2000;

bool block_muted(const uint8_t* pcm, size_t n);
// Bytes of the leading samples with |x| <= kMutedMax, after the tail, in a block that is
// not muted (where the codec let go of it); 0 when no sample is louder (a quiet block,
// kept whole).
size_t muted_lead(const uint8_t* pcm, size_t n);

}  // namespace rec
}  // namespace cpa
