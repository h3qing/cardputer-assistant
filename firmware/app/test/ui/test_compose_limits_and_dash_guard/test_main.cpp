// Compose limits (04 US-01 AC3/AC5, US-02, 05 §5.6): ASCII only, one line, 1..500
// bytes with a visible byte count, spawn / feedback never start with '-', queue and
// spawn need a project, ` asks before throwing text away (docs/20 D5: ` leaves, Fn+`
// types it), Backspace on empty text exits (issue #15).
#include <unity.h>

#include <string>

#include "compose.h"
#include "core/support/lang_pin.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

static ComposeState typed(ComposeKind kind, uint8_t slot, const char* text) {
  ComposeSpec spec;
  spec.kind = kind;
  spec.slot = slot;
  ComposeState s = compose_open(spec);
  for (const char* p = text; *p; ++p) s = compose_key(s, chr(*p)).state;
  return s;
}

static void test_typing_appends_ascii_and_backspace_deletes() {
  const ComposeState s = typed(ComposeKind::Note, 0, "hi there");
  TEST_ASSERT_TRUE(s.text.equals("hi there"));
  const ComposeStep del = compose_key(s, bksp());
  TEST_ASSERT_TRUE(del.state.text.equals("hi ther"));
  TEST_ASSERT_TRUE(del.result == ComposeResult::Edited);
}

// Issue #15: with nothing to delete there is nothing to lose, so a fresh Backspace on
// an empty editor exits. Deleting the last character does not exit, and neither does a
// held Backspace that keeps repeating after the text is gone.
static void test_backspace_on_empty_text_exits() {
  TEST_ASSERT_TRUE(compose_key(compose_open(ComposeSpec{}), bksp()).result == ComposeResult::Cancelled);
  const ComposeStep last = compose_key(typed(ComposeKind::Note, 0, "x"), bksp());
  TEST_ASSERT_TRUE(last.result == ComposeResult::Edited);
  TEST_ASSERT_TRUE(last.state.text.empty());
  KeyEvent held = bksp();
  held.repeat = true;
  const ComposeStep still = compose_key(last.state, held);
  TEST_ASSERT_TRUE(still.result == ComposeResult::None);
  TEST_ASSERT_TRUE(compose_key(still.state, bksp()).result == ComposeResult::Cancelled);
  // blank text is content too: spaces are deleted one by one, never an exit
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Note, 0, " "), bksp()).result == ComposeResult::Edited);
}

static void test_non_ascii_and_control_chars_are_refused() {
  const ComposeState s = typed(ComposeKind::Note, 0, "a");
  TEST_ASSERT_TRUE(compose_key(s, chr('\x7f')).state.text.equals("a"));
  TEST_ASSERT_TRUE(compose_key(s, chr('\x1f')).state.text.equals("a"));
  TEST_ASSERT_TRUE(compose_key(s, chr(static_cast<char>(0xE4))).state.text.equals("a"));
  TEST_ASSERT_TRUE(compose_key(s, chr('~')).state.text.equals("a~"));
  TEST_ASSERT_TRUE(compose_key(s, chr(' ')).state.text.equals("a "));
}

static void test_input_stops_at_500_bytes() {
  ComposeState s = compose_open(ComposeSpec{});
  for (int i = 0; i < 500; ++i) s = compose_key(s, chr('x')).state;
  TEST_ASSERT_EQUAL_UINT32(500, s.text.size());
  const ComposeStep more = compose_key(s, chr('y'));
  TEST_ASSERT_TRUE(more.result == ComposeResult::Full);
  TEST_ASSERT_EQUAL_UINT32(500, more.state.text.size());
  TEST_ASSERT_TRUE(compose_key(s, enter()).result == ComposeResult::Submit);
}

static void test_empty_or_blank_text_is_not_submitted() {
  TEST_ASSERT_TRUE(compose_key(compose_open(ComposeSpec{}), enter()).result == ComposeResult::EmptyText);
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Note, 0, "   "), enter()).result == ComposeResult::EmptyText);
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Note, 0, "x"), g0tap()).result == ComposeResult::Submit);
}

