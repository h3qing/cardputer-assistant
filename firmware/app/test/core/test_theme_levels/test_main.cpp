// The level history (docs/19 §3.5, §6.6-§6.7, §7.1): every audio block's RMS as a 0-255 level
// on a dB scale from -60 dBFS (the room: a dot) to -24 dBFS (full height), the recording's
// first block as silence (the codec settling), and the voice Mando nods to, with hysteresis.
// The memo's waveform keeps the last 35 blocks, newest last; the meeting's timeline keeps the
// whole meeting in 94 cells, two merging into one (the louder) whenever it is full, and the
// stars sit on the cells of the blocks they were added in, through merges.
#include <unity.h>

#include <vector>

#include "support/memo_levels.h"
#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core
#include "theme_levels.h"

using namespace cpa;
using namespace cpa::theme;

void setUp() {}
void tearDown() {}

namespace {

LevelHistory pushed(LevelHistory h, uint16_t rms, uint32_t n, uint8_t stars = 0) {
  for (uint32_t i = 0; i < n; ++i) h = level_push(h, rms, stars);
  return h;
}

// A recording under way: its first block (the codec settling) is behind it.
LevelHistory started() { return level_push(LevelHistory{}, 600, 0); }

struct Row {
  uint16_t rms;
  uint8_t q;
  int16_t half;  // of the memo's 11
};

}  // namespace

// docs/19 §6.6: 36 dB over the 11 steps, about 3.3 dB a step. The room stays a dot, a word
// is a mid-to-high bar, and louder speech than the memos' still has room above it.
static void test_levels_are_db_from_the_room_to_loud_speech() {
  static const Row kRows[] = {
      {0, 0, 1},       {16, 0, 1},      // the room between words (RMS 9-34)
      {33, 0, 1},      {52, 28, 1},     // -60 dBFS: empty; -56: where a voice ends
      {104, 71, 3},    {224, 118, 5},   // -50: where a voice starts; the memos' 5th percentile
      {454, 162, 7},   {847, 200, 9},   // their median (-37 dBFS) and 95th percentile
      {1083, 215, 9},  {2068, 255, 11},  // their loudest block; -24 dBFS: full height
      {8192, 255, 11}, {65535, 255, 11},
  };
  for (const Row& r : kRows) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(r.q, level_q(r.rms), "q");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(r.half, level_half(level_q(r.rms), 11), "half");
  }
  uint8_t last = 0;
  for (uint32_t rms = 0; rms <= 0xFFFF; ++rms) {  // louder is never lower
    TEST_ASSERT_TRUE(level_q(static_cast<uint16_t>(rms)) >= last);
    last = level_q(static_cast<uint16_t>(rms));
  }
  TEST_ASSERT_EQUAL_INT16(8, level_half(255, 8));  // the meeting's timeline
  TEST_ASSERT_EQUAL_INT16(5, level_half(level_q(454), 8));
}

// The first block of every recording is the codec settling as the microphone starts (575-600
// RMS in all four memos): drawn as a dot, and no voice.
static void test_the_first_block_is_silence() {
  LevelHistory h = started();
  TEST_ASSERT_EQUAL_UINT8(1, h.wave_n);
  TEST_ASSERT_EQUAL_UINT8(0, h.wave[0]);
  TEST_ASSERT_EQUAL_UINT8(0, h.cells[0]);
  TEST_ASSERT_FALSE(level_voice(h));
  h = level_push(h, 600, 0);
  TEST_ASSERT_EQUAL_UINT8(level_q(600), h.wave[1]);
  TEST_ASSERT_TRUE(level_voice(h));
}

// docs/19 §7.1: a block at -50 dBFS or louder starts a voice, one above -56 keeps it, and two
// blocks in a row at or below that (256 ms) end it, so the pauses between words keep him nodding.
static void test_a_voice_starts_loud_and_ends_after_two_quiet_blocks() {
  static const struct {
    uint16_t rms;
    bool voice;
  } kSteps[] = {
      {16, false},  {103, false},  // the room; just under -50 dBFS
      {60, false},                 // between the two: does not start one
      {104, true},                 // -50 dBFS
      {60, true},                  // above -56 keeps it
      {52, true},  {454, true},    // one quiet block, and the next word
      {16, true},  {16, false},    // two quiet blocks: 256 ms
      {53, false},
  };
  TEST_ASSERT_FALSE(level_voice(LevelHistory{}));
  LevelHistory h = started();
  TEST_ASSERT_FALSE(level_voice(h));
  for (const auto& s : kSteps) {
    h = level_push(h, s.rms, 0);
    TEST_ASSERT_EQUAL_MESSAGE(s.voice, level_voice(h), "voice");
  }
}

