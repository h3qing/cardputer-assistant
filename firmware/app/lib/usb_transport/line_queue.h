// LineQueue (05 §5.8): the fixed send queue of the USB transport, at most Slots lines.
// Plain C++ with no Arduino headers, so the drop policy is unit-tested on the host
// (test/ui/test_line_queue). Policy when full:
//   - a resendable line (outbox.item) is dropped: the outbox resends it later;
//   - a reply (or a device request) evicts the newest queued resendable line; if every
//     queued line is a reply it is dropped and counted (the credit window, 05 §6.3,
//     keeps this from happening).
// This is a mutable hardware buffer, inside the usb_transport/ exception boundary.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "transport.h"

namespace cpa {

template <uint8_t Slots, size_t Cap>
class LineQueue {
 public:
  struct Line {
    size_t n = 0;
    TxClass cls = TxClass::Reply;
    char data[Cap] = {};
  };

  bool empty() const { return count_ == 0; }
  uint8_t size() const { return count_; }
  uint32_t dropped_resendable() const { return dropped_resendable_; }
  uint32_t dropped_replies() const { return dropped_replies_; }
  const Line* front() const { return count_ ? &lines_[order_[0]] : nullptr; }

  bool push(const char* data, size_t n, TxClass cls) {
    if (n > Cap) return drop(cls);
    if (count_ == Slots && !(cls == TxClass::Reply && evict_resendable())) return drop(cls);
    const uint8_t slot = free_slot();
    lines_[slot].n = n;
    lines_[slot].cls = cls;
    std::memcpy(lines_[slot].data, data, n);
    order_[count_++] = slot;
    return true;
  }

  void pop() {
    if (count_ == 0) return;
    for (uint8_t i = 1; i < count_; ++i) order_[i - 1] = order_[i];
    --count_;
  }

 private:
  bool drop(TxClass cls) {
    if (cls == TxClass::Resendable) {
      ++dropped_resendable_;
    } else {
      ++dropped_replies_;
    }
    return false;
  }

  bool evict_resendable() {
    for (int i = count_ - 1; i >= 0; --i) {
      if (lines_[order_[i]].cls != TxClass::Resendable) continue;
      for (uint8_t k = static_cast<uint8_t>(i + 1); k < count_; ++k) order_[k - 1] = order_[k];
      --count_;
      ++dropped_resendable_;
      return true;
    }
    return false;
  }

  uint8_t free_slot() const {
    for (uint8_t s = 0; s < Slots; ++s) {
      bool used = false;
      for (uint8_t i = 0; i < count_; ++i) used = used || order_[i] == s;
      if (!used) return s;
    }
    return 0;  // unreachable: push() makes room first
  }

  Line lines_[Slots];
  uint8_t order_[Slots] = {};
  uint8_t count_ = 0;
  uint32_t dropped_resendable_ = 0;
  uint32_t dropped_replies_ = 0;
};

}  // namespace cpa
