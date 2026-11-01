// KeySet (05 §5.7): the set of held keys as a value. Bit i is the key at layout
// index i = y * 14 + x of the Cardputer 4x14 layout (the order of firmware/hwtest and
// of debug.key names); bit 56 is G0. Matrix coordinates follow hwtest / the TCA8418
// remap: row = x / 2, col = y + 4 * (x % 2), a 7 x 8 matrix.
#pragma once

#include <cstdint>

namespace cpa {

constexpr uint8_t kLayoutCols = 14;
constexpr uint8_t kLayoutRows = 4;
constexpr uint8_t kMatrixKeys = 56;

namespace key {
constexpr uint8_t kGrave = 0;
constexpr uint8_t kBksp = 13;
constexpr uint8_t kTab = 14;
constexpr uint8_t kFn = 28;
constexpr uint8_t kShift = 29;
constexpr uint8_t kEnter = 41;
constexpr uint8_t kCtrl = 42;
constexpr uint8_t kOpt = 43;
constexpr uint8_t kAlt = 44;
constexpr uint8_t kSpace = 55;
constexpr uint8_t kG0 = 56;
}  // namespace key

struct MatrixPos {
  uint8_t row = 0;
  uint8_t col = 0;
};

constexpr uint8_t layout_index(uint8_t x, uint8_t y) { return static_cast<uint8_t>(y * kLayoutCols + x); }

constexpr MatrixPos matrix_of(uint8_t index) {
  return MatrixPos{static_cast<uint8_t>((index % kLayoutCols) / 2),
                   static_cast<uint8_t>(index / kLayoutCols + 4 * ((index % kLayoutCols) % 2))};
}

constexpr uint8_t layout_of(MatrixPos p) {
  return layout_index(static_cast<uint8_t>(p.row * 2 + (p.col > 3 ? 1 : 0)), static_cast<uint8_t>(p.col % 4));
}

class KeySet {
 public:
  constexpr KeySet() = default;
  constexpr KeySet with(uint8_t k) const { return KeySet(bits_ | mask_of(k)); }
  constexpr KeySet without(uint8_t k) const { return KeySet(bits_ & ~mask_of(k)); }
  constexpr KeySet minus(KeySet o) const { return KeySet(bits_ & ~o.bits_); }
  constexpr bool has(uint8_t k) const { return (bits_ & mask_of(k)) != 0; }
  constexpr bool empty() const { return bits_ == 0; }
  constexpr uint64_t raw() const { return bits_; }
  uint8_t count() const {
    uint8_t n = 0;
    for (uint64_t b = bits_; b; b &= b - 1) ++n;
    return n;
  }
  constexpr bool operator==(KeySet o) const { return bits_ == o.bits_; }
  constexpr bool operator!=(KeySet o) const { return bits_ != o.bits_; }

 private:
  constexpr explicit KeySet(uint64_t b) : bits_(b) {}
  static constexpr uint64_t mask_of(uint8_t k) { return k <= key::kG0 ? (uint64_t{1} << k) : 0; }
  uint64_t bits_ = 0;
};

constexpr bool is_modifier(uint8_t k) {
  return k == key::kFn || k == key::kShift || k == key::kCtrl || k == key::kOpt || k == key::kAlt;
}

}  // namespace cpa
