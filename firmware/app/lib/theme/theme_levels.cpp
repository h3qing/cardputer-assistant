#include "theme_levels.h"

#include <cmath>
#include <cstring>

namespace cpa {
namespace theme {
namespace {

LevelHistory wave_push(const LevelHistory& h, uint8_t q) {
  LevelHistory out = h;
  if (out.wave_n == kWaveBars) {
    std::memmove(out.wave, out.wave + 1, kWaveBars - 1);
    out.wave_n = kWaveBars - 1;
  }
  out.wave[out.wave_n++] = q;
  return out;
}

// Two cells into one, the louder, and each cell spans twice the blocks.
LevelHistory line_merge(const LevelHistory& h) {
  LevelHistory out = h;
  out.cells_n = h.cells_n / 2;
  for (uint8_t i = 0; i < out.cells_n; ++i) {
    const uint8_t a = h.cells[2 * i], b = h.cells[2 * i + 1];
    out.cells[i] = a > b ? a : b;
  }
  std::memset(out.cells + out.cells_n, 0, kLineCells - out.cells_n);
  out.per_cell = static_cast<uint16_t>(h.per_cell * 2);
  return out;
}

LevelHistory line_push(const LevelHistory& h, uint8_t q) {
  LevelHistory out = h;
  if (out.in_cell == 0) {
    if (out.cells_n == kLineCells) out = line_merge(out);
    out.cells[out.cells_n++] = q;
  } else if (q > out.cells[out.cells_n - 1]) {
    out.cells[out.cells_n - 1] = q;
  }
  out.in_cell = static_cast<uint16_t>(out.in_cell + 1 == out.per_cell ? 0 : out.in_cell + 1);
  return out;
}

// Every star added since the last block, on block `at`; the oldest go when there are more.
LevelHistory star_push(const LevelHistory& h, uint8_t stars, uint32_t at) {
  LevelHistory out = h;
  for (uint8_t s = h.stars_seen; s < stars; ++s) {
    if (out.stars_n == kLineStars) {
      std::memmove(out.star_block, out.star_block + 1, (kLineStars - 1) * sizeof out.star_block[0]);
      --out.stars_n;
    }
    out.star_block[out.stars_n++] = at;
  }
  out.stars_seen = stars;
  return out;
}

// A block at kVoiceOn starts a voice; kVoiceQuiet in a row at or below kVoiceHold end it.
LevelHistory voice_push(const LevelHistory& h, uint8_t q) {
  LevelHistory out = h;
  out.quiet = static_cast<uint8_t>(h.voice && q <= kVoiceHold ? h.quiet + 1 : 0);
  out.voice = q >= kVoiceOn || (h.voice && out.quiet < kVoiceQuiet);
  if (!out.voice) out.quiet = 0;
  return out;
}

}  // namespace

uint8_t level_q(uint16_t rms) {
  if (rms == 0) return 0;
  const float db = 20.0f * std::log10(static_cast<float>(rms) / 32768.0f);
  const float f = (db - kLevelFloorDb) / (kLevelFullDb - kLevelFloorDb);
  if (f <= 0.0f) return 0;
  if (f >= 1.0f) return 255;
  return static_cast<uint8_t>(255.0f * f + 0.5f);
}

// The first block is the codec settling as the microphone starts, not a voice: silence.
LevelHistory level_push(const LevelHistory& h, uint16_t rms, uint8_t stars) {
  const uint8_t q = h.blocks == 0 ? 0 : level_q(rms);
  LevelHistory out = voice_push(star_push(line_push(wave_push(h, q), q), stars, h.blocks), q);
  out.blocks = h.blocks + 1;
  return out;
}

bool level_voice(const LevelHistory& h) { return h.voice; }

int16_t level_half(uint8_t q, int16_t max_half) {
  const int16_t half = static_cast<int16_t>((q * max_half + 127) / 255);
  return half > 0 ? half : 1;
}

uint8_t star_cell(const LevelHistory& h, uint8_t i) {
  return static_cast<uint8_t>(h.star_block[i] / h.per_cell);
}

bool same_levels(const LevelHistory& a, const LevelHistory& b) { return a.blocks == b.blocks && same_line(a, b); }

bool same_line(const LevelHistory& a, const LevelHistory& b) {
  return a.cells_n == b.cells_n && a.per_cell == b.per_cell && a.stars_n == b.stars_n &&
         std::memcmp(a.cells, b.cells, kLineCells) == 0 &&
         std::memcmp(a.star_block, b.star_block, sizeof a.star_block) == 0;
}

}  // namespace theme
}  // namespace cpa
