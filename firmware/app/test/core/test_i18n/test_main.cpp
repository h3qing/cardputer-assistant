// i18n (docs/21 §6.3-§6.5, ST-1): the current language, the zh/en pair Txt and its
// build-time checks, tr / trn / both, LangScope. The checks run at compile time for a
// constexpr Txt (test/compile_fail/i18n has the cases that must not build); here the same
// predicates run at run time, so each rule is pinned case by case.
#include <unity.h>

#include <cstring>

#include "lang.h"
#include "lang_names.h"
#include "theme.h"
#include "txt.h"
#include "words.h"

using namespace cpa;
using namespace cpa::i18n;

void setUp() { set_lang(Lang::Zh); }
void tearDown() { set_lang(Lang::Zh); }

namespace {

constexpr Txt kQueueEmpty{"队列为空", "Queue is empty"};
constexpr ToastTxt kSaved{"已保存", "Saved"};
constexpr CountTxt kNotes{"%u 条笔记", "%u note", "%u notes"};
constexpr Txt kWeekdays[2] = {{"周日", "Sun"}, {"周一", "Mon"}};
constexpr Txt kLanguage{"语言", "Language"};

}  // namespace

static void test_tr_in_chinese() {
  TEST_ASSERT_TRUE(lang() == Lang::Zh);
  TEST_ASSERT_EQUAL_STRING("队列为空", tr(kQueueEmpty));
}

static void test_set_lang_switches_every_tr() {
  set_lang(Lang::En);
  TEST_ASSERT_TRUE(lang() == Lang::En);
  TEST_ASSERT_EQUAL_STRING("Queue is empty", tr(kQueueEmpty));
  TEST_ASSERT_EQUAL_STRING("Saved", tr(kSaved));
  TEST_ASSERT_EQUAL_STRING("Mon", tr(kWeekdays[1]));
  set_lang(Lang::Zh);
  TEST_ASSERT_EQUAL_STRING("周一", tr(kWeekdays[1]));
}

static void test_nvs_byte_round_trip_and_unknown_is_chinese() {
  TEST_ASSERT_EQUAL_UINT8(0, lang_to_nvs(Lang::Zh));
  TEST_ASSERT_EQUAL_UINT8(1, lang_to_nvs(Lang::En));
  TEST_ASSERT_TRUE(lang_from_nvs(0) == Lang::Zh);
  TEST_ASSERT_TRUE(lang_from_nvs(1) == Lang::En);
  TEST_ASSERT_TRUE(lang_from_nvs(2) == Lang::Zh);
  TEST_ASSERT_TRUE(lang_from_nvs(0xFF) == Lang::Zh);  // an erased byte
}

static void test_other_and_codes() {
  TEST_ASSERT_TRUE(lang_other(Lang::Zh) == Lang::En);
  TEST_ASSERT_TRUE(lang_other(Lang::En) == Lang::Zh);
  TEST_ASSERT_EQUAL_STRING("zh", lang_code(Lang::Zh));
  TEST_ASSERT_EQUAL_STRING("en", lang_code(Lang::En));
  TEST_ASSERT_TRUE(lang_from_code("en").value == Lang::En);
  TEST_ASSERT_TRUE(lang_from_code("zh").value == Lang::Zh);
  TEST_ASSERT_FALSE(lang_from_code("fr").has);
  TEST_ASSERT_FALSE(lang_from_code("").has);
}

static void test_names_are_in_their_own_script() {
  TEST_ASSERT_EQUAL_STRING("中文", lang_name(Lang::Zh));
  TEST_ASSERT_EQUAL_STRING("English", lang_name(Lang::En));
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("中文", lang_name(Lang::Zh));  // never translated
}

static void test_scope_restores() {
  {
    LangScope s(Lang::En);
    TEST_ASSERT_TRUE(lang() == Lang::En);
    {
      LangScope inner(Lang::Zh);
      TEST_ASSERT_TRUE(lang() == Lang::Zh);
    }
    TEST_ASSERT_TRUE(lang() == Lang::En);
  }
  TEST_ASSERT_TRUE(lang() == Lang::Zh);
}

static void test_trn_plural_only_in_english() {
  TEST_ASSERT_EQUAL_STRING("%u 条笔记", trn(kNotes, 1));
  TEST_ASSERT_EQUAL_STRING("%u 条笔记", trn(kNotes, 2));
  LangScope s(Lang::En);
  TEST_ASSERT_EQUAL_STRING("%u notes", trn(kNotes, 0));
  TEST_ASSERT_EQUAL_STRING("%u note", trn(kNotes, 1));
  TEST_ASSERT_EQUAL_STRING("%u notes", trn(kNotes, 2));
}

static void test_both_draws_the_two_halves() {
  TEST_ASSERT_EQUAL_STRING("语言 Language", both(kLanguage).c_str());
  LangScope s(Lang::En);
  TEST_ASSERT_EQUAL_STRING("语言 Language", both(kLanguage).c_str());  // the same in both
  TEST_ASSERT_EQUAL_INT(78, text_width(both(kLanguage).c_str()));      // docs/21 §4.2
}

