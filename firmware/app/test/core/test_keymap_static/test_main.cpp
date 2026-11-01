// keymap static check (05 §5.7 rule 4, 04 §8.1): every binding of 04 §8 is a single
// key or a two-key combo, can never produce a ghost key, and pressing its keys
// through the ChordDetector yields exactly the event it is bound to. Also the exit keys
// of issue #15.
#include <unity.h>

#include <string>

#include "chord_detector.h"
#include "ghost_filter.h"
#include "keymap.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static KeyEvent first_event(KeySet keys) {
  const ChordStep down = chord_step(ChordState{}, KeySample{keys, 0});
  if (down.effects.count > 0) return down.effects.items[0];
  const ChordStep up = chord_step(down.state, KeySample{KeySet{}, 10});  // G0 tap fires on release
  return up.effects.count > 0 ? up.effects.items[0] : KeyEvent{};
}

static void test_every_binding_is_two_keys_at_most_and_ghost_free() {
  TEST_ASSERT_TRUE(kKeymapCount > 40);
  for (size_t i = 0; i < kKeymapCount; ++i) {
    const KeySet keys = binding_keys(kKeymap[i]);
    const std::string label = "binding " + std::to_string(i);
    TEST_ASSERT_TRUE_MESSAGE(keys.without(key::kG0).count() <= 2, label.c_str());
    TEST_ASSERT_FALSE_MESSAGE(ghost_risk(keys), label.c_str());
    TEST_ASSERT_FALSE_MESSAGE(has_rectangle(keys), label.c_str());
  }
}

static void test_every_binding_round_trips_through_the_detector() {
  for (size_t i = 0; i < kKeymapCount; ++i) {
    const Binding& b = kKeymap[i];
    if (b.trigger.kind == KeyEventKind::G0Long) continue;  // needs a hold; see test_chord_detector
    if (b.trigger.kind == KeyEventKind::G0Double) continue;  // the shell's (g0_gesture.h); below
    const KeyEvent ev = first_event(binding_keys(b));
    const std::string label = "binding " + std::to_string(i);
    TEST_ASSERT_TRUE_MESSAGE(ev == b.trigger, label.c_str());
    const Resolved r = keymap_lookup(b.layer, ev);
    TEST_ASSERT_TRUE_MESSAGE(r.action == b.action && r.arg == b.arg, label.c_str());
  }
}

static void test_typing_combos_are_ghost_free() {
  for (uint8_t k = 0; k < kMatrixKeys; ++k) {
    if (is_modifier(k)) continue;
    TEST_ASSERT_FALSE(ghost_risk(KeySet{}.with(key::kShift).with(k)));
  }
}

static void test_known_bad_three_key_combos_are_caught() {
  // 02 §6 / 04 §8.1: Fn+Shift, Ctrl+Opt, Ctrl+Alt plus a third key can ghost.
  const uint8_t a = layout_index(2, 2), w = layout_index(2, 1), alt = layout_index(2, 3);
  TEST_ASSERT_TRUE(ghost_risk(KeySet{}.with(key::kFn).with(key::kShift).with(a)));
  TEST_ASSERT_TRUE(ghost_risk(KeySet{}.with(key::kCtrl).with(key::kOpt).with(alt)));
  TEST_ASSERT_TRUE(ghost_risk(KeySet{}.with(key::kCtrl).with(key::kAlt).with(w)));
}

static void test_lookup_of_unbound_events() {
  KeyEvent unbound;
  unbound.kind = KeyEventKind::Chord;
  unbound.mods = kModOpt;
  unbound.key = 'z';
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, unbound).action == Action::None);  // ignored, no char
  KeyEvent ch;
  ch.kind = KeyEventKind::Char;
  ch.ch = 'x';
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Compose, ch).action == Action::None);  // typing is not a binding
}

