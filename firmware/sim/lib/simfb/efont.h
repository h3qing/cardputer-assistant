// U8g2Font: the U8g2 bitmap font decoder of M5GFX 0.2.33 (lgfx_fonts.cpp: U8g2font::
// getGlyph, updateFontMetric, drawChar) and the layout of LGFXBase::text_width /
// draw_string with the default top-left datum, text size 1 and UTF-8 on, ported to draw
// into a Frame. Adapted from M5GFX / LovyanGFX (MIT / FreeBSD licence, see NOTICE).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fb.h"

namespace cpasim {

struct GlyphMetric {
  int width = 0;
  int x_offset = 0;
  int x_advance = 0;
};

struct Ink {
  uint8_t fg = 0;
  uint8_t bg = 0;
  bool fill_bg = false;  // setTextColor(fg, bg) with fg != bg: glyph cells get bg
};

using CodePoints = std::vector<uint32_t>;

class U8g2Font {
 public:
  explicit U8g2Font(const uint8_t* data) : f_(data) {}

  int height() const;    // max_char_height: the line height
  int baseline() const;  // height + y_offset: rows from the top of the line
  // updateFontMetric: false when the font has no such glyph (m = a max-width cell).
  bool metric(uint32_t cp, GlyphMetric& m) const;
  // LGFXBase::text_width of these code points (all of which have glyphs).
  int run_width(const CodePoints& cps) const;
  // How far right of the run's start its ink reaches (one past the last lit column).
  int ink_right(const CodePoints& cps) const;
  // LGFXBase::draw_string at (x, y) = top left; returns the summed advance.
  int draw_run(Frame& f, int x, int y, const CodePoints& cps, const Ink& ink) const;

 private:
  const uint8_t* glyph(uint32_t cp) const;
  int draw_glyph(Frame& f, int x, int y, uint32_t cp, const Ink& ink, int& filled_x) const;
  uint8_t byte(size_t i) const { return f_[i]; }

  const uint8_t* f_;
};

const U8g2Font& efont_cn12();  // fonts::efontCN_12

}  // namespace cpasim
