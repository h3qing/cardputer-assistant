#include "chord_detector.h"

namespace cpa {
namespace {

// M5Cardputer 1.2.0 _key_value_map, unshifted and shifted (\b = Backspace, \t = Tab,
// \n = Enter, 0 = modifier).
constexpr char kBase[kMatrixKeys + 1] =
    "`1234567890-=\b"
    "\tqwertyuiop[]\\"
    "\0\0asdfghjkl;'\n"
    "\0\0\0zxcvbnm,./ ";
constexpr char kShifted[kMatrixKeys + 1] =
    "~!@#$%^&*()_+\b"
    "\tQWERTYUIOP{}|"
    "\0\0ASDFGHJKL:\"\n"
    "\0\0\0ZXCVBNM<>? ";

KeyEvent nav(Nav n) {
  KeyEvent e;
  e.kind = KeyEventKind::Nav;
  e.nav = n;
  return e;
}

KeyEvent of_kind(KeyEventKind kind) {
  KeyEvent e;
  e.kind = kind;
  return e;
}

Nav fn_layer(char base) {
  switch (base) {
    case '`': return Nav::Back;
    case ';': return Nav::Up;
    case '.': return Nav::Down;
    case ',': return Nav::Left;
    case '/': return Nav::Right;
    default: return Nav::None;
  }
}

// The event of one newly pressed (non-modifier, non-G0) key; kind None = nothing.
KeyEvent press_event(uint8_t k, uint8_t mods) {
  const char base = key_base(k);
  if (mods & (kModCtrl | kModOpt | kModAlt)) {
    KeyEvent e = of_kind(KeyEventKind::Chord);
    e.mods = mods;
    e.key = base;
    return e;
  }
  if (mods & kModFn) return fn_layer(base) == Nav::None ? KeyEvent{} : nav(fn_layer(base));
  if (k == key::kEnter) return nav(Nav::Enter);
  if (k == key::kBksp) return nav(Nav::Bksp);
  if (k == key::kTab) return nav(Nav::Menu);  // v0.4.0: the Tab menu (docs/13 §6.4)
  KeyEvent e = of_kind(KeyEventKind::Char);
  e.ch = (mods & kModShift) ? key_shifted(k) : base;
  return e;
}

ChordStep g0_step(const ChordStep& s, const KeySample& in) {
  ChordStep out = s;
  const bool was = s.state.prev.has(key::kG0);
  const bool is = in.keys.has(key::kG0);
  if (is && !was) {
    out.state.g0_since = in.now;
    out.state.g0_long_sent = false;
    return out;
  }
  if (!was) return out;
  if (s.state.g0_long_sent) {
    if (!is) out.effects = out.effects.with(of_kind(KeyEventKind::G0LongUp));
    return out;
  }
  const bool long_enough = in.now - s.state.g0_since >= kG0LongMs;
  if (is && long_enough) {
    out.state.g0_long_sent = true;
    out.effects = out.effects.with(of_kind(KeyEventKind::G0Long));
  } else if (!is && long_enough) {  // late path: both in this step
    out.effects = out.effects.with(of_kind(KeyEventKind::G0Long)).with(of_kind(KeyEventKind::G0LongUp));
  } else if (!is) {
    out.effects = out.effects.with(of_kind(KeyEventKind::G0Tap));
  }
  return out;
}

ChordStep repeat_step(const ChordStep& s, const KeySample& in, bool pressed_now) {
  ChordStep out = s;
  const bool held = in.keys.has(key::kBksp) && s.state.prev.has(key::kBksp) && !pressed_now;
  const bool plain = (mods_of(in.keys) & ~kModShift) == 0;
  if (!held || !plain || static_cast<int32_t>(in.now - s.state.bksp_next) < 0) return out;
  const uint32_t next = s.state.bksp_next + kRepeatEveryMs;
  out.state.bksp_next = static_cast<int32_t>(next - in.now) > 0 ? next : in.now + kRepeatEveryMs;
  KeyEvent e = nav(Nav::Bksp);
  e.repeat = true;
  out.effects = out.effects.with(e);
  return out;
}

}  // namespace

char key_base(uint8_t k) { return k < kMatrixKeys ? kBase[k] : 0; }
char key_shifted(uint8_t k) { return k < kMatrixKeys ? kShifted[k] : 0; }

uint8_t mods_of(KeySet keys) {
  uint8_t m = 0;
  m |= keys.has(key::kFn) ? kModFn : 0;
  m |= keys.has(key::kShift) ? kModShift : 0;
  m |= keys.has(key::kCtrl) ? kModCtrl : 0;
  m |= keys.has(key::kOpt) ? kModOpt : 0;
  m |= keys.has(key::kAlt) ? kModAlt : 0;
  return m;
}

ChordStep chord_step(const ChordState& s, const KeySample& in) {
  ChordStep out{s, {}};
  const KeySet pressed = in.keys.minus(s.prev);
  const uint8_t mods = mods_of(in.keys);
  for (uint8_t k = 0; k < kMatrixKeys; ++k) {
    if (!pressed.has(k) || is_modifier(k)) continue;
    const KeyEvent e = press_event(k, mods);
    if (e.kind == KeyEventKind::None) continue;
    if (e.kind == KeyEventKind::Nav && e.nav == Nav::Bksp) out.state.bksp_next = in.now + kRepeatDelayMs;
    out.effects = out.effects.with(e);
  }
  out = repeat_step(out, in, pressed.has(key::kBksp));
  out = g0_step(out, in);
  out.state.prev = in.keys;
  return out;
}

}  // namespace cpa
