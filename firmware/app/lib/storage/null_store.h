// NullStore (docs/09-m2-design.md §3.2): the FileStore used when no storage is available
// (plan_storage chose Store::None). Every operation fails and free_bytes() is 0; the
// Runtime does not even try to save then (SaveResult::NoStorage, §8).
#pragma once

#include "file_store.h"

namespace cpa {

class NullStore : public FileStore {
 public:
  bool open_write(const char*) override { return false; }
  bool write(const char*, size_t) override { return false; }
  bool close() override { return false; }
  bool rename(const char*, const char*) override { return false; }
  bool remove(const char*) override { return false; }
  long read(const char*, char*, size_t) override { return -1; }
  bool list(const char*, DirVisitor&) override { return false; }
  bool mkdir(const char*) override { return false; }
  uint32_t free_bytes() override { return 0; }
};

}  // namespace cpa
