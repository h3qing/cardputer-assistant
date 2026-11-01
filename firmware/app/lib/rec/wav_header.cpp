#include "wav_header.h"

#include <cstring>

namespace cpa {
namespace rec {
namespace {

constexpr uint32_t kMemoMark = 0x4F4D454Du;  // "MEMO" little-endian
constexpr uint32_t kMeetMark = 0x5445454Du;  // "MEET"

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v));
  put16(p + 2, static_cast<uint16_t>(v >> 16));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

WavHeader build(uint32_t riff_size, uint32_t data_bytes) {
  WavHeader h;
  std::memcpy(h.b, "RIFF", 4);
  put32(h.b + 4, riff_size);
  std::memcpy(h.b + 8, "WAVEfmt ", 8);
  put32(h.b + 16, 16);
  put16(h.b + 20, 1);
  put16(h.b + 22, 1);
  put32(h.b + 24, kSampleRate);
  put32(h.b + 28, kSampleRate * 2);
  put16(h.b + 32, 2);
  put16(h.b + 34, 16);
  std::memcpy(h.b + 36, "data", 4);
  put32(h.b + 40, data_bytes);
  return h;
}

bool ours(const uint8_t* h) {
  return std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WAVEfmt ", 8) == 0 && get32(h + 16) == 16 &&
         get16(h + 20) == 1 && get16(h + 22) == 1 && get32(h + 24) == kSampleRate && get16(h + 34) == 16 &&
         std::memcmp(h + 36, "data", 4) == 0;
}

}  // namespace

WavHeader wav_header(uint32_t data_bytes) { return build(36 + data_bytes, data_bytes); }

WavHeader wav_placeholder(RecMode mode) { return build(mode == RecMode::Meeting ? kMeetMark : kMemoMark, 0); }

Opt<WavInfo> wav_parse(const uint8_t* h, size_t n) {
  if (n < kHeaderBytes || !ours(h)) return {};
  WavInfo w;
  w.data_bytes = get32(h + 40);
  const uint32_t riff = get32(h + 4);
  if (w.data_bytes == 0 && riff == kMemoMark) w.hint = some(RecMode::Memo);
  if (w.data_bytes == 0 && riff == kMeetMark) w.hint = some(RecMode::Meeting);
  return some(w);
}

}  // namespace rec
}  // namespace cpa
