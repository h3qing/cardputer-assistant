// The device's UI language (docs/21 §6.3, §6.8). One byte, read and written only on the
// loop thread: the shell sets it once at boot (after NVS is read, before the first frame)
// and when the user switches it (the settings page, or debug.lang in the test build).
// Every UI string is a Txt pair taken with tr() when it is drawn, so a switch needs only a
// redraw. Pure: no Arduino, M5 or esp headers.
#pragma once

#include <cstdint>
#include <cstring>

#include "opt.h"

namespace cpa {

enum class Lang : uint8_t { Zh = 0, En = 1 };

constexpr Lang kLangs[] = {Lang::Zh, Lang::En};

Lang lang();
void set_lang(Lang l);

// NVS (docs/21 §6.8): 0 Chinese, 1 English; anything else (an erased 0xFF) is Chinese.
constexpr Lang lang_from_nvs(uint8_t b) { return b == 1 ? Lang::En : Lang::Zh; }
constexpr uint8_t lang_to_nvs(Lang l) { return l == Lang::En ? 1 : 0; }
constexpr Lang lang_other(Lang l) { return l == Lang::En ? Lang::Zh : Lang::En; }

// The wire names (debug.lang, debug.state.ui.lang).
constexpr const char* lang_code(Lang l) { return l == Lang::En ? "en" : "zh"; }
inline Opt<Lang> lang_from_code(const char* s) {
  if (std::strcmp(s, "zh") == 0) return some(Lang::Zh);
  if (std::strcmp(s, "en") == 0) return some(Lang::En);
  return Opt<Lang>{};
}

// Tests: the language for one scope, the previous one back at its end.
class LangScope {
 public:
  explicit LangScope(Lang l) : saved_(lang()) { set_lang(l); }
  ~LangScope() { set_lang(saved_); }
  LangScope(const LangScope&) = delete;
  LangScope& operator=(const LangScope&) = delete;

 private:
  Lang saved_;
};

}  // namespace cpa
