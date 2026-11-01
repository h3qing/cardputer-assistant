// FakeFatPort (docs/09-m2-design.md §9.1): an in-memory FatPort for the SdStore tests.
// Physical files and directories live in maps keyed by path ("/" is the mount point and
// always exists). Semantics follow the VFS on FatFs as VfsFatPort maps them:
//   open_write creates or truncates (NotFound when the parent directory is missing);
//   sync_close commits what was written; mkdir of an existing name succeeds; list gives
//   direct children. Names are case-sensitive here (FAT is not; nothing relies on it).
// Test controls: fail_call(k, err) makes call number k (0-based, all methods counted)
// return err without effect; fail_all(call, err) does it for every call of one method.
// Every call is counted per method, and calls made while a list() is running (the card
// keeps one directory open at a time) are counted separately.
#pragma once

#include <cstring>
#include <map>
#include <set>
#include <string>

#include "fat_port.h"

namespace testsupport {

enum class FatCall : uint8_t { OpenWrite, Write, SyncClose, Read, Unlink, Mkdir, List, Count };

class FakeFatPort : public cpa::FatPort {
 public:
  using FatErr = cpa::FatErr;

  FakeFatPort() { dirs_.insert("/"); }

  FatErr open_write(const char* path) override {
    FatErr e = begin(FatCall::OpenWrite);
    if (e == FatErr::None) e = !dirs_.count(parent(path)) ? FatErr::NotFound : dirs_.count(path) ? FatErr::Other : e;
    if (e != FatErr::None) return e;
    files_[path] = "";  // the directory entry exists from open on, empty
    open_ = path;
    pending_.clear();
    return e;
  }

  FatErr write(const char* data, size_t n) override {
    const FatErr e = begin(FatCall::Write);
    if (e != FatErr::None) return e;
    if (open_.empty()) return FatErr::Other;
    pending_.append(data, n);
    return e;
  }

  FatErr sync_close() override {
    const FatErr e = begin(FatCall::SyncClose);
    if (e != FatErr::None) return e;
    if (open_.empty()) return FatErr::Other;
    files_[open_] = pending_;
    open_.clear();
    pending_.clear();
    return e;
  }

  long read(const char* path, char* buf, size_t cap, FatErr& err) override {
    err = begin(FatCall::Read);
    if (err == FatErr::None && !files_.count(path)) err = FatErr::NotFound;
    if (err != FatErr::None) return -1;
    const std::string& s = files_[path];
    const size_t n = s.size() < cap ? s.size() : cap;
    std::memcpy(buf, s.data(), n);
    return static_cast<long>(n);
  }

  FatErr unlink(const char* path) override {
    const FatErr e = begin(FatCall::Unlink);
    if (e != FatErr::None) return e;
    if (!files_.count(path)) return dirs_.count(path) ? FatErr::Other : FatErr::NotFound;
    files_.erase(path);
    return e;
  }

  FatErr mkdir(const char* path) override {
    const FatErr e = begin(FatCall::Mkdir);
    if (e != FatErr::None || dirs_.count(path) || files_.count(path)) return e;
    if (!dirs_.count(parent(path))) return FatErr::NotFound;
    dirs_.insert(path);
    return e;
  }

  FatErr list(const char* dir, cpa::DirVisitor& visit) override {
    const FatErr e = begin(FatCall::List);
    if (e != FatErr::None) return e;
    if (!dirs_.count(dir)) return FatErr::NotFound;
    std::map<std::string, bool> entries;  // name -> is_dir
    for (const auto& kv : files_) add_child(entries, dir, kv.first, false);
    for (const auto& d : dirs_) add_child(entries, dir, d, true);
    listing_ = true;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) visit.entry(it->first.c_str(), it->second);  // any order
    listing_ = false;
    return e;
  }

  // ---- test controls ----
  void fail_call(long k, FatErr err) { fail_at_[k] = err; }
  void fail_all(FatCall call, FatErr err) { fail_all_[call] = err; }
  long calls() const { return total_; }
  long calls(FatCall c) const { return count_.count(c) ? count_.at(c) : 0; }
  long calls_while_listing() const { return while_listing_; }
  void put(const std::string& path, const std::string& content) {
    for (size_t i = path.find('/', 1); i != std::string::npos; i = path.find('/', i + 1)) dirs_.insert(path.substr(0, i));
    files_[path] = content;
  }
  void put_dir(const std::string& path) {
    put(path + "/x", "");
    files_.erase(path + "/x");
  }
  bool has(const std::string& path) const { return files_.count(path) != 0; }
  bool has_dir(const std::string& path) const { return dirs_.count(path) != 0; }
  std::string get(const std::string& path) const { return has(path) ? files_.at(path) : std::string(); }
  size_t file_count() const { return files_.size(); }

 private:
  static std::string parent(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == 0 || slash == std::string::npos ? std::string("/") : path.substr(0, slash);
  }

  static void add_child(std::map<std::string, bool>& out, const std::string& dir, const std::string& path, bool is_dir) {
    if (path == "/" || parent(path) != dir) return;
    out[path.substr(path.rfind('/') + 1)] = is_dir;
  }

  FatErr begin(FatCall c) {
    const long k = total_++;
    count_[c]++;
    while_listing_ += listing_ ? 1 : 0;
    if (fail_at_.count(k)) return fail_at_[k];
    return fail_all_.count(c) ? fail_all_[c] : FatErr::None;
  }

  std::map<std::string, std::string> files_;
  std::set<std::string> dirs_;
  std::string open_;
  std::string pending_;
  std::map<long, FatErr> fail_at_;
  std::map<FatCall, FatErr> fail_all_;
  std::map<FatCall, long> count_;
  long total_ = 0;
  long while_listing_ = 0;
  bool listing_ = false;
};

}  // namespace testsupport
