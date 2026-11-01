#include "vfs_rec_file_port.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "fixed_str.h"
#include "sd_health.h"

namespace cpa {
namespace {

using rec::FsErr;
using VfsPath = FixedStr<64>;

VfsPath vfs(const char* path) { return VfsPath::from("/sd").append(path); }

FsErr from_errno(int e) {
  switch (e) {
    case EIO:
    case ENODEV:
    case ENXIO: hal_sd_mark_dead(); return FsErr::Disk;
    case ENOSPC: return FsErr::Full;
    case ENOENT: return FsErr::NotFound;
    case EEXIST: return FsErr::Exists;
    default: return FsErr::Other;
  }
}

FsErr last_error() { return from_errno(errno); }

}  // namespace

FsErr VfsRecFilePort::open_with(const char* path, int flags) {
  if (hal_sd_dead()) return FsErr::Disk;
  if (fd_ >= 0) close();
  fd_ = ::open(vfs(path).c_str(), flags, 0644);
  return fd_ >= 0 ? FsErr::None : last_error();
}

FsErr VfsRecFilePort::open_trunc(const char* path) { return open_with(path, O_WRONLY | O_CREAT | O_TRUNC); }
FsErr VfsRecFilePort::open_rw(const char* path) { return open_with(path, O_WRONLY); }
FsErr VfsRecFilePort::open_append(const char* path) { return open_with(path, O_WRONLY | O_CREAT | O_APPEND); }
FsErr VfsRecFilePort::open_read(const char* path) { return open_with(path, O_RDONLY); }

FsErr VfsRecFilePort::write(const uint8_t* data, size_t n) {
  if (hal_sd_dead()) return FsErr::Disk;
  if (fd_ < 0) return FsErr::Other;
  size_t done = 0;
  while (done < n) {
    const ssize_t w = ::write(fd_, data + done, n - done);
    if (w < 0) return last_error();
    if (w == 0) return FsErr::Full;
    done += static_cast<size_t>(w);
  }
  return FsErr::None;
}

FsErr VfsRecFilePort::fsync() {
  if (hal_sd_dead()) return FsErr::Disk;
  if (fd_ < 0) return FsErr::Other;
  return ::fsync(fd_) == 0 ? FsErr::None : last_error();
}

FsErr VfsRecFilePort::pwrite_at0(const uint8_t* data, size_t n) {
  if (hal_sd_dead()) return FsErr::Disk;
  if (fd_ < 0) return FsErr::Other;
  if (::lseek(fd_, 0, SEEK_SET) != 0) return last_error();
  const FsErr err = write(data, n);
  ::lseek(fd_, 0, SEEK_END);
  return err;
}

// After a Disk error the descriptor is dropped without a call: the card is not touched.
FsErr VfsRecFilePort::close() {
  if (fd_ < 0) return FsErr::Other;
  const int fd = fd_;
  fd_ = -1;
  if (hal_sd_dead()) return FsErr::Disk;
  return ::close(fd) == 0 ? FsErr::None : last_error();
}

long VfsRecFilePort::read_at(uint32_t off, uint8_t* buf, size_t n, FsErr& err) {
  err = hal_sd_dead() ? FsErr::Disk : (fd_ < 0 ? FsErr::Other : FsErr::None);
  if (err != FsErr::None) return -1;
  if (::lseek(fd_, static_cast<off_t>(off), SEEK_SET) < 0) {
    err = last_error();
    return -1;
  }
  size_t got = 0;
  ssize_t r = 0;
  while (got < n && (r = ::read(fd_, buf + got, n - got)) > 0) got += static_cast<size_t>(r);
  if (r < 0) err = last_error();
  return err == FsErr::None ? static_cast<long>(got) : -1;
}

long VfsRecFilePort::size(const char* path, FsErr& err) {
  err = hal_sd_dead() ? FsErr::Disk : FsErr::None;
  if (err != FsErr::None) return -1;
  struct stat st;
  if (::stat(vfs(path).c_str(), &st) != 0) {
    err = last_error();
    return -1;
  }
  return static_cast<long>(st.st_size);
}

FsErr VfsRecFilePort::list(const char* dir, rec::RecDirVisitor& visit) {
  if (hal_sd_dead()) return FsErr::Disk;
  DIR* d = ::opendir(vfs(dir).c_str());
  if (!d) return last_error();
  errno = 0;
  while (const dirent* e = ::readdir(d)) {
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
    visit.entry(e->d_name, e->d_type == DT_DIR);
  }
  const FsErr err = errno == 0 ? FsErr::None : last_error();
  if (err != FsErr::Disk) ::closedir(d);
  return err;
}

FsErr VfsRecFilePort::unlink(const char* path) {
  if (hal_sd_dead()) return FsErr::Disk;
  return ::unlink(vfs(path).c_str()) == 0 ? FsErr::None : last_error();
}

FsErr VfsRecFilePort::mkdir(const char* path) {
  if (hal_sd_dead()) return FsErr::Disk;
  if (::mkdir(vfs(path).c_str(), 0755) == 0 || errno == EEXIST) return FsErr::None;
  return last_error();
}

}  // namespace cpa
