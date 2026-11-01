// efontCN_12 on the host: the U8g2 decoder ported from M5GFX 0.2.33 and FbCanvas, the
// Canvas that mirrors hal/m5_display.cpp (runs, □ for missing glyphs, background cells).
// efontCN_12 is "biwidth": □ ● … ← are 6 px wide, CJK 12 px, ASCII 6 px.
// The oracle tests compare with what M5GFX's own sprite drew (oracle_golden.h, written by
// firmware/sim/oracle/oracle.sh --golden), so a decoder change that moves a pixel fails.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "efont.h"
#include "fb.h"
#include "fb_canvas.h"
#include "oracle_cases.h"
#include "oracle_golden.h"

using namespace cpasim;
using cpa::Color;
using cpa::Style;

void setUp() {}
void tearDown() {}

static int advance(uint32_t cp) {
  GlyphMetric m;
  return efont_cn12().metric(cp, m) ? m.x_advance : -1;
}

static void test_header_metrics() {
  TEST_ASSERT_EQUAL(12, efont_cn12().height());
  TEST_ASSERT_EQUAL(10, efont_cn12().baseline());
}

static void test_glyph_advances() {
  TEST_ASSERT_EQUAL(6, advance('A'));
  TEST_ASSERT_EQUAL(6, advance(' '));
  TEST_ASSERT_EQUAL(12, advance(0x52A9));  // 助
  TEST_ASSERT_EQUAL(6, advance(0x25A1));   // □
  TEST_ASSERT_EQUAL(6, advance(0x25CF));   // ●
  TEST_ASSERT_EQUAL(6, advance(0x2026));   // …
  TEST_ASSERT_EQUAL(-1, advance(0x2713));  // ✓ is not in the font
  TEST_ASSERT_EQUAL(-1, advance(0x231B));  // ⌛ neither
}

static void test_canvas_widths() {
  Frame f(240, 135);
  const FbCanvas c(f);
  TEST_ASSERT_EQUAL(84, c.textWidth("Cardputer 助手"));
  TEST_ASSERT_EQUAL(36, c.textWidth("● 在线"));
  TEST_ASSERT_EQUAL(6, c.textWidth("\xE2\x9C\x93"));  // ✓ -> □
  TEST_ASSERT_EQUAL(0, c.textWidth(""));
  TEST_ASSERT_EQUAL(12, c.lineHeight());
}

static int count(const Frame& f, int x0, int y0, int w, int h, uint8_t px) {
  int n = 0;
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) n += f.at(x, y) == px ? 1 : 0;
  }
  return n;
}

// fg != bg: every pixel of the 6 x 12 cell is ink or background, nothing outside changes.
static void test_text_fills_the_cell_background() {
  Frame f(20, 20, 0x49);
  FbCanvas c(f);
  c.text(2, 3, "A", Style{Color::Fg, Color::Select});
  const int ink = count(f, 2, 3, 6, 12, entry_of(Color::Fg));
  const int bg = count(f, 2, 3, 6, 12, entry_of(Color::Select));
  TEST_ASSERT_TRUE(ink > 8);
  TEST_ASSERT_EQUAL(6 * 12, ink + bg);
  TEST_ASSERT_EQUAL(20 * 20 - 6 * 12, count(f, 0, 0, 20, 20, 0x49));
}

// The baseline is 10 px below the top: 'A' sits on rows 3..9 of its cell (ascent 7).
static void test_text_y_is_the_top_of_the_line() {
  Frame f(10, 14);
  FbCanvas c(f);
  c.text(0, 0, "A", Style{Color::Fg, Color::Bg});
  const uint8_t ink = entry_of(Color::Fg);
  TEST_ASSERT_EQUAL(0, count(f, 0, 0, 10, 3, ink));
  TEST_ASSERT_TRUE(count(f, 0, 3, 10, 7, ink) > 8);
  TEST_ASSERT_EQUAL(0, count(f, 0, 10, 10, 4, ink));
}

