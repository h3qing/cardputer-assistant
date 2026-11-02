// RecEngine (docs/11 §4.5-§4.7): the recording and transfer work of the audio task,
// behind RecFilePort so it runs on the host too. hal's audio task owns the microphone
// and the ring; it calls the engine with each command and each full block, and turns what
// the engine emits into queue events.
//   scan:   boot recovery (rec_recovery.h) over <root>/rec/*/, fills the index, IndexReady
//   start / write / stop / mark / tag: one session through RecWriter; a session shorter
//           than rec_min_ms is discarded; the cap ends it (Saved, auto_stop). Each block
//           written emits its Level: the RMS above 130 Hz (level_hpf.h), not the room's hum
//   arm:    the audio task's start (issue #31, mic_start.h): the session opens with the
//           first block the codec does not hold muted, at its first live sample, or after
//           kMuteWaitMs anyway (write() or tick(); counted). Before that nothing is written
//           or emitted, so the loop stays in the Cue (● Wait, not ● REC); a stop discards.
//           The session's clock (up_ms, ★, lead_ms, segment bytes) starts at the first
//           written sample.
//   list / get / del: file.* work the loop has validated (file_req.h). The file of the
//           last get stays open for the next get of the same id (G-A1 §3.3) and is closed
//           before any other file call.
//   ledger (docs/28 S1a, rec_ledger*.cpp): the only writer of each epoch's SENT.L. A del
//           that may keep its segment appends DELIV (fsync) instead of deleting; Ledger
//           commands replay the ledger into the caller's scratch (the idle ring) and append
//           AGAIN / GONE before they requeue or delete anything. A dirty or long ledger is
//           compacted into the other name (SENT.L <-> SENT.K), never truncated in place.
// Never more than one open file (the writer's, a .M, the transfer file or a ledger).
#pragma once

#include <cstddef>
#include <cstdint>

#include "ledger_policy.h"
#include "ledger_rec.h"
#include "level_hpf.h"
#include "rec_index.h"
#include "rec_msgs.h"
#include "rec_paths.h"
#include "rec_recovery.h"
#include "rec_writer.h"

namespace cpa {
namespace rec {

constexpr uint16_t kDropMax = 8;      // docs/28: recordings a Clear / Tidy deletes per command
constexpr uint16_t kAgainSpare = 12;  // waiting places deliver again leaves free (2 h of segments)

MonoId slot_id(uint32_t epoch, const LedgerSlot& s);  // a ledger slot's segment id (rec_ledger.cpp)

struct EngineDeps {
  RecFilePort* fs = nullptr;
  Sha256Port* sha = nullptr;
  MonoCounter* ids = nullptr;
  RecIndex* index = nullptr;  // written by scan() only
  TransferBuf* xfer = nullptr;
  Crc32Fn crc = crc32_ieee;
  const char* root = "/cpa";
  FixedStr<31> fw;
};

class RecEngine : private CommitSink {
 public:
  explicit RecEngine(const EngineDeps& d);
  RecEngine(const RecEngine&) = delete;
  RecEngine& operator=(const RecEngine&) = delete;

  // scratch: room for the names found on the card (16 B each, 4-byte aligned), e.g. the
  // idle ring.
  void scan(uint8_t* scratch, size_t bytes, EvtSink& out);
  void start(const StartSpec& s, uint32_t up_ms, uint32_t now, EvtSink& out);
  void arm(const StartSpec& s, uint32_t up_ms, uint32_t now);  // up_ms: the microphone's first sample
  void write(const uint8_t* pcm, size_t n, uint32_t now, EvtSink& out);
  void tick(uint32_t now, EvtSink& out);  // armed: the wait's end without a block
  void stop(EvtSink& out);
  bool mark() { return writer_.mark(); }
  void tag(const Opt<ProjectKey>& project, uint8_t slot);
  void list(const RecCmd& c, EvtSink& out);
  void get(const RecCmd& c, EvtSink& out);
  void del(const RecCmd& c, EvtSink& out);
  void close_transfer();
  void set_seg_ms(uint32_t ms) { seg_ms_ = ms; }
  bool recording() const { return arm_.on || writer_.active(); }
  bool waiting() const { return arm_.on; }  // armed: the microphone is on, no file yet
  // docs/28 S1a (rec_ledger_ops.cpp): a Ledger command, with scratch for the replay (none
  // while recording: the ring is the microphone's then).
  void ledger(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out);
  uint16_t kept() const { return kept_total_; }
  uint16_t kept_max() const { return kept_max_; }
  enum class HeadKind : uint8_t { Broken, Original, Gen };
  struct Head {
    HeadKind kind = HeadKind::Broken;
    uint32_t gen = 0;
  };
  uint32_t mute_timeouts() const { return mute_timeouts_; }  // sessions opened by the wait's end

