// G0 from the contact to the double tap (gh #51 review, finding 7): the button read once per
// loop pass, through the release debounce (g0_debounce.h, the device's hal_keys()), the
// chord detector and the gesture (g0_gesture.h), in the order the runtime and the shell
// chain them (a sample's events, then its edge). The simulator and the shell tests start
// after the debounce, so this is where the two meet.
// A release needs two "up" samples kG0ReleaseMs apart, so the shortest gap between two taps
// that always comes through is max(2 passes, kG0ReleaseMs + 1 pass): 40 ms at the 10 ms
// loop, 200 ms during a 100 ms pass (a full redraw; docs/07 has saves of 100-195 ms). A
// shorter gap merges the taps into one press: the note (or, after long passes, a hold too
// short to keep), never a hands-free memo nobody asked for.
#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "g0_debounce.h"
#include "g0_gesture.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

namespace {

struct Contact {
  uint32_t down;
  uint32_t up;
};

struct Seen {
  int presses = 0;  // presses the debounce let through
  bool hands_free = false;
  bool note = false;
  bool hold = false;  // push-to-talk (a G0Long nobody took)
};

bool raw_at(const std::vector<Contact>& cs, uint32_t t) {
  bool down = false;
  for (const Contact& c : cs) down = down || (t >= c.down && t < c.up);
  return down;
}

// One G0 event, as Shell::on_key: eaten (or Hold), else a tap waits, a hold records.
G0Gesture event(const G0Gesture& g, const KeyEvent& ev, uint32_t t, Seen& seen) {
  const G0Step st = g0_event(g, ev);
  if (st.out != G0Out::None) return st.state;
  if (ev.kind == KeyEventKind::G0Tap) return g0_tap(g, t, true);
  seen.hold = seen.hold || ev.kind == KeyEventKind::G0Long;
  return g;
}

// One G0 edge, as Shell::on_g0 (nothing records: a memo may start).
G0Gesture edge(const G0Gesture& g, bool down, uint32_t t, Seen& seen) {
  if (!down) return g0_release(g, t);
  ++seen.presses;
  const G0Step st = g0_press(g, t, true);
  seen.hands_free = seen.hands_free || st.out == G0Out::HandsFree || st.out == G0Out::Late;
  seen.note = seen.note || st.out == G0Out::Note;
  return st.state;
}

Seen play(const std::vector<Contact>& cs, uint32_t pass, uint32_t phase) {
  G0Debounce db;
  ChordState ch;
  G0Gesture g;
  Seen seen;
  bool was = false;
  for (uint32_t t = 1000 + phase; t < cs.back().up + 3000; t += pass) {
    db = g0_debounce(db, raw_at(cs, t), t);
    const ChordStep c = chord_step(ch, KeySample{db.down ? KeySet{}.with(key::kG0) : KeySet{}, t});
    ch = c.state;
    for (uint8_t i = 0; i < c.effects.count; ++i) g = event(g, c.effects.items[i], t, seen);
    if (db.down != was) g = edge(g, db.down, t, seen);
    was = db.down;
    const G0Step st = g0_tick(g, t);
    seen.note = seen.note || st.out == G0Out::Note;
    g = st.state;
  }
  return seen;
}

// Two taps of `tap` ms, `gap` ms apart (contact to contact), from 2000.
std::vector<Contact> two_taps(uint32_t tap, uint32_t gap) {
  return {Contact{2000, 2000 + tap}, Contact{2000 + tap + gap, 2000 + 2 * tap + gap}};
}

uint32_t shortest_gap(uint32_t pass) { return std::max(2 * pass, kG0ReleaseMs + pass); }

}  // namespace

// The shortest gap that always comes through, at every phase of the loop against the taps.
static void test_a_double_tap_through_the_debounce() {
  for (uint32_t pass : {10u, 50u, 100u}) {
    const uint32_t tap = pass + 20;  // long enough to be sampled at all
    for (uint32_t phase = 0; phase < pass; phase += 5) {
      const Seen s = play(two_taps(tap, shortest_gap(pass)), pass, phase);
      TEST_ASSERT_EQUAL_INT_MESSAGE(2, s.presses, "two presses");
      TEST_ASSERT_TRUE_MESSAGE(s.hands_free && !s.note && !s.hold, "a double tap");
    }
  }
  TEST_ASSERT_EQUAL_UINT32(40, shortest_gap(10));
  TEST_ASSERT_EQUAL_UINT32(200, shortest_gap(100));
}

// Under one pass (or under kG0ReleaseMs) the debounce cannot see the gap: one press. At the
// 10 ms loop that is one tap (the note). During 100 ms passes the late release can also push
// the merged press past kG0LongMs: a hold, whose memo the grace ends too short to keep.
// Never a hands-free memo nobody asked for.
static void test_a_gap_the_debounce_cannot_see_is_one_press() {
  for (uint32_t pass : {10u, 100u}) {
    const uint32_t gap = pass == 10 ? kG0ReleaseMs - 10 : pass - 10;
    for (uint32_t phase = 0; phase < pass; phase += 5) {
      const Seen s = play(two_taps(pass + 20, gap), pass, phase);
      char m[64];
      std::snprintf(m, sizeof m, "pass %u phase %u", static_cast<unsigned>(pass), static_cast<unsigned>(phase));
      TEST_ASSERT_EQUAL_INT_MESSAGE(1, s.presses, m);
      TEST_ASSERT_FALSE_MESSAGE(s.hands_free, m);
      TEST_ASSERT_TRUE_MESSAGE(pass == 10 ? s.note && !s.hold : s.note != s.hold, m);
    }
  }
}

// The reason for the debounce: one open sample in a held press after a long pass is no
// release (no second press, no gesture): the hold stays one hold.
static void test_a_bounce_in_a_hold_stays_one_hold() {
  const std::vector<Contact> bounce{Contact{2000, 2900}, Contact{2905, 4000}};  // 5 ms open
  for (uint32_t pass : {10u, 100u}) {
    const Seen s = play(bounce, pass, 0);
    TEST_ASSERT_EQUAL_INT(1, s.presses);
    TEST_ASSERT_TRUE(s.hold && !s.hands_free && !s.note);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_double_tap_through_the_debounce);
  RUN_TEST(test_a_gap_the_debounce_cannot_see_is_one_press);
  RUN_TEST(test_a_bounce_in_a_hold_stays_one_hold);
  return UNITY_END();
}
