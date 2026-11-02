// v0.5.2 made English the default UI language (docs/21 §6.8). Suites written while it was
// Chinese check Chinese text, so their setUp() calls pin_zh(): the UI language, and the NVS
// of every fake device made after it (FakePlatform, so a Runtime boots in Chinese), are
// Chinese. Suites about English, or both languages, set the language themselves.
#pragma once

#include "lang.h"
#include "settings.h"

namespace testsupport {

// What a new FakePlatform's NVS holds: Settings{} (English) until a suite pins Chinese.
inline cpa::Settings& fake_nvs() {
  static cpa::Settings s;
  return s;
}

inline void pin_zh() {
  cpa::set_lang(cpa::Lang::Zh);
  fake_nvs() = cpa::with_lang(cpa::Settings{}, cpa::Lang::Zh);
}

}  // namespace testsupport
