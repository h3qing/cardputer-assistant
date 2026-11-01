// VfsRecFilePort (docs/11 §4.5): RecFilePort over ::open / ::write / ::fsync / ... on
// "/sd" + path, for the audio task. One descriptor at most. errno: EIO / ENODEV / ENXIO ->
// Disk (and sd_health dead: neither task touches the card again), ENOSPC or a zero-byte
// write -> Full, ENOENT -> NotFound, EEXIST -> Exists, others -> Other. Device only.
#pragma once

#include "rec_file_port.h"

namespace cpa {

class VfsRecFilePort : public rec::RecFilePort {
 public:
  rec::FsErr open_trunc(const char* path) override;
  rec::FsErr open_rw(const char* path) override;
  rec::FsErr open_append(const char* path) override;
  rec::FsErr open_read(const char* path) override;
  rec::FsErr write(const uint8_t* data, size_t n) override;
  rec::FsErr fsync() override;
  rec::FsErr pwrite_at0(const uint8_t* data, size_t n) override;
  rec::FsErr close() override;
  long read_at(uint32_t off, uint8_t* buf, size_t n, rec::FsErr& err) override;
  long size(const char* path, rec::FsErr& err) override;
  rec::FsErr list(const char* dir, rec::RecDirVisitor& visit) override;
  rec::FsErr unlink(const char* path) override;
  rec::FsErr mkdir(const char* path) override;

 private:
  rec::FsErr open_with(const char* path, int flags);
  int fd_ = -1;
};

}  // namespace cpa
