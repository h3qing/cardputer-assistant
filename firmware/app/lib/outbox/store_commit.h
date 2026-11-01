// Commit protocol over a FileStore (docs/09-m2-design.md §3.6). The outbox (outbox_io),
// the migration and the project cache write files only through these two functions:
//
//                 atomic_rename() = true (LittleFS, v0.1)   false (FAT on the SD card)
//   commit_file   open(tmp) write close, rename to path      open(path) write close
//                 (a failure removes tmp)                    (a failure removes path)
//   move_file     rename(from, to)                           read from (2 KB), commit_file
//                                                            to, then remove from
//
// FAT's rename is neither atomic nor replacing, so on FAT neither function ever renames
// and no .tmp is ever written. A file opened but never synced is empty after power loss;
// the boot scan deletes an empty .json on FAT. move_file removes the source only after
// the copy is committed: power loss leaves the item at the source, the target, or both.
#pragma once

#include <cstddef>

#include "file_store.h"

namespace cpa {

// True only once `data` is durable as `path` (only then is "saved" shown). `tmp` is the
// staging name used where rename is atomic; FAT ignores it.
bool commit_file(FileStore& fs, const char* path, const char* tmp, const char* data, size_t n);

// Moves one outbox file (at most limits::kLineMax bytes on FAT; a larger one stays where
// it is). Not re-entrant on FAT: the copy goes through one static 2 KB buffer.
bool move_file(FileStore& fs, const char* from, const char* to);

}  // namespace cpa
