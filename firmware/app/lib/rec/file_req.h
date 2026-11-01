// file.* requests (docs/11 §5.3-§5.4, §4.7), pure: what the loop answers itself and what
// it hands to the audio task.
//   gate:  the recording state machine is not Idle -> E_BUSY "live"; the boot scan is not
//          done -> E_NOT_READY; 4 already in flight -> E_BUSY "full"
//   get:   an id not on the card -> E_UNKNOWN_ITEM; off >= size -> n:0 at once (not an
//          error); else read min(len, size - off) bytes
//   del:   not on the card -> ack (a resend is idempotent); size differs -> E_BAD_FIELD
//          size; else delete (the audio task checks the recorded sha256)
//   list:  the ids after `after`, at most max (default 4); none -> an empty page at once.
//          The page then shrinks until its line fits 2048 B, with more:true.
// docs/28 S1a: what a Mac sees (Viewer): never an entry taken back from it (RecIndex excl),
// and, for the plug-in hint's 5 s, none of the offline ones (hold: every id >= hold_from).
// An unseen entry is as one not on the card: get E_UNKNOWN_ITEM, del ack without deleting,
// and neither listed nor counted in files.n.
#pragma once

#include <cstdint>

#include "messages.h"
#include "rec_index.h"

namespace cpa {
namespace rec {

enum class FileAnswer : uint8_t { Queue, Error, Data0, Ack, Items0 };

struct FileVerdict {
  FileAnswer answer = FileAnswer::Queue;
  ErrCode code = ErrCode::None;
  Opt<proto::FieldPath> field;
  Opt<proto::ErrText> msg;
  MonoId id;          // get / del: the parsed id
  uint32_t size = 0;  // the indexed file size
  uint16_t len = 0;   // get: bytes to read
};

struct FileGate {
  bool live = false;      // RecCore not Idle
  uint8_t inflight = 0;   // accepted file.* not answered yet
};

struct ListPlan {
  uint8_t n = 0;
  MonoId ids[proto::kFileListMax];
  uint32_t sizes[proto::kFileListMax] = {};
  bool more = false;
};

FileVerdict file_gate(bool index_ready, const FileGate& g);
FileVerdict file_check_get(const proto::FileGet& r, const RecIndex& idx);
FileVerdict file_check_del(const proto::FileDel& r, const RecIndex& idx);
struct Viewer {
  uint32_t host = 0;  // the Mac's host_id hash (0: unknown, sees every entry not held)
  bool hold = false;
  MonoId hold_from;
};

bool file_visible(const RecIndex::Entry& e, const RecIndex& idx, const Viewer& v);

// docs/28 §3.2: what waits from the offline mark on that this Mac would be given.
struct Offline {
  uint16_t segs = 0;
  uint16_t recs = 0;  // their segment 0s (at least 1 when segs > 0)
};
Offline file_offline(const RecIndex& idx, const MonoId& from, uint32_t host);
uint16_t file_visible_count(const RecIndex& idx, const Viewer& v);
FileVerdict file_check_get(const proto::FileGet& r, const RecIndex& idx, const Viewer& v);
FileVerdict file_check_del(const proto::FileDel& r, const RecIndex& idx, const Viewer& v);
ListPlan file_plan_list(const proto::FileList& r, const RecIndex& idx);
ListPlan file_plan_list(const proto::FileList& r, const RecIndex& idx, const Viewer& v);

// The largest k <= n for which fits(ctx, k) holds (0 when none does): the file.items page
// that still fits one line.
using PageFits = bool (*)(void* ctx, uint8_t k);
uint8_t file_fit_page(uint8_t n, PageFits fits, void* ctx);

}  // namespace rec
}  // namespace cpa
