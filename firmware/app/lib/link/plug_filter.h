// PlugFilter (05 §5.8): pure debounce of Transport::plugged() before LinkSession sees
// it. On Arduino core 2.0.17, HWCDC::isPlugged() can read false for about one FreeRTOS
// tick with the cable in: its SOF tick hook allows 5 missed SOFs but refills that
// allowance only on a disconnected -> connected change, so isolated misses (tick vs
// 1 kHz SOF phase slip, ticks held back by flash writes) add up and the 6th one reports
// an unplug (HWCDC.cpp lines 47-68). Taken raw, one such sample dropped Ready ->
// Detached -> Attached and the next host message got E_NOT_READY (HIL 2026-10-19).
// A plug counts at once; an unplug only once plugged() has read false on every sample
// for kUnplugConfirmMs. A real unplug stops SOF for good, so it still gets through.
#pragma once

#include <cstdint>

namespace cpa {

constexpr uint32_t kUnplugConfirmMs = 500;

struct PlugFilter {
  bool plugged = false;   // the debounced value
  bool falling = false;   // plugged, but the samples since since_ms all read false
  uint32_t since_ms = 0;  // first false sample of the current dropout
};

PlugFilter plug_filter(const PlugFilter& f, bool raw, uint32_t now);

}  // namespace cpa
