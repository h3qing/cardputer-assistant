// HWCDC helpers shared by the gate commands (gate_cmds.h): the app's TX kick and re-arm,
// whole-line writes and a runtime TX ring swap.
#include <Arduino.h>

#include "gate_cmds.h"
#include "hal/usb_serial_jtag_ll.h"

namespace gate_io {

namespace {
int s_cap = 0;  // free space of the empty TX ring
}  // namespace

void beginSerial(size_t tx_bytes) {
  Serial.setTxBufferSize(tx_bytes);
  Serial.begin(115200);
  s_cap = Serial.availableForWrite();
}

bool tickUsb(uint32_t now_ms, int empty_free) {
  static uint32_t last = 0;
  if (now_ms - last < 20) return false;
  last = now_ms;
  (void)static_cast<bool>(Serial);
  if (!HWCDC::isPlugged() || Serial.availableForWrite() >= empty_free) return false;
  usb_serial_jtag_ll_txfifo_flush();
  usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
  return true;
}

static bool waitDrained(uint32_t max_ms) {
  const uint32_t t0 = millis();
  while (Serial.availableForWrite() < s_cap) {
    if (millis() - t0 > max_ms) return false;
    tickUsb(millis(), s_cap);
    delay(1);
  }
  return true;
}

int setTxRing(size_t bytes) {
  waitDrained(3000);
  usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
  if (Serial.setTxBufferSize(bytes) != bytes) Serial.setTxBufferSize(4096);
  usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
  s_cap = Serial.availableForWrite();
  return s_cap;
}

bool writeWhole(const char* p, size_t n) {
  if (Serial.availableForWrite() < static_cast<int>(n)) return false;
  return Serial.write(reinterpret_cast<const uint8_t*>(p), n) == n;
}

}  // namespace gate_io
