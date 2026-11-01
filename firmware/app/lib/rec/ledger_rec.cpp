#include "ledger_rec.h"

#include <cstring>

namespace cpa {
namespace rec {
namespace {

constexpr size_t kCrcAt = kLedgerRecBytes - 4;

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

void ledger_encode(const LedgerRec& r, uint8_t out[kLedgerRecBytes], Crc32Fn crc) {
  out[0] = kLedgerMagic;
  out[1] = static_cast<uint8_t>(r.op);
  out[2] = r.prof;
  out[3] = r.flags;
  put32(out + 4, r.boot);
  put32(out + 8, r.seq);
  put16(out + 12, r.seg);
  put16(out + 14, r.back);
  put32(out + 16, r.wall_s);
  put32(out + 20, r.host);
  put32(out + kCrcAt, crc(out, kCrcAt));
}

Opt<LedgerRec> ledger_decode(const uint8_t* p, Crc32Fn crc) {
  if (p[0] != kLedgerMagic || p[1] > static_cast<uint8_t>(LedgerOp::Gone)) return {};
  if (get32(p + kCrcAt) != crc(p, kCrcAt)) return {};
  LedgerRec r;
  r.op = static_cast<LedgerOp>(p[1]);
  r.prof = p[2];
  r.flags = p[3];
  r.boot = get32(p + 4);
  r.seq = get32(p + 8);
  r.seg = get16(p + 12);
  r.back = get16(p + 14);
  r.wall_s = get32(p + 16);
  r.host = get32(p + 20);
  return some(r);
}

LedgerParse ledger_parse(const uint8_t* p, size_t n, bool end, Crc32Fn crc, LedgerVisitor& v, size_t* consumed) {
  LedgerParse out;
  size_t i = 0;
  while (n - i >= kLedgerRecBytes) {
    const Opt<LedgerRec> r = ledger_decode(p + i, crc);
    if (r.has) {
      v.record(r.value);
      ++out.good;
      i += kLedgerRecBytes;
      continue;
    }
    out.dirty = true;  // resync: the next magic byte may start a good record
    ++i;
    while (i < n && p[i] != kLedgerMagic) ++i;
  }
  if (end && i < n) out.dirty = true;  // a torn tail
  if (consumed) *consumed = end ? n : i;
  return out;
}

void LedgerStream::feed(const uint8_t* p, size_t n, Crc32Fn crc, LedgerVisitor& v) {
  while (n > 0) {
    const size_t take = sizeof carry_ - n_ < n ? sizeof carry_ - n_ : n;
    std::memcpy(carry_ + n_, p, take);
    n_ += take;
    p += take;
    n -= take;
    size_t used = 0;
    const LedgerParse r = ledger_parse(carry_, n_, false, crc, v, &used);
    total_.good += r.good;
    total_.dirty = total_.dirty || r.dirty;
    std::memmove(carry_, carry_ + used, n_ - used);
    n_ -= used;
  }
}

LedgerParse LedgerStream::finish(Crc32Fn crc, LedgerVisitor& v) {
  const LedgerParse r = ledger_parse(carry_, n_, true, crc, v);
  n_ = 0;
  LedgerParse out = total_;
  out.good += r.good;
  out.dirty = out.dirty || r.dirty;
  total_ = LedgerParse{};
  return out;
}

const char* ledger_name(uint8_t which) { return which == 0 ? "SENT.L" : "SENT.K"; }

}  // namespace rec
}  // namespace cpa
