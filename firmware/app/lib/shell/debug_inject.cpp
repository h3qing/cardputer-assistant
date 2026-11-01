#include "debug_inject.h"

#if CPA_TEST_HOOKS

namespace cpa {
namespace {

InjectQueue pop_front(const InjectQueue& q, uint32_t now) {
  InjectQueue out = q;
  for (uint8_t i = 1; i < q.n; ++i) out.items[i - 1] = q.items[i];
  out.n = static_cast<uint8_t>(q.n - 1);
  out.active = false;
  out.idle_from_ms = now;
  return out;
}

}  // namespace

InjectQueue inject_push(const InjectQueue& q, const proto::DebugKey& k) {
  InjectQueue out = q;
  if (out.n >= kInjectMax) {
    out.dropped++;
    return out;
  }
  Injection inj;
  for (uint8_t i = 0; i < k.n; ++i) inj.keys = inj.keys.with(k.keys[i]);
  inj.hold_ms = k.hold_ms.value_or(kInjectHoldDefaultMs);
  out.items[out.n++] = inj;
  return out;
}

InjectStep inject_tick(const InjectQueue& q, uint32_t now) {
  InjectStep st{q, KeySet{}};
  if (q.n == 0) return st;
  if (!q.active) {
    if (now - q.idle_from_ms < kInjectGapMs) return st;
    st.queue.active = true;
    st.queue.started_ms = now;
    st.keys = q.items[0].keys;
    return st;
  }
  if (now - q.started_ms < q.items[0].hold_ms) {
    st.keys = q.items[0].keys;
    return st;
  }
  st.queue = pop_front(q, now);  // released this pass
  return st;
}

}  // namespace cpa

#endif  // CPA_TEST_HOOKS
