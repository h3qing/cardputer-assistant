// Power-loss recovery (docs/11 §4.6) as a pure plan per segment id found on the card:
//   .W size = 44 + bytes, .M ok (id = file name)   committed: index it
//   no .W, a .M                                     a half-done delete (.W goes first): drop .M
//   .W < 44 B, any .M                               drop .W (and .M)
//   .W >= 44 B, .M missing / empty / bad / other id RECOVER: drop the bad .M, data = (size -
//                                                   44) rounded down to whole samples,
//                                                   backfill the header, fsync; rec, seg and
//                                                   at from the segment whose .M names this one
//                                                   as next (else a session of its own, seg 0,
//                                                   at null); write a new .M with rcv:true, no
//                                                   sha256, last:true. Idempotent.
//   .W size != 44 + bytes, .M ok                    keep; served at its real size, as rcv
// Invariant: a .W with a valid .M is never removed here (only file.del removes it).
#pragma once

#include <cstdint>

#include "rec_meta.h"

namespace cpa {
namespace rec {

enum class MetaState : uint8_t { Missing, Bad, Ok };

struct ScanEntry {
  MonoId id;
  bool has_w = false;
  uint32_t w_size = 0;
  MetaState m = MetaState::Missing;
  RecMeta meta;        // m == Ok
  Opt<RecMode> hint;   // from the .W placeholder header
  uint32_t excl_host = 0;  // docs/28: the Mac the ledger keeps it from (0: none)
};

enum class StepKind : uint8_t { UnlinkMeta, UnlinkWav, Backfill, WriteMeta, Index };

struct RecoveryStep {
  StepKind kind = StepKind::Index;
  uint32_t n = 0;  // Backfill: PCM bytes; Index: the size to serve
};

struct RecoveryPlan {
  uint8_t n = 0;
  RecoveryStep steps[4];
  RecMeta meta;  // WriteMeta
  bool recovered = false;
};

bool needs_recovery(const ScanEntry& e);  // the RECOVER row: look for a predecessor first
RecoveryPlan plan_rec_recovery(const ScanEntry& e, const Opt<RecMeta>& prev, const FixedStr<31>& fw);
// The first-sample time of the segment after `prev`: prev.at plus its duration.
Opt<proto::RecAt> at_after(const RecMeta& prev);

}  // namespace rec
}  // namespace cpa
