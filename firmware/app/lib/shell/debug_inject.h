// debug.key injection (test firmware only, protocol README "contract additions" 9):
// each request presses its keys for hold_ms (default 50), releases them, and leaves a
// 30 ms gap before the next one so that repeated keys are seen as separate presses.
#pragma once

#if CPA_TEST_HOOKS

#include <cstdint>

#include "key_set.h"
#include "messages.h"

namespace cpa {

constexpr uint8_t kInjectMax = 16;
constexpr uint16_t kInjectHoldDefaultMs = 50;
constexpr uint32_t kInjectGapMs = 30;

struct Injection {
  KeySet keys;
  uint16_t hold_ms = kInjectHoldDefaultMs;
};

struct InjectQueue {
  uint8_t n = 0;
  Injection items[kInjectMax];
  bool active = false;  // items[0] started
  uint32_t started_ms = 0;
  uint32_t idle_from_ms = 0;  // end of the last release
  uint16_t dropped = 0;
};

InjectQueue inject_push(const InjectQueue& q, const proto::DebugKey& k);

struct InjectStep {
  InjectQueue queue;
  KeySet keys;  // keys held by injection right now
};

InjectStep inject_tick(const InjectQueue& q, uint32_t now);

}  // namespace cpa

#endif  // CPA_TEST_HOOKS
