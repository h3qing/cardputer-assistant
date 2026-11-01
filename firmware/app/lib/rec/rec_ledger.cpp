// RecEngine: the SENT.L files (docs/28 §3.4, S1a). One book per epoch directory: which of
// SENT.L / SENT.K is live, its generation and size. Rules:
//   - appends: open for appending, write, fsync, close; nothing is ever written in place;
//   - a dirty file (a record that fails, a torn tail) or one much longer than what it says
//     is compacted: the live records go into the other name behind a GEN of generation + 1,
//     fsync, close, and only then is the old one unlinked;
//   - both names at a probe: that compaction was cut before its unlink, so the older one
//     (lower generation; a file without a good GEN is the original only as SENT.L) is
//     complete and the newer one is dropped.
#include "rec_engine.h"

namespace cpa {
namespace rec {
namespace {

constexpr uint32_t kCompactMin = 16 * 1024;   // docs/28 §3.4: past 16 KB
constexpr uint32_t kCompactSlack = 4 * 1024;  // ... or this much past what is live

}  // namespace

RecEngine::Book* RecEngine::book(uint32_t epoch) {
  Book* free_slot = nullptr;
  for (Book& b : books_) {
    if (b.used && b.epoch == epoch) return &b;
    if (!b.used && !free_slot) free_slot = &b;
  }
  if (!free_slot) return nullptr;
  *free_slot = Book{};
  free_slot->used = true;
  free_slot->epoch = epoch;
  probe(*free_slot);
  return free_slot;
}

// What a file's first record says: a GEN (a compaction's), another good record (the
// original SENT.L), or nothing good (empty, or a compaction cut in its first write).
RecEngine::Head RecEngine::head_of(const RecPath& path) {
  Head h;
  uint8_t head[kLedgerRecBytes] = {};
  if (d_.fs->open_read(path.c_str()) != FsErr::None) return h;
  FsErr err = FsErr::None;
  const long got = d_.fs->read_at(0, head, sizeof head, err);
  d_.fs->close();
  const Opt<LedgerRec> r = got == static_cast<long>(sizeof head) ? ledger_decode(head, d_.crc) : Opt<LedgerRec>{};
  if (!r.has) return h;
  h.kind = r.value.op == LedgerOp::Gen ? HeadKind::Gen : HeadKind::Original;
  h.gen = h.kind == HeadKind::Gen ? r.value.seq : 0;
  return h;
}

namespace {

// Both names are there: which one is older, and so complete (0: SENT.L).
uint8_t older_of(const RecEngine::Head& l, const RecEngine::Head& k) {
  using K = RecEngine::HeadKind;
  if (l.kind == K::Broken || k.kind == K::Broken) return l.kind == K::Broken && k.kind != K::Broken ? 1 : 0;
  if (l.kind == K::Gen && k.kind == K::Gen) return l.gen <= k.gen ? 0 : 1;
  return l.kind == K::Original ? 0 : (k.kind == K::Original ? 1 : 0);
}

}  // namespace

void RecEngine::probe(Book& b) {
  FsErr err = FsErr::None;
  long size[2];
  Head head[2];
  for (uint8_t w = 0; w < 2; ++w) {
    const RecPath p = rec_ledger_path(d_.root, b.epoch, w);
    size[w] = d_.fs->size(p.c_str(), err);
    if (size[w] > 0) head[w] = head_of(p);
  }
  b.known = true;
  b.active = size[0] < 0 && size[1] >= 0 ? 1 : 0;
  if (size[0] >= 0 && size[1] >= 0) {
    b.active = older_of(head[0], head[1]);
    d_.fs->unlink(rec_ledger_path(d_.root, b.epoch, static_cast<uint8_t>(1 - b.active)).c_str());
    b.dirty = true;
  }
  b.gen = head[b.active].gen;
  b.size = size[b.active] > 0 ? static_cast<uint32_t>(size[b.active]) : 0;
}

LedgerParse RecEngine::read_ledger(const Book& b, LedgerVisitor& v) {
  LedgerStream st;
  if (b.size == 0 || d_.fs->open_read(rec_ledger_path(d_.root, b.epoch, b.active).c_str()) != FsErr::None) {
    return st.finish(d_.crc, v);
  }
  uint32_t off = 0;
  for (;;) {
    FsErr err = FsErr::None;
    const long got = d_.fs->read_at(off, lbuf_, sizeof lbuf_, err);
    if (got <= 0) break;
    st.feed(lbuf_, static_cast<size_t>(got), d_.crc, v);
    off += static_cast<uint32_t>(got);
  }
  d_.fs->close();
  return st.finish(d_.crc, v);
}

// The two passes: where the last batch starts, then the table.
LedgerTable RecEngine::load(Book& b, uint8_t* scratch, size_t bytes) {
  BatchFinder f;
  read_ledger(b, f);
  LedgerTable t(scratch, bytes, f.start, f.host);
  if (read_ledger(b, t).dirty) b.dirty = true;
  return t;
}

FsErr RecEngine::open_book(const Book& b) {
  FsErr err = d_.fs->open_append(rec_ledger_path(d_.root, b.epoch, b.active).c_str());
  if (err != FsErr::NotFound) return err;
  d_.fs->mkdir(rec_dir(d_.root).c_str());  // a new epoch directory
  MonoId id;
  id.epoch = b.epoch;
  d_.fs->mkdir(rec_epoch_dir(d_.root, id).c_str());
  return d_.fs->open_append(rec_ledger_path(d_.root, b.epoch, b.active).c_str());
}

FsErr RecEngine::close_book(Book& b, Appender& a) {
  const FsErr err = a.finish();
  b.size += a.bytes;
  if (err != FsErr::None) b.dirty = true;  // part of it may be on the card: compact later
  return err;
}

FsErr RecEngine::append(Book& b, const LedgerRec* recs, size_t n) {
  const FsErr err = open_book(b);
  if (err != FsErr::None) return err;
  Appender a{*this};
  for (size_t i = 0; i < n; ++i) a.add(recs[i]);
  return close_book(b, a);
}

namespace {

LedgerRec slot_record(const LedgerSlot& s, LivePass p) {
  LedgerRec r;
  r.boot = s.boot();
  if (p == LivePass::Gone) {  // the recording's
    r.op = LedgerOp::Gone;
    r.seq = rec_of(s).seq;
    r.back = 0;
    return r;
  }
  r.seq = s.seq();
  r.back = s.back() == kSlotBackUnknown ? kBackUnknown : s.back();
  r.host = s.host;
  if (p == LivePass::Excl) {
    r.op = LedgerOp::Again;
    r.flags = kLfHold;
    return r;
  }
  r.op = LedgerOp::Deliv;
  r.flags = s.has(kSlotLast) ? kLfLast : 0;
  r.wall_s = s.wall_s;
  return r;
}

// The GEN: its generation, and the batch's Mac and first record number (after the GEN and
// every record of the passes before the batch's).
LedgerRec gen_record(const LedgerTable& t, uint32_t gen) {
  LedgerRec r;
  r.op = LedgerOp::Gen;
  r.seq = gen;
  r.flags = kLfBatch;
  uint32_t batch = 0;
  for (uint16_t i = 0; i < t.count(); ++i) {
    if (!live_in_pass(t, i, LivePass::Batch)) continue;
    r.host = t.at(i).host;
    ++batch;
  }
  r.wall_s = 1u + t.live_records() - batch;
  return r;
}

}  // namespace

MonoId slot_id(uint32_t epoch, const LedgerSlot& s) {
  MonoId id;
  id.epoch = epoch;
  id.boot = s.boot();
  id.seq = s.seq();
  return id;
}

// A GONE replayed from the file says nothing of its files: gone (or deleted now, a cut
// delete finished) and its record need not live on; still there, it stays.
void RecEngine::finish_gones(LedgerTable& t, uint32_t epoch) {
  for (uint16_t i = 0; i < t.count(); ++i) {
    LedgerSlot& s = t.at(i);
    if (!s.has(kSlotGone) || s.has(kSlotDone)) continue;
    const FsErr w = d_.fs->unlink(rec_wav_path(d_.root, slot_id(epoch, s)).c_str());
    const FsErr m = w == FsErr::None || w == FsErr::NotFound ? d_.fs->unlink(rec_meta_path(d_.root, slot_id(epoch, s)).c_str()) : w;
    if (m == FsErr::None || m == FsErr::NotFound) s.set(kSlotDone);
  }
}

// The live records in the passes' order behind a GEN that says where the batch is, so a
// replay finds the same batch and the same kept, waiting and deleted segments.
FsErr RecEngine::compact(Book& b, LedgerTable& t) {
  finish_gones(t, b.epoch);
  const uint8_t next = static_cast<uint8_t>(1 - b.active);
  FsErr err = d_.fs->open_trunc(rec_ledger_path(d_.root, b.epoch, next).c_str());
  if (err != FsErr::None) return err;
  Appender a{*this};
  const LedgerRec gen = gen_record(t, b.gen + 1);
  a.add(gen);
  for (uint8_t p = 0; p < kLivePasses; ++p) {
    for (uint16_t i = 0; i < t.count(); ++i) {
      if (live_in_pass(t, i, static_cast<LivePass>(p))) a.add(slot_record(t.at(i), static_cast<LivePass>(p)));
    }
  }
  err = a.finish();
  if (err == FsErr::None) err = d_.fs->unlink(rec_ledger_path(d_.root, b.epoch, b.active).c_str());
  if (err != FsErr::None && err != FsErr::NotFound) return err;  // the next probe drops the new one
  b.active = next;
  b.gen = gen.seq;
  b.size = a.bytes;
  b.dirty = false;
  return FsErr::None;
}

bool RecEngine::tidy_due(const Book& b, const LedgerTable& t) const {
  const uint32_t live_bytes = static_cast<uint32_t>(t.live_records() + 1) * kLedgerRecBytes;
  const uint32_t limit = live_bytes + kCompactSlack > kCompactMin ? live_bytes + kCompactSlack : kCompactMin;
  return !t.overflow() && (b.dirty || b.size > limit);
}

// The scan's first step: a dirty or long ledger is compacted now, while the ring is free;
// the batch and the oldest copy of the device's own epoch are what the screen shows.
void RecEngine::settle(uint32_t epoch, uint8_t* scratch, size_t bytes) {
  Book* b = book(epoch);
  if (!b || b->size == 0) return;
  LedgerTable t = load(*b, scratch, bytes);
  if (epoch == d_.ids->epoch()) info_ = t.info();
  if (tidy_due(*b, t)) compact(*b, t);
}

uint8_t RecEngine::epochs(FixedStr<7>* names) {
  class Visit : public RecDirVisitor {
   public:
    explicit Visit(FixedStr<7>* out) : out_(out) {}
    uint8_t n = 0;
    void entry(const char* name, bool is_dir) override {
      if (is_dir && n < kMaxEpochs && rec_is_epoch_dir(name)) out_[n++] = FixedStr<7>::from(name);
    }

   private:
    FixedStr<7>* out_;
  } v(names);
  d_.fs->list(rec_dir(d_.root).c_str(), v);
  return v.n;
}

}  // namespace rec
}  // namespace cpa