// A tap on the case or a thump on the table is one loud block: he nods for it, then stops.
static void test_a_thump_does_not_latch_the_voice() {
  LevelHistory h = pushed(started(), 16, 5);
  h = level_push(h, 3000, 0);
  TEST_ASSERT_TRUE(level_voice(h));
  TEST_ASSERT_EQUAL_UINT8(255, h.wave[h.wave_n - 1]);
  h = pushed(h, 16, 2);
  TEST_ASSERT_FALSE(level_voice(h));
  h = pushed(h, 30, 20);
  TEST_ASSERT_FALSE(level_voice(h));
}

// The user's four memos (support/memo_levels.h) at the levels the high-pass gives them (the
// words and the room told apart by the RMS as recorded): every word is a mid-to-high bar and a
// voice, the room is a dot and, from its second block on, no voice; the bars move from block to
// block, and the voice comes and goes 14 times, once at each end of each stretch of talk.
static void test_the_memos_move_the_bars_and_mando() {
  std::vector<int16_t> words;
  int moved = 0, pairs = 0, switches = 0;
  for (const memo_levels::Memo& m : memo_levels::kMemos) {
    LevelHistory h;
    int16_t was = 0;
    for (uint16_t i = 0; i < m.n; ++i) {
      const bool voiced = level_voice(h);
      h = level_push(h, m.level[i], 0);
      switches += level_voice(h) != voiced ? 1 : 0;
      const int16_t half = level_half(h.wave[h.wave_n - 1], 11);
      const bool word = i > 0 && m.rms[i] >= memo_levels::kSpeech;
      const bool room = i == 0 || m.rms[i] < memo_levels::kRoom;
      if (word) {
        words.push_back(half);
        TEST_ASSERT_TRUE_MESSAGE(half >= 4, "a word is a bar");
        TEST_ASSERT_TRUE_MESSAGE(level_voice(h), "a word is a voice");
      }
      if (room) TEST_ASSERT_EQUAL_INT16_MESSAGE(1, half, "the room is a dot");
      if (room && (i == 0 || m.rms[i - 1] < memo_levels::kRoom)) TEST_ASSERT_FALSE_MESSAGE(level_voice(h), "quiet");
      if (word && i > 1 && m.rms[i - 1] >= memo_levels::kSpeech) {
        ++pairs;
        moved += half > was ? half - was : was - half;
      }
      was = half;
    }
  }
  TEST_ASSERT_EQUAL(180, words.size());
  TEST_ASSERT_EQUAL(14, switches);
  std::vector<int> n(12, 0);
  for (const int16_t w : words) ++n[w];
  int below = 0, median = 0;
  for (int k = 0; k < 12 && below * 2 < static_cast<int>(words.size()); ++k) below += n[median = k];
  TEST_ASSERT_EQUAL(7, median);
  TEST_ASSERT_EQUAL(0, n[10] + n[11]);  // headroom: louder speech still grows
  TEST_ASSERT_TRUE(n[9] > 0);
  TEST_ASSERT_TRUE(moved > pairs);  // more than a step between neighbouring words, on average
}

