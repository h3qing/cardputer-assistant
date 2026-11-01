#include "sd_common.h"

#include <SD.h>
#include <SPI.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstring>

#include "ff.h"

namespace sdt {

uint32_t g_hz = 25000000;

namespace {

constexpr size_t kPathMax = 256;
constexpr int kMaxDepth = 16;

bool s_bus = false;
alignas(4) uint8_t s_scratch[kScratchBytes];
char s_reply[2048];

// SDFS keeps its FatFs drive number protected; a member pointer formed in a derived
// class reads it without touching the library.
struct SdPeek : fs::SDFS {
    static uint8_t pdrv(fs::SDFS& s) { return s.*(&SdPeek::_pdrv); }
};
size_t s_reply_len = 0;

// FatFs refuses to delete read-only entries; the VFS has no chmod, so go to FatFs.
void clearReadOnly(const char* vfs_path) {
    const int drv = ffDrive();
    if (drv < 0) return;
    char p[kPathMax + 4];
    snprintf(p, sizeof(p), "%d:%s", drv, vfs_path + strlen(kMount));
    f_chmod(p, 0, AM_RDO);
}

bool removeEntry(const char* path, bool is_dir) {
    int rc = is_dir ? rmdir(path) : unlink(path);
    if (rc == 0) return true;
    clearReadOnly(path);
    rc = is_dir ? rmdir(path) : unlink(path);
    return rc == 0;
}

void walkChildren(char* path, size_t len, Tree& r, bool remove, int depth) {
    DIR* d = opendir(path);
    if (!d) { r.failed++; return; }
    while (const dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        const size_t n = strlen(e->d_name);
        if (len + 1 + n >= kPathMax) { r.failed++; continue; }
        path[len] = '/';
        memcpy(path + len + 1, e->d_name, n + 1);
        const bool is_dir = e->d_type == DT_DIR;
        if (is_dir && depth < kMaxDepth) walkChildren(path, len + 1 + n, r, remove, depth + 1);
        struct stat st;
        const uint64_t size = (!is_dir && stat(path, &st) == 0) ? (uint64_t)st.st_size : 0;
        if (remove && !removeEntry(path, is_dir)) r.failed++;
        else if (is_dir) r.dirs++;
        else { r.files++; r.bytes += size; }
        path[len] = '\0';
        kickUsb();
    }
    closedir(d);
}

}  // namespace

void beginBus() {
    if (s_bus) return;
    SPI.begin(kSck, kMiso, kMosi, kCs);
    s_bus = true;
}

void endBus() {
    SD.end();
    if (s_bus) SPI.end();
    s_bus = false;
}

bool mount(uint32_t hz, bool format_if_empty) {
    beginBus();
    SD.end();
    return SD.begin(kCs, SPI, hz, kMount, 5, format_if_empty);
}

void unmount() { SD.end(); }

uint32_t actualHz() { return spiClockDivToFrequency(SPI.getClockDivider()); }

int ffDrive() {
    const uint8_t pdrv = SdPeek::pdrv(SD);
    return pdrv == 0xFF ? -1 : pdrv;
}

uint8_t* scratch() { return s_scratch; }

uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void fillPattern(uint8_t* p, size_t n, uint32_t seed) {
    uint32_t* w = reinterpret_cast<uint32_t*>(p);
    for (size_t i = 0; i < n / 4; ++i) w[i] = (seed + i) * 2654435761u;
}

Pct percentiles(uint32_t* v, size_t n) {
    if (n == 0) return {0, 0, 0};
    std::sort(v, v + n);
    auto rank = [n](uint32_t pct) { return std::max<size_t>(1, (pct * n + 99) / 100) - 1; };
    return {v[rank(50)], v[rank(95)], v[n - 1]};
}

void kickUsb() {
    static uint32_t last = 0;
    if (millis() - last < 20) return;
    last = millis();
    (void)static_cast<bool>(Serial);
}

void jcat(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(s_reply + s_reply_len, sizeof(s_reply) - s_reply_len, fmt, ap);
    va_end(ap);
    if (n > 0) s_reply_len = std::min(sizeof(s_reply) - 1, s_reply_len + (size_t)n);
}

void jsend() {
    Serial.println(s_reply);
    s_reply_len = 0;
    s_reply[0] = '\0';
}

bool mkdirs(const char* path) {
    char p[kPathMax];
    snprintf(p, sizeof(p), "%s", path);
    for (char* s = p + strlen(kMount) + 1; *s; ++s) {
        if (*s != '/') continue;
        *s = '\0';
        mkdir(p, 0755);
        *s = '/';
    }
    return mkdir(p, 0755) == 0 || errno == EEXIST;
}

Tree walkTree(const char* path, bool remove, bool keep_root) {
    Tree r;
    char p[kPathMax];
    snprintf(p, sizeof(p), "%s", path);
    struct stat st;
    if (!keep_root && stat(p, &st) != 0) return r;  // nothing there (FatFs cannot stat the root)
    walkChildren(p, strlen(p), r, remove, 0);
    if (remove && !keep_root) {
        if (removeEntry(p, true)) r.dirs++;
        else r.failed++;
    }
    return r;
}

}  // namespace sdt
