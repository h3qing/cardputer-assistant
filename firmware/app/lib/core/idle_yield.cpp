#include "idle_yield.h"

namespace cpa {

namespace {

bool quiet(const IdleFacts& f) {
  return f.dark && f.rec_idle && !f.host && !f.audio_busy && !f.writes_due;
}

bool keys_rest(const IdleFacts& f, uint32_t now) {
  return !f.keys_down && (!f.key_seen || now - f.key_ms >= kYieldGraceMs);
}

}  // namespace

uint32_t idle_yield_ms(const IdleFacts& f, uint32_t now) {
  return quiet(f) && keys_rest(f, now) ? kIdleYieldMs : 0;
}

}  // namespace cpa
