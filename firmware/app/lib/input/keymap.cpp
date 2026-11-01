#include "keymap.h"

namespace cpa {
namespace {

constexpr KeyEvent opt(char k) { return KeyEvent{KeyEventKind::Chord, 0, kModOpt, k, Nav::None}; }
constexpr KeyEvent chr(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }
constexpr KeyEvent nav(Nav n) { return KeyEvent{KeyEventKind::Nav, 0, 0, 0, n}; }
constexpr KeyEvent g0(KeyEventKind k) { return KeyEvent{k, 0, 0, 0, Nav::None}; }

constexpr Binding bind(Layer l, Action a, KeyEvent t, uint8_t arg = 0) { return Binding{l, a, arg, t}; }

uint8_t key_for_char(char c) {
  for (uint8_t k = 0; k < kMatrixKeys; ++k) {
    if (key_base(k) == c && !is_modifier(k)) return k;
  }
  return key::kG0;  // unreachable for the bound characters
}

KeySet mod_keys(uint8_t mods) {
  KeySet s;
  s = (mods & kModFn) ? s.with(key::kFn) : s;
  s = (mods & kModShift) ? s.with(key::kShift) : s;
  s = (mods & kModCtrl) ? s.with(key::kCtrl) : s;
  s = (mods & kModOpt) ? s.with(key::kOpt) : s;
  return (mods & kModAlt) ? s.with(key::kAlt) : s;
}

KeySet nav_keys(Nav n) {
  switch (n) {
    case Nav::Enter: return KeySet{}.with(key::kEnter);
    case Nav::Bksp: return KeySet{}.with(key::kBksp);
    case Nav::Back: return KeySet{}.with(key::kFn).with(key::kGrave);
    case Nav::Menu: return KeySet{}.with(key::kTab);
    case Nav::Up: return KeySet{}.with(key::kFn).with(key_for_char(';'));  // docs/20 U17: Fn arrows
    case Nav::Down: return KeySet{}.with(key::kFn).with(key_for_char('.'));
    case Nav::Left: return KeySet{}.with(key::kFn).with(key_for_char(','));
    case Nav::Right: return KeySet{}.with(key::kFn).with(key_for_char('/'));
    default: return KeySet{};
  }
}

}  // namespace

// clang-format off
const Binding kKeymap[] = {
    // 04 §8.2 global
    bind(Layer::Global, Action::QuickNote, g0(KeyEventKind::G0Tap)),
    bind(Layer::Global, Action::RecMemo, g0(KeyEventKind::G0Long)),
    bind(Layer::Global, Action::RecHandsFree, g0(KeyEventKind::G0Double)),  // gh #51: g0_gesture.h
    bind(Layer::Global, Action::RecMeeting, opt('r')),
    bind(Layer::Global, Action::Home, opt('h')),      // v0.4.0: the home launcher (docs/13 §5.2)
    bind(Layer::Global, Action::Projects, opt('p')),  // v0.4.0: the board, Opt+H before
    bind(Layer::Global, Action::Inbox, opt('i')),
    bind(Layer::Global, Action::OpenProject, opt('1'), 1), bind(Layer::Global, Action::OpenProject, opt('2'), 2),
    bind(Layer::Global, Action::OpenProject, opt('3'), 3), bind(Layer::Global, Action::OpenProject, opt('4'), 4),
    bind(Layer::Global, Action::OpenProject, opt('5'), 5), bind(Layer::Global, Action::OpenProject, opt('6'), 6),
    bind(Layer::Global, Action::OpenProject, opt('7'), 7), bind(Layer::Global, Action::OpenProject, opt('8'), 8),
    bind(Layer::Global, Action::OpenProject, opt('9'), 9),
    bind(Layer::Global, Action::NewNote, opt('n')),
    bind(Layer::Global, Action::NewQueue, opt('q')),
    bind(Layer::Global, Action::NewSpawn, opt('s')),
    bind(Layer::Global, Action::System, opt('m')),
    bind(Layer::Global, Action::Escape, nav(Nav::Back)),
    // 04 §8.3 lists
    bind(Layer::List, Action::Up, chr(';')),
    bind(Layer::List, Action::Down, chr('.')),
    bind(Layer::List, Action::TabPrev, chr(',')),
    bind(Layer::List, Action::TabNext, chr('/')),
    bind(Layer::List, Action::Primary, nav(Nav::Enter)),
    bind(Layer::List, Action::Dismiss, opt('d')),
    bind(Layer::List, Action::Back, chr('`')),
    bind(Layer::List, Action::Back, nav(Nav::Bksp)),
    // docs/20 U17: the arrows printed on ; . , / also with Fn (the M5Cardputer Fn layer),
    // on every list, the home launcher and the menu
    bind(Layer::List, Action::Up, nav(Nav::Up)),
    bind(Layer::List, Action::Down, nav(Nav::Down)),
    bind(Layer::List, Action::TabPrev, nav(Nav::Left)),
    bind(Layer::List, Action::TabNext, nav(Nav::Right)),
    // 04 §8.4 compose
    bind(Layer::Compose, Action::DeleteChar, nav(Nav::Bksp)),
    bind(Layer::Compose, Action::Submit, nav(Nav::Enter)),
    bind(Layer::Compose, Action::Submit, g0(KeyEventKind::G0Tap)),
    bind(Layer::Compose, Action::Cancel, chr('`')),         // docs/20 D5: ` leaves, as on every page
    bind(Layer::Compose, Action::Backtick, nav(Nav::Back)),  // ... and Fn+` types it
    bind(Layer::Compose, Action::Target, opt('0'), 0), bind(Layer::Compose, Action::Target, opt('1'), 1),
    bind(Layer::Compose, Action::Target, opt('2'), 2), bind(Layer::Compose, Action::Target, opt('3'), 3),
    bind(Layer::Compose, Action::Target, opt('4'), 4), bind(Layer::Compose, Action::Target, opt('5'), 5),
    bind(Layer::Compose, Action::Target, opt('6'), 6), bind(Layer::Compose, Action::Target, opt('7'), 7),
    bind(Layer::Compose, Action::Target, opt('8'), 8), bind(Layer::Compose, Action::Target, opt('9'), 9),
    bind(Layer::Compose, Action::TypeNote, opt('n')),
    bind(Layer::Compose, Action::TypeQueue, opt('q')),
    bind(Layer::Compose, Action::TypeSpawn, opt('s')),
    bind(Layer::Compose, Action::Menu, nav(Nav::Menu)),  // docs/13 §5.3: the draft stays
    bind(Layer::Compose, Action::Lock, opt('l')),
    // 04 §8.4 confirm
    bind(Layer::Confirm, Action::Confirm, nav(Nav::Enter)),
    bind(Layer::Confirm, Action::Cancel, nav(Nav::Back)),
    bind(Layer::Confirm, Action::Cancel, chr('`')),  // issue #15: the top-left key alone
    bind(Layer::Confirm, Action::Menu, nav(Nav::Menu)),  // no Opt+L: the box is dealt with first
    // docs/20 D3: the meeting's boxes (docs/11 §4.1). Opt+L leaves the box (the start box is
    // cancelled, the end box goes back to the meeting) and locks.
    bind(Layer::RecConfirm, Action::Confirm, nav(Nav::Enter)),
    bind(Layer::RecConfirm, Action::Cancel, nav(Nav::Back)),
    bind(Layer::RecConfirm, Action::Cancel, chr('`')),
    bind(Layer::RecConfirm, Action::Menu, nav(Nav::Menu)),
    bind(Layer::RecConfirm, Action::Lock, opt('l')),
    // docs/11 §4.1 memo HUD (G0's edges are the recording core's: let go, it ends after the
    // grace; gh #51)
    bind(Layer::RecMemo, Action::RecTag, chr('0'), 0), bind(Layer::RecMemo, Action::RecTag, chr('1'), 1),
    bind(Layer::RecMemo, Action::RecTag, chr('2'), 2), bind(Layer::RecMemo, Action::RecTag, chr('3'), 3),
    bind(Layer::RecMemo, Action::RecTag, chr('4'), 4), bind(Layer::RecMemo, Action::RecTag, chr('5'), 5),
    bind(Layer::RecMemo, Action::RecTag, chr('6'), 6), bind(Layer::RecMemo, Action::RecTag, chr('7'), 7),
    bind(Layer::RecMemo, Action::RecTag, chr('8'), 8), bind(Layer::RecMemo, Action::RecTag, chr('9'), 9),
    // gh #51: the hands-free memo. G0 ends it on its down edge (Shell::on_g0: the press is
    // the memo's, its tap never comes here), so the tap is bound for the help alone (the
    // menu's row is shown only: Tab, Enter must not end it); digits as on any memo; Tab and
    // Opt+L as on a meeting, and it goes on
    bind(Layer::RecLatch, Action::RecStop, g0(KeyEventKind::G0Tap)),
    bind(Layer::RecLatch, Action::RecTag, chr('0'), 0), bind(Layer::RecLatch, Action::RecTag, chr('1'), 1),
    bind(Layer::RecLatch, Action::RecTag, chr('2'), 2), bind(Layer::RecLatch, Action::RecTag, chr('3'), 3),
    bind(Layer::RecLatch, Action::RecTag, chr('4'), 4), bind(Layer::RecLatch, Action::RecTag, chr('5'), 5),
    bind(Layer::RecLatch, Action::RecTag, chr('6'), 6), bind(Layer::RecLatch, Action::RecTag, chr('7'), 7),
    bind(Layer::RecLatch, Action::RecTag, chr('8'), 8), bind(Layer::RecLatch, Action::RecTag, chr('9'), 9),
    bind(Layer::RecLatch, Action::Menu, nav(Nav::Menu)),
    bind(Layer::RecLatch, Action::Lock, opt('l')),
    // docs/11 §4.1 meeting page
    bind(Layer::RecPage, Action::RecStar, g0(KeyEventKind::G0Tap)),
    bind(Layer::RecPage, Action::RecStar, g0(KeyEventKind::G0Long)),
    bind(Layer::RecPage, Action::RecStar, chr(' ')),
    bind(Layer::RecPage, Action::RecTag, chr('0'), 0), bind(Layer::RecPage, Action::RecTag, chr('1'), 1),
    bind(Layer::RecPage, Action::RecTag, chr('2'), 2), bind(Layer::RecPage, Action::RecTag, chr('3'), 3),
    bind(Layer::RecPage, Action::RecTag, chr('4'), 4), bind(Layer::RecPage, Action::RecTag, chr('5'), 5),
    bind(Layer::RecPage, Action::RecTag, chr('6'), 6), bind(Layer::RecPage, Action::RecTag, chr('7'), 7),
    bind(Layer::RecPage, Action::RecTag, chr('8'), 8), bind(Layer::RecPage, Action::RecTag, chr('9'), 9),
    bind(Layer::RecPage, Action::RecEnd, opt('r')),  // docs/20 D3: ` only says so (shell.cpp)
    bind(Layer::RecPage, Action::Menu, nav(Nav::Menu)),
    bind(Layer::RecPage, Action::Lock, opt('l')),  // the recording goes on
    // v0.4.0 (docs/13 §5.3): Tab and Opt+L on home and the apps, checked before the
    // module. The overlays above carry their own (§5.3 table); the memo HUD has none.
    bind(Layer::System, Action::Menu, nav(Nav::Menu)),
    bind(Layer::System, Action::Lock, opt('l')),
    // v0.4.0 (docs/13 §7.1): the unlock keys the lock gate lets through. Opt+L lights the
    // lock card and opens the unlock window, Enter inside it unlocks. G0 is not here: the
    // gate handles it before this table (it lights the card, records a memo from the lit
    // card, or stars a meeting; lock.h).
    bind(Layer::Lock, Action::Lock, opt('l')),
    bind(Layer::Lock, Action::Unlock, nav(Nav::Enter)),
    // v0.5.0 (docs/04, hidden keys): an easter egg, on a layer of its own that nothing lists
    bind(Layer::Egg, Action::PatMando, opt('w')),
};
// clang-format on

const size_t kKeymapCount = sizeof(kKeymap) / sizeof(kKeymap[0]);

Resolved keymap_lookup(Layer layer, const KeyEvent& ev) {
  for (size_t i = 0; i < kKeymapCount; ++i) {
    if (kKeymap[i].layer == layer && kKeymap[i].trigger == ev) return Resolved{kKeymap[i].action, kKeymap[i].arg};
  }
  return Resolved{};
}

KeySet binding_keys(const Binding& b) {
  const KeyEvent& t = b.trigger;
  switch (t.kind) {
    case KeyEventKind::G0Tap:
    case KeyEventKind::G0Long:
    case KeyEventKind::G0LongUp:
    case KeyEventKind::G0Double: return KeySet{}.with(key::kG0);
    case KeyEventKind::Nav: return nav_keys(t.nav);
    case KeyEventKind::Chord: return mod_keys(t.mods).with(key_for_char(t.key));
    case KeyEventKind::Char: return KeySet{}.with(key_for_char(t.ch));
    default: return KeySet{};
  }
}

}  // namespace cpa
