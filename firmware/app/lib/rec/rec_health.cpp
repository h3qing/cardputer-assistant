#include "rec_health.h"

namespace cpa {
namespace rec {
namespace {

template <class T>
T max_of(T a, T b) {
  return a > b ? a : b;
}

template <class T>
T min_of(T a, T b) {
  return a < b ? a : b;
}

}  // namespace

RecHealth health_begin(uint32_t heap_free, uint32_t heap_block) {
  RecHealth h;
  h.measured = true;
  h.heap_free = heap_free;
  h.heap_block = heap_block;
  return h;
}

RecHealth health_block(const RecHealth& h, const BlockHealth& b) {
  RecHealth out = h;
  out.ring_high = max_of<uint16_t>(h.ring_high, b.ring_high);
  out.mic_missing = max_of(h.mic_missing, b.mic_missing);
  out.rot_ms_max = b.rotated ? max_of(h.rot_ms_max, b.write_ms) : h.rot_ms_max;
  out.heap_free = min_of(h.heap_free, b.heap_free);
  out.heap_block = min_of(h.heap_block, b.heap_block);
  return out;
}

bool rearm_missed(bool stopping) { return !stopping; }

}  // namespace rec
}  // namespace cpa
