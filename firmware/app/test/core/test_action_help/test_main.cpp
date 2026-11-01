// Action help (docs/13 §3.1, §6.2): every Action has one short Chinese line, and so
// does the action of every binding in kKeymap (which also covers Actions added after
// Projects, once they are bound). Widths use FakeCanvas's efontCN_12 model: ASCII 6 px,
// any other code point 12 px.
#include <unity.h>

#include <cstring>
#include <string>

#include "action_help.h"
#include "lang.h"
#include "module.h"  // lib/help is built whole: its menu files need module.h's libraries
#include "ui/support/fake_canvas.h"

using namespace cpa;

void setUp() {}
void tearDown() { set_lang(Lang::Zh); }  // a failed English test skips its LangScope

static const int16_t kMaxW = 120;

static void check_label(Action a) {
  const uitest::FakeCanvas c;
  const char* label = action_help(a);
  const std::string what = "action " + std::to_string(static_cast<int>(a)) + " " + label;
  TEST_ASSERT_NOT_NULL_MESSAGE(label, what.c_str());
  TEST_ASSERT_TRUE_MESSAGE(std::strlen(label) > 0, what.c_str());
  TEST_ASSERT_TRUE_MESSAGE(c.textWidth(label) <= kMaxW, what.c_str());
}

static void test_every_action_has_a_label_that_fits_120_px() {
  for (int a = 1; a <= static_cast<int>(Action::Projects); ++a) check_label(static_cast<Action>(a));
}

static void test_every_binding_has_a_label() {
  for (size_t i = 0; i < kKeymapCount; ++i) check_label(kKeymap[i].action);
}

static void test_none_has_no_label() { TEST_ASSERT_EQUAL_STRING("", action_help(Action::None)); }

static void test_labels_follow_docs_13() {
  TEST_ASSERT_EQUAL_STRING("主页", action_help(Action::Home));
  TEST_ASSERT_EQUAL_STRING("项目看板", action_help(Action::Projects));
  TEST_ASSERT_EQUAL_STRING("等你", action_help(Action::Inbox));
  TEST_ASSERT_EQUAL_STRING("锁屏", action_help(Action::Lock));
  TEST_ASSERT_EQUAL_STRING("加 ★", action_help(Action::RecStar));
  TEST_ASSERT_EQUAL_STRING("记一条 → 今日笔记", action_help(Action::QuickNote));
  TEST_ASSERT_EQUAL_STRING("说一段（语音便条）", action_help(Action::RecMemo));
}

// docs/21: someone who reads only English, on a device left in Chinese, finds the way back
// on 去哪: the Opt+M row names the page in both languages (the home tile stays 设置).
static void test_settings_row_names_itself_in_english_too() {
  TEST_ASSERT_EQUAL_STRING("设置 Settings", action_help(Action::System));
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("Settings", action_help(Action::System));
  set_lang(Lang::Zh);
}

static void test_labels_are_distinct_where_the_keys_differ() {
  // Two different actions never read the same, except the two "back" ones (Escape is
  // Fn+` everywhere, Back is ` or 退格 in lists) and the two ways to leave a confirm.
  for (int a = 1; a <= static_cast<int>(Action::Projects); ++a) {
    for (int b = a + 1; b <= static_cast<int>(Action::Projects); ++b) {
      const Action x = static_cast<Action>(a), y = static_cast<Action>(b);
      if (x == Action::Escape && y == Action::Back) continue;
      const std::string what = std::to_string(a) + " vs " + std::to_string(b);
      TEST_ASSERT_TRUE_MESSAGE(std::strcmp(action_help(x), action_help(y)) != 0, what.c_str());
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_action_has_a_label_that_fits_120_px);
  RUN_TEST(test_every_binding_has_a_label);
  RUN_TEST(test_none_has_no_label);
  RUN_TEST(test_labels_follow_docs_13);
  RUN_TEST(test_labels_are_distinct_where_the_keys_differ);
  RUN_TEST(test_settings_row_names_itself_in_english_too);
  return UNITY_END();
}
