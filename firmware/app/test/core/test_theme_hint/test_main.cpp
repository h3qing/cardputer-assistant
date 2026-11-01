// Hint strings as keycaps (docs/19 §4.2): the five cutting rules on every hint() string
// the firmware has, written as "[key]text" items separated by '|'.
#include <unity.h>

#include <cstring>
#include <string>

#include "hint_layout.h"
#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core

using namespace cpa::theme;

void setUp() {}
void tearDown() {}

static std::string items_of(const char* hint, const HintItems& h) {
  std::string out;
  for (uint8_t i = 0; i < h.n; ++i) {
    const HintItem& it = h.items[i];
    if (!out.empty()) out += "|";
    if (it.key_len > 0) out += "[" + std::string(hint + it.key_at, it.key_len) + "]";
    out += std::string(hint + it.text_at, it.text_len);
  }
  return out;
}

static std::string items(const char* hint) { return items_of(hint, hint_items(hint)); }

static void test_key_then_words_in_one_word() {
  TEST_ASSERT_EQUAL_STRING("[Enter]打开|[;.]选择|[Opt+1-9]项目|[Tab]菜单", items("Enter打开 ;.选择 Opt+1-9项目  Tab菜单").c_str());
  TEST_ASSERT_EQUAL_STRING("[Enter]回复|[Opt+D]忽略|[`]返回|[Tab]菜单", items("Enter回复 Opt+D忽略 `返回  Tab菜单").c_str());
  TEST_ASSERT_EQUAL_STRING("[Enter]开session|[Opt+D]完成|[`]返回|[Tab]菜单",
                           items("Enter开session Opt+D完成 `返回  Tab菜单").c_str());
  TEST_ASSERT_EQUAL_STRING("[G0]加★|[1-9]项目|[Opt+R]结束|[Tab]菜单", items("G0加★ 1-9项目 Opt+R结束 Tab菜单").c_str());
}

static void test_a_lone_key_takes_the_next_han_word() {
  TEST_ASSERT_EQUAL_STRING("[Enter]确认|[`]取消", items("Enter 确认    ` 取消").c_str());
  TEST_ASSERT_EQUAL_STRING("[Enter]保存|[Fn+`]退出|[Tab]菜单", items("Enter 保存  Fn+` 退出  Tab菜单").c_str());
  TEST_ASSERT_EQUAL_STRING("[`]返回|[Opt+H]主页|[Tab]菜单", items("` 返回  Opt+H 主页  Tab菜单").c_str());
  TEST_ASSERT_EQUAL_STRING("松开|[G0]结束|[1-9]项目|[0]不归属", items("松开 G0 结束  1-9 项目  0 不归属").c_str());
}

static void test_ascii_words_in_a_row_are_one_key() {
  TEST_ASSERT_EQUAL_STRING("[退格]退出|[Opt N/Q/S]类型|[Tab]菜单", items("退格 退出  Opt N/Q/S 类型  Tab菜单").c_str());
  TEST_ASSERT_EQUAL_STRING("[probe hint]", items("probe hint").c_str());
}

static void test_han_words_are_plain_text() {
  TEST_ASSERT_EQUAL_STRING("保存中…", items("保存中…").c_str());
  TEST_ASSERT_EQUAL_STRING("字母直达|[;.,/]移动|[Enter]打开|[Tab]菜单", items("字母直达 ;.,/移动 Enter打开  Tab菜单").c_str());
  TEST_ASSERT_EQUAL_STRING("[;.]选|[Enter]做|[,/]换页|字母直达|[Tab]关", items(";.选 Enter做 ,/换页 字母直达 Tab关").c_str());
  TEST_ASSERT_EQUAL_STRING("[空格]加★", items("空格 加★").c_str());
  TEST_ASSERT_EQUAL_STRING("[空格]★", items("空格★").c_str());
}

static void test_empty_spaces_and_too_many() {
  TEST_ASSERT_EQUAL_UINT8(0, hint_items("").n);
  TEST_ASSERT_EQUAL_UINT8(0, hint_items("   ").n);
  TEST_ASSERT_EQUAL_STRING("[a]", items("  a  ").c_str());
  const HintItems h = hint_items("A一 B二 C三 D四 E五 F六 G七 H八 I九 J十");
  TEST_ASSERT_EQUAL_UINT8(kHintItemsMax, h.n);
  TEST_ASSERT_TRUE(h.overflow);
  TEST_ASSERT_FALSE(hint_items("A一 B二").overflow);
}

// docs/21 §6.6: a structured hint is never parsed, so English words stay words.
static std::string line_items(const HintLine& l) { return items_of(l.text, l.items); }

