// ChordDetector (05 §5.7): KeySet samples -> KeyEvents, pure.
//   1. Fires on press, from the full key set at that moment; releases fire nothing
//      (except G0).
//   2. Modifiers: Fn, Shift, Ctrl, Opt, Alt. While Ctrl, Opt or Alt is held no Char is
//      produced: the press becomes Chord{mods, key}.
//   3. G0 released before 500 ms -> G0Tap; held 500 ms -> G0Long at once, no gap; the
//      release after G0Long -> G0LongUp (v0.3, docs/11 §4.1). When G0Long fires on the
//      release sample itself (no sample while held past 500 ms), G0LongUp follows in the
//      same step.
//   4. Only Backspace repeats: after 500 ms, then every 60 ms (marked repeat).
//   G0Double never comes from here: the double tap is the shell's gesture (g0_gesture.h),
//   and the kind exists for its binding, which the Tab menu's row replays (gh #51).
// Fn layer as in M5Cardputer 1.2.0: Fn+` = Esc (Nav::Back), Fn+; . , / = arrows.
// Nav::Menu is Tab, the persistent menu (v0.4.0, docs/13 §6.4); chord_step emits it
// from V40-F on.
#pragma once

#include <cstdint>

#include "key_set.h"
#include "step.h"

namespace cpa {

constexpr uint8_t kModFn = 1;
constexpr uint8_t kModShift = 2;
constexpr uint8_t kModCtrl = 4;
constexpr uint8_t kModOpt = 8;
constexpr uint8_t kModAlt = 16;

constexpr uint32_t kG0LongMs = 500;
constexpr uint32_t kRepeatDelayMs = 500;
constexpr uint32_t kRepeatEveryMs = 60;

enum class KeyEventKind : uint8_t { None, Char, Chord, G0Tap, G0Long, Nav, G0LongUp, G0Double };
enum class Nav : uint8_t { None, Up, Down, Left, Right, Enter, Back, Bksp, Menu };

struct KeyEvent {
  KeyEventKind kind = KeyEventKind::None;
  char ch = 0;       // Char: printable ASCII (Shift applied)
  uint8_t mods = 0;  // Chord: kMod* bits held at press time
  char key = 0;      // Chord: unshifted key ('s', '1', '\n' for Enter, '\b' for Backspace)
  Nav nav = Nav::None;
  // A Backspace repeat while the key is held (rule 4). Not part of the event's identity:
  // keymap lookups match repeats like presses; Compose only exits on a fresh press.
  bool repeat = false;

  bool operator==(const KeyEvent& o) const {
    return kind == o.kind && ch == o.ch && mods == o.mods && key == o.key && nav == o.nav;
  }
};

struct KeySample {
  KeySet keys;
  uint32_t now = 0;
};

struct ChordState {
  KeySet prev;
  uint32_t g0_since = 0;
  bool g0_long_sent = false;
  uint32_t bksp_next = 0;  // next Backspace repeat while held
};

using ChordStep = Step<ChordState, KeyEvent>;

ChordStep chord_step(const ChordState& s, const KeySample& sample);

// Unshifted / shifted character of a layout key (0 for keys that type nothing).
char key_base(uint8_t k);
char key_shifted(uint8_t k);
uint8_t mods_of(KeySet keys);

}  // namespace cpa
