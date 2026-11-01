// Mando's easter eggs in the shell (v0.5.0, docs/04 "hidden keys", mando_eggs.h): Opt+W pats
// him on home (step 7 of the dispatch, before the global keys: nothing over home, nothing
// recording) and on the lit lock card's clock (the lock gate, which swallows it as any other
// key: never while recording, under a G0 or on a result card). Silent: no toast, no hint;
// the card stays lit while he hops. A pat or the all clear going off redraws the page.
#include <cstring>

#include "keymap.h"
#include "lock_screen.h"
#include "mando_eggs.h"
#include "shell.h"

namespace cpa {
namespace {

bool is_pat(const KeyEvent& ev) { return keymap_lookup(Layer::Egg, ev).action == Action::PatMando; }

}  // namespace

bool Shell::on_home() const {
  if (lock_.locked || menu_open_ || overlay_ != proto::Overlay::None || rec_up()) return false;
  return std::strcmp(top_id(), "home") == 0;
}

bool Shell::pat_key(const KeyEvent& ev) {
  if (!is_pat(ev) || std::strcmp(top_id(), "home") != 0) return false;
  pat(now_);
  return true;
}

bool Shell::pat_allowed_locked(uint32_t now) const {
  if (!lock_lit(now) || rec_up() || lock_.eat_up || lock_.swallowed || g0_down_ || memo_ready_) return false;
  return lock_mando_act(state()) == theme::MandoAct::Doze;  // the clock card, not a result
}

bool Shell::lock_pat(const KeyEvent& ev, uint32_t now) {
  if (!is_pat(ev) || !pat_allowed_locked(now)) return false;
  lock_flash(now, kPatShowMs);
  pat(now);
  return true;
}

void Shell::pat(uint32_t now) {
  pat_ = pat_next(pat_, now);
  egg_on_ = true;
  dirty_ = true;
  mirror();
}

void Shell::egg_tick(uint32_t now) {
  const bool on = pat_showing(pat_, now) || all_clear_showing(state().ui, now);
  if (on == egg_on_) return;
  egg_on_ = on;
  dirty_ = true;
}

}  // namespace cpa
