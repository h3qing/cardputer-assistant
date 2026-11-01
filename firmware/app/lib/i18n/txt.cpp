#include "txt.h"

namespace cpa {

// Reached only by a Txt built at run time (a constexpr one that breaks a rule does not
// compile, which is the point): tools/i18n/check_texts.py forbids those, so these do nothing.
void txt_empty() {}
void txt_control_char() {}
void txt_han_in_english() {}
void txt_missing_glyph() {}
void txt_format_mismatch() {}
void txt_too_wide() {}

BothText both(const Txt& t) { return BothText::from(t.zh).append(" ").append(t.en); }

}  // namespace cpa
