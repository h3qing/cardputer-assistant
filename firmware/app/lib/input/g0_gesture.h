// G0's double tap (gh #51, docs/20 U26), pure: what the chord detector's tap and hold
// (chord_detector.h) leave to the shell. Every function returns a new state.
//   tap        a G0 tap where it opens the quick note (g0_tap) waits kDoubleTapMs from its
//              release; no G0 press by then (g0_tick), or another key first (g0_key), and
//              the note opens (Note). Locked, a tap on the card a key lit waits too and
//              opens nothing (note false; Shell::locked_g0).
//   tap, tap   a press beginning less than kDoubleTapMs after that release, where a memo
//              may start, starts a hands-free memo on its down edge (HandsFree), so no
//              word is lost; letting go of it ends nothing.
//   slow tap   a note tap's second press from kDoubleTapMs to kLateTapMs, no other key
//              since (the note it opened is still empty): the same, Late (the shell closes
//              that note first). A stiff button makes a double tap slow (review of gh #51).
//   tap, hold  that press still down at kG0LongMs (its G0Long) makes the memo a held one
//              after all (Hold): letting go ends it, as a hold does.
//   a press a gesture took (the second one above, or one a memo took: g0_take) is eaten:
//   its G0 events do nothing else (g0_event: Eat) until it goes up (g0_release).
//   end, tap   the press that ended a hands-free memo (g0_take(s, true)) hushes a press
//              beginning less than kDoubleTapMs after its release: eaten, so tapping
//              twice to end, as it started, opens nothing.
// A press at or after kLateTapMs opens the waiting note first (Note) and is then an
// ordinary press. A press held from the start is no gesture here: it is the hold (G0Long).
#pragma once

#include <cstdint>

#include "chord_detector.h"

namespace cpa {

constexpr uint32_t kDoubleTapMs = 350;
constexpr uint32_t kLateTapMs = 1000;

enum class G0Out : uint8_t { None, Note, HandsFree, Late, Hold, Eat };

struct G0Gesture {
  bool waits = false;    // a tap waits for a second press ...
  uint32_t tap_ms = 0;   // ... since its release (or the release of a press that ended a memo)
  bool note = false;     // ... and opens the quick note if none comes
  bool late = false;     // its note opened, untouched: a press before kLateTapMs is Late
  bool eat = false;      // the press now down was taken: its G0 events are eaten ...
  bool started = false;  // ... and it started a hands-free memo (its G0Long: Hold)
  bool ends = false;     // ... or it ended one: its release hushes ...
  bool hush = false;     // ... a press beginning less than kDoubleTapMs after it
};

struct G0Step {
  G0Gesture state;
  G0Out out = G0Out::None;
};

G0Gesture g0_tap(const G0Gesture& s, uint32_t now, bool note);  // a first tap, released now
// G0 went down. memo: a memo may start here now (nothing records; locked, the card is lit).
G0Step g0_press(const G0Gesture& s, uint32_t now, bool memo);
// A memo took the press now down (resumed it in its grace; ends: ended a hands-free one).
G0Gesture g0_take(const G0Gesture& s, bool ends);
G0Gesture g0_release(const G0Gesture& s, uint32_t now);  // G0 went up: its events are over
G0Step g0_tick(const G0Gesture& s, uint32_t now);
G0Step g0_key(const G0Gesture& s);                       // a key other than G0
G0Step g0_event(const G0Gesture& s, const KeyEvent& ev);  // Eat / Hold: taken, do nothing else
inline bool g0_waits(const G0Gesture& s) { return s.waits; }

}  // namespace cpa
