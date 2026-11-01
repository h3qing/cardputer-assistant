// G0's double tap (gh #51, docs/20 U26): a tap that opens the quick note waits
// kDoubleTapMs from its release; a second press inside that window starts a hands-free memo
// on its down edge, a press held on to a hold makes it a held memo after all, and every
// press a gesture took has its G0 events eaten until it goes up. The note is never lost: it
// opens when the window runs out, or before another key. A slow second press (before
// kLateTapMs, no other key since) is still the double tap: Late (the shell closes the empty
// note it opened). The press that ended a hands-free memo hushes a second one right after
// it (tapping twice to end opens nothing).
#include <unity.h>

#include <initializer_list>

#include "g0_gesture.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static KeyEvent g0(KeyEventKind k) { return KeyEvent{k, 0, 0, 0, Nav::None}; }

// A note tap released at `at`.
static G0Gesture tapped(uint32_t at) { return g0_tap(G0Gesture{}, at, true); }

static void test_a_tap_opens_the_note_when_the_window_runs_out() {
  TEST_ASSERT_EQUAL_UINT32(350, kDoubleTapMs);
  const G0Gesture s = tapped(1000);
  TEST_ASSERT_TRUE(g0_waits(s));
  TEST_ASSERT_TRUE(g0_tick(s, 1000 + kDoubleTapMs - 1).out == G0Out::None);
  const G0Step st = g0_tick(s, 1000 + kDoubleTapMs);
  TEST_ASSERT_TRUE(st.out == G0Out::Note && !g0_waits(st.state));
  TEST_ASSERT_TRUE(g0_tick(st.state, 2000).out == G0Out::None);  // once
  TEST_ASSERT_TRUE(g0_tick(G0Gesture{}, 5000).out == G0Out::None);  // nothing waits
}

// The second press's down edge: under kDoubleTapMs after the release it starts the memo
// (and is taken); from kDoubleTapMs on the note opens first and the press is a press.
static void test_double_tap_timings() {
  for (uint32_t gap : {100u, 300u, 349u}) {
    const G0Step st = g0_press(tapped(1000), 1000 + gap, true);
    TEST_ASSERT_TRUE_MESSAGE(st.out == G0Out::HandsFree, "a double tap");
    TEST_ASSERT_TRUE(!g0_waits(st.state) && g0_event(st.state, g0(KeyEventKind::G0Tap)).out == G0Out::Eat);
    TEST_ASSERT_TRUE(g0_tick(st.state, 1000 + gap + 1000).out == G0Out::None);  // no note after it
  }
  for (uint32_t gap : {kLateTapMs, kLateTapMs + 200}) {
    const G0Step st = g0_press(tapped(1000), 1000 + gap, true);
    TEST_ASSERT_TRUE_MESSAGE(st.out == G0Out::Note, "too late: the note, then the press");
    TEST_ASSERT_TRUE(g0_event(st.state, g0(KeyEventKind::G0Tap)).out == G0Out::None);
  }
}

// A slow second press (gh #51 review: a stiff button): from kDoubleTapMs to kLateTapMs it is
// still the double tap, Late, whether the note opened already (the window's tick ran) or
// not; it starts on the down edge and is taken like the second tap. From kLateTapMs on it
// is an ordinary press in the note.
static void test_a_slow_second_press_is_late() {
  TEST_ASSERT_EQUAL_UINT32(1000, kLateTapMs);
  for (uint32_t gap : {350u, 351u, 600u, 999u}) {
    G0Step st = g0_press(tapped(1000), 1000 + gap, true);
    TEST_ASSERT_TRUE_MESSAGE(st.out == G0Out::Late, "before the tick");
    TEST_ASSERT_TRUE(g0_event(st.state, g0(KeyEventKind::G0Tap)).out == G0Out::Eat);
    const G0Step note = g0_tick(tapped(1000), 1000 + kDoubleTapMs);
    st = g0_press(note.state, 1000 + gap, true);
    TEST_ASSERT_TRUE_MESSAGE(st.out == G0Out::Late, "the note open");
    TEST_ASSERT_TRUE(g0_event(st.state, g0(KeyEventKind::G0Long)).out == G0Out::Hold);  // tap, slow hold
    TEST_ASSERT_TRUE(g0_tick(st.state, 9000).out == G0Out::None);
  }
  const G0Step note = g0_tick(tapped(1000), 1000 + kDoubleTapMs);
  const G0Step st = g0_press(note.state, 1000 + kLateTapMs, true);
  TEST_ASSERT_TRUE(st.out == G0Out::None && g0_event(st.state, g0(KeyEventKind::G0Tap)).out == G0Out::None);
  TEST_ASSERT_TRUE(g0_press(st.state, 1000 + kLateTapMs + 50, true).out == G0Out::None);  // once
}

