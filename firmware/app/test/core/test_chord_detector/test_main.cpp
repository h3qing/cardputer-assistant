// ChordDetector (05 §5.7): actions fire on press from the full key set at that moment;
// Ctrl/Opt/Alt suppress Char; G0 < 500 ms is G0Tap, G0 held 500 ms fires G0Long at
// once; only Backspace repeats (after 500 ms, then every 60 ms).
#include <unity.h>

#include <vector>

#include "chord_detector.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static uint8_t at(uint8_t x, uint8_t y) { return layout_index(x, y); }

struct Run {
  ChordState state;
  std::vector<KeyEvent> events;
};

static Run sample(const Run& r, KeySet keys, uint32_t now) {
  const ChordStep s = chord_step(r.state, KeySample{keys, now});
  Run out{s.state, r.events};
  for (uint8_t i = 0; i < s.effects.count; ++i) out.events.push_back(s.effects.items[i]);
  return out;
}

static Run press(std::initializer_list<uint8_t> keys, uint32_t now) {
  KeySet set;
  for (uint8_t k : keys) set = set.with(k);
  return sample(Run{}, set, now);
}

static void test_chars_and_shift() {
  const Run a = press({at(2, 2)}, 0);
  TEST_ASSERT_EQUAL_UINT(1, a.events.size());
  TEST_ASSERT_TRUE(a.events[0].kind == KeyEventKind::Char);
  TEST_ASSERT_EQUAL_CHAR('a', a.events[0].ch);
  TEST_ASSERT_EQUAL_CHAR('A', press({key::kShift, at(2, 2)}, 0).events[0].ch);
  TEST_ASSERT_EQUAL_CHAR('!', press({key::kShift, at(1, 0)}, 0).events[0].ch);
  TEST_ASSERT_EQUAL_CHAR(' ', press({key::kSpace}, 0).events[0].ch);
  TEST_ASSERT_EQUAL_CHAR('"', press({key::kShift, at(12, 2)}, 0).events[0].ch);
  // a modifier alone does nothing; releasing does nothing
  const Run shift = press({key::kShift}, 0);
  TEST_ASSERT_EQUAL_UINT(0, shift.events.size());
  TEST_ASSERT_EQUAL_UINT(0, sample(a, KeySet{}, 10).events.size() - 1);
}

static void test_ctrl_opt_alt_never_type() {
  for (uint8_t mod : {key::kCtrl, key::kOpt, key::kAlt}) {
    const Run r = press({mod, at(3, 2)}, 0);
    TEST_ASSERT_EQUAL_UINT(1, r.events.size());
    TEST_ASSERT_TRUE(r.events[0].kind == KeyEventKind::Chord);
    TEST_ASSERT_EQUAL_CHAR('s', r.events[0].key);
  }
  const Run opt1 = press({key::kOpt, at(1, 0)}, 0);
  TEST_ASSERT_TRUE(opt1.events[0].mods == kModOpt);
  TEST_ASSERT_EQUAL_CHAR('1', opt1.events[0].key);
  // the full set at press time counts: Opt held first, then S
  const Run held = sample(press({key::kOpt}, 0), KeySet{}.with(key::kOpt).with(at(3, 2)), 50);
  TEST_ASSERT_TRUE(held.events[0].kind == KeyEventKind::Chord);
}

static void test_navigation_keys() {
  TEST_ASSERT_TRUE(press({key::kEnter}, 0).events[0].nav == Nav::Enter);
  TEST_ASSERT_TRUE(press({key::kFn, at(0, 0)}, 0).events[0].nav == Nav::Back);  // Fn+` = Esc
  TEST_ASSERT_TRUE(press({key::kFn, at(11, 2)}, 0).events[0].nav == Nav::Up);
  TEST_ASSERT_TRUE(press({key::kFn, at(11, 3)}, 0).events[0].nav == Nav::Down);
  TEST_ASSERT_TRUE(press({key::kFn, at(10, 3)}, 0).events[0].nav == Nav::Left);
  TEST_ASSERT_TRUE(press({key::kFn, at(12, 3)}, 0).events[0].nav == Nav::Right);
  TEST_ASSERT_TRUE(press({key::kBksp}, 0).events[0].nav == Nav::Bksp);
  TEST_ASSERT_EQUAL_UINT(0, press({key::kFn, at(2, 2)}, 0).events.size());  // no Fn meaning
  TEST_ASSERT_TRUE(press({key::kTab}, 0).events[0].nav == Nav::Menu);  // v0.4.0: the Tab menu (docs/13 §6.4)
  // without Fn, ; . , / are characters (list pages read them as navigation)
  TEST_ASSERT_EQUAL_CHAR(';', press({at(11, 2)}, 0).events[0].ch);
}

