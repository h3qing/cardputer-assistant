#include "lock.h"

#include "keymap.h"

namespace cpa {
namespace {

// now is before until (wrap-safe).
bool before(uint32_t now, uint32_t until) { return static_cast<int32_t>(until - now) > 0; }

LockStep swallow(const LockState& s) { return LockStep{s, LockOut::Swallow}; }
LockStep pass(const LockState& s) { return LockStep{s, LockOut::Pass}; }

LockState lit(const LockState& s, uint32_t now) {
  LockState out = s;
  out.card = true;
  out.card_until_ms = now + kLockCardMs;
  out.flash = false;
  out.swallowed = false;  // a key lit it: the card answers that key now
  return out;
}

LockState armed(const LockState& s, uint32_t now) {
  LockState out = s;
  out.armed = true;
  out.arm_until_ms = now + kLockCardMs;
  return out;
}

// Lit until its time, or longer while G0 is held on it (docs/20 U3c), kHoldLitMs at most.
bool card_on(const LockState& s, uint32_t now) {
  const bool held = (s.eat_up || s.g0_held) && before(now, s.card_until_ms + kHoldLitMs);
  return s.card && (before(now, s.card_until_ms) || held);
}

bool is_g0(const KeyEvent& ev) {
  return ev.kind == KeyEventKind::G0Tap || ev.kind == KeyEventKind::G0Long || ev.kind == KeyEventKind::G0LongUp;
}

// G0 outside a meeting (header table). G0Long comes kG0LongMs after the press, which is
// when the card counts (docs/20 U3b).
LockStep g0_key(const LockState& s, const KeyEvent& ev, uint32_t now) {
  if (ev.kind == KeyEventKind::G0Tap) return swallow(armed(lit(s, now), now));
  if (ev.kind == KeyEventKind::G0LongUp) return pass(s);
  if (lock_arms(s, now - kG0LongMs)) return pass(s);
  LockState out = lit(s, now);
  out.eat_up = true;
  out.swallowed = true;
  return swallow(out);
}

}  // namespace

LockState lock_engage(uint32_t now, uint32_t lit_ms) {
  LockState s;
  s.locked = true;
  s.card = lit_ms > 0;
  s.card_until_ms = now + lit_ms;
  return s;
}

LockStep lock_key(const LockState& s, const KeyEvent& ev, uint32_t now, bool meeting) {
  if (!s.locked) return pass(s);
  if (s.eat_up && ev.kind == KeyEventKind::G0LongUp) {
    LockState out = s;
    out.eat_up = false;
    return swallow(out);
  }
  if (is_g0(ev)) return meeting ? pass(s) : g0_key(s, ev, now);
  switch (keymap_lookup(Layer::Lock, ev).action) {
    case Action::Lock: {
      LockState out = lit(s, now);
      out.window = true;
      out.window_until_ms = now + kUnlockWindowMs;
      return swallow(out);
    }
    case Action::Unlock:
      if (s.window && before(now, s.window_until_ms)) return LockStep{LockState{}, LockOut::Unlock};
      return swallow(s);
    default: return swallow(s);
  }
}

LockState lock_tick(const LockState& s, uint32_t now) {
  LockState out = s;
  out.card = card_on(s, now);
  out.window = s.window && before(now, s.window_until_ms);
  out.swallowed = s.swallowed && out.card;
  return out;
}

LockState lock_dark(const LockState& s) {
  LockState out = s;
  out.card = false;
  out.armed = false;
  out.g0_held = false;
  out.swallowed = false;
  return out;
}

LockState lock_flash(const LockState& s, uint32_t now, uint32_t ms) {
  if (!s.locked) return s;
  LockState out = s;
  const bool on = card_on(s, now);
  out.card = true;
  out.card_until_ms = !on || before(s.card_until_ms, now + ms) ? now + ms : s.card_until_ms;
  out.flash = !on || s.flash;
  return out;
}

LockState lock_g0(const LockState& s, bool down, uint32_t now) {
  LockState out = s;
  out.g0_held = down && lock_lit(s, now);
  return out;
}

bool lock_arms(const LockState& s, uint32_t press_ms) { return s.armed && before(press_ms, s.arm_until_ms); }

bool lock_lit(const LockState& s, uint32_t now) { return s.locked && card_on(s, now); }

}  // namespace cpa
