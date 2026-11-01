// RecEngine::scan: the boot recovery of docs/11 §4.6 over <root>/rec/<E7>/. Names are
// collected first (no file is opened while a directory is listed), then each segment id
// is classified, planned (rec_recovery.h) and repaired one at a time.
// docs/28 S1a: first each epoch's SENT.L is settled (a dirty or long one compacted); after
// the names, the ledger is replayed onto them: a kept name is neither indexed nor has its
// .M read, a name a GONE covers is deleted (.W first), and a waiting one keeps the Mac it
// is never for (the index's excl). More names than the table holds (D15): IndexReady says
// the index is incomplete, and the loop refuses to start a recording.
#include <cstring>

#include "rec_engine.h"
#include "wav_header.h"

namespace cpa {
namespace rec {
namespace {

constexpr uint8_t kHasW = 1;
constexpr uint8_t kHasM = 2;
constexpr uint8_t kKept = 4;  // docs/28: handed over and kept
constexpr uint8_t kGone = 8;  // ... and deleted by a GONE (its files go now)
constexpr uint8_t kFiles = kHasW | kHasM;

struct NameSlot {
  MonoId id;
  uint8_t flags = 0;
  uint8_t excl = 0;  // RecIndex host slot: never to that Mac
  uint16_t back = kBackUnknown;
};

static_assert(sizeof(NameSlot) <= 16, "16 B per name");

class NameVisitor : public RecDirVisitor {
 public:
  NameVisitor(NameSlot* table, uint16_t cap, uint16_t n, const char* epoch)
      : table_(table), cap_(cap), n_(n), epoch_(epoch) {}
  uint16_t count() const { return n_; }
  bool overflow = false;

  void entry(const char* name, bool is_dir) override {
    const Opt<RecName> r = is_dir ? Opt<RecName>{} : rec_parse_name(epoch_, name);
    if (!r.has) return;
    const uint8_t bit = r.value.wav ? kHasW : kHasM;
    for (uint16_t i = 0; i < n_; ++i) {
      if (table_[i].id != r.value.id) continue;
      table_[i].flags = static_cast<uint8_t>(table_[i].flags | bit);
      return;
    }
    if (n_ >= cap_) {  // more names than room (D15): the index is incomplete
      overflow = true;
      return;
    }
    table_[n_] = NameSlot{};
    table_[n_].id = r.value.id;
    table_[n_].flags = bit;
    ++n_;
  }

 private:
  NameSlot* table_;
  uint16_t cap_;
  uint16_t n_;
  const char* epoch_;
};

MonoId rec_id(const NameSlot& s) {
  MonoId r = s.id;
  if (s.back < kSlotBackUnknown && s.back <= s.id.seq) r.seq = s.id.seq - s.back;
  return r;
}

// One epoch's ledger onto the names found (a record about a name not on the card is moot).
class NameApplier : public LedgerVisitor {
 public:
  NameApplier(NameSlot* t, uint16_t n, uint32_t epoch, RecIndex& idx) : t_(t), n_(n), epoch_(epoch), idx_(idx) {}
  void record(const LedgerRec& r) override {
    if (r.op == LedgerOp::Gone) return gone(r);
    NameSlot* s = find(r.boot, r.seq);
    if (!s) return;
    if (r.op == LedgerOp::Deliv) {
      s->flags = static_cast<uint8_t>((s->flags & kFiles) | kKept);
      s->excl = 0;
      s->back = r.back;
    } else if (r.op == LedgerOp::Again && !((r.flags & kLfHold) && (s->flags & kKept))) {
      s->flags = static_cast<uint8_t>(s->flags & kFiles);
      s->excl = slot(r.host);
      if (s->back == kBackUnknown) s->back = r.back;
    }
  }

