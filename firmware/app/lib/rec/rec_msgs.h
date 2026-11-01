// The two queues between the loop and the audio task (docs/11 §4.7):
//   loop -> audio:  RecCmd  Start{mode,project,consent} / Stop / Mark / Tag / List / Get /
//                           Del / SegMs / MicGain / Codec / Ledger
//   audio -> loop:  RecEvt  IndexReady / Started / Segment / Committed / Saved / Discarded /
//                           Failed / Level / Items / Data / DelDone / Requeued / LedgerDone / Kept
// docs/28 S1a: the audio task is the only writer of the SENT.L ledger (D18): a Del keeps the
// segment and appends DELIV, and the screen's "deliver again", "not to this Mac", "clear kept
// copies" and the loop's tidying are Ledger commands whose LedgerDone the screen waits for.
// Plain values: FreeRTOS copies them into its queues. Items and Data payloads travel in
// the shared TransferBuf, which only one file request uses at a time.
#pragma once

#include <cstdint>

#include "domain.h"
#include "err_code.h"
#include "ledger_table.h"
#include "messages.h"
#include "mic_gain.h"
#include "mono_id.h"

namespace cpa {
namespace rec {

struct StartSpec {
  RecMode mode = RecMode::Memo;
  Opt<ProjectKey> project;  // the slot's project when the device knows it
  uint8_t slot = 0;         // 0 = none
  bool consent = false;     // meeting: Enter pressed on the consent confirm
  Opt<int64_t> wall_offset_ms;  // wall clock = up_ms + offset, when known
  uint8_t gen = 0;  // the recording's generation: its events carry it back (rec_core.h)
};

enum class CmdKind : uint8_t { None, Start, Stop, Mark, Tag, List, Get, Del, SegMs, MicGain, Codec, Ledger };

// docs/28 S1a, CmdKind::Ledger:
//   Again  the last batch handed to a Mac waits again, never for that Mac (room: how many
//          more may wait, 256 - waiting: a batch that leaves fewer than kAgainSpare is refused)
//   Hold   the plug-in hint's "not to this Mac": every segment from `lo` on, never to `host`
//   Clear  every kept copy goes (System -> storage, after its confirm)
//   Tidy   what the policy says goes (ledger_policy.h): wall_s a trusted clock or 0
//   Give   undoes Again / Hold for `host` (the Mac on the link): what waits, never for it,
//          is for every Mac again (one AGAIN HOLD with host 0 a segment); n: segments
// A Del carries the receiving Mac's `host` and the wall clock `wall_s` for its DELIV.
enum class LedgerAsk : uint8_t { Again, Hold, Clear, Tidy, Give };

struct LedgerSpec {
  LedgerAsk ask = LedgerAsk::Tidy;
  uint32_t host = 0;
  uint32_t wall_s = 0;
  MonoId lo;
  uint16_t room = 0;
  bool free_known = false;
  uint64_t free_bytes = 0;
};

// Why a Ledger command changed nothing (LedgerDone); None: done and durable.
enum class LedgerNo : uint8_t { None, Storage, Incomplete, TooMany, Nothing, Busy };

struct RecCmd {
  CmdKind kind = CmdKind::None;
  StartSpec start;  // Start; Tag uses start.project / start.slot
  uint8_t n = 0;  // List
  MonoId ids[proto::kFileListMax];
  uint32_t sizes[proto::kFileListMax] = {};
  MonoId fid;  // Get, Del
  uint32_t off = 0;
  uint16_t len = 0;
  uint32_t size = 0;  // the indexed file size
  Opt<Sha256Hex> sha256;
  uint32_t seg_ms = 0;  // SegMs (debug.rec)
  MicGain gain;         // MicGain (debug.mic): used from the next Start on
  bool warm = false;    // Codec (debug.codec, issue #31): the boot's warm-up after the reset
  LedgerSpec ledger;    // Ledger; Del: host and wall_s
};

enum class EvtKind : uint8_t {
  None,
  IndexReady,  // n, rev: the boot scan is done; the loop owns the index from now on
  Started,     // id = session, up_ms of the first written sample (after a muted start)
  Segment,     // seg = the segment now being written
  Committed,   // id, size: a .M was written (add to the index)
  Saved,       // the session ended normally; its last .M is written
  Discarded,   // shorter than rec_min_ms, or stopped before the mic started: nothing kept
  Failed,      // fail: the session stopped (Full: the last segment was still committed)
  Level,       // level: RMS of the last block, int16 scale
  Items,       // n entries in TransferBuf::items, or code
  Data,        // id, off, n bytes in TransferBuf::data, size, crc, or code
  DelDone,     // id deleted or kept (kept), or code (field_sha: E_BAD_FIELD sha256)
  Requeued,    // docs/28: id, size waits again, never for `host`; first: segment 0
  LedgerDone,  // ask done: n recordings (or segments for Hold), freed MiB in size, or no
  Kept,        // the kept copies now (after the scan, a Del that kept, a Ledger command)
};

enum class RecFail : uint8_t { None, Disk, Full, NoMemory, Internal };

struct RecEvt {
  EvtKind kind = EvtKind::None;
  MonoId id;
  uint32_t up_ms = 0;
  uint32_t off = 0;
  uint32_t size = 0;
  uint32_t crc = 0;
  uint32_t rev = 0;
  uint16_t seg = 0;
  uint16_t n = 0;
  uint16_t level = 0;
  RecFail fail = RecFail::None;
  ErrCode code = ErrCode::None;  // Items / Data / DelDone: the error to answer with
  bool field_sha = false;
  bool auto_stop = false;  // Saved: the cap was reached
  uint8_t gen = 0;  // Started / Saved / Discarded / Failed: the Start's gen (0: untagged)
  // docs/28 S1a
  bool kept = false;        // DelDone: kept (not deleted); IndexReady: incomplete instead
  bool first = false;       // Requeued: segment 0 of its recording; LedgerDone: more to do
  uint32_t host = 0;        // Requeued: never to this Mac
  LedgerAsk ask = LedgerAsk::Tidy;  // LedgerDone
  LedgerNo no = LedgerNo::None;     // LedgerDone
  KeptInfo info;            // Kept
};

// docs/20 S1: the events of one recording carry the gen of its Start, so the loop can
// tell a late end of an earlier recording from the end of the current one.
inline bool carries_gen(EvtKind k) {
  return k == EvtKind::Started || k == EvtKind::Saved || k == EvtKind::Discarded || k == EvtKind::Failed;
}

struct TransferBuf {
  proto::FileItem items[proto::kFileListMax];
  uint8_t data[proto::kFileGetMax] = {};
};

class EvtSink {
 public:
  virtual ~EvtSink() = default;
  virtual void emit(const RecEvt& e) = 0;
};

}  // namespace rec
}  // namespace cpa
