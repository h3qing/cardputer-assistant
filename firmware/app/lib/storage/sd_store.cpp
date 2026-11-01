#include "sd_store.h"

#include <cstring>

namespace cpa {
namespace {

constexpr char kOutbox[] = "/outbox";
constexpr char kOutboxBad[] = "/outbox/bad";
constexpr char kProbe[] = "/PROBE.BIN";
constexpr char kBadName[] = "BAD";
constexpr uint8_t kEpochDirsMax = 8;
constexpr size_t kEpochLen = 7;

struct EpochDirs {
  uint8_t n = 0;
  char names[kEpochDirsMax][kEpochLen + 1] = {};
};

// One pass over <root>/outbox (or BAD): epoch directories are only noted, to be listed
// after this listing ends; BAD is reported as "bad"; anything else as it is.
class TopVisitor : public DirVisitor {
 public:
  TopVisitor(DirVisitor& out, bool top, EpochDirs& epochs) : out_(out), top_(top), epochs_(epochs) {}

  void entry(const char* name, bool is_dir) override {
    if (is_dir && top_ && std::strcmp(name, kBadName) == 0) return out_.entry("bad", true);
    if (!is_dir || !sd_is_epoch_dir(name)) return out_.entry(name, is_dir);
    if (epochs_.n >= kEpochDirsMax) return;  // left on the card, not listed
    std::memcpy(epochs_.names[epochs_.n++], name, kEpochLen + 1);
  }

 private:
  DirVisitor& out_;
  bool top_;
  EpochDirs& epochs_;
};

// The files of one epoch directory, 8.3 item names reported as "<MonoId>.json".
class EpochVisitor : public DirVisitor {
 public:
  EpochVisitor(DirVisitor& out, const char* epoch) : out_(out), epoch_(epoch) {}

  void entry(const char* name, bool is_dir) override {
    const Opt<SdItemName> item = is_dir ? Opt<SdItemName>{} : sd_item_name(epoch_, name);
    out_.entry(item.has ? item.value.c_str() : name, is_dir);
  }

 private:
  DirVisitor& out_;
  const char* epoch_;
};

}  // namespace

SdStore::SdStore(FatPort& port, const char* root)
    : port_(port), root_(root && std::strlen(root) <= kSdRootMax ? FixedStr<kSdRootMax>::from(root) : FixedStr<kSdRootMax>{}) {}

bool SdStore::ok(FatErr e) {
  if (e == FatErr::Disk) failed_ = true;
  return e == FatErr::None;
}

Opt<SdPath> SdStore::physical(const char* logical) const { return sd_physical(root_.c_str(), logical); }

bool SdStore::prepare() {
  const Opt<SdPath> outbox = physical(kOutbox);
  const Opt<SdPath> probe = physical(kProbe);
  if (failed_ || !outbox.has || !probe.has) return false;
  if (!ok(port_.mkdir(root_.c_str())) || !ok(port_.mkdir(outbox.value.c_str()))) return false;
  if (!ok(port_.open_write(probe.value.c_str()))) return false;
  const bool written = ok(port_.write("1", 1));
  if (failed_ || !ok(port_.sync_close()) || !written) return false;
  return ok(port_.unlink(probe.value.c_str()));
}

bool SdStore::make_parents(const SdPath& path) {
  for (size_t i = 1; i < path.size(); ++i) {
    if (path.c_str()[i] != '/') continue;
    if (!ok(port_.mkdir(SdPath::from(path.c_str(), i).c_str()))) return false;
  }
  return true;
}

bool SdStore::open_write(const char* path) {
  const Opt<SdPath> p = physical(path);
  if (failed_ || !p.has) return false;
  FatErr e = port_.open_write(p.value.c_str());
  if (e == FatErr::NotFound && make_parents(p.value)) e = port_.open_write(p.value.c_str());
  open_ = ok(e);
  return open_;
}

bool SdStore::write(const char* data, size_t n) { return !failed_ && open_ && ok(port_.write(data, n)); }

bool SdStore::close() {
  if (failed_ || !open_) return false;
  open_ = false;
  return ok(port_.sync_close());
}

bool SdStore::rename(const char*, const char*) { return false; }

bool SdStore::remove(const char* path) {
  const Opt<SdPath> p = physical(path);
  return !failed_ && p.has && ok(port_.unlink(p.value.c_str()));
}

long SdStore::read(const char* path, char* buf, size_t cap) {
  const Opt<SdPath> p = physical(path);
  if (failed_ || !p.has) return -1;
  FatErr e = FatErr::None;
  const long n = port_.read(p.value.c_str(), buf, cap, e);
  return ok(e) ? n : -1;
}

bool SdStore::list(const char* dir, DirVisitor& visit) {
  const Opt<SdPath> p = physical(dir);
  if (failed_ || !p.has) return false;
  const bool top = std::strcmp(dir, kOutbox) == 0;
  if (top || std::strcmp(dir, kOutboxBad) == 0) return list_items(p.value, top, visit);
  return ok(port_.list(p.value.c_str(), visit));
}

// Lists `dir`, then each epoch directory found in it: one directory open at a time.
bool SdStore::list_items(const SdPath& dir, bool top, DirVisitor& visit) {
  EpochDirs epochs;
  TopVisitor tv(visit, top, epochs);
  if (!ok(port_.list(dir.c_str(), tv))) return false;
  for (uint8_t i = 0; i < epochs.n && !failed_; ++i) {
    EpochVisitor ev(visit, epochs.names[i]);
    ok(port_.list(dir.append("/").append(epochs.names[i]).c_str(), ev));
  }
  return !failed_;
}

bool SdStore::mkdir(const char* path) {
  const Opt<SdPath> p = physical(path);
  return !failed_ && p.has && ok(port_.mkdir(p.value.c_str()));
}

uint32_t SdStore::free_bytes() { return failed_ ? 0u : 0xFFFFFFFFu; }

}  // namespace cpa
