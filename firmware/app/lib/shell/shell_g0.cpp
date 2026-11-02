// The shell's G0 beyond the keymap (gh #51, docs/20 U26): its edges, which a memo follows
// (rec_core.h: the grace, hands free), and the double tap (g0_gesture.h). The runtime calls
// on_g0 for each edge after that key sample's events; the events reach lock_gate and on_key,
// which hand them here first (eat_g0, locked_g0).
#include "rec_overlay.h"
#include "shell.h"

namespace cpa {
namespace {

bool memo_page(const RecInfo& r) {
  return r.mode == RecMode::Memo && (r.phase == RecPhase::Cue || r.phase == RecPhase::Recording);
}

constexpr KeyEvent kTap{KeyEventKind::G0Tap, 0, 0, 0, Nav::None};

bool is_g0(const KeyEvent& ev) {
  return ev.kind == KeyEventKind::G0Tap || ev.kind == KeyEventKind::G0Long || ev.kind == KeyEventKind::G0LongUp;
}

}  // namespace

// A memo follows G0 edge by edge, whatever is over it (the lock, a toast, the menu); so
// does the release a held end waits for (U15).
void Shell::on_g0(bool down, uint32_t now) {
  now_ = now;
  if (down) return g0_down();
  g0_ = g0_release(g0_, now);
  const RecInfo& r = state().rec;
  if (!memo_page(r) && !r.held) return;
  dirty_ = true;
  port_.rec_input(rec::RecInput::Release, 0);
}

// A press on a memo is the memo's: it resumes one in its grace, or ends a hands-free one,
// on its down edge (and closes the menu: G0 does what it does, docs/04 §8.7). Locked, G0
// acts on the lit card only: from dark the press lights the card, as a tap does (lock.h),
// and ends nothing; its events are the memo's too, so the card never answers it with
// "Not recording" while the memo records (docs/20 U3, B1). The end lights the card for its
// own 3 s, which then say how the memo ended (lock_screen.cpp, docs/20 D7). Anywhere else
// the press may be the second of a double tap, or a slow one (Late: the empty note the
// first tap opened closes).
void Shell::g0_down() {
  const RecInfo& r = state().rec;
  const bool lit = !lock_.locked || lock_lit(now_);
  if (memo_page(r)) return memo_press(r, lit);
  const G0Step st = g0_press(g0_, now_, lit && !rec_up());
  g0_ = st.state;
  if (st.out == G0Out::Note) open_note();
  if (st.out != G0Out::HandsFree && st.out != G0Out::Late) return;
  const bool note = st.out == G0Out::Late && empty_note();
  if (hands_free(true) && note) close_overlay();
}

void Shell::memo_press(const RecInfo& r, bool lit) {
  const bool ends = r.latched && lit;
  g0_ = g0_take(g0_, ends);
  dirty_ = true;
  if (r.latched && lock_.locked) lock_ = lock_key(lock_, kTap, now_, false).state;  // as a tap: lit 3 s
  if (r.latched && !lit) return;
  menu_open_ = menu_open_ && !r.latched;
  port_.rec_input(rec::RecInput::Press, 0);
}

// The rest of a press a gesture or a memo took: eaten. The G0Long of the press that started
// a hands-free memo makes it a held memo after all (tap, then hold).
bool Shell::eat_g0(const KeyEvent& ev) {
  const G0Step st = g0_event(g0_, ev);
  g0_ = st.state;
  if (st.out == G0Out::Hold) port_.rec_input(rec::RecInput::Hold, 0);
  return st.out != G0Out::None;
}

// Locked (lock_gate, before lock.h): the rest of a taken press is swallowed; a tap on the
// card a key lit (and nothing recording) may be the first of a double tap, and opens
// nothing; any other key ends that wait. A tap from dark only lights the card: it is no
// first tap, nor is one on a card lit by itself (docs/20 D7: it may light an empty pocket).
bool Shell::locked_g0(const KeyEvent& ev) {
  if (eat_g0(ev)) return true;
  if (!is_g0(ev)) {
    g0_ = g0_key(g0_).state;
    return false;
  }
  if (ev.kind == KeyEventKind::G0Tap && !rec_up() && lock_seen(now_)) g0_ = g0_tap(g0_, now_, false);
  return false;
}

// A key other than G0 while a tap waits: its note opens first, so the key lands in it.
void Shell::g0_other_key() {
  const G0Step st = g0_key(g0_);
  g0_ = st.state;
  if (st.out == G0Out::Note) open_note();
}

// The double tap's window ran out (tick).
void Shell::g0_due(uint32_t now) {
  const G0Step st = g0_tick(g0_, now);
  g0_ = st.state;
  if (st.out == G0Out::Note) open_note();
}

// The quick note of a single tap. Nothing opens over the lock or a recording (a host's
// debug.rec may have started one meanwhile).
void Shell::open_note() {
  if (lock_.locked || rec_up()) return;
  ComposeSpec spec;
  spec.kind = ComposeKind::Note;
  spec.slot = compose_slot();
  compose(spec);
}

// A hands-free memo (the double tap, or its menu row): a memo, latched from its start.
// From the menu's row G0 is up: the core hears so at once, or a memo that ends by itself
// would wait for a release (U15) that never comes (review of gh #51).
bool Shell::hands_free(bool g0_down) {
  if (!rec_gesture(RecMode::Memo)) return false;
  port_.rec_input(rec::RecInput::Latch, 0);
  if (!g0_down) port_.rec_input(rec::RecInput::Release, 0);
  return true;
}

// The quick note, opened and nothing typed: G0 has no use in it but to record.
bool Shell::empty_note() const {
  return overlay_ == proto::Overlay::Compose && !compose_.discard && compose_.spec.kind == ComposeKind::Note &&
         compose_.text.empty();
}

// Holding G0 in an empty quick note closes it and records, as the hold does on a page (a
// slow tap, then hold, recorded nothing: review of gh #51). Refused, the note stays.
bool Shell::note_hold(const KeyEvent& ev) {
  if (ev.kind != KeyEventKind::G0Long || !empty_note()) return false;
  if (rec_gesture(RecMode::Memo)) close_overlay();
  return true;
}

}  // namespace cpa
