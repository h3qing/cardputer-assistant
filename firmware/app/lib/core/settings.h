// Device settings (docs/21 §6.8): what the user changes on the settings page and the
// device keeps across restarts. One NVS key per setting (namespace "cpa"), never one blob,
// so a later setting needs no migration. Pure.
#pragma once

#include <cstdint>

#include "lang.h"

namespace cpa {

struct Settings {
  Lang lang = Lang::Zh;

  constexpr bool operator==(const Settings& o) const { return lang == o.lang; }
  constexpr bool operator!=(const Settings& o) const { return !(*this == o); }
};

constexpr Settings with_lang(const Settings& s, Lang l) {
  Settings out = s;
  out.lang = l;
  return out;
}

// When a change is written (docs/21 §6.8 step 5): only when it differs from what NVS holds,
// and never while recording (a flash write stalls the cache), but as soon as it has ended.
enum class SaveWhen : uint8_t { Never, Now, AfterRec };

constexpr SaveWhen save_when(const Settings& want, const Settings& stored, bool recording) {
  if (want == stored) return SaveWhen::Never;
  return recording ? SaveWhen::AfterRec : SaveWhen::Now;
}

// What became of a switch's write: stored, waiting for the recording to end, or failed
// (the switch holds for this boot; the next one shows what NVS still has).
enum class SettingsSave : uint8_t { Saved, Pending, Failed };

}  // namespace cpa