static void test_dash_guard_for_spawn_and_feedback_only() {
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Spawn, 1, "-c test"), enter()).result == ComposeResult::DashText);
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Spawn, 1, "  --x"), enter()).result == ComposeResult::DashText);
  TEST_ASSERT_TRUE(compose_dash_blocked(typed(ComposeKind::Spawn, 1, " -")));
  ComposeSpec fb;
  fb.kind = ComposeKind::Feedback;
  fb.slot = 1;
  fb.sid = some(Sid::from("5c2e8a14"));
  ComposeState f = compose_open(fb);
  f = compose_key(f, chr('-')).state;
  TEST_ASSERT_TRUE(compose_key(f, enter()).result == ComposeResult::DashText);
  // notes and queue items may start with '-'
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Note, 0, "-ok"), enter()).result == ComposeResult::Submit);
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Queue, 1, "-ok"), enter()).result == ComposeResult::Submit);
  TEST_ASSERT_FALSE(compose_dash_blocked(typed(ComposeKind::Note, 0, "-ok")));
  TEST_ASSERT_TRUE(compose_key(typed(ComposeKind::Spawn, 1, "a-b"), enter()).result == ComposeResult::Submit);
}

static void test_kind_and_target_switch_with_opt() {
  ComposeState s = typed(ComposeKind::Note, 0, "x");
  s = compose_key(s, opt('q')).state;
  TEST_ASSERT_TRUE(s.spec.kind == ComposeKind::Queue);
  TEST_ASSERT_TRUE(compose_key(s, enter()).result == ComposeResult::NeedProject);
  s = compose_key(s, opt('3')).state;
  TEST_ASSERT_EQUAL_UINT8(3, s.spec.slot);
  TEST_ASSERT_TRUE(compose_key(s, enter()).result == ComposeResult::Submit);
  s = compose_key(s, opt('s')).state;
  TEST_ASSERT_TRUE(s.spec.kind == ComposeKind::Spawn);
  s = compose_key(s, opt('0')).state;
  TEST_ASSERT_EQUAL_UINT8(0, s.spec.slot);
  TEST_ASSERT_TRUE(compose_key(s, enter()).result == ComposeResult::NeedProject);
  s = compose_key(s, opt('n')).state;
  TEST_ASSERT_TRUE(s.spec.kind == ComposeKind::Note);
  TEST_ASSERT_TRUE(compose_key(s, enter()).result == ComposeResult::Submit);  // a note may be unassigned
  TEST_ASSERT_TRUE(s.text.equals("x"));                                      // switching kept the text
}

static void test_global_combos_are_eaten() {
  const ComposeState s = typed(ComposeKind::Note, 2, "x");
  const ComposeStep h = compose_key(s, opt('h'));
  TEST_ASSERT_TRUE(h.result == ComposeResult::None);
  TEST_ASSERT_TRUE(h.state.text.equals("x"));
  TEST_ASSERT_EQUAL_UINT8(2, h.state.spec.slot);
  TEST_ASSERT_TRUE(compose_key(s, opt('i')).result == ComposeResult::None);
  TEST_ASSERT_TRUE(compose_key(s, g0long()).result == ComposeResult::None);
}

static void test_feedback_keeps_its_session_until_the_kind_changes() {
  ComposeSpec fb;
  fb.kind = ComposeKind::Feedback;
  fb.slot = 1;
  fb.sid = some(Sid::from("5c2e8a14"));
  ComposeState s = compose_open(fb);
  s = compose_key(s, opt('2')).state;  // the target of feedback is the session
  TEST_ASSERT_EQUAL_UINT8(1, s.spec.slot);
  s = compose_key(s, opt('q')).state;
  TEST_ASSERT_TRUE(s.spec.kind == ComposeKind::Queue);
  TEST_ASSERT_FALSE(s.spec.sid.has);
}

// docs/20 D5: the cancel key is ` (it was Fn+`, which now types the backtick:
// test_compose_back); on the prompt both still go back.
static void test_cancel_asks_only_when_text_exists() {
  TEST_ASSERT_TRUE(compose_key(compose_open(ComposeSpec{}), chr('`')).result == ComposeResult::Cancelled);
  const ComposeStep ask = compose_key(typed(ComposeKind::Note, 0, "keep me"), chr('`'));
  TEST_ASSERT_TRUE(ask.result == ComposeResult::DiscardAsked);
  TEST_ASSERT_TRUE(ask.state.discard);
  // Esc goes back to editing, the text is intact
  const ComposeStep back = compose_key(ask.state, esc());
  TEST_ASSERT_FALSE(back.state.discard);
  TEST_ASSERT_TRUE(back.state.text.equals("keep me"));
  // the top-left key alone goes back as well (issue #15)
  const ComposeStep grave = compose_key(ask.state, chr('`'));
  TEST_ASSERT_FALSE(grave.state.discard);
  TEST_ASSERT_TRUE(grave.result == ComposeResult::Edited);
  TEST_ASSERT_TRUE(grave.state.text.equals("keep me"));
  // while asking, typing does nothing; Enter discards
  TEST_ASSERT_TRUE(compose_key(ask.state, chr('z')).state.text.equals("keep me"));
  TEST_ASSERT_TRUE(compose_key(ask.state, chr('z')).state.discard);
  TEST_ASSERT_TRUE(compose_key(ask.state, enter()).result == ComposeResult::Cancelled);
}

