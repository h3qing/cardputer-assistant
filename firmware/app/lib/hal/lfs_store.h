// LfsStore (05 §5.9, docs/09-m2-design.md §3.3): FileStore on LittleFS (esp_littlefs,
// mounted at /littlefs on the "cpafs" partition of partitions/cpa_8MB.csv), through the
// VFS so every step reports errors. Only cpafs is ever mounted or formatted: without it
// begin() fails (internal storage disabled), never falling back to "spiffs", which other
// firmware shares under the Launcher (launcher.md R1, R2). The partition is looked up by
// label at every begin(): the Launcher moves partitions.
// Same semantics as test/core/support/FakeStore:
//   open_write creates or truncates; close() fsyncs and commits; rename atomically
//   replaces the target (littlefs lfs_rename); remove is atomic; list gives direct
//   children; one file open for writing at a time. Device only.
// Physical layout: littlefs looks names up linearly within a directory, and with 200
// items in /outbox an open or rename took 75-90 ms (p95 save 513 ms, measured on the
// device). Outbox items "/outbox/<MonoId>.json|.tmp" are therefore stored in shard
// directories, invisible through this interface: list("/outbox") reports their files as
// direct children. A cpafs of 1 MiB or more has v0.1's 32 shards "/outbox/_<last MonoId
// digit>/"; a smaller one (448 KB under the Launcher) has 4, "_0".."_3" (lfs_shards,
// lfs_shard), so the shard directories leave room for items. Paths are otherwise as given.
#pragma once

#include <cstdint>

#include "file_store.h"

namespace cpa {

class LfsStore : public FileStore {
 public:
  bool begin();  // find cpafs and mount it, formatting only cpafs when it does not mount
  bool open_write(const char* path) override;
  bool write(const char* data, size_t n) override;
  bool close() override;
  bool rename(const char* from, const char* to) override;
  bool remove(const char* path) override;
  long read(const char* path, char* buf, size_t cap) override;
  bool list(const char* dir, DirVisitor& visit) override;
  bool mkdir(const char* path) override;
  uint32_t free_bytes() override;
  uint32_t total_bytes();
  uint8_t shards() const { return shards_; }
  void end();

 private:
  void list_shard(const char* shard_dir, DirVisitor& visit);

  int fd_ = -1;
  uint8_t shards_ = 32;
};

}  // namespace cpa
