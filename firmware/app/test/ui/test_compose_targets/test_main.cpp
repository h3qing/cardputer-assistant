// Project targets that do not exist (docs/20 U13, A7): a queue item or a new session for
// an empty slot is refused like slot 0 ("Opt+数字：1 cpa · 2 blog"), never as a false
// E_STORAGE, and the draft stays; with no project at all the hint names the Mac command
// and Opt+N; a digit for an empty slot says so instead of opening an empty page; the
// board's "其他" row says where those projects are. The refusal goes away once the target
// changes (U20).
#include <unity.h>

#include <string>

#include "board_logic.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() { set_lang(Lang::Zh); }  // a failed English test skips its LangScope

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  cpa::Module* mods[1] = {&board};
  Shell shell{port, mods, 1};
  uint32_t now = 0;

  Rig() {
    port.app = demo_state();  // projects 1 cpa, 2 blog
    shell.begin(0);
  }
  void key(const KeyEvent& ev) { shell.on_key(ev, now += 10); }
  void type(const char* text) {
    for (const char* p = text; *p; ++p) key(chr(*p));
  }
  std::string screen() {
    FakeCanvas c;
    shell.render(c, now += 10);
    return c.dump();
  }
};

static void test_queue_to_an_empty_slot_names_the_projects() {
  Rig r;
  r.key(opt('q'));
  r.type("ask lawyer");
  r.key(opt('5'));  // only 2 projects
  r.key(enter());
  const std::string s = r.screen();
  TEST_ASSERT_TRUE_MESSAGE(s.find("Opt+数字：1 cpa · 2 blog") != std::string::npos, s.c_str());
  TEST_ASSERT_TRUE(s.find("E_STORAGE") == std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.queued.size());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.key(opt('1'));  // the target changed: the refusal is stale
  TEST_ASSERT_TRUE(r.screen().find("Opt+数字") == std::string::npos);
  r.key(enter());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.queued.size());
}

static void test_spawn_to_an_empty_slot_never_opens_the_confirm() {
  Rig r;
  r.key(opt('s'));
  r.type("fix tests");
  r.key(opt('7'));
  r.key(enter());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_TRUE(r.screen().find("Opt+数字：1 cpa") != std::string::npos);
  r.key(opt('0'));  // slot 0 is refused with the same words
  r.key(enter());
  TEST_ASSERT_TRUE(r.screen().find("Opt+数字：1 cpa") != std::string::npos);
}

static void test_no_project_at_all_names_the_mac_command() {
  Rig r;
  r.port.app.projects.data.n = 0;
  r.key(opt('q'));
  r.type("x");
  r.key(opt('1'));
  r.key(enter());
  const std::string s = r.screen();
  TEST_ASSERT_TRUE_MESSAGE(s.find("还没有项目：Mac 上 cpa project add") != std::string::npos, s.c_str());
  TEST_ASSERT_TRUE(s.find("E_STORAGE") == std::string::npos);
  for (int i = 0; i < 300; ++i) r.shell.tick(r.now += 10);  // the next hint: Opt+N
  TEST_ASSERT_TRUE_MESSAGE(r.screen().find("Opt+N") != std::string::npos, r.screen().c_str());
}

static void test_a_digit_for_an_empty_slot_stays_put() {
  Rig r;
  r.key(opt('5'));
  TEST_ASSERT_EQUAL_UINT32(1, r.board.entered.size());  // only begin()'s
  const std::string s = r.screen();
  TEST_ASSERT_TRUE_MESSAGE(s.find("没有第 5 个项目（共 2 个）") != std::string::npos, s.c_str());
  r.key(opt('2'));
  TEST_ASSERT_EQUAL_UINT32(2, r.board.entered.size());  // a real one opens
  TEST_ASSERT_EQUAL_UINT8(2, r.board.entered.back());
  TEST_ASSERT_TRUE(r.screen().find("没有第 5 个项目") == std::string::npos);  // a new page: the hint went
}

static void test_a_digit_for_an_empty_slot_says_so_in_english() {
  LangScope en(Lang::En);
  Rig r;
  r.key(opt('5'));
  const std::string s = r.screen();
  TEST_ASSERT_TRUE_MESSAGE(s.find("No project 5 (only 2)") != std::string::npos, s.c_str());
}

static void test_reply_without_a_project_is_not_a_storage_error() {
  Rig r;
  r.port.app.sessions.data.rows[1].slot = 0;  // a session of a project the device has no slot for
  ComposeSpec fb;
  fb.kind = ComposeKind::Feedback;
  fb.sid = some(Sid::from("5c2e8a14"));
  r.shell.compose(fb);
  r.type("ok");
  r.key(enter());  // offline: it would be queued, but there is no project to queue it under
  r.now += 500;
  r.key(enter());
  const std::string s = r.screen();
  TEST_ASSERT_TRUE(s.find("E_STORAGE") == std::string::npos);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);  // the text is kept
  // where to reply instead, whole in the toast capsule (docs/20 B6)
  TEST_ASSERT_TRUE_MESSAGE(s.find("session 的项目不在设备上：去 Mac 回复") != std::string::npos, s.c_str());
}

static void test_other_row_says_where_those_projects_are() {
  const AppState s = demo_state();
  BoardUi ui;
  ui.cursor = 2;  // after cpa and blog: "其他"
  const BoardStep st = board_next(ui, enter(), s);
  TEST_ASSERT_TRUE(st.handled && st.cmd == BoardCmd::OtherRow);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_queue_to_an_empty_slot_names_the_projects);
  RUN_TEST(test_spawn_to_an_empty_slot_never_opens_the_confirm);
  RUN_TEST(test_no_project_at_all_names_the_mac_command);
  RUN_TEST(test_a_digit_for_an_empty_slot_stays_put);
  RUN_TEST(test_a_digit_for_an_empty_slot_says_so_in_english);
  RUN_TEST(test_reply_without_a_project_is_not_a_storage_error);
  RUN_TEST(test_other_row_says_where_those_projects_are);
  return UNITY_END();
}
