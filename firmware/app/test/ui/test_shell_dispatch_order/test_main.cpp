// Key dispatch order (05 §5.6, 04 §8.1): overlay -> global combos -> module. Inside
// Compose, Opt+1..9 retargets instead of switching projects and Opt+H/I are ignored.
// Back that the module does not handle pops the navigation stack (<= 4 levels).
// v0.4.0 (docs/13 §5.4): the lock gate, then the menu, then Tab / Opt+L per the §5.3
// table, before the v0.3 order.
#include <unity.h>

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
  ProbeModule system{"system", true};  // claims every key
  cpa::Module* mods[3] = {&board, &inbox, &system};
  Shell shell{port, mods, 3};
  Rig() {
    port.app = demo_state();
    shell.begin(0);
  }
};

static void test_starts_on_board_home() {
  Rig r;
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_EQUAL_UINT32(1, r.board.entered.size());
  TEST_ASSERT_EQUAL_UINT8(0, r.board.entered[0]);
}

static void test_global_combo_beats_the_module() {
  Rig r;
  r.shell.on_key(opt('2'), 10);
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_EQUAL_UINT8(2, r.board.entered.back());
  r.shell.on_key(opt('i'), 20);
  TEST_ASSERT_EQUAL_STRING("inbox", r.shell.top_id());
  r.shell.on_key(opt('m'), 30);
  TEST_ASSERT_EQUAL_STRING("system", r.shell.top_id());
  r.shell.on_key(opt('h'), 40);
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_EQUAL_UINT8(0, r.board.entered.back());
}

static void test_other_keys_reach_the_module() {
  Rig r;
  r.shell.on_key(chr(';'), 10);
  r.shell.on_key(enter(), 20);
  r.shell.on_key(opt('d'), 30);
  TEST_ASSERT_EQUAL_UINT32(3, r.board.keys.size());
  TEST_ASSERT_TRUE(r.board.keys[2] == opt('d'));
}

static void test_compose_overlay_eats_everything() {
  Rig r;
  r.shell.on_key(g0tap(), 10);
  r.shell.tick(10 + kDoubleTapMs);  // gh #51: the note opens once no second tap came
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(opt('3'), 20);  // retarget, not a project page
  r.shell.on_key(opt('h'), 30);  // ignored
  r.shell.on_key(opt('i'), 40);  // ignored
  r.shell.on_key(chr(';'), 50);  // typed
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_EQUAL_UINT8(0, r.board.entered.back());
  TEST_ASSERT_EQUAL_UINT8(3, r.shell.compose_slot());
  r.shell.on_key(enter(), 60);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
  TEST_ASSERT_TRUE(r.port.notes[0].text.equals(";"));
  TEST_ASSERT_FALSE(r.port.notes[0].project.has);  // slot 3 does not exist: unassigned
}

static void test_compose_default_target_follows_context() {
  Rig r;
  r.board.slot = 1;  // looking at project 1
  r.shell.on_key(opt('n'), 10);
  TEST_ASSERT_EQUAL_UINT8(1, r.shell.compose_slot());
  TEST_ASSERT_EQUAL_UINT8(1, r.port.context_slot);
  r.shell.on_key(chr('x'), 20);
  r.shell.on_key(g0tap(), 30);  // G0 submits inside Compose
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
  TEST_ASSERT_TRUE(r.port.notes[0].project.value.equals("cpa"));
}

static void test_back_pops_when_module_does_not_handle_it() {
  Rig r;
  r.shell.on_key(opt('i'), 10);
  TEST_ASSERT_EQUAL_STRING("inbox", r.shell.top_id());
  r.shell.on_key(esc(), 20);  // offered to inbox first, then popped
  TEST_ASSERT_EQUAL_UINT32(1, r.inbox.keys.size());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.shell.on_key(opt('m'), 30);
  r.shell.on_key(esc(), 40);  // system claims it: no pop
  TEST_ASSERT_EQUAL_STRING("system", r.shell.top_id());
  r.shell.on_key(chr('`'), 50);  // list Back, also claimed
  TEST_ASSERT_EQUAL_STRING("system", r.shell.top_id());
}

