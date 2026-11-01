#include "store_commit.h"

#include "protocol_limits.h"

namespace cpa {
namespace {

// open(path) -> write -> close (fsync); any failure removes the half-written file. An
// opened file is always closed first: FatFs must never unlink a file that is open.
bool fat_commit(FileStore& fs, const char* path, const char* data, size_t n) {
  if (fs.open_write(path)) {
    const bool written = fs.write(data, n);
    if (fs.close() && written) return true;
  }
  fs.remove(path);
  return false;
}

// v0.1 exactly: open(tmp) -> write -> close (commit) -> rename to path.
bool atomic_commit(FileStore& fs, const char* path, const char* tmp, const char* data, size_t n) {
  if (!fs.open_write(tmp)) return false;
  const bool written = fs.write(data, n);
  if (!fs.close() || !written) {
    fs.remove(tmp);
    return false;
  }
  return fs.rename(tmp, path);
}

}  // namespace

bool commit_file(FileStore& fs, const char* path, const char* tmp, const char* data, size_t n) {
  return fs.atomic_rename() ? atomic_commit(fs, path, tmp, data, n) : fat_commit(fs, path, data, n);
}

bool move_file(FileStore& fs, const char* from, const char* to) {
  if (fs.atomic_rename()) return fs.rename(from, to);
  // Static, not on the 16 KB loop stack (05 §5.13): one outbox line plus one byte to
  // tell a larger file, which is never truncated into the target.
  static char buf[limits::kLineMax + 1];
  const long n = fs.read(from, buf, sizeof buf);
  if (n < 0 || static_cast<size_t>(n) > limits::kLineMax) return false;
  return fat_commit(fs, to, buf, static_cast<size_t>(n)) && fs.remove(from);
}

}  // namespace cpa
