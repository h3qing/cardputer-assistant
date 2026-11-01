// Keyboard (05 §5.7): the held keys as a KeySet (layout index y * 14 + x, G0 = bit 56)
// read from M5Cardputer after M5Cardputer.update(). Device only. G0's release is debounced
// (g0_debounce.h, gh #51).
#pragma once

#include "key_set.h"

namespace cpa {

// v0.3 (docs/11 §4.2): M5Cardputer.update() and the key list under the shared I2C lock;
// the loop calls this once per pass instead of M5Cardputer.update() + hal_keys().
KeySet hal_update_and_keys();
KeySet hal_keys();
void hal_keyboard_reinit();  // watchdog: re-run Keyboard.begin()

}  // namespace cpa