static void test_list_back_keys_pop_too() {
  Rig r;
  r.shell.on_key(opt('i'), 10);
  r.shell.on_key(bksp(), 20);
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.shell.on_key(opt('i'), 30);
  r.shell.on_key(chr('`'), 40);
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.shell.on_key(esc(), 50);  // empty stack: stays home
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
}

static void test_stack_keeps_at_most_four_levels() {
  NavStack s = nav_home(0);
  s = nav_open(s, NavEntry{1, 0});
  s = nav_open(s, NavEntry{0, 1});
  s = nav_open(s, NavEntry{2, 0});
  TEST_ASSERT_EQUAL_UINT8(4, s.n);
  s = nav_open(s, NavEntry{1, 0});
  TEST_ASSERT_EQUAL_UINT8(4, s.n);
  TEST_ASSERT_TRUE(s.items[0] == (NavEntry{1, 0}));  // home dropped
  s = nav_open(s, NavEntry{1, 0});                   // same on top: no new level
  TEST_ASSERT_EQUAL_UINT8(4, s.n);
  s = nav_open(s, NavEntry{1, 5});  // same module: replaces the arg
  TEST_ASSERT_EQUAL_UINT8(4, s.n);
  TEST_ASSERT_EQUAL_UINT8(5, nav_top(s).arg);
  for (int i = 0; i < 4; ++i) s = nav_pop(s, 0);
  TEST_ASSERT_EQUAL_UINT8(1, s.n);
  TEST_ASSERT_TRUE(nav_top(s) == (NavEntry{0, 0}));
}

static void test_g0_long_and_unbound_opt_do_not_type() {
  Rig r;
  r.shell.on_key(g0long(), 10);
  r.shell.on_key(opt('z'), 20);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  FakeCanvas c;
  r.shell.render(c, 30);
  // v0.3 (docs/11 §4.1): G0 held is a voice memo; this port has no SD card, so it is refused
  TEST_ASSERT_TRUE(c.has("录音需要 SD 卡"));
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.board.keys.size());  // opt+z went to the module, which ignored it
}

// docs/20 D4 replaces docs/11 §4.1's "overlays are exclusive" for G0: held, it records a
// memo while Compose (with text in it) or Confirm is open, which stay as they are (docs/13
// principle 5). Opt+R (a meeting, which asks first) still starts nothing there. gh #51
// review: an empty quick note closes and records instead (shell_g0.cpp, test_g0_double_tap).
static void test_recording_gestures_under_compose_and_confirm() {
  Rig r;
  r.port.next_refusal = rec::RecRefusal::None;
  r.shell.on_key(g0tap(), 10);
  r.shell.tick(10 + kDoubleTapMs);  // gh #51
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(chr('x'), 15);
  r.shell.on_key(g0long(), 20);
  r.shell.on_key(opt('r'), 30);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_TRUE(r.port.gestures[0].first == RecMode::Memo);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
  r.shell.on_key(bksp(), 35);    // empty again
  r.shell.on_key(chr('`'), 40);  // docs/20 D5: ` leaves the empty editor
  set_ready(r.port.app);
  r.board.slot = 1;
  r.shell.on_key(opt('s'), 50);
  r.shell.on_key(chr('x'), 60);
  r.shell.on_key(enter(), 70);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  r.shell.on_key(g0long(), 1000);
  r.shell.on_key(opt('r'), 1000);
  TEST_ASSERT_EQUAL_UINT32(2, r.port.gestures.size());
  TEST_ASSERT_TRUE(r.port.gestures[1].first == RecMode::Memo);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
}

static void test_confirm_overlay_eats_globals() {
  Rig r;
  set_ready(r.port.app);
  r.board.slot = 1;
  r.shell.on_key(opt('s'), 10);
  r.shell.on_key(chr('x'), 10);
  r.shell.on_key(enter(), 10);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  r.shell.on_key(opt('i'), 1000);
  r.shell.on_key(g0tap(), 1000);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.shell.on_key(esc(), 1000);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);  // docs/20 D6: back to the draft
  TEST_ASSERT_EQUAL_UINT32(0, r.port.agents.size());
}