static void test_spot_bindings_from_04_section_8() {
  KeyEvent opt_s;
  opt_s.kind = KeyEventKind::Chord;
  opt_s.mods = kModOpt;
  opt_s.key = 's';
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, opt_s).action == Action::NewSpawn);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Compose, opt_s).action == Action::TypeSpawn);
  KeyEvent opt_3 = opt_s;
  opt_3.key = '3';
  const Resolved project = keymap_lookup(Layer::Global, opt_3);
  TEST_ASSERT_TRUE(project.action == Action::OpenProject);
  TEST_ASSERT_EQUAL_UINT8(3, project.arg);
  const Resolved target = keymap_lookup(Layer::Compose, opt_3);
  TEST_ASSERT_TRUE(target.action == Action::Target);
  TEST_ASSERT_EQUAL_UINT8(3, target.arg);
  KeyEvent g0;
  g0.kind = KeyEventKind::G0Tap;
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, g0).action == Action::QuickNote);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Compose, g0).action == Action::Submit);
}

// Issue #15 (Fn+` is hard to remember): the top-left key ` alone goes back on every
// screen, the confirm box included; Fn+` keeps working everywhere but in Compose. docs/20
// D5: in Compose ` cancels too (asking first with text) and Fn+` types the backtick;
// Backspace (DeleteChar) still exits when there is nothing to delete.
static void test_top_left_key_goes_back_outside_compose() {
  KeyEvent grave;
  grave.kind = KeyEventKind::Char;
  grave.ch = '`';
  KeyEvent fn_grave;
  fn_grave.kind = KeyEventKind::Nav;
  fn_grave.nav = Nav::Back;
  KeyEvent bksp = fn_grave;
  bksp.nav = Nav::Bksp;
  TEST_ASSERT_TRUE(keymap_lookup(Layer::List, grave).action == Action::Back);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Confirm, grave).action == Action::Cancel);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Confirm, fn_grave).action == Action::Cancel);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, fn_grave).action == Action::Escape);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Compose, fn_grave).action == Action::Backtick);  // docs/20 D5
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Compose, grave).action == Action::Cancel);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Compose, bksp).action == Action::DeleteChar);
  KeyEvent held = bksp;
  held.repeat = true;  // a held Backspace still deletes
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Compose, held).action == Action::DeleteChar);
}

// v0.3 recording (docs/11 §4.1): G0 held and Opt+R are global; the memo HUD takes digits;
// the meeting page takes G0 / Space for a star, digits for the tag, Opt+R to end (docs/20
// D3: ` no longer ends, the shell says how). The two recording confirms have their own
// layer, RecConfirm: the Confirm layer's keys plus Opt+L. G0's edges are not bindings: a
// memo follows them (Shell::on_g0, rec_core.h). gh #51: the double tap (global) starts a
// hands-free memo, whose layer takes digits, G0 (the end), Tab and Opt+L; Space on a memo
// does nothing, as before.
static void test_recording_bindings() {
  KeyEvent g0long;
  g0long.kind = KeyEventKind::G0Long;
  KeyEvent g0tap;
  g0tap.kind = KeyEventKind::G0Tap;
  KeyEvent opt_r;
  opt_r.kind = KeyEventKind::Chord;
  opt_r.mods = kModOpt;
  opt_r.key = 'r';
  KeyEvent space;
  space.kind = KeyEventKind::Char;
  space.ch = ' ';
  KeyEvent seven = space;
  seven.ch = '7';
  KeyEvent zero = space;
  zero.ch = '0';
  KeyEvent grave = space;
  grave.ch = '`';
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, g0long).action == Action::RecMemo);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, opt_r).action == Action::RecMeeting);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecMemo, seven).action == Action::RecTag);
  TEST_ASSERT_EQUAL_UINT8(7, keymap_lookup(Layer::RecMemo, seven).arg);
  TEST_ASSERT_EQUAL_UINT8(0, keymap_lookup(Layer::RecMemo, zero).arg);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecMemo, zero).action == Action::RecTag);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecMemo, space).action == Action::None);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecMemo, opt_r).action == Action::None);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecMemo, g0tap).action == Action::None);  // G0's edges: the core's
  KeyEvent g0double;
  g0double.kind = KeyEventKind::G0Double;
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, g0double).action == Action::RecHandsFree);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecLatch, seven).action == Action::RecTag);
  TEST_ASSERT_EQUAL_UINT8(7, keymap_lookup(Layer::RecLatch, seven).arg);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecLatch, zero).action == Action::RecTag);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecLatch, space).action == Action::None);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecLatch, opt_r).action == Action::None);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecLatch, grave).action == Action::None);  // pocket presses
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecLatch, g0tap).action == Action::RecStop);  // the menu's row
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecLatch, g0long).action == Action::None);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecPage, g0tap).action == Action::RecStar);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecPage, g0long).action == Action::RecStar);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecPage, space).action == Action::RecStar);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecPage, seven).action == Action::RecTag);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecPage, opt_r).action == Action::RecEnd);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecPage, grave).action == Action::None);  // docs/20 D3
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecConfirm, grave).action == Action::Cancel);
  KeyEvent enter_key;
  enter_key.kind = KeyEventKind::Nav;
  enter_key.nav = Nav::Enter;
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecConfirm, enter_key).action == Action::Confirm);
  KeyEvent a = space;
  a.ch = 'a';
  TEST_ASSERT_TRUE(keymap_lookup(Layer::RecPage, a).action == Action::None);  // pocket presses do nothing
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, g0tap).action == Action::QuickNote);  // unchanged
}

