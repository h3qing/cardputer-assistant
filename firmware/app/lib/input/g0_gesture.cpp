#include "g0_gesture.h"

namespace cpa {
namespace {

bool is_g0(const KeyEvent& ev) {
  return ev.kind == KeyEventKind::G0Tap || ev.kind == KeyEventKind::G0Long || ev.kind == KeyEventKind::G0LongUp;
}

// The wait is over without a second tap: the note, when the tap was a note tap. late: the
// window ran out (nothing else happened), so a slow second press may still follow.
G0Step settle(const G0Gesture& s, bool late) {
  const bool note = s.waits && s.note;
  G0Step out{s, note ? G0Out::Note : G0Out::None};
  out.state.waits = false;
  out.state.late = late && note;
  return out;
}

// The press now down starts a hands-free memo: it is taken (its G0Long: Hold).
G0Step take_second(const G0Gesture& s, G0Out out) {
  G0Step st{s, out};
  st.state.waits = false;
  st.state.late = false;
  st.state.eat = true;
  st.state.started = true;
  return st;
}

}  // namespace

G0Gesture g0_tap(const G0Gesture& s, uint32_t now, bool note) {
  G0Gesture out = s;
  out.waits = true;
  out.tap_ms = now;
  out.note = note;
  out.late = false;
  out.hush = false;
  return out;
}

G0Step g0_press(const G0Gesture& in, uint32_t now, bool memo) {
  G0Gesture s = in;
  s.hush = false;
  const uint32_t gap = now - in.tap_ms;
  if (in.hush && gap < kDoubleTapMs) {  // the second tap of a double tap that ended a memo
    s.eat = true;
    return G0Step{s, G0Out::None};
  }
  if (in.waits && memo && gap < kDoubleTapMs) return take_second(s, G0Out::HandsFree);
  const bool slow = in.note && (in.waits || in.late);
  if (slow && memo && gap < kLateTapMs) return take_second(s, G0Out::Late);
  return settle(s, false);
}

G0Gesture g0_take(const G0Gesture& s, bool ends) {
  G0Gesture out = s;
  out.eat = true;
  out.started = false;
  out.ends = ends;
  return out;
}

G0Gesture g0_release(const G0Gesture& s, uint32_t now) {
  G0Gesture out = s;
  out.eat = false;
  out.started = false;
  out.ends = false;
  out.hush = s.ends;
  if (s.ends) out.tap_ms = now;
  return out;
}

G0Step g0_tick(const G0Gesture& s, uint32_t now) {
  if (!s.waits || now - s.tap_ms < kDoubleTapMs) return G0Step{s, G0Out::None};
  return settle(s, true);
}

G0Step g0_key(const G0Gesture& s) { return settle(s, false); }

G0Step g0_event(const G0Gesture& s, const KeyEvent& ev) {
  if (!s.eat || !is_g0(ev)) return G0Step{s, G0Out::None};
  if (!s.started || ev.kind != KeyEventKind::G0Long) return G0Step{s, G0Out::Eat};
  G0Step out{s, G0Out::Hold};
  out.state.started = false;
  return out;
}

}  // namespace cpa
