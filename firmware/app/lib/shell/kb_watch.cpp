#include "kb_watch.h"

namespace cpa {

KbWatchStep kb_watch_step(const KbWatch& w, KeySet keys, uint32_t now) {
  const KeySet matrix = keys.without(key::kG0);  // G0 is a GPIO, not the key matrix
  KbWatchStep out{w, false};
  if (matrix != w.last) {
    out.state.last = matrix;
    out.state.since = now;
    out.state.fired = false;
    return out;
  }
  if (matrix.empty() || w.fired || now - w.since < kKbStuckMs) return out;
  out.state.fired = true;
  out.reinit = true;
  return out;
}

}  // namespace cpa
