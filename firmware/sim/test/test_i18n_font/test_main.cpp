// The i18n width table against efontCN_12 itself (docs/21 §6.3 rule 5, §9.3): what
// txt_rules.h counts for a code point is never less than the real glyph takes, and is
// exact for ASCII and the 11 narrow symbols; the symbols it calls missing really are
// missing (they would draw as □). So a Txt that passes the build-time width check fits on
// the device.
#include <unity.h>

#include <cstdint>
#include <cstdio>

#include "efont.h"
#include "fb.h"
#include "fb_canvas.h"
#include "mando_eggs.h"
#include "txt_rules.h"

using namespace cpasim;
using cpa::i18n::cp_width;

void setUp() {}
void tearDown() {}

static int run_width(uint32_t cp) { return efont_cn12().run_width(CodePoints{cp}); }

static void test_ascii_and_the_narrow_symbols_are_exact() {
  for (uint32_t cp = 0x20; cp < 0x7F; ++cp) {
    GlyphMetric m;
    TEST_ASSERT_TRUE(efont_cn12().metric(cp, m));
    TEST_ASSERT_EQUAL_INT(cp_width(cp), m.x_advance);
  }
  for (uint32_t cp : cpa::i18n::kNarrow) {
    GlyphMetric m;
    TEST_ASSERT_TRUE_MESSAGE(efont_cn12().metric(cp, m), "a narrow symbol without a glyph");
    TEST_ASSERT_EQUAL_INT(6, m.x_advance);
    TEST_ASSERT_EQUAL_INT(6, cp_width(cp));
  }
}

static void test_no_glyph_is_ever_wider_than_counted() {
  int glyphs = 0;
  for (uint32_t cp = 0x20; cp <= 0xFFFF; ++cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) continue;
    GlyphMetric m;
    if (!efont_cn12().metric(cp, m)) continue;
    ++glyphs;
    if (m.x_advance > cp_width(cp) || run_width(cp) > cp_width(cp)) {
      char msg[48];
      std::snprintf(msg, sizeof msg, "U+%04X is wider than counted", static_cast<unsigned>(cp));
      TEST_FAIL_MESSAGE(msg);
    }
  }
  TEST_ASSERT_TRUE(glyphs > 7000);  // the whole font was walked (7544 glyphs in M5GFX 0.2.33)
}

static void test_the_missing_symbols_are_missing() {
  for (uint32_t cp : cpa::i18n::kNoGlyph) {
    GlyphMetric m;
    TEST_ASSERT_FALSE_MESSAGE(efont_cn12().metric(cp, m), "listed as missing, but the font has it");
  }
}

static void test_a_string_measures_as_the_canvas_does() {
  Frame f(240, 135);
  const FbCanvas c(f);
  const char* const kSamples[] = {"语言 Language", "无 · 只用 USB 线", "None · USB only", "↑0 录2",
                                  "English · Enter again: 中文", "Sync   3 min ago · to sync ↑0 R2"};
  for (const char* s : kSamples) TEST_ASSERT_EQUAL_INT(c.textWidth(s), cpa::i18n::text_width(s));
}

// v0.5.0 (docs/04, hidden keys): Mando's new words use no character efontCN_12 lacks
// (□ on the device), in either language.
static void has_every_glyph(const char* s) {
  for (size_t i = 0; s[i] != '\0';) {
    const cpa::i18n::Cp c = cpa::i18n::cp_at(s, i);
    GlyphMetric m;
    TEST_ASSERT_TRUE_MESSAGE(c.cp == '%' || efont_cn12().metric(c.cp, m), s);
    i += c.len;
  }
}

static void test_the_eggs_words_have_glyphs() {
  namespace egg = cpa::egg;
  const cpa::Txt* const words[] = {&egg::kRareLine,  &egg::kPatCount,     &egg::kLateSign,    &egg::kEarlySign,
                                   &egg::kWeekendSign, &egg::kLateGreeting, &egg::kWeekendGreeting, &egg::kAllClear};
  for (const cpa::Txt* t : words) {
    has_every_glyph(t->zh);
    has_every_glyph(t->en);
  }
  for (const cpa::Txt& t : egg::kPatLines) {
    has_every_glyph(t.zh);
    has_every_glyph(t.en);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ascii_and_the_narrow_symbols_are_exact);
  RUN_TEST(test_no_glyph_is_ever_wider_than_counted);
  RUN_TEST(test_the_missing_symbols_are_missing);
  RUN_TEST(test_a_string_measures_as_the_canvas_does);
  RUN_TEST(test_the_eggs_words_have_glyphs);
  return UNITY_END();
}
