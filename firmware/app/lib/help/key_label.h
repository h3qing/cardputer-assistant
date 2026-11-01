// Key names for the Tab menu and the hint bar (docs/13 §6.3), as the hint bar writes
// them: "Opt+H", "Tab", "G0", "按住G0", "双击G0", "Enter", "退格", "`", "空格", "Fn+`".
// A run of digit bindings is one range label: "Opt+1-9", "0-9".
#pragma once

#include "chord_detector.h"
#include "fixed_str.h"

namespace cpa {

using KeyLabel = FixedStr<23>;

KeyLabel key_label(const KeyEvent& ev);  // "" for KeyEventKind::None

// "<key_label(lo)>-<last digit of hi>": lo and hi are the same kind of digit key.
KeyLabel key_range_label(const KeyEvent& lo, const KeyEvent& hi);

}  // namespace cpa