// v0.4.0 (docs/13 §5.2-§5.3, §7.1). Tab opens the menu and Opt+L locks on the layers
// §5.3 marks: System (home and the apps), Compose, Confirm (Tab only: a confirm box is
// dealt with first) and the meeting page; never in the memo HUD while G0 is held. gh #51: a
// hands-free memo takes both (it goes on, as a meeting does). The Lock layer holds
// what the lock gate lets through: Opt+L, Enter (inside the window) and G0.
static KeyEvent opt_key(char k) { return KeyEvent{KeyEventKind::Chord, 0, kModOpt, k, Nav::None}; }
static KeyEvent nav_key(Nav n) { return KeyEvent{KeyEventKind::Nav, 0, 0, 0, n}; }
static KeyEvent g0_key(KeyEventKind k) { return KeyEvent{k, 0, 0, 0, Nav::None}; }
static KeyEvent char_key(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }

// docs/20 D3: the meeting's boxes (RecConfirm) have both.
static void test_tab_and_opt_l_follow_the_layer_table() {
  const KeyEvent tab = nav_key(Nav::Menu);
  const KeyEvent opt_l = opt_key('l');
  for (Layer l : {Layer::System, Layer::Compose, Layer::Confirm, Layer::RecPage, Layer::RecConfirm, Layer::RecLatch}) {
    TEST_ASSERT_TRUE(keymap_lookup(l, tab).action == Action::Menu);
  }
  for (Layer l : {Layer::System, Layer::Compose, Layer::RecPage, Layer::Lock, Layer::RecConfirm, Layer::RecLatch}) {
    TEST_ASSERT_TRUE(keymap_lookup(l, opt_l).action == Action::Lock);
  }
  for (Layer l : {Layer::RecMemo, Layer::Lock, Layer::Global, Layer::List}) {
    TEST_ASSERT_TRUE(keymap_lookup(l, tab).action == Action::None);
  }
  for (Layer l : {Layer::Confirm, Layer::RecMemo, Layer::Global, Layer::List}) {
    TEST_ASSERT_TRUE(keymap_lookup(l, opt_l).action == Action::None);
  }
}

// The Lock layer holds the unlock keys only; what G0 does while locked is lock.h's (it is
// checked before the table), so the table has no G0 rows that would never be read.
static void test_lock_layer_lets_only_the_unlock_keys_through() {
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Lock, nav_key(Nav::Enter)).action == Action::Unlock);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Lock, g0_key(KeyEventKind::G0Tap)).action == Action::None);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Lock, g0_key(KeyEventKind::G0Long)).action == Action::None);
  size_t n = 0;
  for (size_t i = 0; i < kKeymapCount; ++i) n += kKeymap[i].layer == Layer::Lock ? 1 : 0;
  TEST_ASSERT_EQUAL_UINT32(2, n);
  for (const KeyEvent& k : {char_key('a'), char_key(' '), nav_key(Nav::Back), opt_key('h'), nav_key(Nav::Bksp)}) {
    TEST_ASSERT_TRUE(keymap_lookup(Layer::Lock, k).action == Action::None);
  }
}

