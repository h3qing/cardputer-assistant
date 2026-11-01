// The language names, each in its own script (docs/21 §4.3): the only Chinese outside a
// Txt. The language row and the switch toasts fill them in with %s, so the English half
// of a Txt never holds a Han character.
#pragma once

#include "lang.h"

namespace cpa {

constexpr const char* kLangNames[] = {"中文", "English"};  // by Lang

constexpr const char* lang_name(Lang l) { return kLangNames[static_cast<uint8_t>(l)]; }

}  // namespace cpa
