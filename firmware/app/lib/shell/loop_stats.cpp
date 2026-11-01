#include "loop_stats.h"

namespace cpa {
namespace {

uint32_t max_of(uint32_t a, uint32_t b) { return a > b ? a : b; }

// The pass's own counters; the gap needs the previous start (none before the first pass).
LoopStats with_pass(const LoopStats& s, uint32_t start_us, uint32_t dt) {
  LoopStats out = s;
  if (s.n > 0) {
    const uint32_t gap = start_us - s.last_start_us;
    out.gap_max_us = max_of(s.gap_max_us, gap);
    out.gap_win_us = max_of(s.gap_win_us, gap);
  }
  out.last_start_us = start_us;
  out.n = s.n + 1;
  out.max_us = max_of(s.max_us, dt);
  out.win_us = max_of(s.win_us, dt);
  out.over_20 = s.over_20 + (dt > 20000 ? 1 : 0);
  out.over_50 = s.over_50 + (dt > 50000 ? 1 : 0);
  out.over_100 = s.over_100 + (dt > 100000 ? 1 : 0);
  out.over_500 = s.over_500 + (dt > 500000 ? 1 : 0);
  return out;
}

}  // namespace

LoopStats loop_stats_add(const LoopStats& s, uint32_t start_us, uint32_t end_us, uint32_t now_ms) {
  LoopStats out = with_pass(s, start_us, end_us - start_us);
  if (now_ms - out.win_start_ms < kLoopWindowMs) return out;
  out.win_prev_us = out.win_us;
  out.gap_win_prev_us = out.gap_win_us;
  out.win_us = 0;
  out.gap_win_us = 0;
  out.win_start_ms = now_ms;
  return out;
}

uint32_t loop_win_max_us(const LoopStats& s) { return max_of(s.win_us, s.win_prev_us); }
uint32_t gap_win_max_us(const LoopStats& s) { return max_of(s.gap_win_us, s.gap_win_prev_us); }

}  // namespace cpa