static void test_missing_glyph_is_drawn_as_box_and_counted() {
  Frame f(20, 12);
  FbCanvas c(f);
  TEST_ASSERT_EQUAL(0u, c.missing_glyphs());
  c.text(0, 0, "\xE2\x9C\x93", Style{});
  Frame box(20, 12);
  FbCanvas b(box);
  b.text(0, 0, "\xE2\x96\xA1", Style{});
  TEST_ASSERT_TRUE(f.pixels() == box.pixels());
  TEST_ASSERT_TRUE(count(f, 0, 0, 6, 12, entry_of(Color::Fg)) > 8);
  TEST_ASSERT_EQUAL(1u, c.missing_glyphs());
  c.textWidth("\xE2\x9C\x93");  // measuring does not count
  TEST_ASSERT_EQUAL(1u, c.missing_glyphs());
}

static void test_control_characters_are_boxes() {
  Frame f(20, 12);
  const FbCanvas c(f);
  TEST_ASSERT_EQUAL(12, c.textWidth("a\tb") - 6);
}

static void test_text_is_clipped_at_the_edge() {
  Frame f(10, 12);
  FbCanvas c(f);
  c.text(6, 0, "AB", Style{Color::Fg, Color::Select});
  const uint8_t fg = entry_of(Color::Fg), bg = entry_of(Color::Select);
  TEST_ASSERT_EQUAL(0, count(f, 0, 0, 6, 12, bg) + count(f, 0, 0, 6, 12, fg));
  TEST_ASSERT_EQUAL(4 * 12, count(f, 6, 0, 4, 12, bg) + count(f, 6, 0, 4, 12, fg));
}

static void test_fill_uses_the_palette() {
  Frame f(10, 10);
  FbCanvas c(f);
  c.fill(cpa::Rect{1, 1, 3, 2}, Color::Dim);
  TEST_ASSERT_EQUAL(6, count(f, 0, 0, 10, 10, entry_of(Color::Dim)));
}

// fg == bg draws the glyphs only (theme::over): the cell keeps what was there.
static void test_same_fg_and_bg_draws_no_cell() {
  Frame f(10, 12, entry_of(Color::Card));
  FbCanvas c(f);
  c.text(0, 0, "A", Style{Color::OnAccent, Color::OnAccent});
  const int ink = count(f, 0, 0, 6, 12, entry_of(Color::OnAccent));
  TEST_ASSERT_TRUE(ink > 8);
  TEST_ASSERT_EQUAL(10 * 12 - ink, count(f, 0, 0, 10, 12, entry_of(Color::Card)));
}

// Case i of oracle_cases.h on a black frame, as the oracle draws it; width = textWidth.
static uint64_t draw_case(Frame& f, FbCanvas& c, int i, int& width) {
  const oracle::Case& k = oracle::kCases[i];
  c.fill(cpa::Rect{0, 0, 240, 135}, Color::Bg);
  width = 0;
  if (k.fill) {
    c.fill(cpa::Rect{static_cast<int16_t>(k.x), static_cast<int16_t>(k.y), static_cast<int16_t>(k.w),
                     static_cast<int16_t>(k.h)},
           static_cast<Color>(k.fg));
  } else {
    c.text(static_cast<int16_t>(k.x), static_cast<int16_t>(k.y), k.text,
           Style{static_cast<Color>(k.fg), static_cast<Color>(k.bg)});
    width = c.textWidth(k.text);
  }
  return oracle::fnv1a(f.pixels().data(), f.pixels().size());
}