 private:
  NameSlot* find(uint32_t boot, uint32_t seq) {
    for (uint16_t i = 0; i < n_; ++i) {
      if (t_[i].id.epoch == epoch_ && t_[i].id.boot == boot && t_[i].id.seq == seq) return &t_[i];
    }
    return nullptr;
  }
  // The index's slot for `host`; one taken over from another Mac is for every Mac in the
  // names too (review 7), as RecIndex::host_slot does to its entries.
  uint8_t slot(uint32_t host) {
    const uint8_t k = idx_.slot_for(host);
    const uint32_t was = idx_.host_at(k);
    for (uint16_t i = 0; i < n_ && was != 0 && was != host; ++i) {
      if (t_[i].excl == k) t_[i].excl = 0;
    }
    return idx_.host_slot(host);
  }
  void gone(const LedgerRec& r) {
    for (uint16_t i = 0; i < n_; ++i) {
      const MonoId k = rec_id(t_[i]);
      if (!(t_[i].flags & kKept) || k.epoch != epoch_ || k.boot != r.boot || k.seq != r.seq) continue;
      t_[i].flags = static_cast<uint8_t>((t_[i].flags & kFiles) | kGone);
    }
  }

  NameSlot* t_;
  uint16_t n_;
  uint32_t epoch_;
  RecIndex& idx_;
};

uint32_t epoch_of(const FixedStr<7>& name) {
  const Opt<uint64_t> v = crockford_decode(name.c_str(), 7);
  return v.has ? static_cast<uint32_t>(v.value) : 0;
}

}  // namespace

void RecEngine::scan(uint8_t* scratch, size_t bytes, EvtSink& out) {
  close_transfer();
  FixedStr<7> names[kMaxEpochs];
  const uint8_t ne = epochs(names);
  for (uint8_t i = 0; i < ne; ++i) settle(epoch_of(names[i]), scratch, bytes);
  const uint16_t cap = static_cast<uint16_t>(bytes / sizeof(NameSlot) > 0xFFFF ? 0xFFFF : bytes / sizeof(NameSlot));
  kept_max_ = kept_max_for(cap);
  bool overflow = false;
  uint16_t n = 0;
  for (uint8_t i = 0; i < ne; ++i) {
    const RecPath dir = rec_dir(d_.root).append("/").append(names[i].c_str());
    NameVisitor v(reinterpret_cast<NameSlot*>(scratch), cap, n, names[i].c_str());
    d_.fs->list(dir.c_str(), v);
    n = v.count();
    overflow = overflow || v.overflow;
  }
  for (uint8_t i = 0; i < ne; ++i) {
    Book* b = book(epoch_of(names[i]));
    NameApplier apply(reinterpret_cast<NameSlot*>(scratch), n, epoch_of(names[i]), *d_.index);
    if (b) read_ledger(*b, apply);
  }
  uint32_t recoveries = 0;
  scan_classify(scratch, n, recoveries);
  d_.index->set_ready(recoveries);
  RecEvt e;
  e.kind = EvtKind::IndexReady;
  e.n = d_.index->count();
  e.rev = d_.index->rev();
  e.kept = overflow;  // incomplete
  out.emit(e);
  emit_kept(out);
}

// Kept: counted, its .M never read. Gone: its files go (a cut delete finished). Else as ever.
void RecEngine::scan_classify(const uint8_t* raw, uint16_t n, uint32_t& recoveries) {
  const NameSlot* table = reinterpret_cast<const NameSlot*>(raw);
  kept_total_ = 0;
  for (uint16_t i = 0; i < n; ++i) {
    const NameSlot& s = table[i];
    if (s.flags & kKept) {
      kept_total_ = static_cast<uint16_t>(kept_total_ + ((s.flags & kHasW) ? 1 : 0));
      continue;
    }
    if (s.flags & kGone) {
      if (s.flags & kHasW) d_.fs->unlink(rec_wav_path(d_.root, s.id).c_str());
      if (s.flags & kHasM) d_.fs->unlink(rec_meta_path(d_.root, s.id).c_str());
      continue;
    }
    scan_one(raw, n, i, recoveries);
  }
}

void RecEngine::scan_one(const uint8_t* raw, uint16_t n, uint16_t i, uint32_t& recoveries) {
  const NameSlot& slot = reinterpret_cast<const NameSlot*>(raw)[i];
  ScanEntry e = scan_entry(slot.id, slot.flags & kHasW, slot.flags & kHasM);
  e.excl_host = d_.index->host_at(slot.excl);
  const Opt<RecMeta> prev = needs_recovery(e) ? predecessor(e.id, raw, n) : Opt<RecMeta>{};
  const RecoveryPlan plan = plan_rec_recovery(e, prev, d_.fw);
  if (apply(e, plan) && plan.recovered) ++recoveries;
}

ScanEntry RecEngine::scan_entry(const MonoId& id, bool has_w, bool has_m) {
  ScanEntry e;
  e.id = id;
  FsErr err = FsErr::None;
  const long size = has_w ? d_.fs->size(rec_wav_path(d_.root, id).c_str(), err) : -1;
  e.has_w = size >= 0;
  e.w_size = size >= 0 ? static_cast<uint32_t>(size) : 0;
  if (has_m) e.m = read_meta(id, e.meta) ? MetaState::Ok : MetaState::Bad;
  if (!needs_recovery(e)) return e;
  uint8_t head[kHeaderBytes] = {};
  if (d_.fs->open_read(rec_wav_path(d_.root, id).c_str()) != FsErr::None) return e;
  const long got = d_.fs->read_at(0, head, sizeof head, err);
  d_.fs->close();
  const Opt<WavInfo> w = got == static_cast<long>(kHeaderBytes) ? wav_parse(head, sizeof head) : Opt<WavInfo>{};
  if (w.has) e.hint = w.value.hint;
  return e;
}

// The committed segment whose .M names `id` as its next one, if any (kept and deleted names
// are never read: docs/28 S1a).
Opt<RecMeta> RecEngine::predecessor(const MonoId& id, const uint8_t* raw, uint16_t n) {
  const NameSlot* table = reinterpret_cast<const NameSlot*>(raw);
  RecMeta m;
  for (uint16_t j = 0; j < n; ++j) {
    if (!(table[j].flags & kHasM) || (table[j].flags & (kKept | kGone)) || table[j].id == id) continue;
    if (!read_meta(table[j].id, m)) continue;
    if (m.next.has && m.next.value == id) return some(m);
  }
  return {};
}

bool RecEngine::backfill(const MonoId& id, uint32_t data_bytes) {
  RecFilePort& fs = *d_.fs;
  if (fs.open_rw(rec_wav_path(d_.root, id).c_str()) != FsErr::None) return false;
  const WavHeader h = wav_header(data_bytes);
  FsErr err = fs.pwrite_at0(h.b, sizeof h.b);
  if (err == FsErr::None) err = fs.fsync();
  const FsErr closed = fs.close();
  return err == FsErr::None && closed == FsErr::None;
}

bool RecEngine::apply(const ScanEntry& e, const RecoveryPlan& p) {
  RecFilePort& fs = *d_.fs;
  const bool first = (e.m == MetaState::Ok ? e.meta.seg : p.meta.seg) == 0;
  for (uint8_t i = 0; i < p.n; ++i) {
    const RecoveryStep& s = p.steps[i];
    FsErr err = FsErr::None;
    switch (s.kind) {
      case StepKind::UnlinkMeta: err = fs.unlink(rec_meta_path(d_.root, e.id).c_str()); break;
      case StepKind::UnlinkWav: err = fs.unlink(rec_wav_path(d_.root, e.id).c_str()); break;
      case StepKind::Backfill: err = backfill(e.id, s.n) ? FsErr::None : FsErr::Disk; break;
      case StepKind::WriteMeta: err = writer_.commit_meta(p.meta); break;
      case StepKind::Index: d_.index->insert(e.id, s.n, e.excl_host, first); break;
    }
    if (err != FsErr::None && err != FsErr::NotFound) return false;
  }
  return true;
}

}  // namespace rec
}  // namespace cpa
