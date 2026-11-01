// The big digits (docs/19 §2.3-§2.4): Nunito ExtraBold in tabular cells (a running timer never
// shifts), 0-9 and ':' only, six levels drawn through the baked ramps of Fg on Bg and on
// Sheet, and on any other pair the ink from level 3 up, never a wrong colour.
#include <unity.h>

#include <string>
#include <vector>

#include "raster_canvas.h"
#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core
#include "theme_digits.h"
#include "theme_palette.h"

using namespace cpa;
using namespace cpa::theme;

void setUp() {}
void tearDown() {}

namespace {

struct Buf {
  std::vector<uint8_t> px;
  Raster r;
  Buf(int16_t w, int16_t h, uint8_t fill) : px(static_cast<size_t>(w) * h, fill), r{px.data(), w, h} {}
  uint8_t at(int x, int y) const { return px[static_cast<size_t>(y) * r.w + x]; }
};

class TestCanvas : public RasterCanvas {
 public:
  explicit TestCanvas(Buf& b) : b_(b) {}
  void text(int16_t, int16_t, const char*, const Style&) override {}
  int16_t textWidth(const char* s) const override { return static_cast<int16_t>(6 * std::string(s).size()); }
  int16_t lineHeight() const override { return 12; }

 protected:
  Raster surface() const override { return b_.r; }

 private:
  Buf& b_;
};

uint8_t level_at(const GlyphView& g, int x, int y) {
  const int i = y * g.w + x;
  const uint8_t b = g.px[i / 2];
  return (i & 1) ? (b & 15) : (b >> 4);
}

}  // namespace

static void test_cells_are_tabular() {
  TEST_ASSERT_EQUAL(18 * 4 + 7, digits_width("12:48", DigitFont::Timer));
  TEST_ASSERT_EQUAL(18 * 4 + 7, digits_width("00:00", DigitFont::Timer));
  TEST_ASSERT_EQUAL(20 * 4 + 9, digits_width("09:41", DigitFont::Clock));
  TEST_ASSERT_EQUAL(18 * 3 + 7, digits_width("0:07", DigitFont::Timer));
  TEST_ASSERT_EQUAL(21, digits_height(DigitFont::Timer));
  TEST_ASSERT_EQUAL(24, digits_height(DigitFont::Clock));
}

static void test_only_digits_and_colons() {
  TEST_ASSERT_EQUAL(digits_width("12:48", DigitFont::Timer), digits_width("1a2 :4-8", DigitFont::Timer));
  TEST_ASSERT_EQUAL(0, digits_width("", DigitFont::Clock));
  TEST_ASSERT_EQUAL(0, digit_glyph(DigitFont::Timer, 'x').w);
  TEST_ASSERT_EQUAL(18, digit_glyph(DigitFont::Timer, '7').w);
  TEST_ASSERT_EQUAL(9, digit_glyph(DigitFont::Clock, ':').w);
}

static void test_glyphs_hold_every_level() {
  for (DigitFont f : {DigitFont::Timer, DigitFont::Clock}) {
    for (char ch : std::string("0123456789:")) {
      const GlyphView g = digit_glyph(f, ch);
      uint8_t top = 0;
      int ink = 0;
      for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
          const uint8_t v = level_at(g, x, y);
          TEST_ASSERT_TRUE(v <= 5);
          top = v > top ? v : top;
          ink += v > 0 ? 1 : 0;
        }
      }
      TEST_ASSERT_EQUAL(5, top);
      TEST_ASSERT_TRUE(ink > 20);
    }
  }
}

static void test_the_baked_pairs_use_their_ramps() {
  const DigitInk bg = digit_ink(Color::Fg, Color::Bg);
  const DigitInk sheet = digit_ink(Color::Fg, Color::Sheet);
  TEST_ASSERT_EQUAL_HEX8(0, bg.entry[0]);
  TEST_ASSERT_EQUAL_HEX8(index_of(Color::Fg).value(), bg.entry[5]);
  for (uint8_t k = 1; k < 4; ++k) {
    TEST_ASSERT_EQUAL_HEX8(bg.entry[k] + 1, bg.entry[k + 1]);  // consecutive, as a VLW ramp
    TEST_ASSERT_EQUAL_HEX8(sheet.entry[k] + 1, sheet.entry[k + 1]);
  }
  TEST_ASSERT_TRUE(bg.entry[1] != sheet.entry[1]);
  for (uint8_t k = 1; k < 5; ++k) {
    TEST_ASSERT_FALSE(is_sentinel(bg.entry[k]));
    TEST_ASSERT_TRUE(rgb888_of(bg.entry[k]) != 0xFF00FF && rgb888_of(sheet.entry[k]) != 0xFF00FF);
  }
}

static void test_other_pairs_draw_the_ink_from_level_three() {
  const DigitInk d = digit_ink(Color::Accent, Color::Card);
  const uint8_t accent = index_of(Color::Accent).value();
  TEST_ASSERT_EQUAL_HEX8(0, d.entry[1]);
  TEST_ASSERT_EQUAL_HEX8(0, d.entry[2]);
  TEST_ASSERT_EQUAL_HEX8(accent, d.entry[3]);
  TEST_ASSERT_EQUAL_HEX8(accent, d.entry[4]);
  TEST_ASSERT_EQUAL_HEX8(accent, d.entry[5]);
}

static void test_canvas_draws_cells_side_by_side() {
  const uint8_t bg = index_of(Color::Bg).value();
  Buf b(120, 30, bg);
  TestCanvas c(b);
  TEST_ASSERT_EQUAL(18 * 3 + 7, c.digits(2, 3, "0:07", DigitFont::Timer, Color::Fg, Color::Bg));
  TEST_ASSERT_EQUAL(61, c.digitsWidth("0:07", DigitFont::Timer));
  const DigitInk ink = digit_ink(Color::Fg, Color::Bg);
  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 120; ++x) {
      const uint8_t v = b.at(x, y);
      const bool inside = x >= 2 && x < 2 + 61 && y >= 3 && y < 3 + 21;
      if (!inside) TEST_ASSERT_EQUAL_HEX8(bg, v);
      bool known = v == bg;
      for (uint8_t k = 1; k < 6; ++k) known = known || v == ink.entry[k];
      TEST_ASSERT_TRUE(known);
    }
  }
  // the second digit's cell starts after the colon: its glyph lands where the 7 is
  const GlyphView seven = digit_glyph(DigitFont::Timer, '7');
  for (int y = 0; y < seven.h; ++y) {
    for (int x = 0; x < seven.w; ++x) {
      const uint8_t lv = level_at(seven, x, y);
      if (lv == 5) TEST_ASSERT_EQUAL_HEX8(ink.entry[5], b.at(2 + 18 + 7 + 18 + x, 3 + y));
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cells_are_tabular);
  RUN_TEST(test_only_digits_and_colons);
  RUN_TEST(test_glyphs_hold_every_level);
  RUN_TEST(test_the_baked_pairs_use_their_ramps);
  RUN_TEST(test_other_pairs_draw_the_ink_from_level_three);
  RUN_TEST(test_canvas_draws_cells_side_by_side);
  return UNITY_END();
}
