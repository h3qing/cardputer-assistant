// One vocabulary (docs/20 U22, A8): each action has one name wherever it is written, the
// home tiles, the Tab menu's rows (action_help) and the editor's title
// (compose_kind_label): 记一条 · 排队给 AI · 开 session · 回复 · 项目 · 等你 (Note · AI queue ·
// New session · Reply · Projects · Inbox). Ages are one Chinese function, rounded ("2 小时",
// 2 h, for 1 h 59 min, not "1h"); a session's kind is 终端 / 后台 (terminal / background),
// not tty / bg; digit ranges are written "Opt+1-9".
#include <unity.h>

#include <string>

#include "action_help.h"
#include "compose.h"
#include "core/support/lang_pin.h"
#include "home_logic.h"
#include "lang.h"
#include "text_fit.h"

using namespace cpa;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

static bool has(const char* s, const char* word) { return std::string(s).find(word) != std::string::npos; }

static void test_each_action_has_one_name() {
  const struct {
    Action action;
    char tile;
    ComposeKind kind;
    const char* word;
  } words[] = {{Action::NewNote, 'n', ComposeKind::Note, "记一条"},
               {Action::NewQueue, 'q', ComposeKind::Queue, "排队给 AI"},
               {Action::NewSpawn, 's', ComposeKind::Spawn, "开 session"}};
  for (const auto& w : words) {
    TEST_ASSERT_TRUE_MESSAGE(has(action_help(w.action), w.word), w.word);
    TEST_ASSERT_TRUE_MESSAGE(has(compose_kind_label(w.kind), w.word), w.word);
    bool tiled = false;
    for (const HelpRow& t : kHomeTiles) tiled = tiled || (t.ev.key == w.tile && has(help_label(t), w.word));
    TEST_ASSERT_TRUE_MESSAGE(tiled, w.word);
  }
  TEST_ASSERT_TRUE(has(compose_kind_label(ComposeKind::Feedback), "回复"));
  TEST_ASSERT_TRUE(has(action_help(Action::TypeNote), "记一条"));
  TEST_ASSERT_TRUE(has(action_help(Action::TypeQueue), "排队给 AI"));
  TEST_ASSERT_TRUE(has(action_help(Action::TypeSpawn), "开 session"));
  TEST_ASSERT_TRUE(has(action_help(Action::Inbox), "等你"));
  TEST_ASSERT_TRUE(has(action_help(Action::Projects), "项目"));
  TEST_ASSERT_FALSE(has(compose_result_text(ComposeResult::NeedProject), ".."));  // "Opt+1-9"
}

// docs/21 §6.2: one name in English too. The S tile says "Session" (a tile is 60 px), the
// menu and the editor "New session".
static void test_each_action_has_one_name_in_english() {
  LangScope en(Lang::En);
  const struct {
    Action action;
    char tile;
    ComposeKind kind;
    const char* word;
  } words[] = {{Action::NewNote, 'n', ComposeKind::Note, "Note"},
               {Action::NewQueue, 'q', ComposeKind::Queue, "AI queue"},
               {Action::NewSpawn, 's', ComposeKind::Spawn, "Session"}};
  for (const auto& w : words) {
    TEST_ASSERT_TRUE_MESSAGE(has(action_help(w.action), w.word) || has(action_help(w.action), "New session"), w.word);
    TEST_ASSERT_TRUE_MESSAGE(has(compose_kind_label(w.kind), w.word) || has(compose_kind_label(w.kind), "session"),
                             w.word);
    bool tiled = false;
    for (const HelpRow& t : kHomeTiles) tiled = tiled || (t.ev.key == w.tile && has(help_label(t), w.word));
    TEST_ASSERT_TRUE_MESSAGE(tiled, w.word);
  }
  TEST_ASSERT_EQUAL_STRING("New session", action_help(Action::NewSpawn));
  TEST_ASSERT_EQUAL_STRING("New session", compose_kind_label(ComposeKind::Spawn));
  TEST_ASSERT_EQUAL_STRING("Reply", compose_kind_label(ComposeKind::Feedback));
  TEST_ASSERT_TRUE(has(action_help(Action::TypeNote), "Note"));
  TEST_ASSERT_TRUE(has(action_help(Action::TypeQueue), "AI queue"));
  TEST_ASSERT_TRUE(has(action_help(Action::TypeSpawn), "New session"));
  TEST_ASSERT_EQUAL_STRING("Inbox", action_help(Action::Inbox));
  TEST_ASSERT_EQUAL_STRING("Settings", action_help(Action::System));
  TEST_ASSERT_EQUAL_STRING("bg", session_kind_text(SessionKind::Bg));
}

