// The level of a recording's block (docs/19 §6.6, §11 item 15): the RMS that draws the waveform
// and tells Mando someone is talking, taken after a high-pass so the room's hum is not a voice.
// On the device a low hum (peaks near 35 and 68 Hz, -52 to -57 dBFS) kept Mando nodding through
// the pauses and drew 5 px bars with no one speaking. The high-pass is a 4th-order Butterworth
// at 130 Hz, two biquads in float (the ESP32-S3 has an FPU): 35 Hz -46 dB, 68 Hz -23 dB, 300 Hz
// and up within 0.01 dB. Only the level: the WAV keeps every byte as the microphone gave it.
// The state runs on from block to block of one recording; LevelHpf{} at its start. Pure.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cpa {
namespace rec {

struct LevelHpf {
  float z[4] = {};  // two sections in transposed direct form II, two delays each
};

struct LevelOut {
  LevelHpf hpf;      // the state after the block, for the next one
  uint16_t rms = 0;  // the block's RMS, high-passed
};

// n bytes of little-endian int16 samples, filtered from state h.
LevelOut level_of(const LevelHpf& h, const uint8_t* pcm, size_t n);

}  // namespace rec
}  // namespace cpa
