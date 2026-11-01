#include "lfs_store.h"

#include <LittleFS.h>
#include <dirent.h>
#include <esp_partition.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "fixed_str.h"
#include "storage_plan.h"

namespace cpa {
namespace {

constexpr char kBase[] = "/littlefs";
constexpr char kLabel[] = "cpafs";

constexpr char kOutbox[] = "/outbox";
constexpr size_t kOutboxLen = sizeof(kOutbox) - 1;
constexpr size_t kStemLen = 18;  // MonoId
constexpr size_t kMaxShards = 32;
constexpr uint8_t kV01Shards = 32;

using FullPath = FixedStr<80>;

FullPath full(const char* path) { return FullPath::from(kBase).append(path); }

// The shard of "/outbox/<MonoId>.<ext>", or 0 for any other path. 32 shards: v0.1's
// mapping, unchanged; 4: the last digit's Crockford value mod 4.
char shard_of(const char* path, uint8_t shards) {
  if (std::strncmp(path, kOutbox, kOutboxLen) != 0 || path[kOutboxLen] != '/') return 0;
  const char* name = path + kOutboxLen + 1;
  const char* dot = std::strrchr(name, '.');
  if (std::strchr(name, '/') || !dot || static_cast<size_t>(dot - name) != kStemLen) return 0;
  const char last = name[kStemLen - 1];
  return shards == kV01Shards ? last : lfs_shard(last, shards);
}

FullPath shard_dir(char shard) {
  const char tail[3] = {'_', shard, '\0'};
  return full(kOutbox).append("/").append(tail);
}

FullPath physical(const char* path, uint8_t shards) {
  const char shard = shard_of(path, shards);
  return shard ? shard_dir(shard).append(path + kOutboxLen) : full(path);
}

bool is_shard_name(const char* name) { return name[0] == '_' && name[1] != '\0' && name[2] == '\0'; }

}  // namespace

bool LfsStore::begin() {
  const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kLabel);
  if (!part) return false;  // no cpafs: internal storage disabled, no other partition used
  shards_ = lfs_shards(part->size);
  return LittleFS.begin(true, kBase, 4, kLabel);  // formats cpafs only, and only if it fails to mount
}

void LfsStore::end() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  LittleFS.end();
}

bool LfsStore::open_write(const char* path) {
  if (fd_ >= 0) ::close(fd_);
  const FullPath p = physical(path, shards_);
  fd_ = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  const char shard = shard_of(path, shards_);
  if (fd_ < 0 && errno == ENOENT && shard) {  // first item of this shard
    ::mkdir(shard_dir(shard).c_str(), 0755);
    fd_ = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }
  return fd_ >= 0;
}

bool LfsStore::write(const char* data, size_t n) {
  if (fd_ < 0) return false;
  size_t done = 0;
  while (done < n) {
    const ssize_t w = ::write(fd_, data + done, n - done);
    if (w <= 0) return false;
    done += static_cast<size_t>(w);
  }
  return true;
}

bool LfsStore::close() {
  if (fd_ < 0) return false;
  const bool synced = ::fsync(fd_) == 0;
  const bool closed = ::close(fd_) == 0;
  fd_ = -1;
  return synced && closed;
}

bool LfsStore::rename(const char* from, const char* to) {
  const FullPath a = physical(from, shards_);
  const FullPath b = physical(to, shards_);
  return ::rename(a.c_str(), b.c_str()) == 0;
}

bool LfsStore::remove(const char* path) { return ::unlink(physical(path, shards_).c_str()) == 0; }

long LfsStore::read(const char* path, char* buf, size_t cap) {
  const int fd = ::open(physical(path, shards_).c_str(), O_RDONLY);
  if (fd < 0) return -1;
  size_t got = 0;
  ssize_t r = 0;
  while (got < cap && (r = ::read(fd, buf + got, cap - got)) > 0) got += static_cast<size_t>(r);
  ::close(fd);
  return r < 0 ? -1 : static_cast<long>(got);
}

bool LfsStore::list(const char* dir, DirVisitor& visit) {
  const bool outbox = std::strcmp(dir, kOutbox) == 0;
  DIR* d = ::opendir(full(dir).c_str());
  if (!d) return false;
  char shards[kMaxShards];
  size_t n = 0;
  while (const dirent* e = ::readdir(d)) {
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
    const bool is_dir = e->d_type == DT_DIR;
    if (outbox && is_dir && is_shard_name(e->d_name)) {
      if (n < kMaxShards) shards[n++] = e->d_name[1];
      continue;
    }
    visit.entry(e->d_name, is_dir);
  }
  ::closedir(d);
  for (size_t i = 0; i < n; ++i) list_shard(shard_dir(shards[i]).c_str(), visit);
  return true;
}

// The files of one shard, reported as children of /outbox (one directory open at a time).
void LfsStore::list_shard(const char* shard_dir, DirVisitor& visit) {
  DIR* d = ::opendir(shard_dir);
  if (!d) return;
  while (const dirent* e = ::readdir(d)) {
    if (e->d_type != DT_DIR) visit.entry(e->d_name, false);
  }
  ::closedir(d);
}

bool LfsStore::mkdir(const char* path) { return ::mkdir(full(path).c_str(), 0755) == 0 || errno == EEXIST; }

uint32_t LfsStore::free_bytes() {
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  return used < total ? static_cast<uint32_t>(total - used) : 0;
}

uint32_t LfsStore::total_bytes() { return static_cast<uint32_t>(LittleFS.totalBytes()); }

}  // namespace cpa
