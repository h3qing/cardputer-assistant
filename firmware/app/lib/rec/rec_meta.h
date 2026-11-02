// The .M commit marker of a segment (docs/11 §4.4): JSON of at most 512 B written with
// snprintf. It commits its segment only when it parses and its id equals the id in the
// file name (the outbox rule of 09 §3.6: an old cluster cannot pass for another segment).
// Unknown fields are ignored; only v != 1 is rejected.
#pragma once

#include <cstddef>
#include <cstdint>

#include "domain.h"
#include "messages.h"
#include "mono_id.h"

namespace cpa {
namespace rec {

constexpr size_t kMetaMax = 512;
constexpr uint8_t kStarsMax = 16;  // file_marks_max
// The codec transient at the start of segment 0, skipped by WhisperWoof. G-A5 measured
// p50 60 / p95 90 / max 130 ms (docs/gates/m3-device-gates.md §4.3): 150 ms. Those runs
// had a speaker cycle (the cue) before Mic.begin; v0.3.1's silent start at kMicGain
// measured 70-90 ms in five mic_cal lead runs (§5.4), so 150 stays. The transient comes
// after the PGA and scales with REG17 and magnification: at v0.3.0's x8 it lasted 260 ms.
constexpr uint16_t kLeadMs = 150;

struct RecMeta {
  MonoId id;
  MonoId rec;  // session = id of segment 0
  uint16_t seg = 0;
  bool last = false;
  Opt<MonoId> next;  // taken before this .M is written: relinks an uncommitted successor
  RecMode mode = RecMode::Memo;
  uint32_t bytes = 0;  // PCM bytes after the header, even
  Opt<Sha256Hex> sha256;
  Opt<proto::RecAt> at;
  Opt<ProjectKey> project;
  uint8_t slot = 0;  // when the project of the slot was unknown; 0 = none
  uint8_t n_stars = 0;
  uint32_t stars[kStarsMax] = {};
  Opt<uint16_t> lead_ms;  // segment 0
  bool consent = false;   // meeting segment 0: "ack"
  bool rcv = false;       // recovered at boot
  FixedStr<31> fw;
};

using MetaText = FixedStr<kMetaMax>;

MetaText rec_meta_encode(const RecMeta& m);
Opt<RecMeta> rec_meta_decode(const char* s, size_t n, const MonoId& file_id);
// The same into `out` (the audio task's boot scan: no second RecMeta on its stack); out is
// unspecified when false.
bool rec_meta_decode_into(const char* s, size_t n, const MonoId& file_id, RecMeta& out);
// The file.items entry of a committed segment whose .W is file_size bytes long. A size
// other than 44 + bytes (§4.6 last row) is reported as is, without sha256 and with rcv.
proto::FileItem rec_item_of(const RecMeta& m, uint32_t file_size);

}  // namespace rec
}  // namespace cpa