// Opt+H is the home launcher, the board moves to Opt+P, Opt+M is still the system page.
// A C G W E Z stay free (A, C, G, E, W: reserved for later versions; Z: the "unbound"
// example of the dispatch tests).
static void test_v040_global_letters() {
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, opt_key('h')).action == Action::Home);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, opt_key('p')).action == Action::Projects);
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Global, opt_key('m')).action == Action::System);
  for (char c : {'a', 'c', 'g', 'w', 'e', 'z'}) {
    for (size_t i = 0; i < kKeymapCount; ++i) {
      const Binding& b = kKeymap[i];
      const bool letter = b.trigger.kind == KeyEventKind::Chord && b.trigger.key == c;
      TEST_ASSERT_FALSE_MESSAGE(b.layer == Layer::Global && letter, std::string(1, c).c_str());
    }
  }
}

// gh #51: the double tap is the shell's gesture (g0_gesture.h), never a ChordDetector event:
// taps, holds and taps in a row give G0Tap / G0Long / G0LongUp only. Its one binding is
// the global hands-free memo, for the Tab menu's row (Enter replays it).
static void test_the_double_tap_is_a_gesture_not_a_detector_event() {
  const KeySet g0 = KeySet{}.with(key::kG0);
  ChordState s;
  for (uint32_t t : {0u, 100u, 200u, 300u, 1300u, 1400u}) {
    const KeySet keys = (t / 100) % 2 == 0 ? g0 : KeySet{};
    const ChordStep st = chord_step(s, KeySample{keys, t});
    for (uint8_t i = 0; i < st.effects.count; ++i) TEST_ASSERT_TRUE(st.effects.items[i].kind != KeyEventKind::G0Double);
    s = st.state;
  }
  size_t n = 0;
  for (size_t i = 0; i < kKeymapCount; ++i) {
    if (kKeymap[i].trigger.kind != KeyEventKind::G0Double) continue;
    ++n;
    TEST_ASSERT_TRUE(kKeymap[i].layer == Layer::Global && kKeymap[i].action == Action::RecHandsFree);
    TEST_ASSERT_TRUE(binding_keys(kKeymap[i]) == g0);
  }
  TEST_ASSERT_EQUAL_UINT32(1, n);
}

// v0.5.0 (docs/04, hidden keys): Opt+W pats Mando. It is an easter egg: bound on its own
// layer (Egg, which no menu or hint bar lists) and nowhere else, never Global, so every other
// page, the editor and the recording pages do with it what they did before (nothing).
static void test_opt_w_pats_mando_on_the_egg_layer_only() {
  TEST_ASSERT_TRUE(keymap_lookup(Layer::Egg, opt_key('w')).action == Action::PatMando);
  size_t n = 0;
  for (size_t i = 0; i < kKeymapCount; ++i) {
    const Binding& b = kKeymap[i];
    if (b.layer == Layer::Egg || b.action == Action::PatMando) {
      ++n;
      TEST_ASSERT_TRUE(b.layer == Layer::Egg && b.action == Action::PatMando && b.trigger == opt_key('w'));
    }
    const bool w = b.trigger.kind == KeyEventKind::Chord && b.trigger.key == 'w';
    TEST_ASSERT_FALSE(w && b.layer != Layer::Egg);  // a free chord everywhere else
  }
  TEST_ASSERT_EQUAL_UINT32(1, n);
  for (Layer l : {Layer::Global, Layer::List, Layer::System, Layer::Lock, Layer::Compose, Layer::RecMemo,
                  Layer::RecPage, Layer::RecLatch}) {
    TEST_ASSERT_TRUE(keymap_lookup(l, opt_key('w')).action == Action::None);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_top_left_key_goes_back_outside_compose);
  RUN_TEST(test_every_binding_is_two_keys_at_most_and_ghost_free);
  RUN_TEST(test_every_binding_round_trips_through_the_detector);
  RUN_TEST(test_typing_combos_are_ghost_free);
  RUN_TEST(test_known_bad_three_key_combos_are_caught);
  RUN_TEST(test_lookup_of_unbound_events);
  RUN_TEST(test_spot_bindings_from_04_section_8);
  RUN_TEST(test_recording_bindings);
  RUN_TEST(test_tab_and_opt_l_follow_the_layer_table);
  RUN_TEST(test_lock_layer_lets_only_the_unlock_keys_through);
  RUN_TEST(test_v040_global_letters);
  RUN_TEST(test_the_double_tap_is_a_gesture_not_a_detector_event);
  RUN_TEST(test_opt_w_pats_mando_on_the_egg_layer_only);
  return UNITY_END();
}
