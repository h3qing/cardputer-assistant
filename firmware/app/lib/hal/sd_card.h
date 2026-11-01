// SD card on the device (docs/09-m2-design.md §3.9): the only code that touches the SD
// library (the patched copy in firmware/third_party/SD, sdcard_* functions only) and the
// FAT VFS. Card on FSPI: SCK 40, MISO 39, MOSI 14, CS 12; mounted at /sd.
//
// hal_sd_mount(store), once per boot:
//   1. CPA_STORAGE_NO_SD defined: Disabled, the bus is never touched;
//   2. SPI.begin(40, 39, 14, 12); sdcard_init(12, &SPI, 20 MHz) registers the driver
//      (the card is not touched yet);
//   2a. v0.3 (docs/07 §6.7 #3): rec::sd_wake at the 400 kHz init clock, one CMD0; a card
//      that does not answer it may still be inside a transfer a reset cut off, which
//      sd_wake ends (about 12 ms more without a card);
//   3. sdcard_mount(pdrv, "/sd", max_files 2, never format): the card initialises here
//      (docs/11 §4.5: the loop's outbox file and the audio task's one file, the peak; a
//      third slot would cost 4.1 KB of heap the mount does not have, docs/07 §6.7 #2);
//   4. failed: sdcard_type() tells CARD_NONE / CARD_UNKNOWN (Absent) from an initialised
//      card without a usable FAT, e.g. exFAT (Unusable). No sdcard_uninit (another CMD0,
//      300+ ms without a card) and no retry;
//   5. mounted: size_mib from the sector count, then store.prepare() (mkdir + probe
//      write); Ok, or Unusable when prepare fails.
// VfsFatPort: FatPort over ::open / ::write / ::fsync / ... on "/sd" + path, errno mapped
// to FatErr (EIO / ENODEV / ENXIO -> Disk, ENOENT -> NotFound, EEXIST -> Exists, others
// -> Other). After a Disk error it does not even close the descriptor: a dead card is not
// touched again (SdStore latches on the same error). v0.3: the Disk error also sets the
// shared sd_health flag, and a dead card answers Disk on both tasks (sd_health.h).
// hal_sd_free_bytes(): the bounded FSINFO read of docs/11 §4.3 (three sectors).
#pragma once

#include "fat_port.h"
#include "opt.h"
#include "sd_store.h"
#include "storage_plan.h"

namespace cpa {

class VfsFatPort : public FatPort {
 public:
  FatErr open_write(const char* path) override;
  FatErr write(const char* data, size_t n) override;
  FatErr sync_close() override;
  long read(const char* path, char* buf, size_t cap, FatErr& err) override;
  FatErr unlink(const char* path) override;
  FatErr mkdir(const char* path) override;
  FatErr list(const char* dir, DirVisitor& visit) override;

 private:
  int fd_ = -1;
};

SdFacts hal_sd_mount(SdStore& store);
// Free bytes from the card's FSINFO sector, right after the mount (absent: unknown).
Opt<uint64_t> hal_sd_free_bytes();
// Unmounts and releases the driver, so a device test can mount again. Never called by
// the firmware itself (it mounts once per boot).
void hal_sd_end();

}  // namespace cpa
