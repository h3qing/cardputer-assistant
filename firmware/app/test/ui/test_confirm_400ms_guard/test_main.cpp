// Confirm guard (04 US-09 AC1, 05 §5.6): every key in the first 400 ms after the box
// opens is ignored, including Enter, Fn+` and `; from 400 ms on, Enter confirms and Fn+`
// or ` (issue #15) cancels.
#include <unity.h>

#include "confirm.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

static ConfirmState opened_at(uint32_t t) {
  ConfirmState s;
  s.spec.kind = ConfirmKind::Spawn;
  s.spec.slot = 1;
  s.spec.text = UserText::from("write tests");
  s.opened_ms = t;
  return s;
}

static void test_keys_inside_400ms_are_ignored() {
  const ConfirmState s = opened_at(1000);
  TEST_ASSERT_TRUE(confirm_key(s, enter(), 1000) == ConfirmResult::Ignored);
  TEST_ASSERT_TRUE(confirm_key(s, enter(), 1399) == ConfirmResult::Ignored);
  TEST_ASSERT_TRUE(confirm_key(s, esc(), 1399) == ConfirmResult::Ignored);
  TEST_ASSERT_TRUE(confirm_key(s, enter(), 1400) == ConfirmResult::Accepted);
  TEST_ASSERT_TRUE(confirm_key(s, esc(), 1400) == ConfirmResult::Cancelled);
}

// Issue #15: the top-left key alone cancels too, under the same 400 ms guard.
static void test_top_left_key_cancels_after_the_guard() {
  const ConfirmState s = opened_at(1000);
  TEST_ASSERT_TRUE(confirm_key(s, chr('`'), 1000) == ConfirmResult::Ignored);
  TEST_ASSERT_TRUE(confirm_key(s, chr('`'), 1399) == ConfirmResult::Ignored);
  TEST_ASSERT_TRUE(confirm_key(s, chr('`'), 1400) == ConfirmResult::Cancelled);
  TEST_ASSERT_TRUE(confirm_key(s, bksp(), 1400) == ConfirmResult::None);
}

static void test_other_keys_do_nothing_after_the_guard() {
  const ConfirmState s = opened_at(0);
  TEST_ASSERT_TRUE(confirm_key(s, chr('y'), 5000) == ConfirmResult::None);
  TEST_ASSERT_TRUE(confirm_key(s, g0tap(), 5000) == ConfirmResult::None);
  TEST_ASSERT_TRUE(confirm_key(s, opt('h'), 5000) == ConfirmResult::None);
}

static void test_guard_survives_millis_wrap() {
  const ConfirmState s = opened_at(0xFFFFFF00u);
  TEST_ASSERT_TRUE(confirm_key(s, enter(), 0x00000010u) == ConfirmResult::Ignored);
  TEST_ASSERT_TRUE(confirm_key(s, enter(), 0xFFFFFF00u + 400u) == ConfirmResult::Accepted);
}

// Enter typed right after the compose Enter must not start a session.
static void test_shell_double_enter_does_not_execute() {
  FakePort port;
  set_projects(port.app, {project(1, "cpa", "Cardputer 助手")});
  set_ready(port.app);
  ProbeModule board("board");
  board.slot = 1;
  cpa::Module* mods[] = {&board};
  Shell shell(port, mods, 1);
  shell.begin(0);
  shell.on_key(opt('s'), 100);
  shell.on_key(chr('x'), 110);
  shell.on_key(enter(), 120);  // compose -> confirm
  TEST_ASSERT_TRUE(shell.overlay() == proto::Overlay::Confirm);
  TEST_ASSERT_EQUAL_UINT32(120, shell.overlay_ms());
  shell.on_key(enter(), 130);  // bounce: ignored
  shell.on_key(enter(), 519);
  TEST_ASSERT_EQUAL_UINT32(0, port.agents.size());
  TEST_ASSERT_TRUE(shell.overlay() == proto::Overlay::Confirm);
  shell.on_key(enter(), 520);
  TEST_ASSERT_EQUAL_UINT32(1, port.agents.size());
  TEST_ASSERT_TRUE(shell.overlay() == proto::Overlay::None);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_keys_inside_400ms_are_ignored);
  RUN_TEST(test_top_left_key_cancels_after_the_guard);
  RUN_TEST(test_other_keys_do_nothing_after_the_guard);
  RUN_TEST(test_guard_survives_millis_wrap);
  RUN_TEST(test_shell_double_enter_does_not_execute);
  return UNITY_END();
}
