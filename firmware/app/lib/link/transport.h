// Transport interface (05 §5.8). Implemented by usb_transport/ (HWCDC, device only);
// tests use fakes. The link core never calls it: the shell moves bytes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cpa {

enum class TxClass : uint8_t {
  Reply,       // replies are never dropped
  Resendable,  // outbox.item: may be dropped when the send queue is full (retried later)
};

class Transport {
 public:
  virtual ~Transport() = default;
  virtual bool plugged() const = 0;
  // Copies up to cap received bytes into buf; returns the count (0 when none).
  virtual size_t read(char* buf, size_t cap) = 0;
  // Queues one encoded line (with '\n'); false when it was dropped.
  virtual bool send(const char* line, size_t n, TxClass cls) = 0;
  virtual void tick(uint32_t now_ms) = 0;  // TX kick every 20 ms
};

}  // namespace cpa
