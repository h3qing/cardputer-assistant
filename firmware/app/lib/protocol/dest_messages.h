// v0.5 capture destinations (protocol/README item 24, docs/14 §7.2). The codec knows
// dests.snapshot so the contract vectors round-trip; the device answers it E_UNKNOWN_TYPE
// until F5 (docs/25 §4.6), and cpad sends it only to firmware whose revs carry dests.
#pragma once

#include <cstdint>

#include "fixed_str.h"
#include "opt.h"

namespace cpa {
namespace proto {

constexpr uint8_t kDestRowsMax = 12;  // dests.snapshot maxItems

struct DestRow {  // optional fields unset: not sent
  FixedStr<20> id;     // built-in kind, or <kind>.<name> with a name of at most 12 (docs/25 §2.2)
  uint8_t slot = 0;    // 1..12, unique
  FixedStr<24> label;  // 1..24 B, no controls, '"' or '\'
  FixedStr<7> kind;
  Opt<FixedStr<1>> key;  // a lower-case letter
  Opt<FixedStr<8>> fl;   // l live, c leaves the Mac, p needs a project, k confirm on the device; unset = "lck"
};

struct DestsSnapshot {  // h2d request
  uint32_t rev = 0;  // CRC-32 of the rows' canonical JSON
  uint8_t n = 0;
  DestRow rows[kDestRowsMax];
};

}  // namespace proto
}  // namespace cpa