static void test_duplicate_and_failed_modules_are_disabled() {
  FakePort port;
  ProbeModule a{"board"};
  ProbeModule b{"board"};
  ProbeModule bad{"Bad-Id"};
  ProbeModule off{"system"};
  off.begin_ok = false;
  cpa::Module* mods[] = {&a, &b, &bad, &off};
  Shell shell(port, mods, 4);
  shell.begin(0);
  TEST_ASSERT_EQUAL_UINT8(3, shell.disabled_count());
  shell.on_key(opt('m'), 10);  // system is disabled: stays home with a toast
  TEST_ASSERT_EQUAL_STRING("board", shell.top_id());
  TEST_ASSERT_TRUE(b.ctx == nullptr);
}

// -- v0.4.0 (docs/13 §5.3-§5.5) ---------------------------------------------------

// The runtime's order: the lock gate first (docs/13 §5.4 step 1), then the shell.
static void gated(Shell& s, const KeyEvent& ev, uint32_t now) {
  if (s.lock_gate(ev, now)) s.on_key(ev, now);
}

static void set_rec(FakePort& p, RecPhase phase, RecMode mode) {
  p.app.rec.avail = true;
  p.app.rec.phase = phase;
  p.app.rec.mode = mode;
}

// Step 3: the open menu eats every key but G0 and (docs/20 D9) a chord it shows; Tab, `
// and Fn+` close it. Opt+I, Opt+N and Opt+L, which v0.4 ate here, are tested in
// test_menu_chords; Opt+Z and Opt+A (on no page of the menu) are still eaten.
static void test_menu_eats_keys() {
  Rig r;
  r.shell.on_key(nav(Nav::Menu), 10);
  TEST_ASSERT_TRUE(r.shell.menu_open());
  r.shell.on_key(opt('z'), 20);
  r.shell.on_key(chr(';'), 30);
  r.shell.on_key(opt('a'), 40);
  r.shell.on_key(chr('x'), 50);
  TEST_ASSERT_TRUE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::None);
  TEST_ASSERT_FALSE(r.shell.locked());
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
  r.shell.on_key(nav(Nav::Menu), 55);
  TEST_ASSERT_FALSE(r.shell.menu_open());
  for (const KeyEvent& close : {nav(Nav::Menu), chr('`'), esc(), bksp()}) {
    r.shell.on_key(nav(Nav::Menu), 60);
    TEST_ASSERT_TRUE(r.shell.menu_open());
    r.shell.on_key(close, 70);
    TEST_ASSERT_FALSE(r.shell.menu_open());
  }
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());  // closing did not go back
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
}

// docs/13 §3.5, §6.1, §7.1: during a meeting G0 is always ★, menu or lock.
static void test_g0_stars_a_meeting_under_the_menu_and_the_lock() {
  Rig r;
  set_rec(r.port, RecPhase::Recording, RecMode::Meeting);
  r.shell.on_key(nav(Nav::Menu), 10);
  TEST_ASSERT_TRUE(r.shell.menu_open());
  r.shell.on_key(g0tap(), 20);
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.inputs.size());
  TEST_ASSERT_TRUE(r.port.inputs[0].first == rec::RecInput::Star);
  gated(r.shell, opt('l'), 30);
  TEST_ASSERT_TRUE(r.shell.locked());
  gated(r.shell, g0tap(), 40);
  gated(r.shell, g0long(), 50);
  gated(r.shell, chr(' '), 60);  // the lock swallows Space
  TEST_ASSERT_EQUAL_UINT32(3, r.port.inputs.size());
  for (const auto& in : r.port.inputs) TEST_ASSERT_TRUE(in.first == rec::RecInput::Star);
}

// docs/13 §5.3: Opt+L does nothing in a confirm box (the box first) or the memo HUD.
// docs/20 D3 takes the meeting's two boxes out of this: there Opt+L leaves the box and
// locks (test_meeting_boxes).
static void test_opt_l_is_ignored_in_confirm_boxes_and_the_memo_hud() {
  Rig r;
  set_ready(r.port.app);
  r.board.slot = 1;
  r.shell.on_key(opt('s'), 10);
  r.shell.on_key(chr('x'), 10);
  r.shell.on_key(enter(), 10);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  gated(r.shell, opt('l'), 1000);
  TEST_ASSERT_FALSE(r.shell.locked());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  r.shell.on_key(esc(), 1000);
  for (RecPhase p : {RecPhase::Cue, RecPhase::Recording, RecPhase::Finishing}) {
    set_rec(r.port, p, RecMode::Memo);
    gated(r.shell, opt('l'), 3000);
    TEST_ASSERT_FALSE(r.shell.locked());
  }
  TEST_ASSERT_EQUAL_UINT32(0, r.port.inputs.size());
}

