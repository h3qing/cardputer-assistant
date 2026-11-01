// Exit keys (issue #15: "Fn+` to exit is hard to remember"):
//   - Compose: Backspace on an empty editor exits (a fresh press, not a held repeat);
//   - the top-left key ` alone goes back on every screen, the confirm box included, where
//     the 400 ms guard still applies; docs/20 D5: in Compose too (it asks first with text),
//     D6: on the editor's own confirm back to the editor;
//   - Fn+` keeps working everywhere but in Compose, where it types the backtick (D5);
//   - the hint bar always names the exit key in plain words, uncut.
#include <unity.h>

#include <string>

#include "confirm.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "system_module.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  SystemModule system;
  cpa::Module* mods[3] = {&board, &inbox, &system};
  Shell shell{port, mods, 3};
  Rig() {
    port.app = demo_state();
    set_ready(port.app);
    board.slot = 1;
    shell.begin(0);
  }

  // The hint bar as words (keycaps and what they do: "Enter 保存 ` 退出 Tab 菜单").
  std::string hint(uint32_t now) const {
    FakeCanvas c;
    shell.render(c, now);
    return c.line(kHintRect.y);
  }

  // Where the hint bar's last text ends.
  int hint_width(uint32_t now) const {
    FakeCanvas c;
    shell.render(c, now);
    int end = 0;
    for (const TextOp& t : c.texts) {
      if (t.y == kHintRect.y && t.x + c.textWidth(t.text.c_str()) > end) end = t.x + c.textWidth(t.text.c_str());
    }
    return end;
  }
};

static KeyEvent held_bksp() {
  KeyEvent e = bksp();
  e.repeat = true;
  return e;
}

static void test_backspace_on_an_empty_editor_closes_it() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.tick(10 + kDoubleTapMs);  // gh #51: the note opens once no second tap came
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(bksp(), 20);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.notes.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());  // the key did not fall through
}

static void test_held_backspace_clears_but_does_not_close() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.on_key(chr('a'), 20);
  r.shell.on_key(chr('b'), 30);
  r.shell.on_key(bksp(), 40);
  r.shell.on_key(held_bksp(), 540);
  r.shell.on_key(held_bksp(), 600);  // text already gone: the repeat does nothing
  r.shell.on_key(held_bksp(), 660);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(bksp(), 900);  // released and pressed again
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
}

static void test_top_left_key_cancels_confirm_after_the_guard() {
  Rig r;
  r.shell.on_key(opt('s'), 100);
  r.shell.on_key(chr('x'), 110);
  r.shell.on_key(enter(), 120);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  r.shell.on_key(chr('`'), 300);  // inside the 400 ms guard: ignored
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  r.shell.on_key(chr('`'), 520);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);  // docs/20 D6: back to the draft
  TEST_ASSERT_EQUAL_UINT32(0, r.port.agents.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.port.queued.size());
}

// docs/20 D5, D6: in the editor Fn+` types the backtick; on the editor's confirm it goes
// back to the editor; it leaves everything else.
static void test_fn_grave_still_exits_everywhere() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.on_key(esc(), 20);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);  // typed
  r.shell.on_key(opt('s'), 30);
  r.shell.on_key(chr('x'), 40);
  r.shell.on_key(enter(), 50);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  r.shell.on_key(esc(), 500);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(chr('`'), 510);                    // "`x": discard?
  r.shell.on_key(enter(), 510 + kConfirmGuardMs);  // discarded
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.agents.size());
  r.shell.on_key(opt('m'), 1000);
  TEST_ASSERT_EQUAL_STRING("system", r.shell.top_id());
  r.shell.on_key(esc(), 1010);
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
}

static void test_top_left_key_leaves_every_page() {
  Rig r;
  r.shell.on_key(opt('m'), 10);
  r.shell.on_key(chr('`'), 20);
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.shell.on_key(opt('i'), 30);
  r.shell.on_key(chr('`'), 40);
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.shell.on_key(opt('2'), 50);
  TEST_ASSERT_EQUAL_UINT8(2, r.board.entered.back());
  r.shell.on_key(opt('i'), 60);
  r.shell.on_key(chr('`'), 70);  // back from the inbox to project 2, then home
  TEST_ASSERT_EQUAL_UINT8(2, r.board.entered.back());
  r.shell.on_key(chr('`'), 80);
  TEST_ASSERT_EQUAL_UINT8(0, r.board.entered.back());
}

