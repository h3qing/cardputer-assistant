// FakeStore: in-memory FileStore with LittleFS-like semantics and power-loss injection
// (05 §9.2). Primitive operations are open_write, write, close, rename and remove.
// crash_before(k) makes operation number k (0-based) and everything after it fail,
// as if power was cut just before it. power_cycle() then models the reboot:
//   - a file opened for writing exists (created at open) with its last committed
//     content (empty for a new file); data written but not closed is lost;
//   - rename and remove are atomic.
// FAT mode (set_fat(true), docs/09-m2-design.md §9.1): atomic_rename() is false and a
// rename onto an existing target fails (EEXIST). rename calls are counted in both modes,
// so the tests can assert that the outbox never renames on FAT.
// Shared power (share_power): several stores on one PowerRail count their operations
// together, and crash_before(k) cuts power to all of them at global operation k.
#pragma once

#include <cstring>
#include <map>
#include <set>
#include <string>

#include "file_store.h"

namespace testsupport {

struct PowerRail {
  long ops = 0;
  long crash_at = -1;
  bool crashed = false;
};

class FakeStore : public cpa::FileStore {
 public:
  bool open_write(const char* path) override {
    if (!step()) return false;
    open_ = path;
    pending_.clear();
    files_[open_] = "";  // created (and truncated) at open, like lfs_file_open
    return true;
  }

  bool write(const char* data, size_t n) override {
    if (!step() || open_.empty()) return false;
    pending_.append(data, n);
    return true;
  }

  bool close() override {
    if (!step() || open_.empty()) return false;
    files_[open_] = pending_;
    open_.clear();
    pending_.clear();
    return true;
  }

  bool rename(const char* from, const char* to) override {
    ++renames_;
    if (!step() || !files_.count(from)) return false;
    if (fat_ && files_.count(to)) return false;  // FAT: no replacing rename
    files_[to] = files_[from];
    files_.erase(from);
    return true;
  }

  bool remove(const char* path) override {
    if (!step() || !files_.count(path)) return false;
    files_.erase(path);
    return true;
  }

  long read(const char* path, char* buf, size_t cap) override {
    if (rail().crashed || !files_.count(path) || unreadable_.count(path)) return -1;
    const std::string& s = files_[path];
    const size_t n = s.size() < cap ? s.size() : cap;
    std::memcpy(buf, s.data(), n);
    return static_cast<long>(n);
  }

  bool list(const char* dir, cpa::DirVisitor& visit) override {
    if (rail().crashed) return false;
    const std::string prefix = std::string(dir) + "/";
    std::map<std::string, bool> entries;  // name -> is_dir
    for (const auto& kv : files_) {
      if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
      const std::string rest = kv.first.substr(prefix.size());
      const auto slash = rest.find('/');
      entries[rest.substr(0, slash)] = slash != std::string::npos;
    }
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) visit.entry(it->first.c_str(), it->second);  // any order
    return true;
  }

  bool mkdir(const char*) override { return !rail().crashed; }
  uint32_t free_bytes() override { return free_; }
  bool atomic_rename() const override { return !fat_; }

  // ---- test controls ----
  void crash_before(long op) { rail().crash_at = op; }
  bool crashed() const { return rail().crashed; }
  long ops() const { return rail().ops; }
  void set_free(uint32_t bytes) { free_ = bytes; }
  void set_fat(bool fat) { fat_ = fat; }
  long renames() const { return renames_; }
  void share_power(PowerRail& rail) { shared_ = &rail; }
  void power_cycle() {
    open_.clear();
    pending_.clear();
    rail().crashed = false;
    rail().crash_at = -1;
  }
  void put(const std::string& path, const std::string& content) { files_[path] = content; }
  void fail_read(const std::string& path) { unreadable_.insert(path); }  // an I/O error, not bad content
  bool has(const std::string& path) const { return files_.count(path) != 0; }
  std::string get(const std::string& path) const { return has(path) ? files_.at(path) : std::string(); }
  size_t count_in(const std::string& dir, const std::string& suffix) const {
    size_t n = 0;
    const std::string prefix = dir + "/";
    for (const auto& kv : files_) {
      const std::string& p = kv.first;
      const bool direct = p.compare(0, prefix.size(), prefix) == 0 && p.find('/', prefix.size()) == std::string::npos;
      if (direct && p.size() >= suffix.size() && p.compare(p.size() - suffix.size(), suffix.size(), suffix) == 0) ++n;
    }
    return n;
  }

 private:
  PowerRail& rail() { return shared_ ? *shared_ : own_; }
  const PowerRail& rail() const { return shared_ ? *shared_ : own_; }

  bool step() {
    PowerRail& r = rail();
    if (r.crashed) return false;
    if (r.crash_at >= 0 && r.ops >= r.crash_at) {
      r.crashed = true;
      return false;
    }
    ++r.ops;
    return true;
  }

  std::map<std::string, std::string> files_;
  std::set<std::string> unreadable_;
  std::string open_;
  std::string pending_;
  PowerRail own_;
  PowerRail* shared_ = nullptr;
  long renames_ = 0;
  bool fat_ = false;
  uint32_t free_ = 1024 * 1024;
};

}  // namespace testsupport