// Late only follows a note tap that nothing else touched: another key (it landed in the
// note), a press where no memo may start, or a locked tap (no note) ends it.
static void test_late_needs_the_untouched_note() {
  const G0Step note = g0_tick(tapped(1000), 1000 + kDoubleTapMs);
  const G0Step typed = g0_key(note.state);
  TEST_ASSERT_TRUE(typed.out == G0Out::None);
  TEST_ASSERT_TRUE(g0_press(typed.state, 1500, true).out == G0Out::None);
  const G0Step busy = g0_press(note.state, 1500, false);
  TEST_ASSERT_TRUE(busy.out == G0Out::None);
  TEST_ASSERT_TRUE(g0_press(busy.state, 1600, true).out == G0Out::None);
  const G0Gesture locked = g0_tap(G0Gesture{}, 1000, false);
  TEST_ASSERT_TRUE(g0_press(g0_tick(locked, 1400).state, 1500, true).out == G0Out::None);
  TEST_ASSERT_TRUE(g0_press(locked, 1500, true).out == G0Out::None);
}

// Tapping twice to end a hands-free memo, as it was started: the press that ended it
// (g0_take(s, true)) hushes a press beginning less than kDoubleTapMs after its release; all
// of that press's G0 events are eaten. At kDoubleTapMs a press is free again, and a hushed
// press never stops a later double tap.
static void test_a_second_tap_after_the_end_is_hushed() {
  for (uint32_t gap : {50u, 349u}) {
    G0Gesture s = g0_release(g0_take(G0Gesture{}, true), 5000);
    const G0Step st = g0_press(s, 5000 + gap, true);
    TEST_ASSERT_TRUE(st.out == G0Out::None);
    for (KeyEventKind k : {KeyEventKind::G0Tap, KeyEventKind::G0Long, KeyEventKind::G0LongUp}) {
      TEST_ASSERT_TRUE(g0_event(st.state, g0(k)).out == G0Out::Eat);
    }
    s = g0_release(st.state, 5000 + gap + 80);
    TEST_ASSERT_TRUE(g0_event(s, g0(KeyEventKind::G0Tap)).out == G0Out::None);
    s = g0_tap(s, 5600, true);  // later: tap, tap
    TEST_ASSERT_TRUE(g0_press(s, 5700, true).out == G0Out::HandsFree);
  }
  const G0Step free = g0_press(g0_release(g0_take(G0Gesture{}, true), 5000), 5000 + kDoubleTapMs, true);
  TEST_ASSERT_TRUE(free.out == G0Out::None && g0_event(free.state, g0(KeyEventKind::G0Tap)).out == G0Out::None);
  // a press that resumed a memo in its grace hushes nothing
  const G0Step resumed = g0_press(g0_release(g0_take(G0Gesture{}, false), 5000), 5100, true);
  TEST_ASSERT_TRUE(g0_event(resumed.state, g0(KeyEventKind::G0Tap)).out == G0Out::None);
}

// A press held from the start is the chord detector's hold (the push-to-talk memo): no
// gesture here, nothing eaten. A tap where nothing waits (not a note tap) is no first tap.
static void test_a_hold_is_not_a_gesture() {
  const G0Step st = g0_press(G0Gesture{}, 1000, true);
  TEST_ASSERT_TRUE(st.out == G0Out::None);
  for (KeyEventKind k : {KeyEventKind::G0Long, KeyEventKind::G0LongUp, KeyEventKind::G0Tap}) {
    TEST_ASSERT_TRUE(g0_event(st.state, g0(k)).out == G0Out::None);
  }
}

// Tap, then hold: the memo starts on the press's down edge as a hands-free one; its G0Long
// (kG0LongMs later) makes it a held memo after all (Hold), eaten like the rest of the press.
// After the late window the hold is an ordinary hold: the note opened first, nothing is eaten.
static void test_tap_then_hold() {
  G0Step st = g0_press(tapped(1000), 1200, true);
  TEST_ASSERT_TRUE(st.out == G0Out::HandsFree);
  st = g0_event(st.state, g0(KeyEventKind::G0Long));
  TEST_ASSERT_TRUE(st.out == G0Out::Hold);
  TEST_ASSERT_TRUE(g0_event(st.state, g0(KeyEventKind::G0Long)).out == G0Out::Eat);  // once
  TEST_ASSERT_TRUE(g0_event(st.state, g0(KeyEventKind::G0LongUp)).out == G0Out::Eat);
  const G0Gesture up = g0_release(st.state, 2000);
  TEST_ASSERT_TRUE(g0_event(up, g0(KeyEventKind::G0LongUp)).out == G0Out::None);
  const G0Step note = g0_tick(tapped(1000), 1350);
  TEST_ASSERT_TRUE(note.out == G0Out::Note);
  st = g0_press(note.state, 1000 + kLateTapMs, true);
  TEST_ASSERT_TRUE(st.out == G0Out::None && g0_event(st.state, g0(KeyEventKind::G0Long)).out == G0Out::None);
}