static void test_hint_bar_names_the_exit_key() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.tick(10 + kDoubleTapMs);  // gh #51
  TEST_ASSERT_TRUE(r.hint(10).find("退格 退出") != std::string::npos);
  r.shell.on_key(chr('x'), 20);
  TEST_ASSERT_TRUE(r.hint(20).find("` 退出") != std::string::npos);  // docs/20 D5
  TEST_ASSERT_TRUE(r.hint(20).find("Fn+`") == std::string::npos);
  TEST_ASSERT_TRUE(r.hint(20).find("Enter 保存") != std::string::npos);
  r.shell.on_key(chr('`'), 30);  // text exists: asks first
  TEST_ASSERT_TRUE(r.hint(30).find("` 返回") != std::string::npos);
  TEST_ASSERT_TRUE(r.hint(30).find("Enter 放弃") != std::string::npos);
  r.shell.on_key(chr('`'), 40);  // back to editing
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(opt('s'), 50);
  r.shell.on_key(enter(), 60);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  TEST_ASSERT_TRUE(r.hint(70).find("` 返回修改") != std::string::npos);  // docs/20 D6, B6: back to the draft
  TEST_ASSERT_TRUE(r.hint(70).find("Fn+`") == std::string::npos);
}

static void test_every_hint_fits_the_bar() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  TEST_ASSERT_TRUE(r.hint_width(10) <= kScreenW);
  r.shell.on_key(chr('x'), 20);
  TEST_ASSERT_TRUE(r.hint_width(20) <= kScreenW);
  r.shell.on_key(chr('`'), 30);  // docs/20 D5: ` asks (was Fn+`)
  TEST_ASSERT_TRUE(r.hint_width(30) <= kScreenW);
  r.shell.on_key(chr('`'), 40);
  r.shell.on_key(opt('s'), 50);
  r.shell.on_key(enter(), 60);
  TEST_ASSERT_TRUE(r.hint_width(70) <= kScreenW);
  TEST_ASSERT_TRUE(r.hint(70).find("…") == std::string::npos);
}

static void test_discard_prompt_and_confirm_body_name_the_key() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.on_key(chr('x'), 20);
  r.shell.on_key(chr('`'), 30);  // docs/20 D5: ` asks (was Fn+`)
  FakeCanvas c;
  r.shell.render(c, 30);
  TEST_ASSERT_EQUAL_STRING("Enter 放弃 ` 返回编辑", c.line(72).c_str());  // keycaps on the card
  r.shell.on_key(enter(), 30 + kConfirmGuardMs);  // discard (after the guard, docs/20 U18)
  r.shell.on_key(opt('s'), 450);
  r.shell.on_key(chr('y'), 460);
  r.shell.on_key(enter(), 470);
  c.clear();
  r.shell.render(c, 480);
  TEST_ASSERT_EQUAL_STRING("Enter 开始 ` 返回修改", c.line(kContentRect.y + 7 * 12).c_str());  // docs/20 D6, B6
}

// docs/20 U18: the discard prompt has the 400 ms guard of every confirm box (PRD §8.1):
// an Enter right after ` (meant to save) does not throw the draft away.
static void test_discard_prompt_guards_a_fast_enter() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.on_key(chr('x'), 20);
  r.shell.on_key(chr('`'), 30);  // docs/20 D5: ` asks (was Fn+`)
  r.shell.on_key(enter(), 30 + 80);  // 80 ms later: ignored
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_TRUE(r.hint(120).find("Enter 放弃") != std::string::npos);  // still asking
  r.shell.on_key(enter(), 30 + kConfirmGuardMs - 1);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(chr('`'), 200);  // back to editing is never guarded
  TEST_ASSERT_TRUE(r.hint(210).find("Enter 放弃") == std::string::npos);
  r.shell.on_key(chr('`'), 1000);
  r.shell.on_key(enter(), 1000 + kConfirmGuardMs);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.notes.size());
}

// docs/20 U18: over the discard prompt the Tab menu's first row, the one Enter takes at
// once, goes back to editing; throwing the draft away is a row further down.
static void test_discard_menu_starts_on_back_to_editing() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.on_key(chr('x'), 20);
  r.shell.on_key(chr('`'), 30);  // docs/20 D5: ` asks (was Fn+`)
  r.shell.on_key(nav(Nav::Menu), 2000);
  r.shell.on_key(enter(), 2010);
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_TRUE(r.hint(2020).find("Enter 放弃") == std::string::npos);  // editing, the draft kept
  r.shell.on_key(enter(), 2030);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_backspace_on_an_empty_editor_closes_it);
  RUN_TEST(test_held_backspace_clears_but_does_not_close);
  RUN_TEST(test_top_left_key_cancels_confirm_after_the_guard);
  RUN_TEST(test_fn_grave_still_exits_everywhere);
  RUN_TEST(test_top_left_key_leaves_every_page);
  RUN_TEST(test_hint_bar_names_the_exit_key);
  RUN_TEST(test_every_hint_fits_the_bar);
  RUN_TEST(test_discard_prompt_and_confirm_body_name_the_key);
  RUN_TEST(test_discard_prompt_guards_a_fast_enter);
  RUN_TEST(test_discard_menu_starts_on_back_to_editing);
  return UNITY_END();
}
