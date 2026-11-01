#include "lang_switch.h"

#include <cstdio>

#include "lang_names.h"
#include "txt.h"

namespace cpa {
namespace {

// Format strings: their widths are measured with every argument in test_lang_switch.
constexpr Txt kSwitched{"%s · 再按 Enter 换回 %s", "%s · Enter again: %s"};
constexpr Txt kUnsaved{"没存上：重启后会回到%s", "Not saved: %s after a restart"};

}  // namespace

ToastText lang_switched_text(Lang from, Lang to) {
  return ToastText::build([&](char* buf, size_t n) {
    const int w = std::snprintf(buf, n, tr_in(kSwitched, to), lang_name(to), lang_name(from));
    return w < 0 ? size_t{0} : static_cast<size_t>(w);
  });
}

ToastText lang_unsaved_text(Lang stored) {
  return ToastText::build([&](char* buf, size_t n) {
    const int w = std::snprintf(buf, n, tr(kUnsaved), lang_name(stored));
    return w < 0 ? size_t{0} : static_cast<size_t>(w);
  });
}

}  // namespace cpa
