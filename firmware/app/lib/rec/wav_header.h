// WAV header of a recording segment (docs/11 §4.4): 16 kHz, 16 bit, mono PCM, the
// standard 44 B header. Pure: lib/rec never includes Arduino, M5 or esp_* headers.
// An open segment starts with a placeholder (both lengths 0) whose RIFF size field holds
// "MEMO" or "MEET": recovery after a power cut (§4.6) rewrites the header and reads the
// mode from it when no earlier segment names this one.
#pragma once

#include <cstddef>
#include <cstdint>

#include "domain.h"
#include "opt.h"

namespace cpa {
namespace rec {

constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kBytesPerMs = 32;  // 16 kHz x 2 B
constexpr uint32_t kHeaderBytes = 44;

struct WavHeader {
  uint8_t b[kHeaderBytes] = {};
};

struct WavInfo {
  uint32_t data_bytes = 0;
  Opt<RecMode> hint;  // the placeholder of an unfinished segment
};

WavHeader wav_header(uint32_t data_bytes);
WavHeader wav_placeholder(RecMode mode);
// Our format only (RIFF / WAVE / fmt PCM 16 kHz mono 16 bit / data); absent otherwise.
Opt<WavInfo> wav_parse(const uint8_t* h, size_t n);

inline uint32_t dur_ms_of(uint32_t data_bytes) { return data_bytes / kBytesPerMs; }
inline uint32_t bytes_of_ms(uint32_t ms) { return ms * kBytesPerMs; }
// The PCM bytes of a file of `file_size` bytes: whole 16-bit samples after the header.
inline uint32_t even_data(uint32_t file_size) {
  return file_size > kHeaderBytes ? (file_size - kHeaderBytes) & ~1u : 0;
}

}  // namespace rec
}  // namespace cpa
