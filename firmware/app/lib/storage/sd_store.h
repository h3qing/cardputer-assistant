// SdStore (docs/09-m2-design.md §3.3, §3.5, §3.8): FileStore on the SD card's FAT file
// system, through a FatPort (hal's VfsFatPort on the device, FakeFatPort in tests). It
// only uses <root>/ on the card (/cpa; the device tests use another root):
//   - paths map as in sd_paths.h: outbox items become 8.3 names flat per epoch, and
//     list("/outbox") / list("/outbox/bad") fold them back into "<MonoId>.json"
//     (at most 8 epoch directories, listed one after the other, never nested);
//   - open_write creates missing parent directories level by level, then retries once;
//   - no rename (FAT's is neither atomic nor replacing): rename() fails without touching
//     the card and atomic_rename() is false, so the outbox commits by writing the final
//     name and fsync (store_commit.h);
//   - the first Disk error (card pulled or broken) latches: from then on every method
//     fails at once and never calls the port again, until reboot, so a pulled card costs
//     one stalled call instead of one per loop pass;
//   - free_bytes() never scans the FAT: 0xFFFFFFFF, or 0 once latched.
// This is an I/O shell object: the latch and the open-file flag are its only state.
#pragma once

#include <cstdint>

#include "fat_port.h"
#include "file_store.h"
#include "sd_paths.h"

namespace cpa {

class SdStore : public FileStore {
 public:
  explicit SdStore(FatPort& port, const char* root = "/cpa");

  // Creates <root> and <root>/outbox, then probes a write: <root>/PROBE.BIN gets one
  // byte, fsync + close, and is deleted. mkdir alone succeeds on a full or read-only
  // card; the probe shows whether saves would work.
  bool prepare();
  bool failed() const { return failed_; }
  bool atomic_rename() const override { return false; }

  bool open_write(const char* path) override;
  bool write(const char* data, size_t n) override;
  bool close() override;
  bool rename(const char* from, const char* to) override;
  bool remove(const char* path) override;
  long read(const char* path, char* buf, size_t cap) override;
  bool list(const char* dir, DirVisitor& visit) override;
  bool mkdir(const char* path) override;
  uint32_t free_bytes() override;

 private:
  bool ok(FatErr e);  // latches on Disk; true only for None
  Opt<SdPath> physical(const char* logical) const;
  bool make_parents(const SdPath& path);
  bool list_items(const SdPath& dir, bool top, DirVisitor& visit);

  FatPort& port_;
  FixedStr<kSdRootMax> root_;
  bool failed_ = false;
  bool open_ = false;
};

}  // namespace cpa
