// G0's release debounce (gh #51), pure; the device's hal_keys() runs it on BtnA. M5Unified's
// Button_Class debounces 10 ms, but only while update() comes at most 10 ms apart
// (Button_Class.inl: a longer gap turns it off), and a loop pass with a full redraw is
// longer: one open-contact sample of a held side button then read as a release, which
// ended the memo. A press counts at once; a release only when G0 read up on two samples
// or more, kG0ReleaseMs apart at least.
#pragma once

#include <cstdint>

namespace cpa {

constexpr uint32_t kG0ReleaseMs = 30;

struct G0Debounce {
  bool down = false;     // what the firmware sees
  bool up_seen = false;  // down, and the last raw sample read up ...
  uint32_t up_since = 0;  // ... since then
};

G0Debounce g0_debounce(const G0Debounce& s, bool raw, uint32_t now);

}  // namespace cpa
