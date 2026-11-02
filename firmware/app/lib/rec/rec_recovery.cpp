#include "rec_recovery.h"

#include "wav_header.h"

namespace cpa {
namespace rec {
namespace {

// Plans are built in place (no RecoveryPlan copies, each carries a RecMeta): the boot scan
// runs on the audio task's 6 KB stack.
void add(RecoveryPlan& p, StepKind kind, uint32_t n = 0) {
  if (p.n < 4) p.steps[p.n++] = RecoveryStep{kind, n};
}

void recovered_meta(const ScanEntry& e, const Opt<RecMeta>& prev, uint32_t data, const FixedStr<31>& fw, RecMeta& m) {
  m.id = e.id;
  m.last = true;
  m.bytes = data;
  m.rcv = true;
  m.fw = fw;
  if (prev.has) {
    m.rec = prev.value.rec;
    m.seg = static_cast<uint16_t>(prev.value.seg + 1);
    m.mode = prev.value.mode;
    m.at = at_after(prev.value);
    m.project = prev.value.project;
    m.slot = prev.value.slot;
    return;
  }
  m.rec = e.id;  // a session of its own: the codec's start transient is at its start
  m.mode = e.hint.value_or(RecMode::Memo);
  m.lead_ms = some(kLeadMs);
  m.consent = m.mode == RecMode::Meeting;  // a meeting only ever starts after the consent Enter
}

}  // namespace

bool needs_recovery(const ScanEntry& e) { return e.has_w && e.w_size >= kHeaderBytes && e.m != MetaState::Ok; }

Opt<proto::RecAt> at_after(const RecMeta& prev) {
  if (!prev.at.has) return {};
  const uint32_t dur = dur_ms_of(prev.bytes);
  proto::RecAt at;
  at.up_ms = prev.at.value.up_ms + dur;
  if (prev.at.value.wall_ms.has) at.wall_ms = some(prev.at.value.wall_ms.value + dur);
  return some(at);
}

RecoveryPlan plan_rec_recovery(const ScanEntry& e, const Opt<RecMeta>& prev, const FixedStr<31>& fw) {
  RecoveryPlan p;  // returned in place (NRVO)
  const bool has_m = e.m != MetaState::Missing;
  if (!e.has_w || e.w_size < kHeaderBytes) {
    if (e.has_w) add(p, StepKind::UnlinkWav);
    if (has_m) add(p, StepKind::UnlinkMeta);
    return p;
  }
  if (e.m == MetaState::Ok) {
    add(p, StepKind::Index, kHeaderBytes + even_data(e.w_size));
    return p;
  }
  const uint32_t data = even_data(e.w_size);
  if (e.m == MetaState::Bad) add(p, StepKind::UnlinkMeta);
  add(p, StepKind::Backfill, data);
  add(p, StepKind::WriteMeta);
  add(p, StepKind::Index, kHeaderBytes + data);
  recovered_meta(e, prev, data, fw, p.meta);
  p.recovered = true;
  return p;
}

}  // namespace rec
}  // namespace cpa
