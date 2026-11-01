// docs/20 D9 (U17): while the Tab menu is open, a chord that the menu shows (on any of its
// pages) closes the menu and does what it does, as Enter on its row would; a range row
// ("Opt+1-9") counts for each of its keys. A chord the menu does not show is still eaten,
// so a draft or a box under the menu keeps it. Enter on a row that only explains a chord
// says to press it directly.
#include <unity.h>

#include <string>

#include "board_module.h"
#include "home_module.h"
#include "inbox_module.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "system_module.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

struct Rig {
  FakePort port;
  BoardModule board;
  InboxModule inbox;
  SystemModule system;
  HomeModule home;
  cpa::Module* mods[4] = {&board, &inbox, &system, &home};
  Shell shell{port, mods, 4};
  Rig() {
    port.app = demo_state();
    set_ready(port.app);
    port.next_refusal = rec::RecRefusal::None;
    shell.begin(0);
  }
  void keys(std::initializer_list<KeyEvent> ks, uint32_t at = 1000) {
    for (const KeyEvent& k : ks) {
      if (shell.lock_gate(k, at)) shell.on_key(k, at);
    }
  }
  std::string hint_row(uint32_t at = 1000) const {
    FakeCanvas c;
    shell.render(c, at);
    return c.line(kHintRect.y);
  }
};

}  // namespace

static void test_a_shown_chord_closes_the_menu_and_runs() {
  Rig r;
  r.keys({nav(Nav::Menu), opt('i')});  // 去哪 shows Opt+I
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_STRING("inbox", r.shell.top_id());
  r.keys({nav(Nav::Menu), opt('2')});  // the "Opt+1-9" row
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_EQUAL_UINT8(2, r.port.context_slot);
  r.keys({nav(Nav::Menu), opt('n')});
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.keys({nav(Nav::Menu), opt('l')});  // 控制 shows Opt+L; the draft stays under the lock
  TEST_ASSERT_TRUE(r.shell.locked());
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
}

// Over the editor the menu shows the editor's chords (Opt+0-9, Opt+N/Q/S, Opt+L) only.
static void test_a_chord_the_menu_does_not_show_is_eaten() {
  Rig r;
  r.keys({g0tap(), chr('h'), nav(Nav::Menu), opt('i'), opt('z')});
  TEST_ASSERT_TRUE(r.shell.menu_open());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_EQUAL_STRING("home", r.shell.top_id());
  r.keys({opt('3')});  // shown: retargets the draft
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_UINT8(3, r.shell.compose_slot());
}

// Over the meeting's start box the menu shows Opt+L (取消并锁屏): it does that at once.
static void test_opt_l_over_the_meeting_box() {
  Rig r;
  r.port.app.rec.avail = true;
  r.port.app.rec.phase = RecPhase::Confirm;
  r.port.app.rec.mode = RecMode::Meeting;
  r.keys({nav(Nav::Menu), opt('l')});
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_TRUE(r.shell.locked());
  TEST_ASSERT_TRUE(r.port.inputs.size() == 1 && r.port.inputs[0].first == rec::RecInput::Cancel);
}

// A row that only explains a chord ("Opt+0-9") says to press it directly now.
static void test_explaining_a_chord_row_says_press_it() {
  Rig r;
  r.keys({g0tap(), nav(Nav::Menu)});
  const MenuContent m = menu_content(MenuPage::Here, r.shell.menu_source(), r.port.app, r.port.info);
  uint8_t range = 0;
  while (range < m.n && std::string(m.rows[range].keys.c_str()) != "Opt+0-9") ++range;
  TEST_ASSERT_TRUE(range < m.n);
  for (uint8_t i = 0; i < range; ++i) r.keys({chr('.')});
  r.keys({enter()});
  TEST_ASSERT_TRUE(r.shell.menu_open());
  TEST_ASSERT_TRUE_MESSAGE(r.hint_row().find("直接按 Opt+0-9") != std::string::npos, r.hint_row().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_shown_chord_closes_the_menu_and_runs);
  RUN_TEST(test_a_chord_the_menu_does_not_show_is_eaten);
  RUN_TEST(test_opt_l_over_the_meeting_box);
  RUN_TEST(test_explaining_a_chord_row_says_press_it);
  return UNITY_END();
}
