// RecEngine: what the ledger is for (docs/28 §3.2-§3.5, S1a). The write order of D17:
//   DELIV  append, fsync, then the loop takes the entry out of the index and acks; a failed
//          append answers an error (the Mac retries); a full card deletes as before S1a
//   AGAIN  each segment's .M and .W checked first, then the records, fsync, then Requeued
//   Hold   the records, fsync, then LedgerDone (the loop then hides them from the Mac)
//   GONE   the records, fsync, then the files (.W first); a cut between is finished by the
//          next boot's scan, which deletes what a GONE names
// Every command ends with Kept, what the screen shows.
#include "rec_engine.h"
#include "wav_header.h"

namespace cpa {
namespace rec {
namespace {

RecEvt event(EvtKind kind) {
  RecEvt e;
  e.kind = kind;
  return e;
}

RecEvt done(LedgerAsk ask, LedgerNo no, uint16_t n = 0) {
  RecEvt e = event(EvtKind::LedgerDone);
  e.ask = ask;
  e.no = no;
  e.n = n;
  return e;
}

uint16_t back_of(const RecMeta& m) {
  if (m.rec.epoch != m.id.epoch || m.rec.boot != m.id.boot || m.rec.seq > m.id.seq) return kBackUnknown;
  const uint32_t d = m.id.seq - m.rec.seq;
  return d < kSlotBackUnknown ? static_cast<uint16_t>(d) : kBackUnknown;
}

bool first_marked(const LedgerTable& t, uint16_t i) {
  const RecKey k = rec_of(t.at(i));
  for (uint16_t j = 0; j < i; ++j) {
    if (t.at(j).has(kSlotMark) && rec_of(t.at(j)) == k) return false;
  }
  return true;
}

}  // namespace

// docs/28 §3.3: kept unless the card would not hold it (kept_max) or the recording is too
// long to keep; a full card (no room for 28 B) deletes as before.
bool RecEngine::keep_on_del(const RecMeta& m, const RecCmd& c, RecEvt& e) {
  if (kept_total_ >= kept_max_ || !keep_segment(m.seg, kept_max_)) return false;
  Book* b = book(m.id.epoch);
  if (!b) return false;
  LedgerRec r;
  r.op = LedgerOp::Deliv;
  r.flags = m.last ? kLfLast : 0;
  r.boot = m.id.boot;
  r.seq = m.id.seq;
  r.seg = m.seg;
  r.back = back_of(m);
  r.wall_s = c.ledger.wall_s;
  r.host = c.ledger.host;
  const FsErr err = append(*b, &r, 1);
  if (err == FsErr::Full) return false;
  if (err != FsErr::None) {
    e.code = ErrCode::Storage;  // no ack: the Mac asks again
    return true;
  }
  e.kept = true;
  note_deliv(r);
  return true;
}

void RecEngine::note_deliv(const LedgerRec& r) {
  ++kept_total_;
  const RecKey rec{r.boot, r.back == kBackUnknown ? r.seq : r.seq - r.back};
  if (info_.batch_segs == 0 || info_.batch_host != r.host) {
    info_.batch_host = r.host;
    info_.batch_segs = 0;
    info_.batch_recs = 0;
  }
  if (info_.batch_segs == 0 || !(batch_rec_ == rec)) ++info_.batch_recs;
  ++info_.batch_segs;
  batch_rec_ = rec;
  if (r.wall_s && (info_.oldest_s == 0 || r.wall_s < info_.oldest_s)) info_.oldest_s = r.wall_s;
  if (r.wall_s > info_.newest_s) info_.newest_s = r.wall_s;
}

void RecEngine::emit_kept(EvtSink& out) {
  RecEvt e = event(EvtKind::Kept);
  e.info = info_;
  e.info.segs = kept_total_;
  if (kept_total_ == 0) e.info = KeptInfo{};
  out.emit(e);
}

void RecEngine::ledger(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out) {
  close_transfer();
  if (!scratch || recording()) {
    out.emit(done(c.ledger.ask, LedgerNo::Busy));
    return;
  }
  switch (c.ledger.ask) {
    case LedgerAsk::Again: again(c, scratch, bytes, out); break;
    case LedgerAsk::Hold: hold(c, scratch, bytes, out); break;
    case LedgerAsk::Give: give(c, scratch, bytes, out); break;
    default: drop(c, scratch, bytes, out); break;
  }
  emit_kept(out);
}

// Each segment of the batch must still be whole on the card: its .M read back and its .W
// as long as served (kept in wall_s, the slot's time no longer needed; Mark: segment 0).
LedgerNo RecEngine::check_batch(LedgerTable& t, uint32_t epoch, uint16_t room) {
  uint16_t segs = 0;
  for (uint16_t i = 0; i < t.count(); ++i) segs += t.at(i).has(kSlotBatch) ? 1 : 0;
  if (segs == 0) return LedgerNo::Nothing;
  if (segs + kAgainSpare > room) return LedgerNo::TooMany;  // never the whole list: recording goes on
  for (uint16_t i = 0; i < t.count(); ++i) {
    LedgerSlot& s = t.at(i);
    if (!s.has(kSlotBatch)) continue;
    RecMeta m;
    FsErr err = FsErr::None;
    const MonoId id = slot_id(epoch, s);
    const long w = d_.fs->size(rec_wav_path(d_.root, id).c_str(), err);
    if (w < static_cast<long>(kHeaderBytes) || !read_meta(id, m)) return LedgerNo::Incomplete;
    s.wall_s = static_cast<uint32_t>(w);
    if (m.seg == 0) s.set(kSlotMark);
  }
  return LedgerNo::None;
}

// docs/28 §3.5: "deliver again what went to ·3F2A": one AGAIN a segment, never for that Mac.
void RecEngine::again(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out) {
  const uint32_t epoch = d_.ids->epoch();
  Book* b = book(epoch);
  if (!b) return out.emit(done(LedgerAsk::Again, LedgerNo::Storage));
  LedgerTable t = load(*b, scratch, bytes);
  const LedgerNo no = t.overflow() ? LedgerNo::Storage : check_batch(t, epoch, c.ledger.room);
  if (no != LedgerNo::None) return out.emit(done(LedgerAsk::Again, no));
  if (write_agains(*b, t) != FsErr::None) return out.emit(done(LedgerAsk::Again, LedgerNo::Storage));
  uint16_t recs = 0;
  for (uint16_t i = 0; i < t.count(); ++i) {
    LedgerSlot& s = t.at(i);
    if (!s.has(kSlotBatch)) continue;
    RecEvt e = event(EvtKind::Requeued);
    e.id = slot_id(epoch, s);
    e.size = s.wall_s;
    e.host = s.host;
    e.first = s.has(kSlotMark);
    recs = static_cast<uint16_t>(recs + (e.first ? 1 : 0));
    out.emit(e);
    s.clear(kSlotKept | kSlotBatch | kSlotMark);
    s.set(kSlotExcl);
    if (kept_total_ > 0) --kept_total_;
  }
  info_ = t.info();
  out.emit(done(LedgerAsk::Again, LedgerNo::None, recs));
}

FsErr RecEngine::write_agains(Book& b, const LedgerTable& t) {
  FsErr err = open_book(b);
  if (err != FsErr::None) return err;
  Appender a{*this};
  for (uint16_t i = 0; i < t.count(); ++i) {
    const LedgerSlot& s = t.at(i);
    if (!s.has(kSlotBatch)) continue;
    LedgerRec r;
    r.op = LedgerOp::Again;
    r.boot = s.boot();
    r.seq = s.seq();
    r.back = s.back() == kSlotBackUnknown ? kBackUnknown : s.back();
    r.host = s.host;
    a.add(r);
  }
  return close_book(b, a);
}

// docs/28 §3.2: the plug-in hint's "not to this Mac": every segment of the device's epoch
// from c.ledger.lo on, its .W on the card, never to c.ledger.host (HOLD: a kept one stays).
void RecEngine::hold(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out) {
  class Ids : public RecDirVisitor {
   public:
    Ids(MonoId* out, size_t cap, const MonoId& lo) : out_(out), cap_(cap), lo_(lo) {}
    size_t n = 0;
    void entry(const char* name, bool is_dir) override {
      const Opt<RecName> r = is_dir ? Opt<RecName>{} : rec_parse_name(epoch_.c_str(), name);
      if (r.has && r.value.wav && !(r.value.id < lo_) && n < cap_) out_[n++] = r.value.id;
    }
    FixedStr<7> epoch_;

   private:
    MonoId* out_;
    size_t cap_;
    MonoId lo_;
  } ids(reinterpret_cast<MonoId*>(scratch), bytes / sizeof(MonoId), c.ledger.lo);
  ids.epoch_ = FixedStr<7>::from(mono_format(c.ledger.lo).c_str(), 7);
  d_.fs->list(rec_epoch_dir(d_.root, c.ledger.lo).c_str(), ids);
  Book* b = book(c.ledger.lo.epoch);
  if (!b || ids.n == 0) return out.emit(done(LedgerAsk::Hold, b ? LedgerNo::Nothing : LedgerNo::Storage));
  FsErr err = open_book(*b);
  if (err == FsErr::None) {
    Appender a{*this};
    const MonoId* list = reinterpret_cast<const MonoId*>(scratch);
    for (size_t i = 0; i < ids.n; ++i) {
      LedgerRec r;
      r.op = LedgerOp::Again;
      r.flags = kLfHold;
      r.boot = list[i].boot;
      r.seq = list[i].seq;
      r.host = c.ledger.host;
      a.add(r);
    }
    err = close_book(*b, a);
  }
  out.emit(done(LedgerAsk::Hold, err == FsErr::None ? LedgerNo::None : LedgerNo::Storage, static_cast<uint16_t>(ids.n)));
}

// A segment of `epoch` that waits, never for `host`, its .W still on the card.
bool RecEngine::gives(const LedgerSlot& s, uint32_t epoch, uint32_t host) {
  if (s.has(kSlotKept) || !s.has(kSlotExcl) || s.host != host) return false;
  FsErr err = FsErr::None;
  return d_.fs->size(rec_wav_path(d_.root, slot_id(epoch, s)).c_str(), err) >= 0;
}

// Review 6, "give to this Mac": what deliver again or the hint kept from `host` is for every
// Mac again (one AGAIN HOLD with host 0 a segment, then LedgerDone: the loop then shows them).
void RecEngine::give(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out) {
  const uint32_t epoch = d_.ids->epoch();
  Book* b = book(epoch);
  if (!b || c.ledger.host == 0) return out.emit(done(LedgerAsk::Give, b ? LedgerNo::Nothing : LedgerNo::Storage));
  LedgerTable t = load(*b, scratch, bytes);
  uint16_t n = 0;
  for (uint16_t i = 0; i < t.count(); ++i) {
    if (!gives(t.at(i), epoch, c.ledger.host)) continue;
    t.at(i).set(kSlotMark);
    ++n;
  }
  if (t.overflow() || n == 0) return out.emit(done(LedgerAsk::Give, t.overflow() ? LedgerNo::Storage : LedgerNo::Nothing));
  FsErr err = open_book(*b);
  if (err == FsErr::None) {
    Appender a{*this};
    for (uint16_t i = 0; i < t.count(); ++i) {
      if (!t.at(i).has(kSlotMark)) continue;
      LedgerRec r;
      r.op = LedgerOp::Again;
      r.flags = kLfHold;
      r.boot = t.at(i).boot();
      r.seq = t.at(i).seq();
      r.back = t.at(i).back() == kSlotBackUnknown ? kBackUnknown : t.at(i).back();
      a.add(r);
    }
    err = close_book(*b, a);
  }
  out.emit(done(LedgerAsk::Give, err == FsErr::None ? LedgerNo::None : LedgerNo::Storage, n));
}

// Clear and Tidy: every epoch's ledger, recording by recording.
// At most kDropMax recordings a command (more: ask again), so a Start posted meanwhile
// waits for a few deletes, never for the whole card.
void RecEngine::drop(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out) {
  FixedStr<7> names[kMaxEpochs];
  const uint8_t n = epochs(names);
  Drops d;
  for (uint8_t i = 0; i < n && !d.more; ++i) {
    const Opt<uint64_t> e = crockford_decode(names[i].c_str(), 7);
    Book* b = e.has ? book(static_cast<uint32_t>(e.value)) : nullptr;
    if (b && b->size > 0) drop_epoch(*b, c, scratch, bytes, d);
  }
  RecEvt e = done(c.ledger.ask, d.failed ? LedgerNo::Storage : LedgerNo::None, d.recs);
  e.size = static_cast<uint32_t>(d.freed >> 20);
  e.first = d.more && !d.failed;  // more to do: the loop asks again
  out.emit(e);
}

void RecEngine::drop_epoch(Book& b, const RecCmd& c, uint8_t* scratch, size_t bytes, Drops& d) {
  LedgerTable t = load(b, scratch, bytes);
  d.failed = d.failed || t.overflow();
  if (t.overflow()) return;
  if (b.epoch == d_.ids->epoch()) info_ = t.info();
  const uint16_t recs = pick_drops(t, b.epoch, c, d);
  if (recs > 0 && write_gones(b, t) != FsErr::None) {
    d.failed = true;
    return;
  }
  if (recs > 0) unlink_marked(t, b.epoch);
  d.recs = static_cast<uint16_t>(d.recs + recs);
  if (b.epoch == d_.ids->epoch()) info_ = t.info();
  // Clear also drops the old records (the Macs' hashes with them): the file is its GEN alone
  if (tidy_due(b, t) || (recs > 0 && c.ledger.ask == LedgerAsk::Clear && !d.more)) compact(b, t);
}

// The next recording to delete: the first kept one not marked yet (Clear), or what the
// policy picks (Tidy). false: none.
bool RecEngine::next_drop(const LedgerTable& t, const RecCmd& c, const TidyCtx& ctx, RecKey& k) const {
  if (c.ledger.ask != LedgerAsk::Clear) {
    const TidyPick p = tidy_pick(t, ctx);
    k = p.rec;
    return p.has;
  }
  for (uint16_t i = 0; i < t.count(); ++i) {
    if (!t.at(i).has(kSlotKept) || t.at(i).has(kSlotMark)) continue;
    k = rec_of(t.at(i));
    return true;
  }
  return false;
}

// Marks the recordings to delete and counts what their .W files free.
uint16_t RecEngine::pick_drops(LedgerTable& t, uint32_t epoch, const RecCmd& c, Drops& d) {
  TidyCtx ctx;
  ctx.now_s = c.ledger.wall_s;
  ctx.kept_max = kept_max_;
  uint16_t recs = 0;
  uint32_t kept = kept_total_;
  uint64_t& freed = d.freed;
  RecKey k;
  for (;;) {
    ctx.kept_total = kept;
    ctx.space_low = c.ledger.free_known && c.ledger.free_bytes + freed < kKeptFreeFloor;
    if (!next_drop(t, c, ctx, k)) break;
    if (d.recs + recs >= kDropMax) {
      d.more = true;
      break;
    }
    for (uint16_t i = 0; i < t.count(); ++i) {
      LedgerSlot& s = t.at(i);
      if (!s.has(kSlotKept) || s.has(kSlotMark) || !(rec_of(s) == k)) continue;
      s.set(kSlotMark);
      FsErr err = FsErr::None;
      const long w = d_.fs->size(rec_wav_path(d_.root, slot_id(epoch, s)).c_str(), err);
      freed += w > 0 ? static_cast<uint64_t>(w) : 0;
      kept = kept > 0 ? kept - 1 : 0;
    }
    ++recs;
  }
  return recs;
}

FsErr RecEngine::write_gones(Book& b, LedgerTable& t) {
  const FsErr err = open_book(b);
  if (err != FsErr::None) return err;
  Appender a{*this};
  for (uint16_t i = 0; i < t.count(); ++i) {
    if (!t.at(i).has(kSlotMark) || !first_marked(t, i)) continue;
    const RecKey k = rec_of(t.at(i));
    LedgerRec r;
    r.op = LedgerOp::Gone;
    r.boot = k.boot;
    r.seq = k.seq;
    r.back = 0;
    a.add(r);
  }
  return close_book(b, a);
}

void RecEngine::unlink_marked(LedgerTable& t, uint32_t epoch) {
  for (uint16_t i = 0; i < t.count(); ++i) {
    LedgerSlot& s = t.at(i);
    if (!s.has(kSlotMark)) continue;
    const MonoId id = slot_id(epoch, s);
    const FsErr w = d_.fs->unlink(rec_wav_path(d_.root, id).c_str());
    const bool w_ok = w == FsErr::None || w == FsErr::NotFound;
    const FsErr m = w_ok ? d_.fs->unlink(rec_meta_path(d_.root, id).c_str()) : w;
    s.clear(kSlotKept | kSlotBatch | kSlotMark);
    s.set(kSlotGone);
    if (w_ok && (m == FsErr::None || m == FsErr::NotFound)) s.set(kSlotDone);
    if (kept_total_ > 0) --kept_total_;
  }
}

}  // namespace rec
}  // namespace cpa