static void test_structured_parts_are_taken_as_given() {
  const HintPart parts[] = {{";.", "pick"}, {"Enter", "change"}, {"`", "back"}, {"Tab", "menu"}};
  const HintLine l = hint_line(parts, 4);
  TEST_ASSERT_EQUAL_STRING("[;.]pick|[Enter]change|[`]back|[Tab]menu", line_items(l).c_str());
  TEST_ASSERT_FALSE(l.items.overflow);
  // the same words through the parser would make one key of "Enter change"
  TEST_ASSERT_EQUAL_STRING("[Enter change]", items("Enter change").c_str());
}

static void test_structured_text_only_key_only_and_limits() {
  const HintPart parts[] = {{"", "Hello"}, {"G0", ""}};
  TEST_ASSERT_EQUAL_STRING("Hello|[G0]", line_items(hint_line(parts, 2)).c_str());
  TEST_ASSERT_EQUAL_UINT8(0, hint_line(parts, 0).items.n);
  HintPart many[kHintItemsMax + 1];
  for (HintPart& p : many) p = HintPart{"A", "b"};
  const HintLine l = hint_line(many, kHintItemsMax + 1);
  TEST_ASSERT_EQUAL_UINT8(kHintItemsMax, l.items.n);
  TEST_ASSERT_TRUE(l.items.overflow);
  const HintPart huge[] = {{"Enter", "0123456789012345678901234567890123456789012345678901234567890123456789"},
                           {"Tab", "0123456789012345678901234567890123456789012345678901234567890123456789"},
                           {"G0", "0123456789012345678901234567890123456789012345678901234567890123456789"}};
  const HintLine cut = hint_line(huge, 3);
  TEST_ASSERT_TRUE(cut.items.overflow);  // the text would not fit: drawn as one line instead
  TEST_ASSERT_TRUE(std::strlen(cut.text) <= kHintTextMax);
}

// docs/21 §6.6: English words are ASCII like keys, so an English hint string brackets its
// keys: "[Enter]open [`]back [Tab]menu". The words run to the next " [" (spaces inside
// them stay); text before the first key is plain text.
static void test_bracketed_keys_for_english_words() {
  TEST_ASSERT_EQUAL_STRING("[Enter]open|[`]back|[Tab]menu", items("[Enter]open [`]back [Tab]menu").c_str());
  TEST_ASSERT_EQUAL_STRING("[Enter]start|[`]keep recording", items("[Enter]start    [`]keep recording").c_str());
  TEST_ASSERT_EQUAL_STRING("[Opt N/Q/S]type|[Tab]menu", items("[Opt N/Q/S]type  [Tab]menu").c_str());
  TEST_ASSERT_EQUAL_STRING("Let go|[G0]stop|[1-9]project|[0]none", items("Let go [G0]stop [1-9]project [0]none").c_str());
  TEST_ASSERT_EQUAL_STRING("[G0]", items("[G0]").c_str());
  TEST_ASSERT_EQUAL_STRING("[;.,/]move|[Enter]open", items("[;.,/]move [Enter]open").c_str());
  // a "[" that is not at a word's start is text; an unclosed one is the rest as text
  TEST_ASSERT_EQUAL_STRING("[Enter]a[b|[Tab]menu", items("[Enter]a[b [Tab]menu").c_str());
  TEST_ASSERT_EQUAL_STRING("[Enter]x|[oops", items("[Enter]x [oops").c_str());
  // Chinese hints are cut as before
  TEST_ASSERT_EQUAL_STRING("[Enter]确认|[`]取消", items("Enter 确认    ` 取消").c_str());
}

static void test_bracketed_keys_too_many() {
  const HintItems h = hint_items("[a]1 [b]2 [c]3 [d]4 [e]5 [f]6 [g]7 [h]8 [i]9");
  TEST_ASSERT_EQUAL_UINT8(kHintItemsMax, h.n);
  TEST_ASSERT_TRUE(h.overflow);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_key_then_words_in_one_word);
  RUN_TEST(test_a_lone_key_takes_the_next_han_word);
  RUN_TEST(test_ascii_words_in_a_row_are_one_key);
  RUN_TEST(test_han_words_are_plain_text);
  RUN_TEST(test_empty_spaces_and_too_many);
  RUN_TEST(test_bracketed_keys_for_english_words);
  RUN_TEST(test_bracketed_keys_too_many);
  RUN_TEST(test_structured_parts_are_taken_as_given);
  RUN_TEST(test_structured_text_only_key_only_and_limits);
  return UNITY_END();
}