static void test_width_is_the_fonts() {
  TEST_ASSERT_EQUAL_INT(0, text_width(""));
  TEST_ASSERT_EQUAL_INT(84, text_width("Cardputer 助手"));
  TEST_ASSERT_EQUAL_INT(36, text_width("● 在线"));
  // the 11 narrow symbols of efontCN_12
  TEST_ASSERT_EQUAL_INT(66, text_width("●…★↑←→◎◇○·×"));
  TEST_ASSERT_EQUAL_INT(96, text_width("无 · 只用 USB 线"));  // docs/21 §4.2
  TEST_ASSERT_EQUAL_INT(90, text_width("None · USB only"));
  TEST_ASSERT_EQUAL_INT(12, text_width("，"));  // full-width punctuation
}

static void test_han_rule() {
  TEST_ASSERT_TRUE(has_han("语言 Language"));
  TEST_ASSERT_TRUE(has_han("Saved，"));       // full-width comma
  TEST_ASSERT_TRUE(has_han("\xE3\x81\x82"));  // あ, kana
  TEST_ASSERT_FALSE(has_han("None · USB only"));
  TEST_ASSERT_FALSE(has_han("●…→×"));
}

static void test_missing_glyph_rule() {
  TEST_ASSERT_TRUE(has_missing_glyph("a — b"));
  TEST_ASSERT_TRUE(has_missing_glyph("\xE2\x9C\x93"));  // ✓
  TEST_ASSERT_TRUE(has_missing_glyph("\xE2\x96\xB8"));  // ▸
  TEST_ASSERT_FALSE(has_missing_glyph("a - b … ● ↑"));
}

static void test_format_rule() {
  TEST_ASSERT_TRUE(same_conversions("%u 条", "%u notes"));
  TEST_ASSERT_TRUE(same_conversions("%s · 再按 Enter 换回 %s", "%s · Enter again: %s"));
  TEST_ASSERT_TRUE(same_conversions("100%%", "100%%"));
  TEST_ASSERT_TRUE(same_conversions("无", "none"));
  TEST_ASSERT_FALSE(same_conversions("%u 条", "%s notes"));
  TEST_ASSERT_FALSE(same_conversions("%u %s", "%s %u"));  // order matters
  TEST_ASSERT_FALSE(same_conversions("%u", "notes"));
  TEST_ASSERT_TRUE(same_conversions("%02u:%02u", "%02u:%02u"));
  TEST_ASSERT_FALSE(same_conversions("%02u", "%u"));
}

static void test_pair_problems() {
  TEST_ASSERT_NULL(pair_problem("队列为空", "Queue is empty", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("empty", pair_problem("", "x", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("empty", pair_problem("x", "", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("han in english", pair_problem("语言", "语言 Language", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("format", pair_problem("%u 条", "%s", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("missing glyph", pair_problem("一", "a — b", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("control", pair_problem("一\n二", "one", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("control", pair_problem("一", "o\tne", kNoLimit));
  TEST_ASSERT_EQUAL_STRING("too wide", pair_problem("一二三四五", "abc", 48));  // 60 px
  TEST_ASSERT_EQUAL_STRING("too wide", pair_problem("一", "abcdefghi", 48));   // 54 px
  TEST_ASSERT_NULL(pair_problem("一二三四", "abcdefgh", 48));                    // 48 px each
  // a format string is measured by its tests with the longest arguments
  TEST_ASSERT_NULL(pair_problem("%s 一二三四五", "%s abcdefghij", 48));
}

static void test_width_classes() {
  TEST_ASSERT_EQUAL_INT(theme::kToastTextW, kToastTxtW);  // the toast capsule's text
  TEST_ASSERT_EQUAL_INT(232, kLineTxtW);
  TEST_ASSERT_EQUAL_INT(120, kMenuTxtW);
  TEST_ASSERT_EQUAL_INT(60, kTileTxtW);
  TEST_ASSERT_EQUAL_INT(48, kSignTxtW);
  TEST_ASSERT_EQUAL_INT(43, kKeyTxtW);
}

static void test_shared_words() {
  TEST_ASSERT_EQUAL_STRING("菜单", tr(txt::kMenu));
  TEST_ASSERT_EQUAL_STRING("设置", tr(txt::kSettings));
  LangScope s(Lang::En);
  TEST_ASSERT_EQUAL_STRING("menu", tr(txt::kMenu));
  TEST_ASSERT_EQUAL_STRING("Settings", tr(txt::kSettings));
  TEST_ASSERT_EQUAL_STRING("back", tr(txt::kBack));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tr_in_chinese);
  RUN_TEST(test_set_lang_switches_every_tr);
  RUN_TEST(test_nvs_byte_round_trip_and_unknown_is_chinese);
  RUN_TEST(test_other_and_codes);
  RUN_TEST(test_names_are_in_their_own_script);
  RUN_TEST(test_scope_restores);
  RUN_TEST(test_trn_plural_only_in_english);
  RUN_TEST(test_both_draws_the_two_halves);
  RUN_TEST(test_width_is_the_fonts);
  RUN_TEST(test_han_rule);
  RUN_TEST(test_missing_glyph_rule);
  RUN_TEST(test_format_rule);
  RUN_TEST(test_pair_problems);
  RUN_TEST(test_width_classes);
  RUN_TEST(test_shared_words);
  return UNITY_END();
}
