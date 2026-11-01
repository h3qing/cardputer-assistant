#include "keyboard.h"

#include <M5Cardputer.h>

#include "g0_debounce.h"
#include "i2c_lock.h"

namespace cpa {
namespace {

G0Debounce g_g0;  // gh #51: BtnA's release, debounced (g0_debounce.h)

}  // namespace

KeySet hal_keys() {
  KeySet s;
  for (const auto& p : M5Cardputer.Keyboard.keyList()) {
    if (p.x >= 0 && p.x < kLayoutCols && p.y >= 0 && p.y < kLayoutRows) {
      s = s.with(layout_index(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y)));
    }
  }
  g_g0 = g0_debounce(g_g0, M5Cardputer.BtnA.isPressed(), millis());
  return g_g0.down ? s.with(key::kG0) : s;
}

KeySet hal_update_and_keys() {
  hal_i2c_loop_take();
  M5Cardputer.update();
  const KeySet keys = hal_keys();
  hal_i2c_loop_give();
  return keys;
}

void hal_keyboard_reinit() {
  hal_i2c_loop_take();
  M5Cardputer.Keyboard.begin();
  hal_i2c_loop_give();
}

}  // namespace cpa
