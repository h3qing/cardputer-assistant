// RecFilePort (docs/11 §4.5): the file calls the audio task makes, below the VFS mount
// point, so the recording code stays pure and testable on the host. hal's
// VfsRecFilePort implements it with ::open / ::write / ::fsync / ...; tests use
// FakeRecFilePort, which injects power cuts. Errors follow FatPort's FatErr (09 §3.8),
// plus Full for ENOSPC (docs/11 §4.3: the segment is closed normally, "SD card full").
//
// Invariant (docs/11 §4.5): at most one file is open at a time. open_* while one is open
// is a caller bug (FakeRecFilePort counts it); close() first.
#pragma once

#include <cstddef>
#include <cstdint>

#include "domain.h"

namespace cpa {
namespace rec {

enum class FsErr : uint8_t { None, NotFound, Exists, Disk, Full, Other };

class RecDirVisitor {
 public:
  virtual ~RecDirVisitor() = default;
  virtual void entry(const char* name, bool is_dir) = 0;
};

class RecFilePort {
 public:
  virtual ~RecFilePort() = default;
  virtual FsErr open_trunc(const char* path) = 0;  // create or truncate, for appending
  virtual FsErr open_rw(const char* path) = 0;     // an existing file, for the header backfill
  // docs/28 §3.4: create, or open at its end, for appending (the SENT.L ledger)
  virtual FsErr open_append(const char* path) = 0;
  virtual FsErr open_read(const char* path) = 0;
  virtual FsErr write(const uint8_t* data, size_t n) = 0;       // append
  virtual FsErr fsync() = 0;
  virtual FsErr pwrite_at0(const uint8_t* data, size_t n) = 0;  // overwrite from offset 0
  virtual FsErr close() = 0;                                     // flushes like fsync
  virtual long read_at(uint32_t off, uint8_t* buf, size_t n, FsErr& err) = 0;  // open file; -1 on error
  virtual long size(const char* path, FsErr& err) = 0;  // a closed file; -1 on error
  virtual FsErr list(const char* dir, RecDirVisitor& visit) = 0;  // direct children
  virtual FsErr unlink(const char* path) = 0;
  virtual FsErr mkdir(const char* path) = 0;  // an existing directory is success
};

// The SHA-256 of a segment's PCM data (hardware SHA on the device, mbedtls).
class Sha256Port {
 public:
  virtual ~Sha256Port() = default;
  virtual void begin() = 0;
  virtual void update(const uint8_t* data, size_t n) = 0;
  virtual Sha256Hex finish() = 0;
};

using Crc32Fn = uint32_t (*)(const uint8_t* data, size_t n);  // CRC-32/IEEE (zlib)

// Table CRC-32/IEEE for the host and tests; the device passes esp_rom_crc32_le
// (docs/gates/m3-device-gates.md §3.2).
uint32_t crc32_ieee(const uint8_t* data, size_t n);

}  // namespace rec
}  // namespace cpa