static void expect_golden(int i, uint64_t hash, int width) {
  char msg[48];
  std::snprintf(msg, sizeof msg, "oracle case %d", i);
  TEST_ASSERT_TRUE_MESSAGE(hash == oracle::kGolden[i].hash, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(oracle::kGolden[i].width, width, msg);
}

// Status-bar strings, clipping on every side, text starting left of the screen, glyphs
// the font lacks (✓, an emoji above U+FFFF), bad UTF-8, the 96-byte run split, fills.
static void test_every_oracle_case_matches_m5gfx() {
  for (int i = 0; i < oracle::kCaseCount; ++i) {
    Frame f(240, 135);
    FbCanvas c(f);
    int width = 0;
    const uint64_t hash = draw_case(f, c, i, width);
    expect_golden(i, hash, width);
  }
}

// One frame and canvas for all of them, as on the device: nothing carries over.
static void test_a_shared_canvas_keeps_no_state() {
  Frame f(240, 135);
  FbCanvas c(f);
  for (int i = 0; i < oracle::kCaseCount; ++i) {
    int width = 0;
    const uint64_t hash = draw_case(f, c, i, width);
    expect_golden(i, hash, width);
  }
}

// The bitmaps of single glyphs, row by row, as M5GFX drew them ('#' ink).
static void test_glyph_rows_match_m5gfx() {
  for (int i = 0; i < oracle::kGlyphCount; ++i) {
    Frame f(12, oracle::kGlyphRows);
    FbCanvas c(f);
    c.text(0, 0, oracle::kGlyphs[i], Style{Color::Fg, Color::Bg});
    for (int y = 0; y < oracle::kGlyphRows; ++y) {
      const std::string want = oracle::kGlyphRowsGolden[i][y];
      std::string got;
      for (int x = 0; x < static_cast<int>(want.size()); ++x) got += f.at(x, y) == entry_of(Color::Fg) ? '#' : '.';
      TEST_ASSERT_EQUAL_STRING_MESSAGE(want.c_str(), got.c_str(), oracle::kGlyphs[i]);
    }
    TEST_ASSERT_EQUAL(static_cast<int>(std::strlen(oracle::kGlyphRowsGolden[i][0])), c.textWidth(oracle::kGlyphs[i]));
  }
}

// U+1F600 has no glyph (the font stops at U+FFFF): one □, counted.
static void test_code_points_above_ffff_are_boxes() {
  Frame f(12, 12);
  FbCanvas c(f);
  c.text(0, 0, "\xF0\x9F\x98\x80", Style{});
  Frame box(12, 12);
  FbCanvas(box).text(0, 0, "\xE2\x96\xA1", Style{});
  TEST_ASSERT_TRUE(f.pixels() == box.pixels());
  TEST_ASSERT_EQUAL_UINT32(1, c.missing_glyphs());
  TEST_ASSERT_EQUAL(6, c.textWidth("\xF0\x9F\x98\x80"));
}

// No glyph of efontCN_12 starts left of its pen (negative x_offset) or inks past its
// advance, so the port's branches for those (a run moved right, the background clamped
// under an overhang) cannot change a device pixel and the oracle cases cannot reach
// them. A new font pin that breaks this needs oracle cases for it.
static void test_no_glyph_overhangs() {
  int glyphs = 0;
  for (uint32_t cp = 0x20; cp <= 0xFFFF; ++cp) {
    GlyphMetric m;
    if (!efont_cn12().metric(cp, m)) continue;
    ++glyphs;
    TEST_ASSERT_TRUE(m.x_offset >= 0);
    TEST_ASSERT_TRUE(m.width + m.x_offset <= m.x_advance);
  }
  TEST_ASSERT_EQUAL(7544, glyphs);  // GB2312 and the symbols, U+0020 to U+FFFF
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header_metrics);
  RUN_TEST(test_glyph_advances);
  RUN_TEST(test_canvas_widths);
  RUN_TEST(test_text_fills_the_cell_background);
  RUN_TEST(test_text_y_is_the_top_of_the_line);
  RUN_TEST(test_missing_glyph_is_drawn_as_box_and_counted);
  RUN_TEST(test_control_characters_are_boxes);
  RUN_TEST(test_text_is_clipped_at_the_edge);
  RUN_TEST(test_fill_uses_the_palette);
  RUN_TEST(test_same_fg_and_bg_draws_no_cell);
  RUN_TEST(test_every_oracle_case_matches_m5gfx);
  RUN_TEST(test_a_shared_canvas_keeps_no_state);
  RUN_TEST(test_glyph_rows_match_m5gfx);
  RUN_TEST(test_code_points_above_ffff_are_boxes);
  RUN_TEST(test_no_glyph_overhangs);
  return UNITY_END();
}
