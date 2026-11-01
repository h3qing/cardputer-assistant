#include "key_label.h"

#include "txt.h"

namespace cpa {
namespace {

// docs/21 §6.5: the keys printed in words; Del is what the keycap says.
constexpr KeyTxt kBksp{"退格", "Del"};
constexpr KeyTxt kSpace{"空格", "Space"};
constexpr KeyTxt kHoldG0{"按住G0", "Hold G0"};
constexpr KeyTxt kG0Up{"松开G0", "G0 up"};
constexpr KeyTxt kG0Double{"双击G0", "G0 x2"};

const char* nav_name(Nav n) {
  switch (n) {
    case Nav::Up: return "Fn+;";
    case Nav::Down: return "Fn+.";
    case Nav::Left: return "Fn+,";
    case Nav::Right: return "Fn+/";
    case Nav::Enter: return "Enter";
    case Nav::Back: return "Fn+`";
    case Nav::Bksp: return tr(kBksp);
    case Nav::Menu: return "Tab";
    default: return "";
  }
}

// A typed character as a key: letters upper-case in chords ("Opt+H"), as typed otherwise.
KeyLabel char_name(char c, bool chord) {
  if (c == ' ') return KeyLabel::from(tr(kSpace));
  if (c == '\n') return KeyLabel::from("Enter");
  if (c == '\b') return KeyLabel::from(tr(kBksp));
  const char up = (chord && c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  return KeyLabel::from(&up, 1);
}

KeyLabel chord_name(uint8_t mods, char key) {
  KeyLabel out;
  out = (mods & kModFn) ? out.append("Fn+") : out;
  out = (mods & kModShift) ? out.append("Shift+") : out;
  out = (mods & kModCtrl) ? out.append("Ctrl+") : out;
  out = (mods & kModOpt) ? out.append("Opt+") : out;
  out = (mods & kModAlt) ? out.append("Alt+") : out;
  return out.append(char_name(key, true).c_str());
}

}  // namespace

KeyLabel key_label(const KeyEvent& ev) {
  switch (ev.kind) {
    case KeyEventKind::Char: return char_name(ev.ch, false);
    case KeyEventKind::Chord: return chord_name(ev.mods, ev.key);
    case KeyEventKind::G0Tap: return KeyLabel::from("G0");
    case KeyEventKind::G0Long: return KeyLabel::from(tr(kHoldG0));
    case KeyEventKind::G0LongUp: return KeyLabel::from(tr(kG0Up));
    case KeyEventKind::G0Double: return KeyLabel::from(tr(kG0Double));  // gh #51
    case KeyEventKind::Nav: return KeyLabel::from(nav_name(ev.nav));
    default: return KeyLabel{};
  }
}

KeyLabel key_range_label(const KeyEvent& lo, const KeyEvent& hi) {
  const char last = hi.kind == KeyEventKind::Chord ? hi.key : hi.ch;
  return key_label(lo).append("-").append(&last, 1);
}

}  // namespace cpa
