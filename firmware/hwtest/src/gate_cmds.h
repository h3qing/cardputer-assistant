// M3 device gates (docs/gates/m3-device-gates.md, docs/12 M3-H phase 0). The parsing,
// frame envelope and level maths are pure and host-tested in lib/gate_core.
#pragma once

#include <Arduino.h>

bool gateXferCommand(const String& line);  // !xfer !xfermk !gateclean (G-A1, gate_xfer.cpp)
bool gateCueCommand(const String& line);   // !cue                 (G-A5, gate_cue.cpp)
bool gateMicCommand(const String& line);   // !mic                 (G-A10, gate_mic.cpp)
bool gateWarmCommand(const String& line);  // !warm !cw !cdump !mend (issue #31, gate_warm.cpp)

namespace gate_io {

// setTxBufferSize + Serial.begin in setup(); remembers the empty ring's free space.
void beginSerial(size_t tx_bytes);
// HWCDC TX ring swap at runtime: waits (bounded) for the ring to drain, masks IN_EMPTY so
// the ISR cannot touch the ring while it is replaced, then unmasks. Falls back to 4 KB.
// Returns the TX free space of the new, empty ring (0 on failure).
int setTxRing(size_t bytes);
// The app's UsbTransport::tick (05 §5.8): every 20 ms (bool)Serial, and while bytes wait
// in the ring, flush the FIFO and re-enable IN_EMPTY. Returns true when it re-armed.
bool tickUsb(uint32_t now_ms, int empty_free);
// Writes the whole line or nothing (never a partial line into the ring).
bool writeWhole(const char* p, size_t n);

}  // namespace gate_io
