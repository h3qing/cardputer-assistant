// Outbox file I/O over the FileStore interface (05 §5.9). This is the outbox's thin
// I/O shell: it performs the storage effects that the pure OutboxCore / boot scan
// decide, in the order that makes power loss safe (store_commit.h):
//   write:  LittleFS open(<id>.tmp) -> write -> close (commit) -> rename to <id>.json
//           FAT      open(<id>.json) -> write -> close (fsync); never a rename
//   ack:    remove <id>.json
//   rejected: LittleFS rename to bad/<id>.json; FAT copy to bad/<id>.json, then remove
#pragma once

#include <cstddef>
#include <cstdint>

#include "codec.h"
#include "file_store.h"
#include "outbox_core.h"
#include "outbox_scan.h"

namespace cpa {

enum class OutboxFile : uint8_t { Json, Tmp, Bad };
using OutboxPath = FixedStr<40>;

constexpr const char kOutboxDir[] = "/outbox";
constexpr const char kOutboxBadDir[] = "/outbox/bad";

OutboxPath outbox_path(const MonoId& id, OutboxFile file);

// True only when the item is durable as /outbox/<id>.json: only then show "saved".
bool outbox_persist(FileStore& fs, const MonoId& id, const char* line, size_t n);
// Executes a storage effect (Remove / MoveToBad / RemoveBad); other kinds are no-ops.
bool outbox_apply(FileStore& fs, const OutboxEffect& e);
long outbox_read(FileStore& fs, const MonoId& id, char* buf, size_t cap);

struct ScanResult {
  OutboxCore core;
  uint16_t broken = 0;    // .tmp next to .json, on FAT every .tmp (system page)
  uint16_t deferred = 0;  // left for a later boot
  uint16_t other = 0;     // names the firmware never writes (left alone)
  uint16_t failed = 0;    // cleanup actions that failed (retried next boot)
};

// Lists /outbox and /outbox/bad, then checks every pending .json with the codec (after
// the listing, never inside it), executes the cleanup and returns the core to start
// with. Boot only, not re-entrant: the scan state and the result live in static
// storage to spare the 16 KB loop stack, and the result stays valid until the next call.
const ScanResult& outbox_boot_scan(FileStore& fs, proto::Codec& codec);

}  // namespace cpa