static void test_g0_tap_and_long_boundary() {
  const KeySet g0 = KeySet{}.with(key::kG0);
  const Run down = press({key::kG0}, 1000);
  TEST_ASSERT_EQUAL_UINT(0, down.events.size());
  const Run held499 = sample(down, g0, 1499);
  TEST_ASSERT_EQUAL_UINT(0, held499.events.size());
  const Run tap = sample(held499, KeySet{}, 1499);
  TEST_ASSERT_EQUAL_UINT(1, tap.events.size());
  TEST_ASSERT_TRUE(tap.events[0].kind == KeyEventKind::G0Tap);
  const Run at500 = sample(held499, g0, 1500);
  TEST_ASSERT_EQUAL_UINT(1, at500.events.size());
  TEST_ASSERT_TRUE(at500.events[0].kind == KeyEventKind::G0Long);
  const Run released = sample(sample(at500, g0, 2500), KeySet{}, 2600);
  TEST_ASSERT_EQUAL_UINT(2, released.events.size());  // v0.3: the release after G0Long is G0LongUp
  // released at exactly 500 ms without an earlier sample: still long
  const Run late = sample(down, KeySet{}, 1500);
  TEST_ASSERT_TRUE(late.events[0].kind == KeyEventKind::G0Long);
}

// v0.3 (docs/11 §4.1, B5): releasing G0 after G0Long emits G0LongUp; when G0Long itself
// fires on the release sample (the late path) G0LongUp follows in the same step, so a
// memo can never start after the key is already up.
static void test_g0_long_up() {
  const KeySet g0 = KeySet{}.with(key::kG0);
  const Run down = press({key::kG0}, 1000);
  const Run held = sample(down, g0, 1500);
  TEST_ASSERT_TRUE(held.events.back().kind == KeyEventKind::G0Long);
  const Run still = sample(held, g0, 4000);
  TEST_ASSERT_EQUAL_UINT(1, still.events.size());
  const Run up = sample(still, KeySet{}, 4010);
  TEST_ASSERT_EQUAL_UINT(2, up.events.size());
  TEST_ASSERT_TRUE(up.events[1].kind == KeyEventKind::G0LongUp);
  TEST_ASSERT_EQUAL_UINT(2, sample(up, KeySet{}, 4100).events.size());  // once
  // late path: no sample while held past 500 ms; both events in the release step
  const ChordStep late = chord_step(down.state, KeySample{KeySet{}, 1510});
  TEST_ASSERT_EQUAL_UINT8(2, late.effects.count);
  TEST_ASSERT_TRUE(late.effects.items[0].kind == KeyEventKind::G0Long);
  TEST_ASSERT_TRUE(late.effects.items[1].kind == KeyEventKind::G0LongUp);
  // a tap never produces G0LongUp
  const Run tap = sample(sample(down, g0, 1499), KeySet{}, 1499);
  TEST_ASSERT_EQUAL_UINT(1, tap.events.size());
  TEST_ASSERT_TRUE(tap.events[0].kind == KeyEventKind::G0Tap);
}

static void test_g0_with_other_keys() {
  const Run both = press({key::kG0, at(2, 2)}, 0);
  TEST_ASSERT_EQUAL_UINT(1, both.events.size());
  TEST_ASSERT_TRUE(both.events[0].kind == KeyEventKind::Char);
}

