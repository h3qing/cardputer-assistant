// The shell's G0 routing (gh #51, docs/20 U26, g0_gesture.h): a G0 tap that opens the quick
// note opens it once kDoubleTapMs passed without a second press (or before another key);
// a second press inside the window starts a hands-free memo on its down edge, and the rest
// of that press is eaten; a hands-free memo keeps its memo keys, takes Tab and Opt+L, and
// one G0 press ends it. Locked, G0 acts on the lit card only. The runtime calls on_key
// for G0's events and on_g0 for its edges (after that sample's events); lock_gate first.
#include <unity.h>

#include "core/support/lang_pin.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;
using rec::RecInput;

void setUp() { testsupport::pin_zh(); }
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
    port.next_refusal = rec::RecRefusal::None;
    shell.begin(0);
  }
  // What the runtime does with one G0 event: the lock gate, then the shell.
  void key(const KeyEvent& ev, uint32_t now) {
    if (shell.lock_gate(ev, now)) shell.on_key(ev, now);
  }
  // A tap: down at `at`, up (its G0Tap, then the up edge) 80 ms later.
  void tap(uint32_t at) {
    shell.on_g0(true, at);
    key(g0tap(), at + 80);
    shell.on_g0(false, at + 80);
  }
  void hands_free() {
    port.app.rec.avail = true;
    port.app.rec.phase = RecPhase::Recording;
    port.app.rec.mode = RecMode::Memo;
    port.app.rec.latched = true;
  }
  bool composing() const { return shell.overlay() == proto::Overlay::Compose; }
  size_t sent(RecInput in) const {
    size_t n = 0;
    for (const auto& p : port.inputs) n += p.first == in ? 1 : 0;
    return n;
  }
};

}  // namespace

// One tap: the note opens kDoubleTapMs after the release, not before, and only once.
static void test_a_tap_opens_the_note_after_the_window() {
  Rig r;
  r.tap(100);  // released at 180
  TEST_ASSERT_FALSE(r.composing());
  r.shell.tick(180 + kDoubleTapMs - 1);
  TEST_ASSERT_FALSE(r.composing());
  r.shell.tick(180 + kDoubleTapMs);
  TEST_ASSERT_TRUE(r.composing());
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.board.keys.size());
}

// The second press inside the window: a memo, latched at once, on the down edge. Its tap
// is eaten (no note, no submit), and no note comes after it.
static void test_a_double_tap_starts_a_hands_free_memo() {
  Rig r;
  r.tap(100);
  r.shell.on_g0(true, 180 + 200);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_TRUE(r.port.gestures[0].first == RecMode::Memo);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Latch));
  r.key(g0tap(), 460);
  r.shell.on_g0(false, 460);
  r.shell.tick(2000);
  TEST_ASSERT_FALSE(r.composing());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.sent(RecInput::Release));  // nothing records here: no memo page
}

// Refused (no card): the toast says why, nothing latches, and the eaten tap opens nothing.
static void test_a_refused_double_tap() {
  Rig r;
  r.port.next_refusal = rec::RecRefusal::NoSd;
  r.tap(100);
  r.tap(300);
  r.shell.tick(2000);
  TEST_ASSERT_FALSE(r.composing());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.sent(RecInput::Latch));
}

// Too late (at kLateTapMs, no tick since the tap: a long loop pass): the note opens first
// and the press is an ordinary press in it.
static void test_a_late_second_press_is_a_press_in_the_note() {
  Rig r;
  r.tap(100);
  r.shell.on_g0(true, 180 + kLateTapMs);
  TEST_ASSERT_TRUE(r.composing());
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
}

// Another key inside the window: the note opens first, and the key lands in it.
static void test_another_key_lands_in_the_note() {
  Rig r;
  r.tap(100);
  r.shell.on_key(chr('a'), 200);
  TEST_ASSERT_TRUE(r.composing());
  r.shell.on_key(enter(), 300);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());
  TEST_ASSERT_EQUAL_STRING("a", r.port.notes[0].text.c_str());
}

// Tap, then hold: the memo starts on the down edge; the press's G0Long makes it a held memo
// (Hold), its release reaches the memo, and nothing else hears the press.
static void test_tap_then_hold_is_a_held_memo() {
  Rig r;
  r.tap(100);
  r.shell.on_g0(true, 300);
  r.hands_free();  // the runtime mirrors the latched memo
  r.key(g0long(), 800);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Hold));
  r.port.app.rec.latched = false;
  r.key(g0longup(), 2000);
  r.shell.on_g0(false, 2000);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Release));
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_FALSE(r.composing());
}

// A held press (no tap before it) is push-to-talk as before: G0Long starts the memo.
static void test_a_hold_is_push_to_talk() {
  Rig r;
  r.shell.on_g0(true, 100);
  r.key(g0long(), 600);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.sent(RecInput::Latch));
}

