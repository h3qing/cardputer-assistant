#include "ghost_filter.h"

namespace cpa {
namespace {

constexpr uint8_t kRows = 7;
constexpr uint8_t kCols = 8;

// Held matrix keys as one 8-bit column mask per row.
struct Rows {
  uint8_t mask[kRows] = {};
};

Rows rows_of(KeySet s) {
  Rows r;
  for (uint8_t k = 0; k < kMatrixKeys; ++k) {
    if (!s.has(k)) continue;
    const MatrixPos p = matrix_of(k);
    r.mask[p.row] = static_cast<uint8_t>(r.mask[p.row] | (1u << p.col));
  }
  return r;
}

uint8_t popcount8(uint8_t v) {
  uint8_t n = 0;
  for (; v; v &= static_cast<uint8_t>(v - 1)) ++n;
  return n;
}

}  // namespace

bool has_rectangle(KeySet s) {
  const Rows r = rows_of(s);
  for (uint8_t a = 0; a < kRows; ++a) {
    for (uint8_t b = static_cast<uint8_t>(a + 1); b < kRows; ++b) {
      if (popcount8(static_cast<uint8_t>(r.mask[a] & r.mask[b])) >= 2) return true;
    }
  }
  return false;
}

// Three corners: a key whose row holds another key and whose column holds another key.
bool ghost_risk(KeySet s) {
  const Rows r = rows_of(s);
  uint8_t col_count[kCols] = {};
  for (uint8_t row = 0; row < kRows; ++row) {
    for (uint8_t c = 0; c < kCols; ++c) col_count[c] = static_cast<uint8_t>(col_count[c] + ((r.mask[row] >> c) & 1u));
  }
  for (uint8_t row = 0; row < kRows; ++row) {
    if (popcount8(r.mask[row]) < 2) continue;
    for (uint8_t c = 0; c < kCols; ++c) {
      if (((r.mask[row] >> c) & 1u) && col_count[c] >= 2) return true;
    }
  }
  return false;
}

GhostStep ghost_filter(const GhostState& s, KeySet raw) {
  GhostStep out{s, s.last_good};
  out.state.last_raw = raw;
  if (has_rectangle(raw)) {
    if (raw != s.last_raw) out.state.ghost_n = static_cast<uint16_t>(s.ghost_n + 1);
    return out;
  }
  out.state.last_good = raw;
  out.keys = raw;
  return out;
}

}  // namespace cpa