static void test_only_backspace_repeats() {
  const KeySet bksp = KeySet{}.with(key::kBksp);
  Run r = press({key::kBksp}, 0);
  for (uint32_t t = 10; t <= 499; t += 10) r = sample(r, bksp, t);
  TEST_ASSERT_EQUAL_UINT(1, r.events.size());
  r = sample(r, bksp, 500);
  TEST_ASSERT_EQUAL_UINT(2, r.events.size());
  r = sample(r, bksp, 559);
  TEST_ASSERT_EQUAL_UINT(2, r.events.size());
  r = sample(r, bksp, 560);
  r = sample(r, bksp, 620);
  TEST_ASSERT_EQUAL_UINT(4, r.events.size());
  r = sample(r, bksp, 2000);  // late sample: one repeat, no burst
  TEST_ASSERT_EQUAL_UINT(5, r.events.size());
  r = sample(r, KeySet{}, 2100);
  r = sample(r, KeySet{}, 3000);
  TEST_ASSERT_EQUAL_UINT(5, r.events.size());
  Run a = press({at(2, 2)}, 0);
  a = sample(a, KeySet{}.with(at(2, 2)), 2000);
  TEST_ASSERT_EQUAL_UINT(1, a.events.size());
}

// Issue #15: Backspace on an empty Compose exits, but only on a fresh press, so a held
// Backspace that clears the text does not close the editor. Repeats are marked; the
// mark is not part of the event's identity (keymap lookups still match them).
static void test_backspace_repeats_are_marked() {
  const KeySet bksp = KeySet{}.with(key::kBksp);
  Run r = press({key::kBksp}, 0);
  r = sample(r, bksp, 500);
  r = sample(r, bksp, 560);
  TEST_ASSERT_EQUAL_UINT(3, r.events.size());
  TEST_ASSERT_FALSE(r.events[0].repeat);
  TEST_ASSERT_TRUE(r.events[1].repeat);
  TEST_ASSERT_TRUE(r.events[2].repeat);
  TEST_ASSERT_TRUE(r.events[1] == r.events[0]);
  r = sample(r, KeySet{}, 600);
  r = sample(r, bksp, 700);  // released and pressed again: a fresh press
  TEST_ASSERT_EQUAL_UINT(4, r.events.size());
  TEST_ASSERT_FALSE(r.events[3].repeat);
}

static void test_events_are_capped() {
  const Run many = press({at(1, 1), at(2, 1), at(3, 1), at(4, 1), at(5, 1)}, 0);
  TEST_ASSERT_EQUAL_UINT(ChordStep{}.effects.kCapacity, many.events.size());
}

// v0.4.0 (docs/13 §6.4): Tab is Nav::Menu, with or without Shift; Fn+Tab means nothing
// and Opt+Tab stays a chord, so Tab never types.
static void test_tab_is_the_menu_key() {
  const Run tab = press({key::kTab}, 0);
  TEST_ASSERT_EQUAL_UINT(1, tab.events.size());
  TEST_ASSERT_TRUE(tab.events[0].kind == KeyEventKind::Nav && tab.events[0].nav == Nav::Menu);
  TEST_ASSERT_TRUE(press({key::kShift, key::kTab}, 0).events[0].nav == Nav::Menu);
  TEST_ASSERT_EQUAL_UINT(0, press({key::kFn, key::kTab}, 0).events.size());
  const Run opt_tab = press({key::kOpt, key::kTab}, 0);
  TEST_ASSERT_TRUE(opt_tab.events[0].kind == KeyEventKind::Chord);
  TEST_ASSERT_EQUAL_CHAR('\t', opt_tab.events[0].key);
  TEST_ASSERT_EQUAL_UINT(1, sample(tab, KeySet{}.with(key::kTab), 600).events.size());  // no repeat
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chars_and_shift);
  RUN_TEST(test_ctrl_opt_alt_never_type);
  RUN_TEST(test_navigation_keys);
  RUN_TEST(test_g0_tap_and_long_boundary);
  RUN_TEST(test_g0_with_other_keys);
  RUN_TEST(test_g0_long_up);
  RUN_TEST(test_only_backspace_repeats);
  RUN_TEST(test_backspace_repeats_are_marked);
  RUN_TEST(test_events_are_capped);
  RUN_TEST(test_tab_is_the_menu_key);
  return UNITY_END();
}
