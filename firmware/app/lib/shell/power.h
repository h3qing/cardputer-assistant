// Power policy (05 §5.11), pure. With a USB host (isPlugged) the screen stays on. On
// battery, 60 s without a key turns the backlight off; the next key only wakes the
// screen and is swallowed, except G0 tap, which wakes and opens the quick note.
// v0.3 (docs/11 §4.1-§4.2): G0 held (a voice memo) and its release are not swallowed
// either; while recording, the 60 s dim the backlight instead of turning it off, and
// keys on the dimmed screen are not swallowed.
// docs/20 U24: on battery the screen dims kPreDimMs before it goes dark, so it never goes
// from full to black while being read; keys on the dimmed screen work as usual.
// v0.4.0 (docs/13 §7.1-§7.2): locked, the screen is off even with a USB host, unless the
// lock card is lit; the lock gate (lock.h) runs before power_key, so keys it swallows
// never wake the screen. The ● rule: while recording the backlight is never off (dimmed
// when locked or idle on battery).
#pragma once

#include <cstdint>

#include "chord_detector.h"
#include "platform.h"

namespace cpa {

constexpr uint32_t kBacklightIdleMs = 60000;
constexpr uint32_t kPreDimMs = 10000;  // docs/20 U24: dimmed this long before it goes dark

struct PowerState {
  bool dark = false;
  bool dim = false;        // recording on battery: lit low, keys work
  bool recording = false;  // the recording state machine is not Idle
  uint32_t last_key_ms = 0;
  bool locked = false;  // Opt+L: off unless the card is lit
  bool card = false;    // the lock card is lit
};

struct PowerStep {
  PowerState state;
  bool swallow = false;  // the key must not reach the shell
};

PowerStep power_key(const PowerState& s, const KeyEvent& ev, uint32_t now);
// docs/20 D7: something to read came up by itself (a recording's end): lit as after a key,
// then dimmed and dark as usual.
PowerState power_wake(const PowerState& s, uint32_t now);
PowerState power_tick(const PowerState& s, bool plugged, uint32_t now);
PowerState power_recording(const PowerState& s, bool recording);
// The lock as the shell has it; unlocking lights the screen and starts a fresh 60 s.
PowerState power_lock(const PowerState& s, bool locked, bool card, uint32_t now);
Backlight backlight_of(const PowerState& s);

}  // namespace cpa
