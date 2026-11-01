// LedgerTable (docs/28 §3.3-§3.4, S1a): one epoch's SENT.L replayed into a caller's buffer
// (the idle audio ring, 16 B a segment), so the audio task can answer what is kept, what
// was last handed to which Mac, and what to delete, without holding any of it in RAM.
// A segment's state is its own last DELIV / AGAIN and its recording's last GONE, in ledger
// order:
//   DELIV         kept, delivered to `host` (BATCH: in the trailing run of DELIVs to one Mac)
//   AGAIN         waiting again, never to `host`; HOLD on a kept one: ignored
//   GONE (rec)    every kept segment of that recording: deleted
// The last batch is the run of DELIVs since the last DELIV to another Mac: the caller passes
// its start (ledger_batch_start, a first pass) and the records are numbered as they come.
// Recordings are found by `back` (id.seq - rec.seq); kBackUnknown makes a segment its own.
// A recording is complete when its LAST segment is kept: only complete ones age out (§3.3).
#pragma once

#include <cstddef>
#include <cstdint>

#include "ledger_rec.h"

namespace cpa {
namespace rec {

constexpr uint8_t kSlotKept = 0x01;
constexpr uint8_t kSlotLast = 0x02;
constexpr uint8_t kSlotExcl = 0x04;   // waiting, never to `host`
constexpr uint8_t kSlotGone = 0x08;   // deleted by GONE: its files may still be there
constexpr uint8_t kSlotBatch = 0x10;  // kept, in the last batch
constexpr uint8_t kSlotMark = 0x20;   // picked by a plan
constexpr uint8_t kSlotDone = 0x40;   // its files are gone (a GONE needs no record any more)

// 16 B, as a scan name (rec_scan.cpp): boot (25 bits) and flags (7) share a word, seq (20)
// and back (12; kSlotBackUnknown: its own recording) the next.
constexpr uint16_t kSlotBackUnknown = 0xFFF;

struct LedgerSlot {
  uint32_t boot_flags = 0;
  uint32_t seq_back = 0;
  uint32_t wall_s = 0;  // kept: when it was handed over (0: unknown)
  uint32_t host = 0;    // kept: to whom; kSlotExcl: never to whom

  uint32_t boot() const { return boot_flags & 0x1FFFFFFu; }
  uint8_t flags() const { return static_cast<uint8_t>(boot_flags >> 25); }
  uint32_t seq() const { return seq_back & 0xFFFFFu; }
  uint16_t back() const { return static_cast<uint16_t>(seq_back >> 20); }
  bool has(uint8_t f) const { return (flags() & f) != 0; }
  void set(uint8_t f) { boot_flags |= static_cast<uint32_t>(f) << 25; }
  void clear(uint8_t f) { boot_flags &= ~(static_cast<uint32_t>(f) << 25); }
  void set_back(uint16_t b) { seq_back = (seq_back & 0xFFFFFu) | (static_cast<uint32_t>(b < kSlotBackUnknown ? b : kSlotBackUnknown) << 20); }
};
static_assert(sizeof(LedgerSlot) == 16, "16 B a segment");

struct RecKey {
  uint32_t boot = 0;
  uint32_t seq = 0;
  bool operator==(const RecKey& o) const { return boot == o.boot && seq == o.seq; }
};

RecKey rec_of(const LedgerSlot& s);

// What the screen shows of the kept copies (docs/28 §3.3, §3.5).
struct KeptInfo {
  uint16_t segs = 0;
  uint32_t oldest_s = 0;   // the oldest kept segment's wall clock; 0 none or unknown
  uint32_t newest_s = 0;   // the newest (a clock reference after a reboot, D16)
  uint32_t batch_host = 0; // the Mac the last batch went to (0: no batch kept)
  uint16_t batch_recs = 0;
  uint16_t batch_segs = 0;
};

class LedgerTable : public LedgerVisitor {
 public:
  LedgerTable(void* mem, size_t bytes, uint32_t batch_start = 0, uint32_t batch_host = 0);
  void record(const LedgerRec& r) override;  // applies one record (the visitor)

  uint16_t count() const { return n_; }
  LedgerSlot& at(uint16_t i) { return slots_[i]; }
  const LedgerSlot& at(uint16_t i) const { return slots_[i]; }
  bool overflow() const { return overflow_; }
  uint32_t records() const { return seen_; }
  int find(uint32_t boot, uint32_t seq) const;  // -1: none
  KeptInfo info() const;
  uint16_t live_records() const;  // what a compaction writes (without its GEN)

 private:
  LedgerSlot* slot(uint32_t boot, uint32_t seq);  // found or added; nullptr when full
  void deliv(const LedgerRec& r);
  void again(const LedgerRec& r);
  void gone(const LedgerRec& r);

  LedgerSlot* slots_;
  uint16_t cap_;
  uint16_t n_ = 0;
  bool overflow_ = false;
  uint32_t seen_ = 0;  // records applied so far: the next one's number
  uint32_t batch_start_;
  uint32_t batch_host_;
};

// The first pass: where the last batch starts (the record number of the first DELIV after
// the last DELIV to another Mac) and its Mac. A compaction's GEN (kLfBatch) says so for
// the records it wrote: DELIVs before its start are not the batch's.
class BatchFinder : public LedgerVisitor {
 public:
  void record(const LedgerRec& r) override;
  uint32_t start = 0;
  uint32_t host = 0;

 private:
  uint32_t seen_ = 0;
  uint32_t floor_ = 0;  // a GEN's batch start: no DELIV before it starts a batch
  bool any_ = false;
};

// What a compaction writes, in this order (each a pass over the table):
//   Undone   a DELIV for each slot a GONE took whose files are still there, so that
//   Gone     ... one GONE per such recording acts on them again when replayed;
//   Kept     the kept segments not in the last batch (after the GONEs: a segment of the same
//            recording kept later is not taken by them);
//   Excl     the AGAINs (waiting, never to that Mac; host 0, for every Mac, needs none);
//   Batch    the last batch, last; the GEN (kLfBatch) says where it starts.
enum class LivePass : uint8_t { Undone, Gone, Kept, Excl, Batch };
constexpr uint8_t kLivePasses = 5;
bool live_in_pass(const LedgerTable& t, uint16_t i, LivePass p);

// One recording of the table: its kept segments, whether it is complete, its newest wall
// clock, and its first slot (the order it was handed over in).
struct RecSummary {
  RecKey rec;
  uint16_t first = 0;
  uint16_t segs = 0;
  bool complete = false;
  uint32_t newest_s = 0;
};

RecSummary rec_summary(const LedgerTable& t, const RecKey& rec);

}  // namespace rec
}  // namespace cpa
