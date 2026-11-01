#include "plug_filter.h"

namespace cpa {

PlugFilter plug_filter(const PlugFilter& f, bool raw, uint32_t now) {
  PlugFilter out = f;
  if (raw || !f.plugged) {  // plugged, or already unplugged: no dropout pending
    out.plugged = raw;
    out.falling = false;
    return out;
  }
  if (!f.falling) {
    out.falling = true;
    out.since_ms = now;
    return out;
  }
  if (now - f.since_ms >= kUnplugConfirmMs) {
    out.plugged = false;
    out.falling = false;
  }
  return out;
}

}  // namespace cpa
