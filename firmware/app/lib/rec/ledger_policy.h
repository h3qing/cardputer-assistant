// The kept copies' policy (docs/28 §3.3, D4, D15, D16), pure:
//   kept_max_for   the scan table's names less the 256 waiting and 16 spare (a crash's orphan
//                  .W, the segment being written, a half-done delete): 1264 with the 24 KB ring
//   keep_segment   a recording longer than half of that is not kept (deleted when handed over)
//   kept_low       what a tidy goes down to: kept_max less one waiting list (kIndexMax), so a
//                  whole list can be handed over and kept before kept_max (review 5)
//   clock_check    a wall clock more than a day past the last one seen (plus the uptime since),
//                  or more than a day before it, is not used (to age, or to stamp a kept copy)
//                  until another one close to it confirms it
//   tidy_pick      the next recording to delete: one past kKeepS (complete, its newest
//                  segment's time known), else the oldest while over kept_low or short of space
#pragma once

#include <cstdint>

#include "ledger_table.h"

namespace cpa {
namespace rec {

constexpr uint32_t kKeepS = 7u * 86400u;           // D4: 7 days
constexpr uint64_t kKeptFreeFloor = 1ull << 30;   // free under 1 GB: the oldest go
constexpr uint16_t kScanSpare = 16;
constexpr uint32_t kClockSlackS = 86400;           // D16

uint16_t kept_max_for(uint16_t scan_names);
bool keep_segment(uint16_t seg, uint16_t kept_max);
uint16_t kept_low(uint16_t kept_max);

struct ClockRef {
  bool has = false;
  uint32_t wall_s = 0;
  uint32_t up_s = 0;  // the device's uptime when it was seen
};

struct ClockStep {
  ClockRef ref;  // the clock just seen, trusted or not
  bool trusted = false;
};

ClockStep clock_check(const ClockRef& ref, uint32_t wall_s, uint32_t up_s);

enum class TidyWhy : uint8_t { None, Age, Count, Space };

struct TidyCtx {
  uint32_t now_s = 0;       // a trusted wall clock, 0: nothing ages out
  uint32_t kept_total = 0;  // kept segments on the card (every epoch)
  uint16_t kept_max = 0;
  bool space_low = false;   // the free space is known and under kKeptFreeFloor
};

struct TidyPick {
  bool has = false;
  RecKey rec;
  TidyWhy why = TidyWhy::None;
};

// Recordings with a kSlotMark segment were picked already and are skipped.
TidyPick tidy_pick(const LedgerTable& t, const TidyCtx& c);

}  // namespace rec
}  // namespace cpa
