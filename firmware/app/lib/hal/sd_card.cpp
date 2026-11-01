#include "sd_card.h"

#include <SPI.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "fsinfo.h"
#include "sd_diskio.h"
#include "sd_health.h"
#include "sd_wake.h"
// Not used (the app never touches SDFS): it lets the chain-mode dependency finder scan
// SD.h / SD.cpp of the vendored library and find their FS dependency, without which
// SD.cpp, compiled as part of the library, misses vfs_api.h.
#include <SD.h>

namespace cpa {
namespace {

constexpr char kMount[] = "/sd";
using VfsPath = FixedStr<sizeof(kMount) - 1 + kSdPathMax>;

VfsPath vfs(const char* path) { return VfsPath::from(kMount).append(path); }

FatErr from_errno(int e) {
  switch (e) {
    case EIO:
    case ENODEV:
    case ENXIO: hal_sd_mark_dead(); return FatErr::Disk;
    case ENOENT: return FatErr::NotFound;
    case EEXIST: return FatErr::Exists;
    default: return FatErr::Other;
  }
}

FatErr last_error() { return from_errno(errno); }

#ifndef CPA_STORAGE_NO_SD
constexpr int kSck = 40;
constexpr int kMiso = 39;
constexpr int kMosi = 14;
constexpr int kCs = 12;
constexpr int kHz = 20000000;     // in spec, measured 5/5 (sd-card.md §4); no slower retry
constexpr int kInitHz = 400000;   // the driver's own init clock (sd_diskio.cpp)
constexpr uint8_t kPreamble = 10;  // bytes of clocks with chip select high (>= 74 clocks)
// The loop's outbox file and the audio task's one file (docs/11 §4.5, the peak): about
// 4 KB of heap each (FF_MAX_SS 4096). A spare third one does not fit the mount's 16 KB
// heap budget (device test_sd_store, docs/07 §6.7 #2).
constexpr uint8_t kMaxFiles = 2;
constexpr uint8_t kNoDrive = 0xFF;
constexpr uint32_t kSectorsPerMiB = 2048;

uint8_t s_pdrv = kNoDrive;

SdFacts facts(SdState state, uint32_t size_mib) {
  SdFacts f;
  f.state = state;
  f.size_mib = size_mib;
  return f;
}

// rec::SdSpi on the card's bus at the init clock: the power-up clocks with chip select
// high, then chip select low while it lives.
class InitBus : public rec::SdSpi {
 public:
  InitBus() {
    SPI.beginTransaction(SPISettings(kInitHz, MSBFIRST, SPI_MODE0));
    digitalWrite(kCs, HIGH);
    for (uint8_t i = 0; i < kPreamble; ++i) SPI.transfer(0xFF);
    digitalWrite(kCs, LOW);
  }
  ~InitBus() override {
    digitalWrite(kCs, HIGH);
    SPI.transfer(0xFF);  // the card lets go of its data line
    SPI.endTransaction();
  }
  InitBus(const InitBus&) = delete;
  InitBus& operator=(const InitBus&) = delete;
  uint8_t xfer(uint8_t out) override { return SPI.transfer(out); }
  uint32_t ms() override { return millis(); }
};

// Before the driver's first CMD0 (sd_wake.h). Its answer is not needed: the driver
// initialises the card next, and fails as it did before when sd_wake could not help.
void wake_card() {
  InitBus bus;
  (void)rec::sd_wake(bus);
}

SdFacts mount(SdStore& store) {
  SPI.begin(kSck, kMiso, kMosi, kCs);
  s_pdrv = sdcard_init(kCs, &SPI, kHz);  // registers the driver only
  if (s_pdrv == kNoDrive) return facts(SdState::Absent, 0);  // no drive slot: no card addressed
  wake_card();
  if (!sdcard_mount(s_pdrv, kMount, kMaxFiles, false)) {
    const sdcard_type_t type = sdcard_type(s_pdrv);
    return facts(type == CARD_NONE || type == CARD_UNKNOWN ? SdState::Absent : SdState::Unusable, 0);
  }
  const uint32_t mib = sdcard_num_sectors(s_pdrv) / kSectorsPerMiB;
  return facts(store.prepare() ? SdState::Ok : SdState::Unusable, mib);
}
#endif

}  // namespace

SdFacts hal_sd_mount(SdStore& store) {
#ifdef CPA_STORAGE_NO_SD
  (void)store;
  return SdFacts{};  // Disabled
#else
  return mount(store);
#endif
}

#ifndef CPA_STORAGE_NO_SD
namespace {

bool read_sector(uint32_t lba, uint8_t* buf) { return s_pdrv != kNoDrive && sd_read_raw(s_pdrv, buf, lba); }

}  // namespace
#endif

// Three sector reads at most: sector 0, the FAT32 boot sector, FSINFO (fsinfo.h).
Opt<uint64_t> hal_sd_free_bytes() {
#ifdef CPA_STORAGE_NO_SD
  return {};
#else
  static uint8_t sector[rec::kSectorBytes];  // setup() only, before the audio task starts
  if (!read_sector(0, sector)) return {};
  const Opt<uint32_t> boot_lba = rec::fat32_boot_lba(sector);
  if (!boot_lba.has || (boot_lba.value != 0 && !read_sector(boot_lba.value, sector))) return {};
  const Opt<rec::Fat32Boot> boot = rec::fat32_boot(sector, boot_lba.value);
  if (!boot.has || !read_sector(boot.value.fsinfo_lba, sector)) return {};
  return rec::fsinfo_free_bytes(sector, boot.value);
#endif
}

void hal_sd_end() {
#ifndef CPA_STORAGE_NO_SD
  if (s_pdrv == kNoDrive) return;
  sdcard_unmount(s_pdrv);
  sdcard_uninit(s_pdrv);
  s_pdrv = kNoDrive;
#endif
}

FatErr VfsFatPort::open_write(const char* path) {
  if (hal_sd_dead()) return FatErr::Disk;
  if (fd_ >= 0) ::close(fd_);
  fd_ = ::open(vfs(path).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  return fd_ >= 0 ? FatErr::None : last_error();
}

FatErr VfsFatPort::write(const char* data, size_t n) {
  if (hal_sd_dead()) return FatErr::Disk;
  if (fd_ < 0) return FatErr::Other;
  size_t done = 0;
  while (done < n) {
    const ssize_t w = ::write(fd_, data + done, n - done);
    if (w < 0) return last_error();
    if (w == 0) return FatErr::Other;  // the card is full
    done += static_cast<size_t>(w);
  }
  return FatErr::None;
}

FatErr VfsFatPort::sync_close() {
  if (hal_sd_dead()) return FatErr::Disk;
  if (fd_ < 0) return FatErr::Other;
  const int fd = fd_;
  fd_ = -1;
  if (::fsync(fd) != 0) {
    const FatErr e = last_error();
    if (e != FatErr::Disk) ::close(fd);
    return e;
  }
  return ::close(fd) == 0 ? FatErr::None : last_error();
}

long VfsFatPort::read(const char* path, char* buf, size_t cap, FatErr& err) {
  if (hal_sd_dead()) {
    err = FatErr::Disk;
    return -1;
  }
  const int fd = ::open(vfs(path).c_str(), O_RDONLY);
  if (fd < 0) {
    err = last_error();
    return -1;
  }
  size_t got = 0;
  ssize_t r = 0;
  while (got < cap && (r = ::read(fd, buf + got, cap - got)) > 0) got += static_cast<size_t>(r);
  err = r < 0 ? last_error() : FatErr::None;
  if (err != FatErr::Disk) ::close(fd);
  return err == FatErr::None ? static_cast<long>(got) : -1;
}

FatErr VfsFatPort::unlink(const char* path) {
  if (hal_sd_dead()) return FatErr::Disk;
  return ::unlink(vfs(path).c_str()) == 0 ? FatErr::None : last_error();
}

FatErr VfsFatPort::mkdir(const char* path) {
  if (hal_sd_dead()) return FatErr::Disk;
  if (::mkdir(vfs(path).c_str(), 0755) == 0 || errno == EEXIST) return FatErr::None;
  return last_error();
}

FatErr VfsFatPort::list(const char* dir, DirVisitor& visit) {
  if (hal_sd_dead()) return FatErr::Disk;
  DIR* d = ::opendir(vfs(dir).c_str());
  if (!d) return last_error();
  errno = 0;
  while (const dirent* e = ::readdir(d)) {
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
    visit.entry(e->d_name, e->d_type == DT_DIR);
  }
  const FatErr err = errno == 0 ? FatErr::None : last_error();
  if (err != FatErr::Disk) ::closedir(d);
  return err;
}

}  // namespace cpa
