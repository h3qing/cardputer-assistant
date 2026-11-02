// Inbox selection by object (docs/20 U12, A6): the cursor keeps the session it is on,
// whatever a new snapshot does to the order; Enter and Opt+D act on that session, and on
// nothing once it is gone, until the cursor moves; Opt+D's toast names the session.
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "inbox_logic.h"
#include "inbox_module.h"
#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

// wait-a (600 s) above wait-b (300 s): waiting sessions, the longest wait first.
static void two_waiting(AppState& s, uint32_t a_age, uint32_t b_age) {
  set_sessions(s, {session("aaaa0001", 1, "wait-a", SessionKind::Bg, SessionState::Waiting, true, a_age, "sa"),
                   session("bbbb0002", 2, "wait-b", SessionKind::Bg, SessionState::Waiting, true, b_age, "sb")});
}

struct Rig {
  FakeContext ctx;
  InboxModule m;
  Rig() {
    ctx.app = demo_state();
    set_ready(ctx.app);
    two_waiting(ctx.app, 600, 300);
    m.begin(ctx);
    m.onEnter(0);
  }
  std::string draw() {
    FakeCanvas c;
    m.render(c, kContentRect);
    return c.dump();
  }
};

static void test_the_cursor_follows_the_session_not_the_row() {
  Rig r;
  r.m.onKey(chr('.'));  // wait-b, the second row
  two_waiting(r.ctx.app, 600, 900);  // wait-b now waits longest: it moves to the top
  r.m.onKey(opt('d'));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.requests.size());
  TEST_ASSERT_TRUE(r.ctx.requests[0].sid.equals("bbbb0002"));
  TEST_ASSERT_TRUE(r.ctx.toasted("已忽略：wait-b"));
  r.m.onKey(enter());
  TEST_ASSERT_TRUE(r.ctx.composed.back().sid.value.equals("bbbb0002"));
  TEST_ASSERT_EQUAL_UINT8(2, r.m.contextSlot());
  TEST_ASSERT_TRUE(r.draw().find("claude attach sb") != std::string::npos);  // the detail is wait-b's
}

// Entered and not moved yet: the first row at the time is the one selected.
static void test_the_first_row_is_held_from_the_start() {
  Rig r;
  two_waiting(r.ctx.app, 600, 900);  // re-sorted before any key
  r.m.onKey(opt('d'));
  TEST_ASSERT_TRUE(r.ctx.requests.back().sid.equals("aaaa0001"));
}

static void test_a_session_that_left_selects_nothing_until_a_move() {
  Rig r;
  r.m.onKey(chr('.'));  // wait-b
  set_sessions(r.ctx.app, {session("aaaa0001", 1, "wait-a", SessionKind::Bg, SessionState::Waiting, true, 600, "sa"),
                           session("cccc0003", 1, "wait-c", SessionKind::Bg, SessionState::Waiting, true, 100, "sc")});
  r.m.onKey(opt('d'));
  r.m.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(0, r.ctx.requests.size());  // wait-c took the row: not dismissed
  TEST_ASSERT_EQUAL_UINT32(0, r.ctx.composed.size());
  TEST_ASSERT_TRUE(r.ctx.hinted("选中的那条已经不在了：按 ; . 选一条"));  // review: says why
  r.m.onKey(chr('.'));  // a move selects again: the row that took its place
  r.m.onKey(opt('d'));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.requests.size());
  TEST_ASSERT_TRUE(r.ctx.requests[0].sid.equals("cccc0003"));
}

static void test_render_highlights_the_selected_session() {
  Rig r;
  r.m.onKey(chr('.'));  // wait-b, row 2
  two_waiting(r.ctx.app, 600, 900);  // now row 1
  FakeCanvas c;
  r.m.render(c, kContentRect);
  // the theme's list (docs/19 §6.4): 14 px rows from y 14, the selection a Select capsule
  TEST_ASSERT_TRUE(c.filled_at(static_cast<int16_t>(kContentRect.y + 14 + 7), Color::Select));
  TEST_ASSERT_FALSE(c.filled_at(static_cast<int16_t>(kContentRect.y + 28 + 7), Color::Select));
}

// Review (U22 vs U12, U14): the row's right column is no wider than v0.4.0's ("等待 59m
// tty", wait 59m tty), so with a 12-character project key the name keeps its room: the age
// is tight ("59分钟", 59 min) and the kind is left to the detail line (answer on the Mac /
// claude attach). On the theme's row (docs/19 §6.4: the stripe and 8 px insets) that room is
// 22 characters.
static void test_long_names_keep_their_room() {
  Rig r;
  set_projects(r.ctx.app, {project(1, "cardputer-as", "Cardputer")});
  set_sessions(r.ctx.app, {session("aaaa0001", 1, "fix-flaky-refund-webhook-timeout", SessionKind::Tty,
                                   SessionState::Waiting, true, 59 * 60 + 20)});
  FakeCanvas c;
  r.m.render(c, kContentRect);
  TEST_ASSERT_TRUE_MESSAGE(c.has("等待 59分钟"), c.dump().c_str());
  TEST_ASSERT_FALSE(c.has("终端"));
  TEST_ASSERT_TRUE_MESSAGE(c.has("cardputer-as/fix-flaky\xE2\x80\xA6"), c.dump().c_str());  // 22 characters and …
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_cursor_follows_the_session_not_the_row);
  RUN_TEST(test_the_first_row_is_held_from_the_start);
  RUN_TEST(test_a_session_that_left_selects_nothing_until_a_move);
  RUN_TEST(test_render_highlights_the_selected_session);
  RUN_TEST(test_long_names_keep_their_room);
  return UNITY_END();
}
