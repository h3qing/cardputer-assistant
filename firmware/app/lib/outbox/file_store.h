// FileStore (05 §5.9): the storage primitives the outbox needs. LfsStore (hal/,
// LittleFS) and SdStore (storage/, FAT on the SD card) implement it on the device; tests
// use FakeStore with power-loss injection. One file is open for writing at a time;
// content is committed by close(); remove is atomic. rename is atomic and replaces its
// target only where atomic_rename() is true; FAT has neither property, so the commit
// protocol differs there (docs/09-m2-design.md §3.6, store_commit.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace cpa {

class DirVisitor {
 public:
  virtual ~DirVisitor() = default;
  virtual void entry(const char* name, bool is_dir) = 0;
};

class FileStore {
 public:
  virtual ~FileStore() = default;
  virtual bool open_write(const char* path) = 0;  // create or truncate
  virtual bool write(const char* data, size_t n) = 0;
  virtual bool close() = 0;  // commit
  virtual bool rename(const char* from, const char* to) = 0;
  virtual bool remove(const char* path) = 0;
  virtual long read(const char* path, char* buf, size_t cap) = 0;  // bytes read, -1 on error
  virtual bool list(const char* dir, DirVisitor& visit) = 0;       // direct children only
  virtual bool mkdir(const char* path) = 0;
  virtual uint32_t free_bytes() = 0;
  // true: rename is atomic and replaces its target (LittleFS). false: FAT, where the
  // outbox never renames (commit_file / move_file, 09 §3.6).
  virtual bool atomic_rename() const { return true; }
};

}  // namespace cpa