// Double tap, then the end: the hands-free press's release is eaten; the press that ends
// the memo is the memo's (g0_take): its tap or hold and its release are eaten, never a
// Hold, and the next press is free again.
static void test_double_tap_then_end() {
  G0Step st = g0_press(tapped(1000), 1150, true);
  TEST_ASSERT_TRUE(g0_event(st.state, g0(KeyEventKind::G0Tap)).out == G0Out::Eat);
  G0Gesture s = g0_release(st.state, 1230);
  TEST_ASSERT_TRUE(g0_tick(s, 9000).out == G0Out::None);
  s = g0_take(s, true);  // G0 down on the hands-free memo: the end
  for (KeyEventKind k : {KeyEventKind::G0Tap, KeyEventKind::G0Long, KeyEventKind::G0LongUp}) {
    TEST_ASSERT_TRUE(g0_event(s, g0(k)).out == G0Out::Eat);
  }
  s = g0_release(s, 20000);
  TEST_ASSERT_TRUE(g0_event(s, g0(KeyEventKind::G0Tap)).out == G0Out::None);
  TEST_ASSERT_TRUE(g0_event(s, KeyEvent{KeyEventKind::Char, 'a', 0, 0, Nav::None}).out == G0Out::None);
}

// Another key while a tap waits: the note opens first, so the key lands in it.
static void test_another_key_opens_the_note_first() {
  const G0Step st = g0_key(tapped(1000));
  TEST_ASSERT_TRUE(st.out == G0Out::Note && !g0_waits(st.state));
  TEST_ASSERT_TRUE(g0_key(st.state).out == G0Out::None);
  TEST_ASSERT_TRUE(g0_tick(st.state, 2000).out == G0Out::None);
}

// Where no memo may start now (the lock card went dark), a press in the window ends the
// wait as a late press does. Locked, a tap on the lit card waits but opens nothing.
static void test_no_memo_here_and_the_locked_tap() {
  G0Step st = g0_press(tapped(1000), 1100, false);
  TEST_ASSERT_TRUE(st.out == G0Out::Note && !g0_waits(st.state));
  const G0Gesture locked = g0_tap(G0Gesture{}, 1000, false);
  TEST_ASSERT_TRUE(g0_waits(locked));
  TEST_ASSERT_TRUE(g0_tick(locked, 1400).out == G0Out::None && !g0_waits(g0_tick(locked, 1400).state));
  TEST_ASSERT_TRUE(g0_key(locked).out == G0Out::None);
  TEST_ASSERT_TRUE(g0_press(locked, 1300, true).out == G0Out::HandsFree);
  st = g0_press(locked, 1300, false);
  TEST_ASSERT_TRUE(st.out == G0Out::None && !g0_waits(st.state));
}

static void test_wraps_with_millis() {
  const G0Gesture s = tapped(0xFFFFFF00u);
  TEST_ASSERT_TRUE(g0_tick(s, 0xFFFFFF00u + 200).out == G0Out::None);
  TEST_ASSERT_TRUE(g0_press(s, 0xFFFFFF00u + 300, true).out == G0Out::HandsFree);
  TEST_ASSERT_TRUE(g0_tick(s, 0xFFFFFF00u + kDoubleTapMs).out == G0Out::Note);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_tap_opens_the_note_when_the_window_runs_out);
  RUN_TEST(test_double_tap_timings);
  RUN_TEST(test_a_slow_second_press_is_late);
  RUN_TEST(test_late_needs_the_untouched_note);
  RUN_TEST(test_a_second_tap_after_the_end_is_hushed);
  RUN_TEST(test_a_hold_is_not_a_gesture);
  RUN_TEST(test_tap_then_hold);
  RUN_TEST(test_double_tap_then_end);
  RUN_TEST(test_another_key_opens_the_note_first);
  RUN_TEST(test_no_memo_here_and_the_locked_tap);
  RUN_TEST(test_wraps_with_millis);
  return UNITY_END();
}
