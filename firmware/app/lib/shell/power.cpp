#include "power.h"

namespace cpa {

namespace {

// G0 wakes straight into the quick note or a voice memo (and its release ends it).
bool wakes_into_action(const KeyEvent& ev) {
  return ev.kind == KeyEventKind::G0Tap || ev.kind == KeyEventKind::G0Long || ev.kind == KeyEventKind::G0LongUp;
}

}  // namespace

PowerStep power_key(const PowerState& s, const KeyEvent& ev, uint32_t now) {
  PowerStep out{s, false};
  out.state.last_key_ms = now;
  out.state.dim = false;
  if (!s.dark) return out;
  out.state.dark = false;
  out.swallow = !wakes_into_action(ev);
  return out;
}

PowerState power_wake(const PowerState& s, uint32_t now) {
  PowerState out = s;
  out.last_key_ms = now;
  out.dim = false;
  out.dark = false;
  return out;
}

PowerState power_tick(const PowerState& s, bool plugged, uint32_t now) {
  PowerState out = s;
  if (plugged) {
    out.dark = false;
    out.dim = false;
    out.last_key_ms = now;  // unplugging starts a fresh 60 s
    return out;
  }
  const uint32_t idle = now - s.last_key_ms;
  if (s.dark || idle < kBacklightIdleMs - kPreDimMs) return out;
  out.dim = idle < kBacklightIdleMs || s.recording;  // U24: dimmed first
  out.dark = idle >= kBacklightIdleMs && !s.recording;
  return out;
}

PowerState power_recording(const PowerState& s, bool recording) {
  PowerState out = s;
  out.recording = recording;
  return out;
}

PowerState power_lock(const PowerState& s, bool locked, bool card, uint32_t now) {
  PowerState out = s;
  out.locked = locked;
  out.card = locked && card;
  if (!s.locked || locked) return out;
  out.dark = false;
  out.dim = false;
  out.last_key_ms = now;
  return out;
}

Backlight backlight_of(const PowerState& s) {
  if (s.locked && s.card) return Backlight::On;
  if (s.recording && (s.locked || s.dark || s.dim)) return Backlight::Dim;  // ● stays visible
  if (s.locked || s.dark) return Backlight::Off;
  return s.dim ? Backlight::Dim : Backlight::On;
}

}  // namespace cpa
