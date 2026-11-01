#include "usb_transport.h"

#include <Arduino.h>

#include "hal/usb_serial_jtag_ll.h"

namespace cpa {

void UsbTransport::begin() {
  Serial.setRxBufferSize(kUsbRxBuffer);  // must precede begin()
  Serial.setTxBufferSize(kUsbTxBuffer);
  Serial.setTxTimeoutMs(0);
  Serial.begin();
  empty_free_ = Serial.availableForWrite();
}

// Bytes are waiting in the TX ring: push the FIFO and re-enable the IN_EMPTY interrupt
// so the ISR moves them out (what HWCDC::write() does after queueing data).
void UsbTransport::rearm_tx() {
  if (Serial.availableForWrite() >= empty_free_) return;
  usb_serial_jtag_ll_txfifo_flush();
  usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
}

bool UsbTransport::plugged() const { return HWCDC::isPlugged(); }

size_t UsbTransport::read(char* buf, size_t cap) {
  const int avail = Serial.available();
  if (avail <= 0) return 0;
  const size_t want = static_cast<size_t>(avail) < cap ? static_cast<size_t>(avail) : cap;
  return Serial.read(reinterpret_cast<uint8_t*>(buf), want);
}

bool UsbTransport::write_now(const char* line, size_t n) {
  if (Serial.availableForWrite() < static_cast<int>(n)) return false;
  return Serial.write(reinterpret_cast<const uint8_t*>(line), n) == n;
}

void UsbTransport::pump() {
  while (const auto* line = q_.front()) {
    if (!write_now(line->data, line->n)) return;
    q_.pop();
  }
}

bool UsbTransport::send(const char* line, size_t n, TxClass cls) {
  pump();
  if (q_.empty() && write_now(line, n)) return true;
  return q_.push(line, n, cls);
}

void UsbTransport::tick(uint32_t now_ms) {
  if (now_ms - last_kick_ >= kUsbKickMs) {
    last_kick_ = now_ms;
    (void)static_cast<bool>(Serial);  // TX kick (03 §3)
    if (HWCDC::isPlugged()) rearm_tx();
  }
  pump();
}

}  // namespace cpa
