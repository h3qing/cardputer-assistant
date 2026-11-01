#include "ledger_policy.h"

#include "rec_index.h"

namespace cpa {
namespace rec {

uint16_t kept_max_for(uint16_t scan_names) {
  const uint16_t reserve = kIndexMax + kScanSpare;
  return scan_names > reserve ? static_cast<uint16_t>(scan_names - reserve) : 0;
}

bool keep_segment(uint16_t seg, uint16_t kept_max) { return kept_max > 0 && seg < kept_max / 2; }

uint16_t kept_low(uint16_t kept_max) {
  const uint16_t room = kept_max / 2 < kIndexMax ? static_cast<uint16_t>(kept_max / 2) : kIndexMax;
  return static_cast<uint16_t>(kept_max - room);
}

ClockStep clock_check(const ClockRef& ref, uint32_t wall_s, uint32_t up_s) {
  ClockStep s;
  s.ref.has = true;
  s.ref.wall_s = wall_s;
  s.ref.up_s = up_s;
  if (!ref.has) {
    s.trusted = true;
    return s;
  }
  const uint64_t since = up_s > ref.up_s ? up_s - ref.up_s : 0;
  const uint64_t expect = static_cast<uint64_t>(ref.wall_s) + since;
  s.trusted = wall_s <= expect + kClockSlackS && static_cast<uint64_t>(wall_s) + kClockSlackS >= expect;
  return s;
}

namespace {

bool marked(const LedgerTable& t, const RecKey& k) {
  for (uint16_t i = 0; i < t.count(); ++i) {
    if (t.at(i).has(kSlotMark) && rec_of(t.at(i)) == k) return true;
  }
  return false;
}

bool aged(const RecSummary& s, uint32_t now_s) {
  return now_s != 0 && s.complete && s.newest_s != 0 && now_s > s.newest_s && now_s - s.newest_s > kKeepS;
}

TidyPick pick(const RecKey& k, TidyWhy why) {
  TidyPick p;
  p.has = true;
  p.rec = k;
  p.why = why;
  return p;
}

}  // namespace

TidyPick tidy_pick(const LedgerTable& t, const TidyCtx& c) {
  const bool over = c.kept_total > kept_low(c.kept_max);
  const bool squeeze = over || c.space_low;
  TidyPick oldest;
  for (uint16_t i = 0; i < t.count(); ++i) {
    const LedgerSlot& s = t.at(i);
    if (!s.has(kSlotKept)) continue;
    const RecKey k = rec_of(s);
    const RecSummary sum = rec_summary(t, k);
    if (sum.first != i || marked(t, k)) continue;  // each recording once, at its first slot
    if (aged(sum, c.now_s)) return pick(k, TidyWhy::Age);
    if (squeeze && !oldest.has) oldest = pick(k, over ? TidyWhy::Count : TidyWhy::Space);
  }
  return oldest;
}

}  // namespace rec
}  // namespace cpa
