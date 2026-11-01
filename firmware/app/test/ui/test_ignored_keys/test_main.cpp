// Keys that do nothing say so (docs/20 U17, A8): the keyboard has no feedback of its own,
// so a key that is ignored on purpose gets one hint (never twice in 3 s, toast.h): the
// global keys under an open editor, the global keys on the meeting page, Enter on a menu
// row that only explains (docs/20 D9: press the chord directly). The arrows printed on ; . , / work with Fn as well, on every
// list, the home launcher and the menu (one binding in the List layer).
#include <unity.h>

#include <string>

#include "board_logic.h"
#include "home_logic.h"
#include "keymap.h"
#include "menu_model.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  Shell shell{port, mods, 2};
  uint32_t now = 0;
  Rig() {
    port.app = demo_state();
    shell.begin(0);
  }
  void key(const KeyEvent& ev) { shell.on_key(ev, now += 10); }
  std::string hint_row() {
    FakeCanvas c;
    shell.render(c, now += 10);
    return c.row(kHintRect.y);
  }
};

static void test_global_keys_under_the_editor_say_how_to_leave() {
  Rig r;
  r.key(g0tap());
  r.key(opt('h'));
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_TRUE_MESSAGE(r.hint_row().find("编辑器开着：退格 退出") != std::string::npos, r.hint_row().c_str());
  r.key(bksp());  // leaves
  r.key(g0tap());
  r.key(chr('x'));
  r.key(opt('i'));
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_TRUE(r.inbox.entered.empty());  // never opened
  // docs/20 D5: ` leaves the editor now (Fn+` types a backtick)
  TEST_ASSERT_TRUE_MESSAGE(r.hint_row().find("Enter 保存，` 退出") != std::string::npos, r.hint_row().c_str());
  r.key(chr('y'));  // typing is not refused: no hint for it
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
}

static void test_global_keys_on_the_meeting_page_say_what_works() {
  Rig r;
  r.port.app.rec.phase = RecPhase::Recording;
  r.port.app.rec.mode = RecMode::Meeting;
  r.key(opt('n'));
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_TRUE_MESSAGE(r.hint_row().find("开会中：G0 加★，Opt+R 结束") != std::string::npos, r.hint_row().c_str());
  TEST_ASSERT_TRUE(r.port.inputs.empty());
}

static void test_enter_on_an_explaining_menu_row_says_so() {
  Rig r;
  r.key(nav(Nav::Menu));
  r.key(chr('/'));  // 去哪 (no module rows: the Global layer)
  const MenuContent m = menu_content(MenuPage::Goto, r.shell.menu_source(), r.port.app, r.port.info);
  uint8_t row = 0;
  while (row < m.n && m.rows[row].ev.kind != KeyEventKind::None) ++row;
  TEST_ASSERT_TRUE(row < m.n);  // "第 N 个项目页（按 Opt+1-9）" is shown only
  for (uint8_t i = 0; i < row; ++i) r.key(chr('.'));
  r.key(enter());
  TEST_ASSERT_TRUE(r.shell.menu_open());
  // docs/20 D9: the open menu runs a chord it shows, so the row says to press it directly
  TEST_ASSERT_TRUE_MESSAGE(r.hint_row().find("这一行是说明：直接按 Opt+1-9") != std::string::npos,
                           r.hint_row().c_str());
}

// Review: the hold row says to hold G0; docs/20 D4, D9: it works with the menu open.
static void test_enter_on_the_hold_row_says_hold_g0() {
  Rig r;
  r.key(nav(Nav::Menu));
  r.key(chr('/'));
  const MenuContent m = menu_content(MenuPage::Goto, r.shell.menu_source(), r.port.app, r.port.info);
  uint8_t row = 0;
  while (row < m.n && std::string(m.rows[row].keys.c_str()) != "按住G0") ++row;
  TEST_ASSERT_TRUE(row < m.n);
  for (uint8_t i = 0; i < row; ++i) r.key(chr('.'));
  r.key(enter());
  TEST_ASSERT_TRUE_MESSAGE(r.hint_row().find("这一行是说明：直接按住 G0") != std::string::npos, r.hint_row().c_str());
}

// Review: a fact on 控制 (电池, SD, the status bar's legend) explains no key: Enter on it
// says nothing, rather than "press the key it shows".
static void test_enter_on_a_fact_row_says_nothing() {
  Rig r;
  const MenuContent m = menu_content(MenuPage::Control, r.shell.menu_source(), r.port.app, r.port.info);
  for (uint8_t row = 0; row < m.n; ++row) {
    if (m.rows[row].ev.kind != KeyEventKind::None) continue;
    Rig f;
    f.key(nav(Nav::Menu));
    f.key(chr('/'));
    f.key(chr('/'));  // 控制
    for (uint8_t i = 0; i < row; ++i) f.key(chr('.'));
    const std::string before = f.hint_row();
    f.key(enter());
    TEST_ASSERT_TRUE(f.shell.menu_open());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), f.hint_row().c_str(), m.rows[row].keys.c_str());
  }
}

static void test_fn_arrows_work_where_the_arrows_do() {
  const struct {
    Nav nav;
    Action action;
  } cases[] = {{Nav::Up, Action::Up}, {Nav::Down, Action::Down}, {Nav::Left, Action::TabPrev}, {Nav::Right, Action::TabNext}};
  for (const auto& c : cases) TEST_ASSERT_TRUE(keymap_lookup(Layer::List, nav(c.nav)).action == c.action);
  BoardUi ui;
  TEST_ASSERT_EQUAL_UINT8(1, board_next(ui, nav(Nav::Down), demo_state()).ui.cursor);
  TEST_ASSERT_EQUAL_UINT8(1, home_next(HomeUi{}, nav(Nav::Right)).ui.sel);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_global_keys_under_the_editor_say_how_to_leave);
  RUN_TEST(test_global_keys_on_the_meeting_page_say_what_works);
  RUN_TEST(test_enter_on_an_explaining_menu_row_says_so);
  RUN_TEST(test_enter_on_the_hold_row_says_hold_g0);
  RUN_TEST(test_enter_on_a_fact_row_says_nothing);
  RUN_TEST(test_fn_arrows_work_where_the_arrows_do);
  return UNITY_END();
}
