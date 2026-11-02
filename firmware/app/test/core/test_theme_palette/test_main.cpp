// The palette (docs/19 §2): every token is its own non-sentinel entry with the colour of
// tools/theme/tokens.py; the four sentinels are magenta and catch v0.4.0's RGB565
// constants; the dark surfaces stay apart on the panel; every (text, ground) pair the
// theme draws reads at >= 4.5:1 (ink >= 7:1); edges and dimming come from real entries.
#include <unity.h>

#include <cmath>
#include <cstdio>

#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core
#include "theme_palette.h"

using namespace cpa;
using namespace cpa::theme;

void setUp() {}
void tearDown() {}

static uint8_t at(Color c) { return index_of(c).value(); }

static double channel(unsigned v8) {
  const double v = v8 / 255.0;
  return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

// WCAG relative luminance of what the panel shows (RGB565 widened to 8 bits).
static double luminance(uint8_t entry) {
  const uint16_t c = rgb565_of(entry);
  const unsigned r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
  const unsigned r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}


static void test_tokens_have_their_entries_and_colours() {
  const uint32_t want[kColorCount] = {0x120E0B, 0xF3EDE6, 0x96897B, 0xE2A867, 0x72C291, 0xDCC15A, 0xF4675B, 0x463525,
                                      0x1A1512, 0x221C18, 0x2C241E, 0x3A2F27, 0x43372D, 0x5A4E44, 0x4A3E35, 0xBFB2A6,
                                      0xC48A4C, 0x1D130B, 0x926D45, 0x272F25, 0x372F1D, 0x3A2520, 0x36291C, 0xF4675B};
  bool seen[kPaletteSize] = {};
  for (uint8_t t = 0; t < kColorCount; ++t) {
    const uint8_t e = at(static_cast<Color>(t));
    TEST_ASSERT_FALSE(is_sentinel(e));
    TEST_ASSERT_FALSE_MESSAGE(seen[e], "two tokens share an entry");
    seen[e] = true;
    TEST_ASSERT_EQUAL_HEX32(want[t], rgb888_of(e));
  }
  TEST_ASSERT_EQUAL_HEX8(0x01, at(Color::Bg));
  TEST_ASSERT_EQUAL_HEX8(0x0F, at(Color::CardRim));
  TEST_ASSERT_EQUAL_HEX8(0x11, at(Color::Ink2));
  TEST_ASSERT_TRUE(index_of(Color::Ink) == index_of(Color::Fg));
  TEST_ASSERT_TRUE(index_of(Color::Rec) != index_of(Color::Ok));
}

// A v0.4.0 constant passed as a colour would land on its low byte: always a sentinel.
static void test_old_rgb565_constants_land_on_magenta() {
  const uint16_t old[] = {0x0000, 0xFFFF, 0x8410, 0x07FF, 0x07E0, 0xFFE0, 0xF800, 0x0010};
  for (const uint16_t c : old) {
    TEST_ASSERT_TRUE(is_sentinel(static_cast<uint8_t>(c & 0xFF)));
    TEST_ASSERT_EQUAL_HEX32(0xFF00FF, rgb888_of(static_cast<uint8_t>(c & 0xFF)));
  }
  TEST_ASSERT_FALSE(is_sentinel(0x01));
  TEST_ASSERT_EQUAL_HEX16(0xF81F, rgb565_of(0x00));
}

// A value past the last token (a stray cast) is no colour: it draws magenta, never Bg.
static void test_a_colour_past_the_tokens_is_magenta() {
  for (unsigned v = kColorCount; v < 256; v += 29) {
    TEST_ASSERT_TRUE(is_sentinel(at(static_cast<Color>(v))));
    TEST_ASSERT_EQUAL_HEX32(0xFF00FF, rgb888_of(at(static_cast<Color>(v))));
  }
  TEST_ASSERT_TRUE(is_sentinel(at(static_cast<Color>(kColorCount))));
  TEST_ASSERT_TRUE(is_sentinel(at(static_cast<Color>(255))));
  TEST_ASSERT_FALSE(is_sentinel(at(static_cast<Color>(kColorCount - 1))));
}

// bg, sheet, card, raised and line: different colours on the panel, darkest first.
static void test_dark_surfaces_stay_apart_on_the_panel() {
  const Color order[] = {Color::Bg, Color::Sheet, Color::Card, Color::Raised, Color::Line, Color::Cap};
  for (size_t i = 1; i < sizeof order / sizeof order[0]; ++i) {
    TEST_ASSERT_TRUE(rgb565_of(at(order[i])) != rgb565_of(at(order[i - 1])));
    TEST_ASSERT_TRUE(luminance(at(order[i])) > luminance(at(order[i - 1])));
  }
  TEST_ASSERT_TRUE(contrast(Color::CardRim, Color::Card) > 1.5);
  TEST_ASSERT_TRUE(contrast(Color::Card, Color::Card) == 1.0);
  TEST_ASSERT_TRUE(contrast(Color::Bg, Color::Ink) == contrast(Color::Ink, Color::Bg));
  TEST_ASSERT_TRUE(contrast(Color::Rim, Color::Sheet) > 2.0);
}

struct Pair {
  Color ink;
  Color ground;
  double min;
};

// Every pair the theme draws text with (docs/19 §2.5).
static const Pair kPairs[] = {
    {Color::Ink, Color::Bg, 7}, {Color::Ink, Color::Card, 7}, {Color::Ink, Color::Select, 7},
    {Color::Ink, Color::Sheet, 7}, {Color::Ink2, Color::Bg, 4.5}, {Color::Ink2, Color::Card, 4.5},
    {Color::Ink2, Color::Select, 4.5}, {Color::Ink2, Color::Cap, 4.5}, {Color::Ink2, Color::Raised, 4.5},
    {Color::Ink2, Color::Sheet, 4.5}, {Color::Ink3, Color::Bg, 4.5}, {Color::Ink3, Color::Card, 4.5},
    {Color::Ink3, Color::Sheet, 4.5}, {Color::Accent, Color::Bg, 4.5}, {Color::Accent, Color::Card, 4.5},
    {Color::Accent, Color::Cap, 4.5}, {Color::Accent, Color::InfoBg, 4.5}, {Color::OnAccent, Color::Accent, 4.5},
    {Color::OnAccent, Color::Accent2, 4.5}, {Color::Ok, Color::Bg, 4.5}, {Color::Ok, Color::OkBg, 4.5},
    {Color::Warn, Color::Bg, 4.5}, {Color::Warn, Color::Card, 4.5}, {Color::Warn, Color::WarnBg, 4.5},
    {Color::Warn, Color::Select, 4.5}, {Color::Rec, Color::Bg, 4.5}, {Color::Rec, Color::RecBg, 4.5},
    {Color::Rec, Color::Card, 4.5}, {Color::RecDot, Color::Bg, 4.5}, {Color::Bg, Color::Warn, 4.5},
    // S2: the live-words capsule, Mando's "!" and "?" bubbles
    {Color::Accent, Color::Select, 4.5}, {Color::OnAccent, Color::Warn, 4.5}, {Color::OnAccent, Color::Ink2, 4.5},
    // docs/20 B1: the lock card's result and "Not recording" lines
    {Color::Ok, Color::Sheet, 4.5}, {Color::Warn, Color::Sheet, 4.5},
};

static void test_text_pairs_read() {
  for (const Pair& p : kPairs) {
    char what[48];
    std::snprintf(what, sizeof what, "ink %u on %u: %.2f", static_cast<unsigned>(p.ink),
                  static_cast<unsigned>(p.ground), contrast(p.ink, p.ground));
    TEST_ASSERT_TRUE_MESSAGE(contrast(p.ink, p.ground) >= p.min, what);
  }
}

static void test_edges_are_real_entries() {
  for (uint8_t p = 0; p < kColorCount; ++p) {
    for (uint8_t u = 0; u < kColorCount; ++u) {
      const uint8_t e = aa_of(static_cast<Color>(p), static_cast<Color>(u));
      TEST_ASSERT_FALSE(is_sentinel(e));
      TEST_ASSERT_TRUE(rgb888_of(e) != 0xFF00FF);
      TEST_ASSERT_EQUAL(aa_exact(static_cast<Color>(p), static_cast<Color>(u)),
                        aa_exact(static_cast<Color>(u), static_cast<Color>(p)));
    }
  }
  TEST_ASSERT_TRUE(aa_exact(Color::Card, Color::Bg));
  TEST_ASSERT_EQUAL_HEX32(0x1A1512, rgb888_of(aa_of(Color::Card, Color::Bg)));  // (#221c18 + #120e0b) / 2
  TEST_ASSERT_EQUAL_HEX8(at(Color::Ok), aa_of(Color::Ok, Color::Ok));
  TEST_ASSERT_EQUAL_HEX8(0x1A, aa_of(Color::RecDot, Color::Bg));  // RecDotEdge
  TEST_ASSERT_FALSE(aa_exact(Color::Ok, Color::Rim));
}

static void test_dimming_darkens_and_keeps_bg() {
  TEST_ASSERT_EQUAL_HEX8(at(Color::Bg), dim_of(at(Color::Bg)));
  for (uint16_t i = 0; i < kPaletteSize; ++i) {
    const uint8_t e = static_cast<uint8_t>(i);
    TEST_ASSERT_TRUE(luminance(dim_of(e)) <= luminance(e) + 1e-9);
    if (rgb888_of(e) != 0xFF00FF) TEST_ASSERT_TRUE(rgb888_of(dim_of(e)) != 0xFF00FF);
    TEST_ASSERT_EQUAL_HEX8(dim_of(e), dim_table()[e]);
  }
  TEST_ASSERT_TRUE(luminance(dim_of(at(Color::Ink))) < luminance(at(Color::Ink)) / 2);
  TEST_ASSERT_TRUE(palette888() != nullptr);
  TEST_ASSERT_EQUAL_HEX32(rgb888_of(7), palette888()[7]);
}

// S3 (docs/19 §2.3): the big ● breathes in four steps, step 0 full Rec, the darkest still
// >= 4.5:1 on Bg (the judges' rule); the status bar's ● is Rec, which never breathes.
static void test_breathing_never_gets_dark() {
  TEST_ASSERT_EQUAL_HEX8(at(Color::RecDot), breath_dot(0));
  TEST_ASSERT_EQUAL_HEX8(aa_of(Color::RecDot, Color::Bg), breath_edge(0));
  TEST_ASSERT_EQUAL_HEX8(rec_dot_edge(), breath_edge(0));
  TEST_ASSERT_EQUAL_HEX32(rgb888_of(at(Color::Rec)), rgb888_of(breath_dot(0)));
  TEST_ASSERT_EQUAL_HEX32(0xCB574C, rgb888_of(breath_dot(2)));
  for (uint8_t s = 0; s < kBreathSteps; ++s) {
    char what[32];
    std::snprintf(what, sizeof what, "step %u: %.2f", s, contrast_of(breath_dot(s), at(Color::Bg)));
    TEST_ASSERT_TRUE_MESSAGE(contrast_of(breath_dot(s), at(Color::Bg)) >= 4.5, what);
    TEST_ASSERT_FALSE(is_sentinel(breath_dot(s)) || is_sentinel(breath_edge(s)));
    TEST_ASSERT_TRUE(rgb888_of(breath_dot(s)) != 0xFF00FF && rgb888_of(breath_edge(s)) != 0xFF00FF);
  }
  TEST_ASSERT_EQUAL_HEX8(breath_dot(0), breath_dot(kBreathSteps));  // the step wraps
  TEST_ASSERT_EQUAL_HEX8(breath_dot(1), breath_dot(3));
}

static void test_only_the_big_dot_breathes() {
  TEST_ASSERT_TRUE(at(Color::Rec) != at(Color::RecDot));
  for (uint8_t p = 0; p < kColorCount; ++p) {
    for (uint8_t u = 0; u < kColorCount; ++u) {
      const uint8_t e = aa_of(static_cast<Color>(p), static_cast<Color>(u));
      for (uint8_t s = 1; s < kBreathSteps; ++s) TEST_ASSERT_TRUE(e != breath_dot(s) && e != breath_edge(s));
      const bool dot = (p == static_cast<uint8_t>(Color::RecDot) && u == 0) ||
                       (u == static_cast<uint8_t>(Color::RecDot) && p == 0);
      TEST_ASSERT_EQUAL(dot, e == rec_dot_edge());
    }
  }
  TEST_ASSERT_TRUE(aa_exact(Color::Rec, Color::Bg));       // the timeline's end dot
  TEST_ASSERT_TRUE(aa_exact(Color::Accent2, Color::Bg));   // the waveform's bar ends
  TEST_ASSERT_TRUE(aa_exact(Color::Warn, Color::Select));  // the inbox stripe on a selected row
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tokens_have_their_entries_and_colours);
  RUN_TEST(test_old_rgb565_constants_land_on_magenta);
  RUN_TEST(test_a_colour_past_the_tokens_is_magenta);
  RUN_TEST(test_dark_surfaces_stay_apart_on_the_panel);
  RUN_TEST(test_text_pairs_read);
  RUN_TEST(test_edges_are_real_entries);
  RUN_TEST(test_dimming_darkens_and_keeps_bg);
  RUN_TEST(test_breathing_never_gets_dark);
  RUN_TEST(test_only_the_big_dot_breathes);
  return UNITY_END();
}
