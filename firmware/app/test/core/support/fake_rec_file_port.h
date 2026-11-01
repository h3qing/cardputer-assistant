// FakeRecFilePort (docs/11 §4.5-§4.6, §11): an in-memory RecFilePort with power cuts.
// Each file has its current content (what reads see) and its durable content (what a
// power cut leaves): write() and pwrite_at0() change only the current content; fsync()
// and close() make it durable; open_trunc() creates or truncates the entry durably (the
// worst case for recovery: an empty file survives); unlink() and mkdir() are durable at
// once. cut_at(k) makes call k and every later call fail with Disk and no effect, as if
// the power went off just before it; reboot() then rolls every file back to its durable
// content, which is what the next boot finds.
// It also checks the invariant of §4.5: at most one file open at a time.
#pragma once

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "rec_file_port.h"

namespace testsupport {

class FakeRecFilePort : public cpa::rec::RecFilePort {
 public:
  using FsErr = cpa::rec::FsErr;

  FakeRecFilePort() { dirs_.insert("/"); }

  FsErr open_trunc(const char* path) override {
    FsErr e = begin_call(true);
    if (e != FsErr::None) return e;
    if (!dirs_.count(parent(path))) return FsErr::NotFound;
    files_[path] = File{};
    return opened(path);
  }

  FsErr open_rw(const char* path) override { return open_existing(path); }
  // Created durably (empty) when missing, as open_trunc; an existing file keeps its content.
  FsErr open_append(const char* path) override {
    FsErr e = begin_call(true);
    if (e != FsErr::None) return e;
    if (!dirs_.count(parent(path))) return FsErr::NotFound;
    if (!files_.count(path)) files_[path] = File{};
    return opened(path);
  }
  FsErr open_read(const char* path) override {
    ++read_opens[path];
    return open_existing(path);
  }

  FsErr write(const uint8_t* data, size_t n) override {
    const FsErr e = begin_call(false);
    if (e != FsErr::None) return e;
    if (open_.empty()) return FsErr::Other;
    if (full_after_ >= 0 && static_cast<long>(files_[open_].now.size() + n) > full_after_) return FsErr::Full;
    if (tear_ >= 0) return torn(data, n);
    files_[open_].now.append(reinterpret_cast<const char*>(data), n);
    bytes_written += n;
    return e;
  }

  FsErr fsync() override {
    const FsErr e = begin_call(false);
    if (e != FsErr::None) return e;
    if (open_.empty()) return FsErr::Other;
    files_[open_].durable = files_[open_].now;
    ++fsyncs;
    return e;
  }

  FsErr pwrite_at0(const uint8_t* data, size_t n) override {
    const FsErr e = begin_call(false);
    if (e != FsErr::None) return e;
    if (open_.empty()) return FsErr::Other;
    std::string& s = files_[open_].now;
    if (s.size() < n) s.resize(n);
    std::memcpy(&s[0], data, n);
    return e;
  }

  FsErr close() override {
    const FsErr e = begin_call(false);
    if (e != FsErr::None) return e;
    if (open_.empty()) return FsErr::Other;
    files_[open_].durable = files_[open_].now;
    open_.clear();
    return e;
  }

  long read_at(uint32_t off, uint8_t* buf, size_t n, FsErr& err) override {
    err = begin_call(false);
    if (err == FsErr::None && open_.empty()) err = FsErr::Other;
    if (err != FsErr::None) return -1;
    const std::string& s = files_[open_].now;
    if (off >= s.size()) return 0;
    const size_t take = s.size() - off < n ? s.size() - off : n;
    std::memcpy(buf, s.data() + off, take);
    ++reads;
    return static_cast<long>(take);
  }

  long size(const char* path, FsErr& err) override {
    err = begin_call(false);
    if (err == FsErr::None && !files_.count(path)) err = FsErr::NotFound;
    return err == FsErr::None ? static_cast<long>(files_[path].now.size()) : -1;
  }

  FsErr list(const char* dir, cpa::rec::RecDirVisitor& visit) override {
    const FsErr e = begin_call(false);
    if (e != FsErr::None) return e;
    if (!dirs_.count(dir)) return FsErr::NotFound;
    std::map<std::string, bool> kids;
    for (const auto& kv : files_) add_child(kids, dir, kv.first, false);
    for (const auto& d : dirs_) add_child(kids, dir, d, true);
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) visit.entry(it->first.c_str(), it->second);
    return e;
  }

  FsErr unlink(const char* path) override {
    const FsErr e = begin_call(false);
    if (e != FsErr::None) return e;
    if (open_ == path) ++unlink_open;
    return files_.erase(path) ? FsErr::None : FsErr::NotFound;
  }

  FsErr mkdir(const char* path) override {
    const FsErr e = begin_call(false);
    if (e != FsErr::None || dirs_.count(path)) return e;
    if (!dirs_.count(parent(path))) return FsErr::NotFound;
    dirs_.insert(path);
    return e;
  }

