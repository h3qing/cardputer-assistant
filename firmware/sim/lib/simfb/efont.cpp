/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
/----------------------------------------------------------------------------*/
// Ported from M5GFX 0.2.33 (src/lgfx/v1/lgfx_fonts.cpp, U8g2font; LGFXBase.cpp,
// text_width / draw_string; the header above is LGFXBase.cpp's, verbatim):
// Copyright (c) 2021 M5Stack (MIT) and LovyanGFX, Copyright (c) 2020 lovyan03
// (FreeBSD). The U8g2 font format is olikraus's U8g2. Licence texts: NOTICE, section 3.
#include "efont.h"

#include <algorithm>

#include "efont_cn12.h"

namespace cpasim {
namespace {

// Header bytes of a U8g2 font (M5GFX lgfx_fonts.hpp, struct U8g2font).
enum : size_t {
  kBitsPer0 = 2,
  kBitsPer1 = 3,
  kBitsW = 4,
  kBitsH = 5,
  kBitsX = 6,
  kBitsY = 7,
  kBitsDx = 8,
  kMaxW = 9,
  kMaxH = 10,
  kYOffset = 12,
  kPosUpperA = 17,
  kPosLowerA = 19,
  kPosUnicode = 21,
  kGlyphs = 23,
};

// u8g2_font_decode_t: reads bit fields LSB first across the glyph's bytes.
class Bits {
 public:
  explicit Bits(const uint8_t* p) : p_(p) {}

  unsigned get(unsigned cnt) {
    unsigned val = static_cast<unsigned>(*p_) >> pos_;
    unsigned end = pos_ + cnt;
    if (end >= 8) {
      end -= 8;
      val |= static_cast<unsigned>(*++p_) << (8 - pos_);
    }
    pos_ = end;
    return val & ((1u << cnt) - 1);
  }
  int get_signed(unsigned cnt) { return static_cast<int>(get(cnt)) - (1 << (cnt - 1)); }

