// The words of a language switch (docs/21 §4.3): the toast that says how to switch back,
// and the one when the switch could not be stored. Each names the languages in their own
// script (kLangNames through %s), so the English half holds no Han character.
#pragma once

#include "lang.h"
#include "toast.h"

namespace cpa {

// In `to` (the language now shown): "English · Enter again: 中文".
ToastText lang_switched_text(Lang from, Lang to);
// In the shown language: what a restart will show, the language NVS still holds.
ToastText lang_unsaved_text(Lang stored);

}  // namespace cpa
