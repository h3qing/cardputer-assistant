// Keyboard watchdog (05 §5.7 rule 7, 04 US-13 AC2), pure: keys held but the set has
// not changed for 10 s -> re-initialise the keyboard (and count it). Fires once per
// stuck set; any change re-arms it.
#pragma once

#include <cstdint>

#include "key_set.h"

namespace cpa {

constexpr uint32_t kKbStuckMs = 10000;

struct KbWatch {
  KeySet last;
  uint32_t since = 0;
  bool fired = false;
};

struct KbWatchStep {
  KbWatch state;
  bool reinit = false;
};

KbWatchStep kb_watch_step(const KbWatch& w, KeySet keys, uint32_t now);

}  // namespace cpa
