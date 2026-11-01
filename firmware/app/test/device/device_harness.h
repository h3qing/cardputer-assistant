// Shared start-up of the on-device suites (06 WP4). The USB transport is brought up
// exactly as the firmware does it (RX 8192 before begin), then the suite waits for the
// host runner (scripts/device_test_host.py), which opens the port the safe way and
// sends a newline, so no Unity output is written before anyone listens.
#pragma once

#include <Arduino.h>

#include "usb_transport.h"

namespace devtest {

inline void begin(cpa::UsbTransport& usb) {
  usb.begin();
  Serial.setTxTimeoutMs(200);  // Unity writes byte by byte: wait a little for ring space
  const uint32_t t0 = millis();
  while (millis() - t0 < 30000) {
    (void)static_cast<bool>(Serial);  // TX kick (03 §3)
    if (Serial.available() > 0) break;
    delay(10);
  }
  delay(200);
  while (Serial.available() > 0) Serial.read();
}

// After UNITY_END(): keep kicking so the summary is not left parked in the TX ring.
inline void idle() {
  (void)static_cast<bool>(Serial);
  delay(20);
}

inline uint32_t p95(uint32_t* v, size_t n) {  // nearest rank; sorts v
  for (size_t i = 1; i < n; ++i) {
    const uint32_t x = v[i];
    size_t j = i;
    for (; j > 0 && v[j - 1] > x; --j) v[j] = v[j - 1];
    v[j] = x;
  }
  const size_t rank = (n * 95 + 99) / 100;
  return v[rank > 0 ? rank - 1 : 0];
}

}  // namespace devtest