 private:
  void committed(const RecMeta& m, uint32_t file_size) override;
  void segment(uint16_t seg) override;
  void end_session(RecFail fail, bool auto_stop, EvtSink& out);
  void gate(const uint8_t* pcm, size_t n, uint32_t now, EvtSink& out);
  void open_session(uint32_t skipped, uint32_t now, EvtSink& out);
  bool read_meta(const MonoId& id, RecMeta& out);
  ScanEntry scan_entry(const MonoId& id, bool has_w, bool has_m);
  bool predecessor(const MonoId& id, const uint8_t* table, uint16_t n, RecMeta& out);
  bool apply(const ScanEntry& e, const RecoveryPlan& p);
  bool backfill(const MonoId& id, uint32_t data_bytes);
  void scan_one(const uint8_t* table, uint16_t n, uint16_t i, uint32_t& recoveries);
  bool place(const ScanEntry& e, const uint8_t* table, uint16_t n);  // true: recovered
  void scan_classify(const uint8_t* scratch, uint16_t n, uint32_t& recoveries);

  // docs/28 S1a: one epoch's ledger (rec_ledger.cpp)
  struct Book {
    uint32_t epoch = 0;
    bool used = false;
    bool known = false;  // probed this boot
    uint8_t active = 0;  // SENT.L or SENT.K
    uint32_t gen = 0;
    uint32_t size = 0;
    bool dirty = false;  // a bad record or a torn tail: compact at the next replay
  };
  struct Appender;
  Book* book(uint32_t epoch);
  Head head_of(const RecPath& path);
  void probe(Book& b);
  LedgerParse read_ledger(const Book& b, LedgerVisitor& v);
  LedgerTable load(Book& b, uint8_t* scratch, size_t bytes);
  FsErr open_book(const Book& b);
  FsErr close_book(Book& b, Appender& a);
  FsErr append(Book& b, const LedgerRec* recs, size_t n);
  FsErr compact(Book& b, LedgerTable& t);
  void finish_gones(LedgerTable& t, uint32_t epoch);
  bool tidy_due(const Book& b, const LedgerTable& t) const;
  void settle(uint32_t epoch, uint8_t* scratch, size_t bytes);
  // the commands (rec_ledger_ops.cpp)
  bool keep_on_del(const RecMeta& m, const RecCmd& c, RecEvt& e);
  void note_deliv(const LedgerRec& r);
  void again(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out);
  void hold(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out);
  void give(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out);
  bool gives(const LedgerSlot& s, uint32_t epoch, uint32_t host);
  void drop(const RecCmd& c, uint8_t* scratch, size_t bytes, EvtSink& out);  // Clear, Tidy
  struct Drops {
    uint64_t freed = 0;  // bytes of the .W files deleted
    uint16_t recs = 0;
    bool more = false;   // kDropMax reached with more to delete
    bool failed = false;
  };
  void drop_epoch(Book& b, const RecCmd& c, uint8_t* scratch, size_t bytes, Drops& d);
  bool next_drop(const LedgerTable& t, const RecCmd& c, const TidyCtx& ctx, RecKey& k) const;
  uint16_t pick_drops(LedgerTable& t, uint32_t epoch, const RecCmd& c, Drops& d);
  FsErr write_gones(Book& b, LedgerTable& t);
  FsErr write_agains(Book& b, const LedgerTable& t);
  void unlink_marked(LedgerTable& t, uint32_t epoch);
  LedgerNo check_batch(LedgerTable& t, uint32_t epoch, uint16_t room);
  void emit_kept(EvtSink& out);
  uint8_t epochs(FixedStr<7>* names);

  struct Arming {
    bool on = false;
    StartSpec spec;
    uint32_t up_ms = 0;  // the microphone's first sample
    uint32_t at = 0;     // armed at (the wait)
    uint32_t muted = 0;  // bytes dropped so far
  };

  EngineDeps d_;
  RecWriter writer_;
  EvtSink* sink_ = nullptr;  // the current call's sink, for CommitSink callbacks
  uint32_t seg_ms_;
  bool xfer_open_ = false;
  MonoId xfer_id_;
  char meta_buf_[kMetaMax + 1] = {};
  Arming arm_;
  uint32_t mute_timeouts_ = 0;
  LevelHpf hpf_;  // the level's high-pass (level_hpf.h), from the recording's first block
  // docs/28 S1a
  Book books_[kMaxEpochs];
  uint8_t lbuf_[16 * kLedgerRecBytes] = {};
  uint16_t kept_total_ = 0;  // kept segments on the card, every epoch
  uint16_t kept_max_ = 0;    // from the scan table (ledger_policy.h): 0 until the scan
  KeptInfo info_;
  RecKey batch_rec_;  // the recording of the last DELIV (the batch's count)
};

// Records go through lbuf_ to one open file, flushed when it is full.
struct RecEngine::Appender {
  RecEngine& eng;
  FsErr err = FsErr::None;
  size_t n = 0;
  uint32_t bytes = 0;

  void add(const LedgerRec& r) {
    if (err != FsErr::None) return;
    ledger_encode(r, eng.lbuf_ + n * kLedgerRecBytes, eng.d_.crc);
    if (++n * kLedgerRecBytes == sizeof eng.lbuf_) flush();
  }
  void flush() {
    if (err == FsErr::None && n > 0) err = eng.d_.fs->write(eng.lbuf_, n * kLedgerRecBytes);
    bytes += static_cast<uint32_t>(n * kLedgerRecBytes);
    n = 0;
  }
  FsErr finish() {
    flush();
    if (err == FsErr::None) err = eng.d_.fs->fsync();
    const FsErr closed = eng.d_.fs->close();
    return err != FsErr::None ? err : closed;
  }
};

}  // namespace rec
}  // namespace cpa
