// SD card bring-up helpers shared by sd_probe / sd_bench / sd_rec (docs/gates/sd-card.md).
// The card sits on the Arduino SPI bus (FSPI), driven by the patched copy of the core's
// SD library in lib-core2/SD, and is mounted at /sd through esp_vfs_fat; the display
// uses SPI3, so they never share.
#pragma once

#include <Arduino.h>

namespace sdt {

constexpr int kSck = 40, kMiso = 39, kMosi = 14, kCs = 12;
constexpr const char* kMount = "/sd";           // VFS mount point
constexpr const char* kScratch = "/sd/cpa_hwtest";  // everything the probe writes lives here
constexpr size_t kScratchBytes = 32 * 1024;

extern uint32_t g_hz;  // SPI clock for every command except !sdfreq (set by !sdhz)

void beginBus();  // SPI.begin on the SD pins, once
void endBus();    // unmount and SPI.end, so the pins can be used otherwise
// SD.begin at /sd; format_if_empty runs f_mkfs (FM_ANY, auto cluster size) only when
// f_mount reports no FAT file system, e.g. on exFAT or a blank card.
bool mount(uint32_t hz, bool format_if_empty = false);
void unmount();
uint32_t actualHz();  // SPI clock in use (APB / divider); the driver caps it at 25 MHz
int ffDrive();        // FatFs drive number of the mounted card, -1 if none

uint8_t* scratch();   // 32 KB shared buffer (bench data, recording ring); one user at a time
uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n);  // zlib-compatible
void fillPattern(uint8_t* p, size_t n, uint32_t seed);    // deterministic test data

struct Pct {
    uint32_t p50, p95, max;
};
Pct percentiles(uint32_t* v, size_t n);  // sorts v in place; nearest-rank

void kickUsb();  // HWCDC TX kick (03 §3); call from every long-running loop
// One reply line built in a buffer and sent with a single write, so log lines from the
// core or IDF cannot land inside it.
void jcat(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void jsend();
bool mkdirs(const char* path);  // mkdir -p
// Recursive walk below path: counts files / dirs / bytes, and with remove=true deletes
// them (and path itself unless keep_root). No names are reported.
struct Tree {
    uint32_t files = 0, dirs = 0, failed = 0;
    uint64_t bytes = 0;
};
Tree walkTree(const char* path, bool remove, bool keep_root = true);

}  // namespace sdt

bool sdProbeCommand(const String& line);  // !sdhz !sdinfo !sdfreq !sdformat !sdclear !sdlayout !sdtree
bool sdBenchCommand(const String& line);  // !sdbench !sdoutbox
bool sdRecCommand(const String& line);    // !sdrec !sdget
bool sdRawCommand(const String& line);    // !sdrawinit
// The !sdrec pipeline (defaults 2048 x 4, fsync 1 s) into path; after_begin, when set, runs
// right after Mic.begin (gate_mic.cpp: ES8311 PGA). Prints the {"t":"sdrec"} reply.
void sdRecordWav(const char* path, uint32_t secs, void (*after_begin)());
