// LineFramer (05 §5.8, §6.1): pure byte-stream to line splitter. Splits on '\n',
// ignores every '\r', drops lines that do not start with '{' (noise, counted) and
// lines over line_max bytes including '\n' (dropped up to the next '\n', reported as
// E_TOO_LARGE). framer_scan() consumes bytes until one event and returns a new framer;
// after a Line event, framer_line() holds the line until the next scan.
#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol_limits.h"

namespace cpa {

enum class FrameEvent : uint8_t { None, Line, TooLarge, Noise };

struct LineFramer {
  char buf[limits::kLineMax] = {};  // content without '\n' (at most line_max - 1 bytes)
  uint16_t len = 0;                 // stored content bytes
  uint32_t seen = 0;                // content bytes of the current line, stored or not
  bool started = false;             // first non-'\r' byte seen
  bool noise = false;               // current line does not start with '{'
  bool complete = false;            // buf holds a finished line
  uint32_t lines_n = 0;
  uint32_t noise_n = 0;
  uint32_t too_large_n = 0;
};

struct FrameStep {
  LineFramer framer;
  size_t consumed = 0;
  FrameEvent event = FrameEvent::None;
};

FrameStep framer_scan(const LineFramer& f, const char* data, size_t n);
const char* framer_line(const LineFramer& f);
size_t framer_line_size(const LineFramer& f);

}  // namespace cpa
