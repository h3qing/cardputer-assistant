// FatPort (docs/09-m2-design.md §3.3): the POSIX calls SdStore needs below the VFS mount
// point, so SdStore stays pure and testable on the host. hal's VfsFatPort implements it
// with ::open / ::fsync / ... on "/sd" + path; tests use FakeFatPort. Paths are physical
// ("/cpa/..."). One file is open for writing at a time.
#pragma once

#include <cstddef>
#include <cstdint>

#include "file_store.h"

namespace cpa {

// Disk: errno EIO / ENODEV / ENXIO, i.e. FatFs FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY and
// the like: the card is gone or broken, and SdStore stops touching the bus (§3.8).
enum class FatErr : uint8_t { None, NotFound, Exists, Disk, Other };

class FatPort {
 public:
  virtual ~FatPort() = default;
  virtual FatErr open_write(const char* path) = 0;  // O_CREAT | O_TRUNC
  virtual FatErr write(const char* data, size_t n) = 0;
  virtual FatErr sync_close() = 0;  // fsync + close
  virtual long read(const char* path, char* buf, size_t cap, FatErr& err) = 0;  // bytes, -1 on error
  virtual FatErr unlink(const char* path) = 0;
  virtual FatErr mkdir(const char* path) = 0;  // an existing directory is success
  virtual FatErr list(const char* dir, DirVisitor& visit) = 0;  // direct children
};

}  // namespace cpa
