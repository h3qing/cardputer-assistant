// Key presses -> the KeySet of each 10 ms runtime loop (gh #32). Each press is held for
// hold_ms and then released for 30 ms, so repeated keys are separate presses, as with
// debug.key injection (shell/debug_inject.h); keys held down by the script stay down.
#pragma once

#include <cstdint>
#include <vector>

#include "debug_inject.h"
#include "key_set.h"

namespace cpasim {

constexpr uint32_t kTickMs = 10;                // one runtime loop
constexpr uint32_t kGapMs = cpa::kInjectGapMs;  // the release between two presses

std::vector<cpa::KeySet> press_samples(const std::vector<cpa::KeySet>& presses, uint32_t hold_ms, cpa::KeySet held);

// `down` / `up`: held plus every key of ks; held minus them (every key when ks is empty).
cpa::KeySet with_keys(cpa::KeySet held, const std::vector<cpa::KeySet>& ks);
cpa::KeySet without_keys(cpa::KeySet held, const std::vector<cpa::KeySet>& ks);

}  // namespace cpasim
