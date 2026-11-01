// TxPipe (gh #32): the device's USB send path, for `cpa-sim serve`. UsbTransport
// (usb_transport/) writes a line only when all of it fits in the 4 KB TX ring and
// otherwise parks it in the same 4-line LineQueue, with its drop rule. Here the ring
// drains through a WriteFn, which takes what the host side can hold right now (a pty
// takes about 1 KB before the host reads). The sizes come from usb_transport.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "line_queue.h"
#include "protocol_limits.h"
#include "transport.h"
#include "usb_transport.h"

namespace cpasim {

constexpr size_t kTxRing = cpa::kUsbTxBuffer;

// Bytes the host side took from data (0 when it takes none right now).
using WriteFn = std::function<size_t(const char* data, size_t n)>;

class TxPipe {
 public:
  bool send(const char* line, size_t n, cpa::TxClass cls, const WriteFn& write);
  void pump(const WriteFn& write);  // ring -> host, then queued lines -> ring
  void reset();                     // a reboot: the ring and the queue are RAM

  size_t room() const { return kTxRing - ring_.size(); }  // availableForWrite()
  size_t buffered() const { return ring_.size(); }
  uint8_t queued() const { return q_.size(); }
  uint32_t dropped_replies() const { return q_.dropped_replies(); }
  uint32_t dropped_resendable() const { return q_.dropped_resendable(); }

 private:
  using Queue = cpa::LineQueue<cpa::kUsbQueueLines, cpa::limits::kLineMax>;

  bool write_now(const char* line, size_t n);
  void flush(const WriteFn& write);

  std::string ring_;
  Queue q_;
};

}  // namespace cpasim
