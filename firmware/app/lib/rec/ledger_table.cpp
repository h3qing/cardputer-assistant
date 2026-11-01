#include "ledger_table.h"

namespace cpa {
namespace rec {

RecKey rec_of(const LedgerSlot& s) {
  const uint16_t b = s.back();
  RecKey k;
  k.boot = s.boot();
  k.seq = b == kSlotBackUnknown || b > s.seq() ? s.seq() : s.seq() - b;
  return k;
}

LedgerTable::LedgerTable(void* mem, size_t bytes, uint32_t batch_start, uint32_t batch_host)
    : slots_(static_cast<LedgerSlot*>(mem)),
      cap_(static_cast<uint16_t>(bytes / sizeof(LedgerSlot) > 0xFFFF ? 0xFFFF : bytes / sizeof(LedgerSlot))),
      batch_start_(batch_start),
      batch_host_(batch_host) {}

int LedgerTable::find(uint32_t boot, uint32_t seq) const {
  for (uint16_t i = 0; i < n_; ++i) {
    if (slots_[i].boot() == boot && slots_[i].seq() == seq) return i;
  }
  return -1;
}

LedgerSlot* LedgerTable::slot(uint32_t boot, uint32_t seq) {
  const int i = find(boot, seq);
  if (i >= 0) return &slots_[i];
  if (n_ >= cap_) {
    overflow_ = true;
    return nullptr;
  }
  LedgerSlot s;
  s.boot_flags = boot & 0x1FFFFFFu;
  s.seq_back = (seq & 0xFFFFFu) | (static_cast<uint32_t>(kSlotBackUnknown) << 20);
  slots_[n_] = s;
  return &slots_[n_++];
}

void LedgerTable::record(const LedgerRec& r) {
  switch (r.op) {
    case LedgerOp::Deliv: deliv(r); break;
    case LedgerOp::Again: again(r); break;
    case LedgerOp::Gone: gone(r); break;
    default: break;  // GEN, PROF (S2)
  }
  ++seen_;
}

void LedgerTable::deliv(const LedgerRec& r) {
  LedgerSlot* s = slot(r.boot, r.seq);
  if (!s) return;
  s->boot_flags &= 0x1FFFFFFu;
  s->set_back(r.back);
  s->set(kSlotKept);
  if (r.flags & kLfLast) s->set(kSlotLast);
  if (seen_ >= batch_start_ && r.host == batch_host_) s->set(kSlotBatch);
  s->wall_s = r.wall_s;
  s->host = r.host;
}

void LedgerTable::again(const LedgerRec& r) {
  LedgerSlot* s = slot(r.boot, r.seq);
  if (!s || ((r.flags & kLfHold) && s->has(kSlotKept))) return;
  s->clear(kSlotKept | kSlotBatch | kSlotGone | kSlotDone);
  s->set(kSlotExcl);
  if (s->back() == kSlotBackUnknown) s->set_back(r.back);
  s->host = r.host;
}

void LedgerTable::gone(const LedgerRec& r) {
  const RecKey k{r.boot, r.seq};
  for (uint16_t i = 0; i < n_; ++i) {
    LedgerSlot& s = slots_[i];
    if (!s.has(kSlotKept) || !(rec_of(s) == k)) continue;
    s.clear(kSlotKept | kSlotBatch);
    s.set(kSlotGone);
  }
}

namespace {

bool first_of_rec(const LedgerTable& t, uint16_t i, uint8_t flag) {
  const RecKey k = rec_of(t.at(i));
  for (uint16_t j = 0; j < i; ++j) {
    if (t.at(j).has(flag) && rec_of(t.at(j)) == k) return false;
  }
  return true;
}

}  // namespace

KeptInfo LedgerTable::info() const {
  KeptInfo k;
  for (uint16_t i = 0; i < n_; ++i) {
    const LedgerSlot& s = slots_[i];
    if (!s.has(kSlotKept)) continue;
    ++k.segs;
    if (s.wall_s && (k.oldest_s == 0 || s.wall_s < k.oldest_s)) k.oldest_s = s.wall_s;
    if (s.wall_s > k.newest_s) k.newest_s = s.wall_s;
    if (!s.has(kSlotBatch)) continue;
    k.batch_host = s.host;
    ++k.batch_segs;
    if (first_of_rec(*this, i, kSlotBatch)) ++k.batch_recs;
  }
  return k;
}

uint16_t LedgerTable::live_records() const {
  uint16_t n = 0;
  for (uint8_t p = 0; p < kLivePasses; ++p) {
    for (uint16_t i = 0; i < n_; ++i) n = static_cast<uint16_t>(n + (live_in_pass(*this, i, static_cast<LivePass>(p)) ? 1 : 0));
  }
  return n;
}

namespace {

bool undone(const LedgerSlot& s) { return s.has(kSlotGone) && !s.has(kSlotDone); }

bool gone_written(const LedgerTable& t, uint16_t i) {
  const RecKey k = rec_of(t.at(i));
  for (uint16_t j = 0; j < i; ++j) {
    if (undone(t.at(j)) && rec_of(t.at(j)) == k) return true;
  }
  return false;
}

}  // namespace

bool live_in_pass(const LedgerTable& t, uint16_t i, LivePass p) {
  const LedgerSlot& s = t.at(i);
  switch (p) {
    case LivePass::Undone: return undone(s);
    case LivePass::Gone: return undone(s) && !gone_written(t, i);
    case LivePass::Kept: return s.has(kSlotKept) && !s.has(kSlotBatch);
    case LivePass::Excl: return !s.has(kSlotKept) && s.has(kSlotExcl) && s.host != 0;  // host 0: for every Mac
    default: return s.has(kSlotKept) && s.has(kSlotBatch);
  }
}

void BatchFinder::record(const LedgerRec& r) {
  if (r.op == LedgerOp::Gen && (r.flags & kLfBatch)) {
    any_ = r.host != 0;
    host = r.host;
    start = r.wall_s;
    floor_ = r.wall_s;
  }
  if (r.op == LedgerOp::Deliv && seen_ >= floor_ && (!any_ || r.host != host)) {
    any_ = true;
    host = r.host;
    start = seen_;
  }
  ++seen_;
}

RecSummary rec_summary(const LedgerTable& t, const RecKey& rec) {
  RecSummary s;
  s.rec = rec;
  bool seen = false;
  for (uint16_t i = 0; i < t.count(); ++i) {
    const LedgerSlot& x = t.at(i);
    if (!x.has(kSlotKept) || !(rec_of(x) == rec)) continue;
    if (!seen) s.first = i;
    seen = true;
    ++s.segs;
    s.complete = s.complete || x.has(kSlotLast);
    if (x.wall_s > s.newest_s) s.newest_s = x.wall_s;
  }
  return s;
}

}  // namespace rec
}  // namespace cpa
