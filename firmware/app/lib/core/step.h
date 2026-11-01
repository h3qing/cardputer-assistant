// Effects / Step: the output of every pure core function (05 §5.2):
//   Step<State, Effect> step(const State&, const Input&)
// Effects are a fixed array (no heap). with() returns a new value; when full, the
// extra effect is dropped and `overflow` is set so callers and tests can see it.
#pragma once

#include <cstdint>

namespace cpa {

template <class E, uint8_t N = 4>
struct Effects {
  static constexpr uint8_t kCapacity = N;
  E items[N] = {};
  uint8_t count = 0;
  bool overflow = false;

  Effects with(const E& e) const {
    Effects out = *this;
    if (count < N) {
      out.items[count] = e;
      out.count = static_cast<uint8_t>(count + 1);
    } else {
      out.overflow = true;
    }
    return out;
  }
};

template <class S, class E, uint8_t N = 4>
struct Step {
  S state;
  Effects<E, N> effects;
};

}  // namespace cpa
