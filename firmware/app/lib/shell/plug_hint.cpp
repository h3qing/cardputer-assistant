#include "plug_hint.h"

namespace cpa {
namespace {

PlugStep step(const PlugState& s, bool taken, PlugDo act) {
  PlugStep out;
  out.st = s;
  out.taken = taken;
  out.act = act;
  return out;
}

PlugState in(PlugHint phase, const PlugState& s, uint32_t now) {
  PlugState out = s;
  out.phase = phase;
  out.since = now;
  return out;
}

bool is_back(const KeyEvent& ev) {
  return (ev.kind == KeyEventKind::Char && ev.ch == '`') || (ev.kind == KeyEventKind::Nav && ev.nav == Nav::Back);
}

}  // namespace

PlugState plug_open(uint8_t recs, const MonoId& from, uint32_t now) {
  PlugState s;
  s.phase = PlugHint::Shown;
  s.since = now;
  s.opened = now;
  s.from = from;
  s.n = recs;
  return s;
}

PlugStep plug_key(const PlugState& s, const KeyEvent& ev, uint32_t now) {
  const bool tab = ev.kind == KeyEventKind::Nav && ev.nav == Nav::Menu;
  const bool enter = ev.kind == KeyEventKind::Nav && ev.nav == Nav::Enter;
  if (s.phase == PlugHint::Shown) return tab ? step(in(PlugHint::Asking, s, now), true, PlugDo::None) : step(s, false, PlugDo::None);
  if (s.phase != PlugHint::Asking) return step(s, false, PlugDo::None);
  if (enter) return step(in(PlugHint::Busy, s, now), true, PlugDo::Hold);
  return step(PlugState{}, is_back(ev), PlugDo::Release);
}

PlugStep plug_tick(const PlugState& s, uint32_t now) {
  const bool open = s.phase == PlugHint::Shown || s.phase == PlugHint::Asking;
  const bool over = (s.phase == PlugHint::Shown && now - s.since >= kPlugShowMs) ||
                    (s.phase == PlugHint::Asking && now - s.since >= kPlugAskMs) ||
                    (open && now - s.opened >= kPlugHiddenMaxMs);
  return over ? step(PlugState{}, false, PlugDo::Release) : step(s, false, PlugDo::None);
}

PlugState plug_hide(const PlugState& s, uint32_t dt) {
  if (s.phase != PlugHint::Shown && s.phase != PlugHint::Asking) return s;
  PlugState out = s;
  out.since = s.since + dt;
  return out;
}

PlugState plug_done(const PlugState& s) {
  (void)s;
  return PlugState{};
}

}  // namespace cpa
