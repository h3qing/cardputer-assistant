// SENT.L / SENT.K (docs/28 §3.4, S1a): the per-epoch, append-only ledger of what became of
// each segment after it was handed to a Mac. A .M is never rewritten (FAT tears an in-place
// write); the ledger says instead, one 28 B record per event, little-endian:
//   magic(1)=0xA5 op(1) prof(1) flags(1) boot(4) seq(4) seg(2) back(2) wall_s(4) host(4) crc32(4)
// crc32 (CRC-32/IEEE) covers the first 24 B. The ops:
//   GEN    the first record of a compacted file: seq = its generation; BATCH: it says where
//          the last batch is (host = its Mac, 0: none; wall_s = the record number of its
//          first DELIV), since a compaction loses the deliveries that ended older runs
//   DELIV  a segment was handed over and is kept (id = boot+seq of the segment, seg = its
//          number in the recording, back = id.seq - rec.seq, or kBackUnknown; host = the
//          receiving Mac's host_id hash; wall_s = the wall clock then, 0 unknown; LAST: its
//          .M said last)
//   PROF   reserved (S2: a recording moved to another profile)
//   AGAIN  a segment is to be handed over again, never to `host` (S1a: one record per
//          segment, kept or waiting; HOLD: exclusion only, a kept one stays kept)
//   GONE   a kept recording was deleted (id = the recording)
// S1a uses the spec's spare rsv(2) as `back`, so a DELIV knows its recording without the .M.
// A reader resyncs on the magic after a record that fails, so a torn write costs only that
// record; the file is never truncated in place: a dirty one is compacted into the other name.
// Neither name is a segment name (rec_paths.h wants 11 characters, '.' at 8, W or M at 9).
#pragma once

#include <cstddef>
#include <cstdint>

#include "opt.h"
#include "rec_file_port.h"

namespace cpa {
namespace rec {

constexpr uint8_t kLedgerMagic = 0xA5;
constexpr size_t kLedgerRecBytes = 28;
constexpr uint16_t kBackUnknown = 0xFFFF;

enum class LedgerOp : uint8_t { Gen = 0, Deliv = 1, Prof = 2, Again = 3, Gone = 4 };

constexpr uint8_t kLfLast = 0x01;     // DELIV: the segment is its recording's last
constexpr uint8_t kLfForeign = 0x02;  // reserved (docs/28 §3.6, file.items foreign)
constexpr uint8_t kLfHold = 0x04;     // AGAIN: exclusion only (the plug-in hint's "not here")
constexpr uint8_t kLfBatch = 0x08;    // GEN: host and wall_s say where the last batch is

struct LedgerRec {
  LedgerOp op = LedgerOp::Gen;
  uint8_t prof = 0;
  uint8_t flags = 0;
  uint32_t boot = 0;
  uint32_t seq = 0;
  uint16_t seg = 0;
  uint16_t back = kBackUnknown;
  uint32_t wall_s = 0;
  uint32_t host = 0;
};

void ledger_encode(const LedgerRec& r, uint8_t out[kLedgerRecBytes], Crc32Fn crc);
Opt<LedgerRec> ledger_decode(const uint8_t* p, Crc32Fn crc);  // magic, CRC and op checked

class LedgerVisitor {
 public:
  virtual ~LedgerVisitor() = default;
  virtual void record(const LedgerRec& r) = 0;
};

struct LedgerParse {
  uint32_t good = 0;   // records that passed
  bool dirty = false;  // bytes that are not whole good records: a bad record or a torn tail
};

// One buffer: every good record in order. `end`: the buffer is the whole file, so bytes
// short of a record at its end are a torn tail; else they are left (consumed tells how many
// were used).
LedgerParse ledger_parse(const uint8_t* p, size_t n, bool end, Crc32Fn crc, LedgerVisitor& v,
                         size_t* consumed = nullptr);

// The same over a file read in chunks of any size, with a carry of less than one record.
class LedgerStream {
 public:
  void feed(const uint8_t* p, size_t n, Crc32Fn crc, LedgerVisitor& v);
  LedgerParse finish(Crc32Fn crc, LedgerVisitor& v);

 private:
  uint8_t carry_[2 * kLedgerRecBytes] = {};
  size_t n_ = 0;
  LedgerParse total_;
};

const char* ledger_name(uint8_t which);  // 0: SENT.L, 1: SENT.K

}  // namespace rec
}  // namespace cpa
