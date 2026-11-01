// UsbTransport (05 §5.8): the HWCDC (USB-Serial/JTAG) link. Device only.
//   begin(): setRxBufferSize(8192) BEFORE Serial.begin (the core drops bytes silently
//            once its RX queue is full), TX ring 4096, TX timeout 0 (never block);
//   tick():  every 20 ms evaluates (bool)Serial (03 §3: re-arms IN_EMPTY while the
//            core thinks the host is gone) and, while bytes wait in the TX ring, flushes
//            the FIFO and re-arms IN_EMPTY the way HWCDC::write() does. Measured on the
//            device: the ISR sometimes disables IN_EMPTY with a reply still in the ring,
//            and only the device's next write released it (blank lines from the host did
//            not); the plain kick is a no-op once the core counts the host as connected
//            (ADR-0001 §5.2). Then drains the queue;
//   send():  writes only when availableForWrite() >= n, else queues (<= 4 lines).
#pragma once

#include <cstddef>
#include <cstdint>

#include "line_queue.h"
#include "protocol_limits.h"
#include "transport.h"

namespace cpa {

constexpr size_t kUsbRxBuffer = 8192;
constexpr size_t kUsbTxBuffer = 4096;
constexpr uint32_t kUsbKickMs = 20;
constexpr uint8_t kUsbQueueLines = 4;

class UsbTransport : public Transport {
 public:
  void begin();
  bool plugged() const override;
  size_t read(char* buf, size_t cap) override;
  bool send(const char* line, size_t n, TxClass cls) override;
  void tick(uint32_t now_ms) override;
  uint32_t dropped_replies() const { return q_.dropped_replies(); }

 private:
  bool write_now(const char* line, size_t n);
  void pump();

  void rearm_tx();

  LineQueue<kUsbQueueLines, limits::kLineMax> q_;
  uint32_t last_kick_ = 0;
  int empty_free_ = 0;  // availableForWrite() with an empty TX ring
};

}  // namespace cpa