// A hands-free memo: digits tag it; Space, `, Opt+R and letters do nothing (pocket presses);
// Tab opens the menu; one G0 press ends it on its down edge (and closes the menu), and the
// rest of that press is eaten: no note after it, whatever the memo did meanwhile.
static void test_hands_free_keys_and_the_end() {
  Rig r;
  r.hands_free();
  for (const KeyEvent& k : {chr('3'), chr(' '), chr('`'), opt('r'), chr('a'), esc()}) r.shell.on_key(k, 100);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.inputs.size());
  TEST_ASSERT_TRUE(r.port.inputs[0].first == RecInput::Tag && r.port.inputs[0].second == 3);
  r.shell.on_key(nav(Nav::Menu), 200);
  TEST_ASSERT_TRUE(r.shell.menu_open());
  r.shell.on_g0(true, 300);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Press));
  TEST_ASSERT_FALSE(r.shell.menu_open());
  r.port.app.rec.phase = RecPhase::Idle;  // it ended at once
  r.port.app.rec.latched = false;
  r.key(g0long(), 800);
  r.key(g0longup(), 900);
  r.shell.on_g0(false, 900);
  r.shell.tick(3000);
  TEST_ASSERT_FALSE(r.composing());
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Press));
}

// The menu's rows: "G0 Stop and save" is shown only (review of gh #51: Tab, Enter are two
// pocket presses, and they must not end it); "G0 x2" (Go page) starts one, and as G0 is up
// then, the core hears its release at once: a memo that ends by itself is not held (U15).
static void test_menu_rows_show_the_end_and_start() {
  Rig r;
  r.hands_free();
  r.shell.on_key(nav(Nav::Menu), 100);
  r.shell.on_key(enter(), 200);  // the first row: G0
  TEST_ASSERT_EQUAL_UINT32(0, r.port.inputs.size());
  FakeCanvas c;
  r.shell.render(c, 250);
  TEST_ASSERT_TRUE_MESSAGE(c.has("按一下 G0 结束并保存"), c.dump().c_str());  // what to do instead
  Rig s;
  s.shell.on_key(KeyEvent{KeyEventKind::G0Double, 0, 0, 0, Nav::None}, 100);  // as the row replays it
  TEST_ASSERT_EQUAL_UINT32(1, s.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(2, s.port.inputs.size());
  TEST_ASSERT_TRUE(s.port.inputs[0].first == RecInput::Latch && s.port.inputs[1].first == RecInput::Release);
  Rig d;  // the double tap: its second press is down, no release
  d.tap(100);
  d.shell.on_g0(true, 300);
  TEST_ASSERT_EQUAL_UINT32(1, d.sent(RecInput::Latch));
  TEST_ASSERT_EQUAL_UINT32(0, d.sent(RecInput::Release));
}

// Tapping twice to end, as it started: the press that ended it hushes a press right after
// its release, so no note opens, whether the save is done by then or not.
static void test_a_double_tap_to_end_opens_nothing() {
  Rig r;
  r.hands_free();
  r.shell.on_g0(true, 1000);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Press));
  r.port.app.rec.phase = RecPhase::Idle;  // saved at once
  r.port.app.rec.latched = false;
  r.key(g0tap(), 1080);
  r.shell.on_g0(false, 1080);
  r.tap(1180);  // habit: a second tap, 100 ms after
  r.shell.tick(3000);
  TEST_ASSERT_FALSE(r.composing());
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  r.tap(4000);  // a tap later is a tap again
  r.shell.tick(4080 + kDoubleTapMs);
  TEST_ASSERT_TRUE(r.composing());
}

// A slow second tap (from kDoubleTapMs to kLateTapMs, a stiff button): the empty note it
// opened closes and the hands-free memo starts on that press; its tap submits nothing.
static void test_a_slow_second_tap_closes_the_note() {
  for (uint32_t gap : {kDoubleTapMs + 10, 500u, kLateTapMs - 10}) {
    Rig r;
    r.tap(100);  // released at 180
    r.shell.tick(180 + kDoubleTapMs);
    TEST_ASSERT_TRUE(r.composing());
    r.shell.on_g0(true, 180 + gap);
    TEST_ASSERT_FALSE(r.composing());
    TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
    TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Latch));
    r.hands_free();
    r.key(g0tap(), 180 + gap + 80);
    r.shell.on_g0(false, 180 + gap + 80);
    TEST_ASSERT_EQUAL_UINT32(0, r.port.notes.size());
    TEST_ASSERT_EQUAL_UINT32(0, r.sent(RecInput::Press));
  }
}

// Not after a key (it landed in the note), nor at kLateTapMs: G0 is the note's again.
static void test_a_note_touched_or_late_keeps_g0() {
  Rig r;
  r.tap(100);
  r.shell.on_key(chr('a'), 200);
  r.tap(300);
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.port.notes.size());  // G0 submitted "a"
  Rig s;
  s.tap(100);
  s.shell.tick(180 + kDoubleTapMs);
  s.tap(180 + kLateTapMs);
  TEST_ASSERT_EQUAL_UINT32(0, s.port.gestures.size());
  TEST_ASSERT_TRUE(s.composing());  // the empty note's G0: "Empty: not saved", as before
}