static void test_shell_shows_the_byte_count_and_refusals() {
  FakePort port;
  ProbeModule board("board");
  cpa::Module* mods[] = {&board};
  Shell shell(port, mods, 1);
  shell.begin(0);
  shell.on_key(opt('s'), 10);  // spawn, no project page open: target unassigned
  for (char c : std::string("-c test")) shell.on_key(chr(c), 20);
  FakeCanvas canvas;
  shell.render(canvas, 30);
  TEST_ASSERT_TRUE(canvas.has("7/500"));
  shell.on_key(enter(), 40);
  TEST_ASSERT_EQUAL_UINT32(0, port.agents.size());
  TEST_ASSERT_EQUAL_UINT32(0, port.queued.size());
  TEST_ASSERT_TRUE(shell.overlay() == proto::Overlay::Compose);
  canvas.clear();
  shell.render(canvas, 50);
  TEST_ASSERT_TRUE(canvas.has("'-'"));  // the refusal is shown
}

// docs/19 §6.9 (the judges' notes): the title in a Raised pill at (4, 13); the text in a
// card at (4, 28, 232, 92), 6 px in, 36 characters a row; the byte count and a refusal in
// the card's bottom corners. Fn+`'s question is a card too, its keys as keycaps.
static void test_compose_sits_on_a_card() {
  FakePort port;
  ProbeModule board("board");
  cpa::Module* mods[] = {&board};
  Shell shell(port, mods, 1);
  shell.begin(0);
  shell.on_key(opt('s'), 10);
  for (char c : std::string("-0123456789abcdefghijklmnopqrstuvwxyz")) shell.on_key(chr(c), 20);
  FakeCanvas c;
  shell.render(c, 30);
  const TextOp* title = c.find("开 session → 未归属");  // docs/20 U22
  TEST_ASSERT_TRUE(title != nullptr && title->x == 8 && title->y == 13 && title->style.fg == Color::Ink2);
  TEST_ASSERT_TRUE(c.filled_at(13, Color::Raised));
  TEST_ASSERT_TRUE(c.filled_at(60, Color::Card) && c.filled_at(28, Color::CardRim));
  const TextOp* first = c.find("-0123");
  TEST_ASSERT_TRUE(first->x == 10 && first->y == 34);
  TEST_ASSERT_EQUAL_UINT32(36, first->text.size());
  TEST_ASSERT_EQUAL_STRING("z_", c.find("z_")->text.c_str());  // the 37th character starts row 2
  TEST_ASSERT_EQUAL_INT16(46, c.find("z_")->y);
  const TextOp* count = c.find("37/500");
  TEST_ASSERT_TRUE(count->y == 102 && count->style.fg == Color::Ink3);
  TEST_ASSERT_EQUAL_INT16(4 + 232 - 6, count->x + c.textWidth("37/500"));
  const TextOp* dash = c.find("'-'");
  TEST_ASSERT_TRUE(dash->x == 10 && dash->y == 102 && dash->style.fg == Color::Warn);
  shell.on_key(chr('`'), 40);  // docs/20 D5: `: discard?
  FakeCanvas d;
  shell.render(d, 50);
  const TextOp* ask = d.find("放弃已输入的内容？");
  TEST_ASSERT_TRUE(ask->x == 12 && ask->style.fg == Color::Warn && d.filled_at(ask->y, Color::Card));
  TEST_ASSERT_EQUAL_STRING("Enter 放弃 ` 返回编辑", d.line(72).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_typing_appends_ascii_and_backspace_deletes);
  RUN_TEST(test_backspace_on_empty_text_exits);
  RUN_TEST(test_non_ascii_and_control_chars_are_refused);
  RUN_TEST(test_input_stops_at_500_bytes);
  RUN_TEST(test_empty_or_blank_text_is_not_submitted);
  RUN_TEST(test_dash_guard_for_spawn_and_feedback_only);
  RUN_TEST(test_kind_and_target_switch_with_opt);
  RUN_TEST(test_global_combos_are_eaten);
  RUN_TEST(test_feedback_keeps_its_session_until_the_kind_changes);
  RUN_TEST(test_cancel_asks_only_when_text_exists);
  RUN_TEST(test_shell_shows_the_byte_count_and_refusals);
  RUN_TEST(test_compose_sits_on_a_card);
  return UNITY_END();
}
