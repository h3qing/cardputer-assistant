// Boot migration of pending outbox items from one store to another (docs/09-m2-design.md
// §3.7): with the SD card as primary store, cpafs's items move to the card once, in
// Runtime::begin, after the source's boot scan and before the primary's.
// For each id of `pending` (the source scan's core: valid items only, oldest 200):
//   read /outbox/<id>.json from `from` -> commit_file on `to` (on FAT a truncating
//   rewrite, which replaces a copy an interrupted run left) -> remove it from `from`.
// Any failing step keeps the source item, counts it as failed, and the next id follows.
// bad/ and /cfg/projects.json stay behind (never sent; the daemon re-pushes the cache).
// Invariant: an item leaves the source only once the target committed it (fsync +
// close), the same rule as "saved", so power loss at any point leaves every item at the
// source, the target, or both, and a rerun on the next boot converges.
#pragma once

#include <cstdint>

#include "file_store.h"
#include "outbox_core.h"

namespace cpa {

struct MigrateResult {
  uint16_t moved = 0;
  uint16_t failed = 0;
};

class MigrateObserver {
 public:
  virtual ~MigrateObserver() = default;
  // After each item, moved or not: the shell kicks USB and shows "i/n" from here.
  virtual void on_item(uint16_t done, uint16_t total) = 0;
};

// Boot only, not re-entrant: items are copied through one static 2 KB buffer.
MigrateResult outbox_migrate(FileStore& from, FileStore& to, const OutboxCore& pending, MigrateObserver& obs);

}  // namespace cpa
