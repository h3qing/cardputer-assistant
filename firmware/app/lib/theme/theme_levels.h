// The recording's level history (docs/19 §3.5, §6.6-§6.7, §7.1): every audio block's RMS (one
// per 128 ms) as a 0-255 level on a dB scale, the recording's first block as silence (the codec
// settling as the microphone starts), and whether someone is talking (Mando nods). The memo's
// waveform keeps the last kWaveBars blocks, newest last; the meeting's timeline keeps the whole
// meeting in kLineCells cells: when it is full, two cells merge into one (the louder) and each
// cell spans twice the blocks. Stars sit on the blocks they were added in, so they follow the
// merges. Fed on every block whatever the backlight, so the timeline has no gaps; the pages
// draw a copy taken at the last animation tick (theme_anim.h). Pure.
#pragma once

#include <cstdint>

namespace cpa {
namespace theme {

constexpr uint8_t kWaveBars = 35;
constexpr uint8_t kLineCells = 94;
constexpr uint8_t kLineStars = 12;     // the latest stars on the timeline
// The scale (docs/19 §6.6): from the room (-60 dBFS, RMS 33: a dot) to loud speech (-24 dBFS,
// RMS 2068: full height), about 3.3 dB a step of the memo's 11. Speech held in the hand is
// -46 to -30 dBFS (the user's memos, 2026-10-22), so it moves in the middle of the bar.
constexpr float kLevelFloorDb = -60.0f;
constexpr float kLevelFullDb = -24.0f;
// Someone is talking (docs/19 §7.1), judged on the same level as the bars: a block at kVoiceOn
// or louder starts it (-50 dBFS, RMS 104, a 7 px bar), one above kVoiceHold keeps it (-56 dBFS,
// RMS 52), and kVoiceQuiet blocks in a row at or below kVoiceHold end it (256 ms), so the
// pauses between words keep him nodding and one thump does not.
constexpr uint8_t kVoiceOn = 71;
constexpr uint8_t kVoiceHold = 28;
constexpr uint8_t kVoiceQuiet = 2;

struct LevelHistory {
  uint8_t wave[kWaveBars] = {};  // oldest first: wave[0 .. wave_n)
  uint8_t wave_n = 0;
  uint8_t cells[kLineCells] = {};  // cells[0 .. cells_n)
  uint8_t cells_n = 0;
  uint16_t per_cell = 1;  // blocks a cell spans
  uint16_t in_cell = 0;   // blocks in the last cell so far; 0: the next block starts a cell
  uint32_t blocks = 0;
  uint32_t star_block[kLineStars] = {};  // oldest first
  uint8_t stars_n = 0;
  uint8_t stars_seen = 0;  // RecInfo::stars at the last block
  bool voice = false;      // someone is talking
  uint8_t quiet = 0;       // blocks in a row at or below kVoiceHold while voice
};

uint8_t level_q(uint16_t rms);  // 0 at kLevelFloorDb or below, 255 at kLevelFullDb or above

// The history after one more block; `stars` is RecInfo::stars now: every star added since
// the last block sits on this one.
LevelHistory level_push(const LevelHistory& h, uint16_t rms, uint8_t stars);

bool level_voice(const LevelHistory& h);                     // someone is talking (kVoiceOn)
int16_t level_half(uint8_t q, int16_t max_half);            // a bar's half height, 1..max_half
uint8_t star_cell(const LevelHistory& h, uint8_t i);         // the timeline cell of star i

bool same_levels(const LevelHistory& a, const LevelHistory& b);  // nothing to redraw
bool same_line(const LevelHistory& a, const LevelHistory& b);    // the timeline looks the same

}  // namespace theme
}  // namespace cpa