 private:
  const uint8_t* p_;
  unsigned pos_ = 0;
};

unsigned be16(const uint8_t* p) { return (static_cast<unsigned>(p[0]) << 8) + p[1]; }

bool skipped(uint32_t cp) { return cp >= 0xFE00 && cp < 0xFE10; }  // variation selectors

struct Glyph {
  unsigned w = 0;
  unsigned h = 0;
  int x_offset = 0;
  int y_offset = 0;
  int advance = 0;
};

// Where the next run of the RLE bitmap lands (drawChar's lx / ly / y0 / y1).
struct Pen {
  unsigned lx = 0;
  unsigned ly = 0;
  int y0 = 0;
  int y1 = 0;
};

struct Cell {
  int x = 0;     // left edge of the bitmap (pen + x_offset)
  int y = 0;     // top of the line
  int left = 0;  // background starts here, relative to x
  int top = 0;   // first bitmap row, relative to y
  unsigned w = 0;
};

// `length` pixels of one colour, wrapping at the glyph's width (drawChar's inner loop).
void rle_run(Frame& f, const Cell& c, Pen& p, unsigned length, bool ink_bit, const Ink& ink) {
  while (length) {
    const unsigned len = std::min(length, c.w - p.lx);
    length -= len;
    if (ink_bit || ink.fill_bg) {
      int x0 = static_cast<int>(p.lx);
      if (!ink_bit && x0 < c.left) x0 = c.left;
      const int x1 = static_cast<int>(p.lx + len);
      if (x0 < x1) f.fill_rect(c.x + x0, c.y + p.y0, x1 - x0, p.y1 - p.y0, ink_bit ? ink.fg : ink.bg);
    }
    p.lx += len;
    if (p.lx == c.w) {
      p.lx = 0;
      ++p.ly;
      p.y0 = p.y1;
      p.y1 = static_cast<int>(p.ly) + c.top + 1;
    }
  }
}

// The cell's background around the bitmap: above, below, left and right of it.
void fill_around(Frame& f, const Cell& c, int right, unsigned h, int height, uint8_t bg) {
  const int left = c.x + c.left;
  if (c.top > 0) f.fill_rect(left, c.y, right - left, c.top, bg);
  const int below = c.top + static_cast<int>(h);
  if (below < height) f.fill_rect(left, c.y + below, right - left, height - below, bg);
  if (c.w == 0) return;
  if (left < c.x) f.fill_rect(left, c.y + c.top, c.x - left, static_cast<int>(h), bg);
  const int xw = c.x + static_cast<int>(c.w);
  if (xw < right) f.fill_rect(xw, c.y + c.top, right - xw, static_cast<int>(h), bg);
}

}  // namespace

int U8g2Font::height() const { return static_cast<int8_t>(byte(kMaxH)); }

int U8g2Font::baseline() const { return height() + static_cast<int8_t>(byte(kYOffset)); }

// U8g2font::getGlyph: ASCII from the lists at 'A' / 'a', the rest through the unicode LUT.
const uint8_t* U8g2Font::glyph(uint32_t cp) const {
  const uint8_t* font = f_ + kGlyphs;
  if (cp <= 255) {
    if (cp >= 'a') {
      font += be16(f_ + kPosLowerA);
    } else if (cp >= 'A') {
      font += be16(f_ + kPosUpperA);
    }
    for (; font[1]; font += font[1]) {
      if (font[0] == cp) return font + 2;
    }
    return nullptr;
  }
  if (cp > 0xFFFF) return nullptr;
  font += be16(f_ + kPosUnicode);
  const uint8_t* lut = font;
  unsigned e = 0;
  do {
    font += be16(lut);
    e = be16(lut + 2);
    lut += 4;
  } while (e < cp);
  for (; (e = be16(font)) != 0; font += font[2]) {
    if (e == cp) return font + 3;
  }
  return nullptr;
}

bool U8g2Font::metric(uint32_t cp, GlyphMetric& m) const {
  const uint8_t* g = glyph(cp);
  if (!g) {
    m.width = m.x_advance = static_cast<int8_t>(byte(kMaxW));
    m.x_offset = 0;
    return false;
  }
  Bits b(g);
  m.width = static_cast<int>(b.get(byte(kBitsW)));
  b.get(byte(kBitsH));
  m.x_offset = b.get_signed(byte(kBitsX));
  b.get_signed(byte(kBitsY));
  m.x_advance = b.get_signed(byte(kBitsDx));
  return true;
}

// LGFXBase::text_width: the right edge of the last glyph's ink or advance.
int U8g2Font::run_width(const CodePoints& cps) const {
  int left = 0;
  int right = 0;
  for (const uint32_t cp : cps) {
    if (skipped(cp)) continue;
    GlyphMetric m;
    if (!metric(cp, m)) metric(0, m);
    if (left == 0 && right == 0 && m.x_offset < 0) left = right = -m.x_offset;
    right = left + std::max(m.x_advance, m.width + m.x_offset);
    left += m.x_advance;
  }
  return right;
}

int U8g2Font::ink_right(const CodePoints& cps) const {
  int left = 0;
  int right = 0;
  bool first = true;
  for (const uint32_t cp : cps) {
    if (skipped(cp)) continue;
    GlyphMetric m;
    if (!metric(cp, m)) metric(0, m);
    if (first && m.x_offset < 0) left = -m.x_offset;
    first = false;
    if (m.width > 0) right = std::max(right, left + m.x_offset + m.width);
    left += m.x_advance;
  }
  return right;
}

// LGFXBase::draw_string, top-left datum: a negative x_offset of the first glyph moves the
// run right; the background of one call is filled left to right (filled_x).
int U8g2Font::draw_run(Frame& f, int x, int y, const CodePoints& cps, const Ink& ink) const {
  int sum = 0;
  for (const uint32_t cp : cps) {
    if (skipped(cp)) continue;
    GlyphMetric m;
    metric(cp, m);
    if (m.x_offset < 0) sum = -m.x_offset;
    break;
  }
  int filled_x = 0;
  for (const uint32_t cp : cps) {
    if (!skipped(cp)) sum += draw_glyph(f, x + sum, y, cp, ink, filled_x);
  }
  return sum;
}

// U8g2font::drawChar at size 1. Only glyphs the font has are drawn (FbCanvas draws □
// for the others, as hal/m5_display.cpp does).
int U8g2Font::draw_glyph(Frame& f, int x, int y, uint32_t cp, const Ink& ink, int& filled_x) const {
  const uint8_t* data = glyph(cp);
  if (!data) return static_cast<int8_t>(byte(kMaxW));
  Bits b(data);
  Glyph g;
  g.w = b.get(byte(kBitsW));
  g.h = b.get(byte(kBitsH));
  g.x_offset = b.get_signed(byte(kBitsX));
  g.y_offset = b.get_signed(byte(kBitsY));
  g.advance = b.get_signed(byte(kBitsDx));
  Cell c;
  c.y = y;
  c.w = g.w;
  c.top = baseline() - g.y_offset - static_cast<int>(g.h);
  int left = 0;
  int right = 0;
  if (ink.fill_bg) {
    left = std::max(filled_x, x + std::min(g.x_offset, 0));
    right = x + std::max(static_cast<int>(g.w) + g.x_offset, g.advance);
    filled_x = right;
  }
  c.x = x + g.x_offset;
  c.left = left - c.x;
  if (left < right) fill_around(f, c, right, g.h, height(), ink.bg);
  if (g.w == 0) return g.advance;
  Pen p{0, 0, c.top, c.top + 1};
  do {
    const unsigned zeros = b.get(byte(kBitsPer0));
    const unsigned ones = b.get(byte(kBitsPer1));
    do {
      rle_run(f, c, p, zeros, false, ink);
      rle_run(f, c, p, ones, true, ink);
    } while (b.get(1) != 0);
  } while (p.ly < g.h);
  return g.advance;
}

const U8g2Font& efont_cn12() {
  static const U8g2Font font(lgfx_efont_cn_12);
  return font;
}

}  // namespace cpasim
