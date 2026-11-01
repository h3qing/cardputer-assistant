// Boot scan of /outbox (05 §5.9), folded in place into one ScanState that the outbox
// I/O shell (outbox_io.cpp) keeps in static storage: at ~3.4 KB, a new ScanState per
// directory entry overflowed the 16 KB loop stack (05 §5.13), so this fold belongs to
// the shell's mutation boundary. The rules:
//   - <id>.tmp without <id>.json: never committed, never shown as saved -> delete;
//   - <id>.tmp next to <id>.json: keep it, only count it (broken invariant; LittleFS
//     rename is atomic, so this should never happen; the rule is for FatFS in v0.2);
//   - <id>.json is pending once listed; the shell reads and decodes it after the
//     listing: one that fails to decode or whose id differs from its name -> bad/,
//     one that cannot be read stays for a later boot;
//   - bad/ keeps the newest 20; names we never write are left alone and counted.
// Two more rules hold only on FAT (ScanState.fat, i.e. atomic_rename() is false; 09
// §3.6), so LittleFS keeps exactly the rules above:
//   - an empty <id>.json was opened but never synced (power lost before "saved") ->
//     delete it, instead of quarantining it as bad content;
//   - every .tmp is only counted (broken), never deleted: FAT saves never write one, and
//     deleting an entry that may share its cluster chain would destroy the other file.
#pragma once

#include <cstdint>

#include "outbox_core.h"

namespace cpa {

enum class ScanName : uint8_t { Json, Tmp, Dir, Other };

struct ScanClass {
  ScanName kind = ScanName::Other;
  MonoId id;
};

ScanClass scan_classify(const char* name, bool is_dir);

enum class ScanEntry : uint8_t {
  Json,        // /outbox/<id>.json, listed: pending until its content is checked
  Tmp,         // /outbox/<id>.tmp
  Bad,         // a file in /outbox/bad
  JsonBad,     // checked: does not decode, or the id differs -> bad/
  JsonUnread,  // checked: could not be read -> stays for a later boot
  JsonEmpty,   // checked: an empty file -> deleted on FAT, bad content otherwise
};

enum class ScanActionKind : uint8_t { RemoveTmp, MoveToBad, RemoveBad, RemoveJson };

struct ScanAction {
  ScanActionKind kind = ScanActionKind::RemoveTmp;
  MonoId id;
};

constexpr uint8_t kScanTmpMax = 16;
constexpr uint8_t kScanActionsMax = 32;

struct ScanState {
  OutboxCore core;
  uint8_t n_tmp = 0;
  MonoId tmp[kScanTmpMax];
  uint8_t n = 0;
  ScanAction actions[kScanActionsMax];
  uint16_t broken = 0;    // .tmp next to its .json (on FAT: every .tmp)
  uint16_t deferred = 0;  // left on disk for a later boot (capacity, action list full)
  bool json_deferred = false;  // some .json is not tracked: keep every .tmp this boot
  bool fat = false;            // FAT commit protocol: the two FAT-only rules apply
};

void scan_entry(ScanState& s, ScanEntry entry, const MonoId& id);
void scan_finish(ScanState& s);  // once, after every entry: decides the .tmp files

}  // namespace cpa
