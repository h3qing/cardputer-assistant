// Text helpers shared by the shell, the modules and the canvases: UTF-8 stepping and
// fitting a string into a pixel width on code point boundaries (05 §5.12: the device
// only ever cuts between characters, never inside one).
#pragma once

#include <cstddef>
#include <cstdint>

#include "canvas.h"
#include "domain.h"
#include "fixed_str.h"

namespace cpa {

struct Utf8Char {
  uint32_t cp = 0;  // U+FFFD for a byte that does not start a valid sequence
  uint8_t len = 0;  // bytes consumed (>= 1 while i < n)
};

// The code point starting at s[i] (i < n).
Utf8Char utf8_at(const char* s, size_t n, size_t i);

// Bytes of the longest prefix of s[0..n) whose width is <= max_w.
size_t utf8_fit(const Canvas& c, const char* s, size_t n, int16_t max_w);

using TextLine = FixedStr<160>;

// s cut to max_w pixels; when cut, the last character is replaced by "…".
TextLine fit_text(const Canvas& c, const char* s, int16_t max_w);
// s cut from the front to max_w pixels: "…" and its end (docs/20 U14: names that start alike).
TextLine fit_tail(const Canvas& c, const char* s, int16_t max_w);

// Draws s at (x, y) cut to max_w pixels; returns the width drawn.
int16_t draw_fit(Canvas& c, int16_t x, int16_t y, int16_t max_w, const char* s, const Style& st);

// Draws s right-aligned so that it ends at x_end; returns its left edge.
int16_t draw_right(Canvas& c, int16_t x_end, int16_t y, const char* s, const Style& st);

// docs/20 U22: an age in seconds, in one Chinese form and rounded: "9 秒", "40 分钟",
// "2 小时" (1 h 59 min), "2 天" (47 h). tight: no space ("40分钟"), for a list row's right
// column, which is then no wider than v0.4.0's "40m bg" and leaves the name its room.
// In English (docs/21 §6.5) abbreviated: "9 s", "40 min", "2 h", "2 d"; tight "40m".
FixedStr<16> age_text(uint32_t age_s, bool tight = false);
// docs/20 U19: away from the Mac, how old the picture on the screen is:
// "Offline · as of 9 min ago".
FixedStr<48> stale_text(uint32_t age_s);
// docs/20 U22: a session's kind as the screens name it (not tty / bg).
const char* session_kind_text(SessionKind k);
// docs/20 U7, U19: a list of sessions' hint. Enter replies only where the reply reaches the
// session (reply_reaches), else it is a note for the Mac; offline Opt+D is refused, so it is
// left out; an empty list has neither.
const char* reply_hint(bool online, bool reaches, bool empty);
// docs/20 U12: Enter or Opt+D once the selected row left its list; nothing is selected
// until the cursor moves, and the hint says so.
const char* selection_gone_text();

}  // namespace cpa
