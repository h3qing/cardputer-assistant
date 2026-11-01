// Loop timing for debug.state.diag (gh #21, protocol README item 21, test firmware), pure.
// The device's loop() adds every pass: its start and end in micros() and millis() at the
// end. Kept: passes, the longest pass and the longest start-to-start gap since boot, counts
// of passes over 20 / 50 / 100 / 500 ms, and both maxima over a window of two
// kLoopWindowMs buckets (the last 15-30 s). micros() may wrap: every difference is modulo
// 2^32, and n wraps too.
#pragma once

#include <cstdint>

namespace cpa {

constexpr uint32_t kLoopWindowMs = 15000;

struct LoopStats {
  uint32_t n = 0;
  uint32_t max_us = 0;
  uint32_t gap_max_us = 0;
  uint32_t over_20 = 0;
  uint32_t over_50 = 0;
  uint32_t over_100 = 0;
  uint32_t over_500 = 0;
  uint32_t win_us = 0;        // the current bucket's longest pass
  uint32_t win_prev_us = 0;   // the previous bucket's
  uint32_t gap_win_us = 0;
  uint32_t gap_win_prev_us = 0;
  uint32_t win_start_ms = 0;  // the current bucket began
  uint32_t last_start_us = 0;
};

LoopStats loop_stats_add(const LoopStats& s, uint32_t start_us, uint32_t end_us, uint32_t now_ms);
uint32_t loop_win_max_us(const LoopStats& s);
uint32_t gap_win_max_us(const LoopStats& s);

}  // namespace cpa
