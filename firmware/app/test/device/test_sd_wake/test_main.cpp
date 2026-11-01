// A card left inside a cut-off write mounts again (docs/07 §6.7 #3, sd_wake.h). HIL saw
// it once in five: debug.reboot right after a segment change, the next boot mounted no
// card (store=int) and had no recording. Here the test cuts a multi-block write (CMD25)
// the way that reset does, drops the driver (hal_sd_end, as a reboot forgets it) and
// mounts again: Ok, and the file reads back as the card took it. Two cuts: between two
// blocks, and 100 bytes into the second block (that block fails its CRC: not written).
// Needs the FAT32 card; everything stays below /cpatest, removed at the end.
// Safety: the raw write goes only to the first two sectors of the test's own file, found
// through the FAT, and only after that first sector has been read back holding the file's
// bytes. Prints CPA_SD_WAKE with the mount time of each case.
#include <Arduino.h>
#include <SPI.h>
#include <dirent.h>
#include <unity.h>

#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "usb_transport.h"  // listed here so the dependency finder sees it
#include "device_harness.h"
#include "ff.h"
#include "../test_rec_budget/rec_harness.h"  // remove_tree
#include "sd_card.h"
#include "sd_diskio.h"
#include "sd_health.h"
#include "sd_store.h"

extern "C" {
char CRC7(const char* data, int length);  // third_party/SD sd_diskio_crc.c
unsigned short CRC16(const char* data, int length);
}

