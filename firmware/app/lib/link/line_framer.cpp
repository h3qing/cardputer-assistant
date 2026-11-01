#include "line_framer.h"

namespace cpa {
namespace {

constexpr uint32_t kContentMax = limits::kLineMax - 1;  // + '\n' = line_max

LineFramer next_line(const LineFramer& f) {
  LineFramer out = f;
  out.len = 0;
  out.seen = 0;
  out.started = false;
  out.noise = false;
  out.complete = false;
  return out;
}

// Ends the current line at '\n' and classifies it.
FrameStep finish(const LineFramer& f, size_t consumed) {
  FrameStep s;
  s.consumed = consumed;
  if (f.seen > kContentMax) {
    s.framer = next_line(f);
    s.framer.too_large_n = f.too_large_n + 1;
    s.event = FrameEvent::TooLarge;
  } else if (!f.started || f.noise) {
    s.framer = next_line(f);
    s.framer.noise_n = f.noise_n + 1;
    s.event = FrameEvent::Noise;
  } else {
    s.framer = f;
    s.framer.complete = true;
    s.framer.lines_n = f.lines_n + 1;
    s.event = FrameEvent::Line;
  }
  return s;
}

}  // namespace

FrameStep framer_scan(const LineFramer& f, const char* data, size_t n) {
  LineFramer cur = f.complete ? next_line(f) : f;
  for (size_t i = 0; i < n; ++i) {
    const char c = data[i];
    if (c == '\r') continue;
    if (c == '\n') return finish(cur, i + 1);
    if (!cur.started) {
      cur.started = true;
      cur.noise = c != '{';
    }
    if (!cur.noise && cur.seen < kContentMax) cur.buf[cur.len++] = c;
    ++cur.seen;
  }
  FrameStep s;
  s.framer = cur;
  s.consumed = n;
  return s;
}

const char* framer_line(const LineFramer& f) { return f.buf; }
size_t framer_line_size(const LineFramer& f) { return f.complete ? f.len : 0; }

}  // namespace cpa
