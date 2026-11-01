#include "sd_health.h"

#include <atomic>

namespace cpa {
namespace {

std::atomic<bool> s_dead{false};

}  // namespace

bool hal_sd_dead() { return s_dead.load(); }
void hal_sd_mark_dead() { s_dead.store(true); }
void hal_sd_reset_health() { s_dead.store(false); }

}  // namespace cpa
