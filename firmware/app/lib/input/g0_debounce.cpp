#include "g0_debounce.h"

namespace cpa {

G0Debounce g0_debounce(const G0Debounce& s, bool raw, uint32_t now) {
  G0Debounce out = s;
  if (raw || !s.down) {
    out.down = raw;
    out.up_seen = false;
    return out;
  }
  if (!s.up_seen) {
    out.up_seen = true;
    out.up_since = now;
    return out;
  }
  out.down = now - s.up_since < kG0ReleaseMs;
  return out;
}

}  // namespace cpa