static void test_the_wave_keeps_the_last_35_newest_last() {
  LevelHistory h = pushed(LevelHistory{}, 0, 3);
  TEST_ASSERT_EQUAL_UINT8(3, h.wave_n);
  h = level_push(h, 8192, 0);
  TEST_ASSERT_EQUAL_UINT8(255, h.wave[h.wave_n - 1]);
  h = pushed(h, 454, kWaveBars - 1);
  TEST_ASSERT_EQUAL_UINT8(kWaveBars, h.wave_n);
  TEST_ASSERT_EQUAL_UINT8(255, h.wave[0]);
  h = level_push(h, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(kWaveBars, h.wave_n);
  TEST_ASSERT_EQUAL_UINT8(level_q(454), h.wave[0]);
  TEST_ASSERT_EQUAL_UINT8(0, h.wave[kWaveBars - 1]);
  TEST_ASSERT_EQUAL_UINT32(kWaveBars + 4, h.blocks);
  TEST_ASSERT_TRUE(same_levels(h, h));
  TEST_ASSERT_FALSE(same_levels(h, level_push(h, 0, 0)));
}

static void test_the_line_merges_pairs_when_full() {
  LevelHistory h = pushed(LevelHistory{}, 0, kLineCells);
  TEST_ASSERT_EQUAL_UINT8(kLineCells, h.cells_n);
  TEST_ASSERT_EQUAL_UINT16(1, h.per_cell);
  h = level_push(h, 454, 0);  // block 94: no room, two become one
  TEST_ASSERT_EQUAL_UINT16(2, h.per_cell);
  TEST_ASSERT_EQUAL_UINT8(kLineCells / 2 + 1, h.cells_n);
  TEST_ASSERT_EQUAL_UINT8(level_q(454), h.cells[kLineCells / 2]);
  const LevelHistory before = h;
  h = level_push(h, 0, 0);  // block 95 shares the cell, which keeps the louder
  TEST_ASSERT_TRUE(same_line(before, h));  // the timeline has nothing new to draw
  TEST_ASSERT_FALSE(same_levels(before, h));
  TEST_ASSERT_EQUAL_UINT8(kLineCells / 2 + 1, h.cells_n);
  TEST_ASSERT_EQUAL_UINT8(level_q(454), h.cells[kLineCells / 2]);
  h = level_push(h, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(kLineCells / 2 + 2, h.cells_n);
  h = level_push(h, 1083, 0);  // block 97 shares that cell, and is the louder
  TEST_ASSERT_EQUAL_UINT8(kLineCells / 2 + 2, h.cells_n);
  TEST_ASSERT_EQUAL_UINT8(level_q(1083), h.cells[kLineCells / 2 + 1]);
}

static void test_merging_keeps_the_louder() {
  LevelHistory h = pushed(LevelHistory{}, 0, 10);
  h = level_push(h, 8192, 0);  // block 10
  h = pushed(h, 0, kLineCells - 11 + 1);
  TEST_ASSERT_EQUAL_UINT16(2, h.per_cell);
  TEST_ASSERT_EQUAL_UINT8(255, h.cells[5]);  // blocks 10 and 11
  TEST_ASSERT_EQUAL_UINT8(0, h.cells[4]);
  h = pushed(h, 0, 3 * kLineCells);  // hours later the line still spans it all
  TEST_ASSERT_TRUE(h.cells_n <= kLineCells);
  TEST_ASSERT_EQUAL_UINT8(255, h.cells[10 / h.per_cell]);
}

static void test_stars_sit_on_their_blocks_through_merges() {
  LevelHistory h = pushed(LevelHistory{}, 500, 20);
  h = level_push(h, 500, 1);  // the star came during block 20
  TEST_ASSERT_EQUAL_UINT8(1, h.stars_n);
  TEST_ASSERT_EQUAL_UINT8(20, star_cell(h, 0));
  h = pushed(h, 500, kLineCells, 1);  // no new star while the count stays
  TEST_ASSERT_EQUAL_UINT8(1, h.stars_n);
  TEST_ASSERT_EQUAL_UINT8(10, star_cell(h, 0));
  h = level_push(h, 500, 3);  // two at once
  TEST_ASSERT_EQUAL_UINT8(3, h.stars_n);
  TEST_ASSERT_EQUAL_UINT8(star_cell(h, 1), star_cell(h, 2));
  TEST_ASSERT_EQUAL_UINT8(h.cells_n - 1, star_cell(h, 2));
  for (uint8_t s = 4; s < 30; ++s) h = level_push(h, 500, s);
  TEST_ASSERT_EQUAL_UINT8(kLineStars, h.stars_n);  // the latest kLineStars
  TEST_ASSERT_EQUAL_UINT8(h.cells_n - 1, star_cell(h, kLineStars - 1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_levels_are_db_from_the_room_to_loud_speech);
  RUN_TEST(test_the_first_block_is_silence);
  RUN_TEST(test_a_voice_starts_loud_and_ends_after_two_quiet_blocks);
  RUN_TEST(test_a_thump_does_not_latch_the_voice);
  RUN_TEST(test_the_memos_move_the_bars_and_mando);
  RUN_TEST(test_the_wave_keeps_the_last_35_newest_last);
  RUN_TEST(test_the_line_merges_pairs_when_full);
  RUN_TEST(test_merging_keeps_the_louder);
  RUN_TEST(test_stars_sit_on_their_blocks_through_merges);
  return UNITY_END();
}
