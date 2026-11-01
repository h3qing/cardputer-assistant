#include "sd_wake.h"

namespace cpa {
namespace rec {
namespace {

constexpr uint8_t kStopTran = 0xFD;
constexpr uint8_t kHigh = 0xFF;
constexpr uint16_t kBlockTail = 520;  // the rest of a block (512 + 2 CRC) and its answer
constexpr uint8_t kR1Bytes = 9;       // R1 comes within 8 bytes (Ncr)
constexpr uint8_t kR1Idle = 0x01;
constexpr uint8_t kCmd12[6] = {0x4C, 0, 0, 0, 0, 0x61};  // STOP_TRANSMISSION, CRC7 included
constexpr uint8_t kCmd0[6] = {0x40, 0, 0, 0, 0, 0x95};   // GO_IDLE_STATE

struct Budget {
  uint32_t start;
  uint32_t ms;
};

// Until the card lets the data line go high (not busy), or the budget is spent.
bool ready(SdSpi& spi, const Budget& b) {
  while (spi.xfer(kHigh) != kHigh) {
    if (spi.ms() - b.start >= b.ms) return false;
  }
  return true;
}

void send(SdSpi& spi, const uint8_t (&cmd)[6]) {
  for (const uint8_t c : cmd) spi.xfer(c);
}

uint8_t r1(SdSpi& spi) {
  uint8_t b = kHigh;
  for (uint8_t i = 0; i < kR1Bytes && (b & 0x80) != 0; ++i) b = spi.xfer(kHigh);
  return b;
}

// CMD0 answered idle, and the line high after it (a read stream keeps sending data).
bool go_idle(SdSpi& spi) {
  send(spi, kCmd0);
  return r1(spi) == kR1Idle && spi.xfer(kHigh) == kHigh;
}

bool end_transfer(SdSpi& spi, const Budget& b) {
  for (uint16_t i = 0; i < kBlockTail; ++i) spi.xfer(kHigh);
  if (!ready(spi, b)) return false;
  spi.xfer(kStopTran);
  spi.xfer(kHigh);  // Nbr: the busy starts one byte after the token (sd-card.md §11)
  if (!ready(spi, b)) return false;
  send(spi, kCmd12);
  r1(spi);  // its value does not matter, only that the answer is over
  return ready(spi, b);
}

}  // namespace

bool sd_wake(SdSpi& spi, uint32_t budget_ms) {
  const Budget b{spi.ms(), budget_ms};
  if (ready(spi, b) && go_idle(spi)) return true;
  return end_transfer(spi, b) && go_idle(spi);
}

}  // namespace rec
}  // namespace cpa