// docs/13 §5.3: Tab does nothing in the memo HUD (G0 is held); everywhere else it opens
// the menu, over Compose, a confirm box and the meeting page too.
static void test_tab_is_ignored_in_the_memo_hud_only() {
  Rig r;
  for (RecPhase p : {RecPhase::Cue, RecPhase::Recording, RecPhase::Finishing}) {
    set_rec(r.port, p, RecMode::Memo);
    r.shell.on_key(nav(Nav::Menu), 10);
    TEST_ASSERT_FALSE(r.shell.menu_open());
  }
  TEST_ASSERT_EQUAL_UINT32(0, r.port.inputs.size());
  for (RecPhase p : {RecPhase::Confirm, RecPhase::Recording, RecPhase::EndConfirm}) {
    set_rec(r.port, p, RecMode::Meeting);
    r.shell.on_key(nav(Nav::Menu), 20);
    TEST_ASSERT_TRUE(r.shell.menu_open());
    r.shell.on_key(nav(Nav::Menu), 20);
  }
  set_rec(r.port, RecPhase::Idle, RecMode::Memo);
  r.shell.on_key(g0tap(), 30);
  r.shell.on_key(nav(Nav::Menu), 40);
  TEST_ASSERT_TRUE(r.shell.menu_open());
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);  // the draft is under the menu
}

// docs/13 §5.5: the home module is the root when registered (Opt+H), the board is Opt+P;
// without it the board stays the root, as in v0.3.
static void test_home_is_the_root_when_registered() {
  FakePort port;
  port.app = demo_state();
  ProbeModule board{"board"};
  ProbeModule home{"home"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[] = {&board, &home, &inbox};
  Shell shell(port, mods, 3);
  shell.begin(0);
  TEST_ASSERT_EQUAL_STRING("home", shell.top_id());
  shell.on_key(opt('p'), 10);
  TEST_ASSERT_EQUAL_STRING("board", shell.top_id());
  TEST_ASSERT_EQUAL_UINT8(0, board.entered.back());
  shell.on_key(opt('i'), 20);
  shell.on_key(opt('h'), 30);
  TEST_ASSERT_EQUAL_STRING("home", shell.top_id());
  shell.on_key(opt('2'), 40);
  shell.on_key(esc(), 50);  // back from a project page: the root
  TEST_ASSERT_EQUAL_STRING("home", shell.top_id());
  Rig plain;
  plain.shell.on_key(opt('i'), 10);
  plain.shell.on_key(opt('p'), 20);
  TEST_ASSERT_EQUAL_STRING("board", plain.shell.top_id());
  TEST_ASSERT_EQUAL_UINT8(0, plain.board.entered.back());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_on_board_home);
  RUN_TEST(test_global_combo_beats_the_module);
  RUN_TEST(test_other_keys_reach_the_module);
  RUN_TEST(test_compose_overlay_eats_everything);
  RUN_TEST(test_compose_default_target_follows_context);
  RUN_TEST(test_back_pops_when_module_does_not_handle_it);
  RUN_TEST(test_list_back_keys_pop_too);
  RUN_TEST(test_stack_keeps_at_most_four_levels);
  RUN_TEST(test_g0_long_and_unbound_opt_do_not_type);
  RUN_TEST(test_recording_gestures_under_compose_and_confirm);
  RUN_TEST(test_confirm_overlay_eats_globals);
  RUN_TEST(test_duplicate_and_failed_modules_are_disabled);
  RUN_TEST(test_menu_eats_keys);
  RUN_TEST(test_g0_stars_a_meeting_under_the_menu_and_the_lock);
  RUN_TEST(test_opt_l_is_ignored_in_confirm_boxes_and_the_memo_hud);
  RUN_TEST(test_tab_is_ignored_in_the_memo_hud_only);
  RUN_TEST(test_home_is_the_root_when_registered);
  return UNITY_END();
}