static void test_ages_are_one_rounded_chinese_function() {
  TEST_ASSERT_EQUAL_STRING("9 秒", age_text(9).c_str());
  TEST_ASSERT_EQUAL_STRING("1 分钟", age_text(60).c_str());
  TEST_ASSERT_EQUAL_STRING("9 分钟", age_text(9 * 60 + 20).c_str());
  TEST_ASSERT_EQUAL_STRING("10 分钟", age_text(9 * 60 + 30).c_str());  // rounded
  TEST_ASSERT_EQUAL_STRING("2 小时", age_text(3600 + 59 * 60).c_str());  // 1 h 59 min: not "1h"
  TEST_ASSERT_EQUAL_STRING("1 小时", age_text(59 * 60 + 40).c_str());    // 60 minutes: an hour
  TEST_ASSERT_EQUAL_STRING("2 天", age_text(47 * 3600).c_str());         // not "1d"
  TEST_ASSERT_EQUAL_STRING("1 天", age_text(23 * 3600 + 40 * 60).c_str());
  // review: a list row's right column is tight, so it is no wider than v0.4.0's "45m bg"
  TEST_ASSERT_EQUAL_STRING("45分钟", age_text(45 * 60, true).c_str());
  TEST_ASSERT_EQUAL_STRING("2小时", age_text(3600 + 59 * 60, true).c_str());
  TEST_ASSERT_EQUAL_STRING("9秒", age_text(9, true).c_str());
  TEST_ASSERT_EQUAL_STRING("2天", age_text(47 * 3600, true).c_str());
  TEST_ASSERT_EQUAL_STRING("终端", session_kind_text(SessionKind::Tty));
  TEST_ASSERT_EQUAL_STRING("后台", session_kind_text(SessionKind::Bg));
}

// docs/21 §6.5: English ages are abbreviated, so they need no plural: "9 s / 40 min / 2 h /
// 2 d", tight "9s / 40m / 2h / 2d"; rounded as the Chinese are.
static void test_ages_in_english_are_abbreviated() {
  LangScope en(Lang::En);
  TEST_ASSERT_EQUAL_STRING("9 s", age_text(9).c_str());
  TEST_ASSERT_EQUAL_STRING("10 min", age_text(9 * 60 + 30).c_str());
  TEST_ASSERT_EQUAL_STRING("2 h", age_text(3600 + 59 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("2 d", age_text(47 * 3600).c_str());
  TEST_ASSERT_EQUAL_STRING("45m", age_text(45 * 60, true).c_str());
  TEST_ASSERT_EQUAL_STRING("9s", age_text(9, true).c_str());
  TEST_ASSERT_EQUAL_STRING("2h", age_text(3600 + 59 * 60, true).c_str());
  TEST_ASSERT_EQUAL_STRING("2d", age_text(47 * 3600, true).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_each_action_has_one_name);
  RUN_TEST(test_each_action_has_one_name_in_english);
  RUN_TEST(test_ages_are_one_rounded_chinese_function);
  RUN_TEST(test_ages_in_english_are_abbreviated);
  return UNITY_END();
}
