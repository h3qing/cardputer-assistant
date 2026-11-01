// The back key in the editor and on its confirm box (docs/20 A5):
//   D5 (U10)  in the editor ` leaves it, as on every other page: at once when empty, after
//             "放弃？" with text; Fn+` types the backtick; there is no draft parking;
//   D6 (U9)   on the confirm box of a new session or a reply (opened from the editor) ` and
//             Fn+` go back to the editor with the text and the target kept; a confirm with
//             no draft under it (a queue item from the board) still just closes.
#include <unity.h>

#include <string>

#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  Shell shell{port, mods, 2};
  Rig() {
    port.app = demo_state();
    set_ready(port.app, true, true);
    board.slot = 1;
    shell.begin(0);
  }
  void type(const char* s, uint32_t at) {
    for (const char* c = s; *c; ++c) shell.on_key(chr(*c), at);
  }
  std::string screen(uint32_t at) const {
    FakeCanvas c;
    shell.render(c, at);
    return c.dump();
  }
};

}  // namespace

static void test_grave_leaves_an_empty_editor() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.on_key(chr('`'), 20);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.notes.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
}

// The habit "` then Enter" on an editor opened by mistake no longer saves a note "`".
static void test_grave_asks_before_dropping_text() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.type("milk", 20);
  r.shell.on_key(chr('`'), 30);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_TRUE_MESSAGE(r.screen(40).find("放弃已输入的内容？") != std::string::npos, r.screen(40).c_str());
  r.shell.on_key(chr('`'), 50);  // back to editing
  r.shell.on_key(enter(), 60);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
  TEST_ASSERT_EQUAL_STRING("milk", r.port.notes[0].text.c_str());
  r.shell.on_key(g0tap(), 100);
  r.shell.on_key(chr('`'), 110);  // empty: straight out
  r.shell.on_key(enter(), 120);   // a page's Enter, not a save
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
}

static void test_fn_grave_types_a_backtick() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.type("a", 20);
  r.shell.on_key(esc(), 30);
  r.type("b", 40);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(enter(), 50);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
  TEST_ASSERT_EQUAL_STRING("a`b", r.port.notes[0].text.c_str());
}

// D6: a typo seen on the confirm box is one key away from being fixed.
static void test_back_from_the_spawn_confirm_keeps_the_text() {
  for (const KeyEvent& back : {chr('`'), esc()}) {
    Rig r;
    r.shell.on_key(opt('s'), 10);
    r.type("write the refund tests", 20);
    r.shell.on_key(enter(), 30);
    TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
    r.shell.on_key(back, 30 + kConfirmGuardMs);
    TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
    TEST_ASSERT_EQUAL_UINT8(1, r.shell.compose_slot());
    r.shell.on_key(bksp(), 500);
    r.type("!", 510);
    r.shell.on_key(enter(), 520);
    TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
    r.shell.on_key(enter(), 520 + kConfirmGuardMs);
    TEST_ASSERT_EQUAL_UINT32(1, r.port.agents.size());
    TEST_ASSERT_EQUAL_STRING("write the refund test!", r.port.agents[0].text.c_str());
    TEST_ASSERT_TRUE(r.port.agents[0].kind == AgentKind::Spawn);
  }
}

static void test_back_from_the_reply_confirm_keeps_the_text() {
  Rig r;
  ComposeSpec reply;
  reply.kind = ComposeKind::Feedback;
  reply.slot = 1;
  reply.sid = some(Sid::from("5c2e8a14"));
  r.shell.compose(reply);
  r.type("yes", 20);
  r.shell.on_key(enter(), 30);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  r.shell.on_key(chr('`'), 30 + kConfirmGuardMs);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_TRUE_MESSAGE(r.screen(500).find("yes_") != std::string::npos, r.screen(500).c_str());
  TEST_ASSERT_TRUE_MESSAGE(r.screen(500).find("回复") != std::string::npos, r.screen(500).c_str());
}

// A queue item's confirm (no draft under it) closes as before.
static void test_back_from_a_queue_item_confirm_closes() {
  Rig r;
  ConfirmSpec item;
  item.kind = ConfirmKind::Spawn;
  item.slot = 1;
  item.qid = some(Qid::from("q18"));
  item.text = UserText::from("write docs");
  r.shell.confirm(item);
  r.shell.on_key(chr('`'), kConfirmGuardMs);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.agents.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_grave_leaves_an_empty_editor);
  RUN_TEST(test_grave_asks_before_dropping_text);
  RUN_TEST(test_fn_grave_types_a_backtick);
  RUN_TEST(test_back_from_the_spawn_confirm_keeps_the_text);
  RUN_TEST(test_back_from_the_reply_confirm_keeps_the_text);
  RUN_TEST(test_back_from_a_queue_item_confirm_closes);
  return UNITY_END();
}