  // ---- test controls ----
  void cut_at(long k) { cut_ = k; }
  void fail_next(FsErr e) { fail_next_ = e; }  // the next call fails with e, no effect
  void full_after(long bytes) { full_after_ = bytes; }  // writes past this file size: Full
  // docs/28 §9: the next write lands only its first `keep` bytes, durably, then the power goes
  // (every later call fails as after cut_at): a torn append.
  void tear_next_write(size_t keep) { tear_ = static_cast<long>(keep); }
  void reboot() {
    for (auto& kv : files_) kv.second.now = kv.second.durable;
    open_.clear();
    cut_ = -1;
  }
  long calls() const { return calls_; }
  bool has(const std::string& p) const { return files_.count(p) != 0; }
  std::string get(const std::string& p) const { return has(p) ? files_.at(p).now : std::string(); }
  void put(const std::string& p, const std::string& content) {
    for (size_t i = p.find('/', 1); i != std::string::npos; i = p.find('/', i + 1)) dirs_.insert(p.substr(0, i));
    files_[p] = File{content, content};
  }
  size_t file_count() const { return files_.size(); }
  std::set<std::string> paths() const {
    std::set<std::string> out;
    for (const auto& kv : files_) out.insert(kv.first);
    return out;
  }
  bool is_open() const { return !open_.empty(); }

  int max_open = 0;       // most files open at once (the invariant: 1)
  int double_open = 0;    // open_* while a file was open
  int unlink_open = 0;    // unlink of the open file
  long bytes_written = 0;
  int fsyncs = 0;
  int reads = 0;
  std::map<std::string, int> read_opens;  // open_read calls by path

 private:
  struct File {
    std::string now;
    std::string durable;
  };

  FsErr begin_call(bool opening) {
    const long k = calls_++;
    if (cut_ >= 0 && k >= cut_) return FsErr::Disk;
    if (fail_next_ != FsErr::None) {
      const FsErr e = fail_next_;
      fail_next_ = FsErr::None;
      return e;
    }
    if (opening && !open_.empty()) ++double_open;
    return FsErr::None;
  }

  FsErr torn(const uint8_t* data, size_t n) {
    const size_t keep = static_cast<size_t>(tear_) < n ? static_cast<size_t>(tear_) : n;
    files_[open_].now.append(reinterpret_cast<const char*>(data), keep);
    files_[open_].durable = files_[open_].now;
    tear_ = -1;
    cut_ = calls_;
    return FsErr::Disk;
  }

  FsErr open_existing(const char* path) {
    const FsErr e = begin_call(true);
    if (e != FsErr::None) return e;
    if (!files_.count(path)) return FsErr::NotFound;
    return opened(path);
  }

  FsErr opened(const char* path) {
    open_ = path;
    max_open = max_open < 1 ? 1 : max_open;
    if (double_open > 0) max_open = 2;
    return FsErr::None;
  }

  static std::string parent(const std::string& p) {
    const auto slash = p.rfind('/');
    return slash == 0 || slash == std::string::npos ? std::string("/") : p.substr(0, slash);
  }

  static void add_child(std::map<std::string, bool>& kids, const std::string& dir, const std::string& p, bool is_dir) {
    const std::string prefix = dir == "/" ? "/" : dir + "/";
    if (p.size() <= prefix.size() || p.compare(0, prefix.size(), prefix) != 0) return;
    const std::string rest = p.substr(prefix.size());
    if (rest.find('/') == std::string::npos) kids[rest] = is_dir;
  }

  std::map<std::string, File> files_;
  std::set<std::string> dirs_;
  std::string open_;
  long calls_ = 0;
  long cut_ = -1;
  long full_after_ = -1;
  long tear_ = -1;
  FsErr fail_next_ = FsErr::None;
};

// A stand-in SHA-256: a deterministic 32-byte digest of the bytes fed in (FNV-1a lanes),
// enough to check that the writer hashes exactly the PCM data of each segment.
class FakeSha256 : public cpa::rec::Sha256Port {
 public:
  void begin() override {
    for (int i = 0; i < 4; ++i) lanes_[i] = 1469598103934665603ull + i;
    n_ = 0;
  }
  void update(const uint8_t* d, size_t n) override {
    for (size_t i = 0; i < n; ++i, ++n_) lanes_[n_ & 3] = (lanes_[n_ & 3] ^ d[i]) * 1099511628211ull;
  }
  cpa::Sha256Hex finish() override {
    char hex[65];
    for (int i = 0; i < 4; ++i) std::snprintf(hex + 16 * i, 17, "%016llx", static_cast<unsigned long long>(lanes_[i]));
    return cpa::Sha256Hex::from(hex, 64);
  }
  static cpa::Sha256Hex of(const std::string& data) {
    FakeSha256 h;
    h.begin();
    h.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return h.finish();
  }

 private:
  unsigned long long lanes_[4] = {};
  size_t n_ = 0;
};

}  // namespace testsupport
