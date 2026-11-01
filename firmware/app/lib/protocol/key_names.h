// Key names used by debug.key (common.schema.json keyName): the Cardputer 4x14
// layout in row-major order (index = y * 14 + x, as in firmware/hwtest) plus g0 = 56.
#pragma once

#include <cstddef>
#include <cstdint>

#include "opt.h"

namespace cpa {
namespace proto {

constexpr uint8_t kKeyCount = 57;
constexpr uint8_t kKeyG0 = 56;

const char* key_name(uint8_t index);  // "" when out of range
Opt<uint8_t> key_index(const char* s, size_t n);

}  // namespace proto
}  // namespace cpa