// Holding G0 in an empty quick note (a slow tap, then hold) closes it and records, as the
// hold does on a page. With text in it, docs/20 D4: the hold records a memo of its own and
// the note stays, its text kept (before D4 the hold did nothing there).
static void test_a_hold_in_the_empty_note_records() {
  Rig r;
  r.tap(100);
  r.shell.tick(180 + kDoubleTapMs);
  r.shell.on_g0(true, 180 + kLateTapMs + 100);
  r.key(g0long(), 180 + kLateTapMs + 600);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_TRUE(r.port.gestures[0].first == RecMode::Memo);
  TEST_ASSERT_FALSE(r.composing());
  Rig s;
  s.tap(100);
  s.shell.on_key(chr('a'), 200);
  s.shell.on_g0(true, 2000);
  s.key(g0long(), 2500);
  TEST_ASSERT_EQUAL_UINT32(1, s.port.gestures.size());
  TEST_ASSERT_TRUE(s.port.gestures[0].first == RecMode::Memo);
  TEST_ASSERT_TRUE(s.composing());
}

// Opt+L on a hands-free memo locks and it goes on. Locked, G0 acts on the lit card only: a
// press from dark ends nothing and its tap lights the card; a press on the lit card ends it.
// docs/20 D7: the card says "Locked" kLockFlashMs after Opt+L; dark from then on.
static void test_locked_hands_free_ends_on_the_lit_card() {
  Rig r;
  r.hands_free();
  r.shell.on_key(opt('l'), 100);
  TEST_ASSERT_TRUE(r.shell.locked());
  r.shell.tick(100 + kLockFlashMs);
  r.tap(3000);
  TEST_ASSERT_EQUAL_UINT32(0, r.sent(RecInput::Press));
  TEST_ASSERT_TRUE(r.shell.lock_lit(3100));
  r.shell.on_g0(true, 4000);
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Press));
  TEST_ASSERT_TRUE(r.shell.locked());
  // the end lights the card for its own 3 s: it says how the memo ended (lock_screen.cpp)
  TEST_ASSERT_TRUE(r.shell.lock_lit(4000 + kLockCardMs - 1));
}

// Locked, idle: the double tap counts only on the lit card. A tap from dark lights it and is
// no first tap; then tap, tap starts the memo (and the card goes dark, as for any memo from
// it). A tap on the lit card opens nothing when the window runs out. docs/20 D7: the card
// says "Locked" kLockFlashMs after Opt+L; dark from t0.
static void test_locked_double_tap_needs_the_lit_card() {
  Rig r;
  r.shell.on_key(opt('l'), 50);
  const uint32_t t0 = 50 + kLockFlashMs;
  r.shell.tick(t0);
  TEST_ASSERT_FALSE(r.shell.lock_lit(t0));
  r.tap(t0 + 100);  // from dark: lights the card
  TEST_ASSERT_TRUE(r.shell.lock_lit(t0 + 200));
  r.shell.on_g0(true, t0 + 180 + 100);  // in what would be the window
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  r.key(g0tap(), t0 + 330);
  r.shell.on_g0(false, t0 + 330);
  r.shell.tick(t0 + 330 + kDoubleTapMs);  // the lit card's tap waited, and opens nothing
  TEST_ASSERT_FALSE(r.composing());
  r.shell.on_g0(true, t0 + 1000);  // after its window: no second tap
  TEST_ASSERT_EQUAL_UINT32(0, r.port.gestures.size());
  r.key(g0tap(), t0 + 1080);
  r.shell.on_g0(false, t0 + 1080);
  r.shell.on_g0(true, t0 + 1200);
  TEST_ASSERT_EQUAL_UINT32(1, r.port.gestures.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.sent(RecInput::Latch));
  TEST_ASSERT_TRUE(r.shell.locked() && !r.shell.lock_lit(t0 + 1200));
  TEST_ASSERT_FALSE(r.composing());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_tap_opens_the_note_after_the_window);
  RUN_TEST(test_a_double_tap_starts_a_hands_free_memo);
  RUN_TEST(test_a_refused_double_tap);
  RUN_TEST(test_a_late_second_press_is_a_press_in_the_note);
  RUN_TEST(test_another_key_lands_in_the_note);
  RUN_TEST(test_tap_then_hold_is_a_held_memo);
  RUN_TEST(test_a_hold_is_push_to_talk);
  RUN_TEST(test_hands_free_keys_and_the_end);
  RUN_TEST(test_menu_rows_show_the_end_and_start);
  RUN_TEST(test_a_double_tap_to_end_opens_nothing);
  RUN_TEST(test_a_slow_second_tap_closes_the_note);
  RUN_TEST(test_a_note_touched_or_late_keeps_g0);
  RUN_TEST(test_a_hold_in_the_empty_note_records);
  RUN_TEST(test_locked_hands_free_ends_on_the_lit_card);
  RUN_TEST(test_locked_double_tap_needs_the_lit_card);
  return UNITY_END();
}