namespace {

constexpr char kRoot[] = "/cpatest";
constexpr char kVfsRoot[] = "/sd/cpatest";
constexpr char kFile[] = "/sd/cpatest/cut.bin";
constexpr char kFatFile[] = "0:/cpatest/cut.bin";  // the driver's drive 0 (one card)
constexpr int kCs = 12;
constexpr uint32_t kRawHz = 4000000;
constexpr size_t kFileBytes = 64 * 1024;
constexpr size_t kSector = 512;

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_port;
cpa::SdStore g_store(g_port, kRoot);
uint8_t g_was[kFileBytes];  // the file as written
uint8_t g_cut[kSector];     // what the cut write sends
uint8_t g_got[2 * kSector];

uint8_t raw(uint8_t b) { return SPI.transfer(b); }

bool high_within(uint32_t ms) {
  const uint32_t t0 = millis();
  while (raw(0xFF) != 0xFF) {
    if (millis() - t0 > ms) return false;
  }
  return true;
}

// The first sector of the file, when the FAT and the card agree on it (0 otherwise).
uint32_t first_sector() {
  static FIL fil;  // about 4.6 KB with its sector buffer: never on the loop stack
  if (f_open(&fil, kFatFile, FA_READ) != FR_OK) return 0;
  const FATFS* fs = fil.obj.fs;
  const uint32_t sector = fs->database + fs->csize * (fil.obj.sclust - 2);
  const bool two = fs->csize >= 2;  // the second block stays inside the first cluster
  f_close(&fil);
  if (!two || !sd_read_raw(0, g_got, sector)) return 0;
  return std::memcmp(g_got, g_was, kSector) == 0 ? sector : 0;
}

bool send_cmd25(uint32_t arg) {
  char cmd[6] = {static_cast<char>(0x40 | 25), static_cast<char>(arg >> 24), static_cast<char>(arg >> 16),
                 static_cast<char>(arg >> 8), static_cast<char>(arg), 0};
  cmd[5] = static_cast<char>((CRC7(cmd, 5) << 1) | 1);  // the card checks CRCs (CMD59)
  for (const char c : cmd) raw(static_cast<uint8_t>(c));
  uint8_t r1 = 0xFF;
  for (int i = 0; i < 9 && (r1 & 0x80) != 0; ++i) r1 = raw(0xFF);
  return r1 == 0;
}

// CMD25, one whole block of g_cut (accepted), then `partial` bytes of the next one, and
// chip select released as a reset leaves it: no Stop Tran.
bool cut_write(uint32_t sector, size_t partial) {
  const uint32_t arg = sdcard_type(0) == CARD_SDHC ? sector : sector << 9;
  SPI.beginTransaction(SPISettings(kRawHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kCs, LOW);
  bool ok = high_within(500) && send_cmd25(arg);
  if (ok) {
    raw(0xFF);
    raw(0xFC);
    for (size_t i = 0; i < kSector; ++i) raw(g_cut[i]);
    const uint16_t crc = CRC16(reinterpret_cast<const char*>(g_cut), kSector);
    raw(crc >> 8);
    raw(crc & 0xFF);
    ok = (raw(0xFF) & 0x1F) == 0x05 && high_within(500);
  }
  if (ok && partial > 0) {
    raw(0xFC);
    for (size_t i = 0; i < partial; ++i) raw(0xA5);
  }
  digitalWrite(kCs, HIGH);
  SPI.endTransaction();
  return ok;
}

bool write_file() {
  FILE* f = std::fopen(kFile, "wb");
  if (!f) return false;
  const bool all = std::fwrite(g_was, 1, kFileBytes, f) == kFileBytes;
  return std::fclose(f) == 0 && all;
}

bool read_head() {
  FILE* f = std::fopen(kFile, "rb");
  if (!f) return false;
  const bool all = std::fread(g_got, 1, sizeof g_got, f) == sizeof g_got;
  std::fclose(f);
  return all;
}

void cut_and_mount_again(size_t partial) {
  TEST_ASSERT_TRUE_MESSAGE(cpa::hal_sd_mount(g_store).state == cpa::SdState::Ok, "insert the FAT32 card");
  TEST_ASSERT_TRUE(write_file());
  const uint32_t sector = first_sector();
  TEST_ASSERT_NOT_EQUAL_UINT32(0, sector);
  TEST_ASSERT_TRUE(cut_write(sector, partial));
  cpa::hal_sd_end();  // the reboot: the driver forgets the card, which stays powered
  cpa::hal_sd_reset_health();
  const uint32_t t0 = millis();
  const cpa::SdFacts f = cpa::hal_sd_mount(g_store);
  Serial.printf("CPA_SD_WAKE partial=%u state=%d ms=%lu\n", static_cast<unsigned>(partial), static_cast<int>(f.state),
                static_cast<unsigned long>(millis() - t0));
  TEST_ASSERT_TRUE(f.state == cpa::SdState::Ok);
  TEST_ASSERT_TRUE(read_head());
  TEST_ASSERT_EQUAL_MEMORY(g_cut, g_got, kSector);                      // the accepted block
  TEST_ASSERT_EQUAL_MEMORY(g_was + kSector, g_got + kSector, kSector);  // the cut one: not written
  TEST_ASSERT_EQUAL_INT(0, ::unlink(kFile));
  cpa::hal_sd_end();
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_a_write_cut_between_blocks_mounts_again() { cut_and_mount_again(0); }

static void test_a_write_cut_inside_a_block_mounts_again() { cut_and_mount_again(100); }

static void test_clean_up() {
  TEST_ASSERT_TRUE(cpa::hal_sd_mount(g_store).state == cpa::SdState::Ok);
  TEST_ASSERT_EQUAL_INT(0, rectest::remove_tree(kVfsRoot));
  TEST_ASSERT_EQUAL_INT(0, ::rmdir(kVfsRoot));
  TEST_ASSERT_FALSE(cpa::hal_sd_dead());
  cpa::hal_sd_end();
}

void setup() {
  for (size_t i = 0; i < kFileBytes; ++i) g_was[i] = static_cast<uint8_t>(i * 7 + 1);
  for (size_t i = 0; i < kSector; ++i) g_cut[i] = static_cast<uint8_t>(i * 13 + 5);
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_a_write_cut_between_blocks_mounts_again);
  RUN_TEST(test_a_write_cut_inside_a_block_mounts_again);
  RUN_TEST(test_clean_up);
  UNITY_END();
}

void loop() { devtest::idle(); }
