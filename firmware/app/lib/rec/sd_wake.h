// sd_wake (docs/07 §6.7 #3): a reset that keeps the card powered (esp_restart, a panic,
// the watchdog) can cut the recording's SD traffic in the middle of a transfer. The card
// then stays in it and takes no command: the driver's CMD0 gets no answer, the mount
// reports no card, and that whole boot has no recording. hal calls this once per boot,
// after the init preamble (chip select high, 80 clocks) with chip select low at the
// 400 kHz init clock, right before the driver initialises the card.
//   0. busy waited out, CMD0: a card that answers idle (R1 0x01, then the line high) is
//      done. That is every boot without a cut transfer: one CMD0 more than the driver's.
//   Otherwise the transfer is ended, each step harmless to a card that is not in the
//   state it ends (0xFF and 0xFD are not commands; CMD12 outside a read is refused):
//   1. 520 clocks, busy waited out: a block cut inside takes them as the rest of its
//      data and answers (with a CRC error: CRC checking is on since the first init, so
//      nothing is written); single-block writes and reads end here;
//   2. Stop Tran (0xFD), one byte Nbr, busy waited out: a multi-block write ends here;
//   3. CMD12, its answer and busy waited out: a multi-block read streams until it;
//   4. CMD0 again: true when the card answers idle.
// No command goes to a busy or answering card, except step 0's CMD0 when its first clock
// ends a block cut one byte short (the card ignores it). The busy waits share budget_ms
// (false when it runs out). False: no card, or one this could not bring back; the driver
// then fails as before. An absent card, its data line high, costs about 570 bytes
// (12 ms). A read stream whose data happens to hold 0x01 then 0xFF looks idle at step 0
// and is not helped (reads only run for file.get, never while recording).
#pragma once

#include <cstdint>

namespace cpa {
namespace rec {

// The card's SPI bus: one byte out, the byte the card sent meanwhile.
class SdSpi {
 public:
  virtual ~SdSpi() = default;
  virtual uint8_t xfer(uint8_t out) = 0;
  virtual uint32_t ms() = 0;  // a millisecond clock for the busy waits
};

constexpr uint32_t kWakeBudgetMs = 500;  // the driver's own select wait

bool sd_wake(SdSpi& spi, uint32_t budget_ms = kWakeBudgetMs);

}  // namespace rec
}  // namespace cpa
