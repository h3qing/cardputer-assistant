#include "presses.h"

namespace cpasim {
namespace {

cpa::KeySet merged(cpa::KeySet a, cpa::KeySet b) {
  for (uint8_t k = 0; k <= cpa::key::kG0; ++k) a = b.has(k) ? a.with(k) : a;
  return a;
}

uint32_t ticks(uint32_t ms) { return ms == 0 ? 1 : (ms + kTickMs - 1) / kTickMs; }

}  // namespace

std::vector<cpa::KeySet> press_samples(const std::vector<cpa::KeySet>& presses, uint32_t hold_ms, cpa::KeySet held) {
  std::vector<cpa::KeySet> out;
  for (const cpa::KeySet& p : presses) {
    out.insert(out.end(), ticks(hold_ms), merged(held, p));
    out.insert(out.end(), ticks(kGapMs), held);
  }
  return out;
}

cpa::KeySet with_keys(cpa::KeySet held, const std::vector<cpa::KeySet>& ks) {
  for (const cpa::KeySet& k : ks) held = merged(held, k);
  return held;
}

cpa::KeySet without_keys(cpa::KeySet held, const std::vector<cpa::KeySet>& ks) {
  if (ks.empty()) return cpa::KeySet{};
  for (const cpa::KeySet& k : ks) held = held.minus(k);
  return held;
}

}  // namespace cpasim
