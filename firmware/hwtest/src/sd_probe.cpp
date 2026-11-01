// SD card identification, SPI clock sweep, format / clear and the final /cpa layout.
//   !sdhz <kHz>  set the clock for the other commands     -> {"t":"sdhz"}
//   !sdinfo      raw MBR / boot sector + FatFs volume info -> {"t":"sdinfo"}
//   !sdfreq      mount + 256 KB write/read/verify x5 at 4/20/25/26.7/40 MHz -> {"t":"sdfreq"} per clock
//   !sdformat    f_mkfs only if the card does not mount as FAT -> {"t":"sdformat"}
//   !sdclear     delete everything on a FAT card (counts only) -> {"t":"sdclear"}
//   !sdlayout    drop /cpa_hwtest, create /cpa/{outbox,rec,logs} -> {"t":"sdlayout"}
//   !sdtree      every entry on the card (only run after !sdclear) -> {"t":"sdtree"}
// The card's own file names are never printed except by !sdtree.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include <SD.h>
#include <SPI.h>

#include "ff.h"  // before diskio.h, which uses its types
#include "diskio.h"
#include "sd_common.h"
#include "sd_diskio.h"

using namespace sdt;

namespace {

struct RawInfo {
    bool init = false, sig = false, sfd = false;
    int card_type = 0;  // sdcard_type_t: 1 MMC, 2 SD, 3 SDHC/SDXC
    uint32_t sectors = 0, part_lba = 0, part_secs = 0, cluster = 0, init_ms = 0;
    uint8_t ptype = 0;
    char oem[9] = "", fs[9] = "";
};

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

void printable(char* out, const uint8_t* in, size_t n) {
    for (size_t i = 0; i < n; ++i) out[i] = (in[i] >= 0x20 && in[i] < 0x7F && in[i] != '"' && in[i] != '\\') ? in[i] : '?';
    out[n] = '\0';
}

bool isBootSector(const uint8_t* b) { return (b[0] == 0xEB || b[0] == 0xE9) && b[510] == 0x55 && b[511] == 0xAA; }

void parseBoot(const uint8_t* b, RawInfo& r) {
    printable(r.oem, b + 3, 8);
    if (!memcmp(b + 3, "EXFAT   ", 8)) {
        strcpy(r.fs, "exFAT");
        r.cluster = (1u << b[0x6C]) << b[0x6D];
        return;
    }
    r.cluster = (uint32_t)(b[11] | b[12] << 8) * b[13];
    const bool fat32 = (b[0x16] | b[0x17] << 8) == 0;  // BPB_FATSz16 == 0
    printable(r.fs, b + (fat32 ? 0x52 : 0x36), 8);
}

void readLayout(uint8_t pdrv, RawInfo& r) {
    uint8_t* b = scratch();
    if (!sd_read_raw(pdrv, b, 0)) return;
    r.sig = b[510] == 0x55 && b[511] == 0xAA;
    r.sfd = isBootSector(b);
    if (r.sfd) return parseBoot(b, r);
    if (!r.sig) return;
    r.ptype = b[0x1BE + 4];
    r.part_lba = le32(b + 0x1BE + 8);
    r.part_secs = le32(b + 0x1BE + 12);
    if (r.part_lba && sd_read_raw(pdrv, b, r.part_lba) && isBootSector(b)) parseBoot(b, r);
}

// Card init + sector 0 (+ the first partition's boot sector) without mounting anything.
RawInfo rawProbe(uint32_t hz) {
    RawInfo r;
    unmount();
    beginBus();
    const uint8_t pdrv = sdcard_init(kCs, &SPI, hz);
    if (pdrv == 0xFF) return r;
    const uint32_t t0 = millis();
    r.init = !(disk_initialize(pdrv) & STA_NOINIT);
    r.init_ms = millis() - t0;
    if (r.init) {
        r.card_type = sdcard_type(pdrv);
        r.sectors = sdcard_num_sectors(pdrv);
        readLayout(pdrv, r);
    }
    sdcard_uninit(pdrv);
    return r;
}

void printRaw(const RawInfo& r) {
    jcat("\"init\":%d,\"init_ms\":%u,\"card_type\":%d,\"sectors\":%u,\"mbr_sig\":%d,\"sfd\":%d,\"ptype\":%u,"
         "\"part_lba\":%u,\"part_secs\":%u,\"boot_oem\":\"%s\",\"boot_fs\":\"%s\",\"boot_cluster\":%u",
         r.init, (unsigned)r.init_ms, r.card_type, (unsigned)r.sectors, r.sig, r.sfd, r.ptype, (unsigned)r.part_lba,
         (unsigned)r.part_secs, r.oem, r.fs, (unsigned)r.cluster);
}

// FatFs view of the mounted volume; f_getfree may scan the whole FAT once (timed).
void printVolume() {
    const int drv = ffDrive();
    char p[3] = {(char)('0' + drv), ':', 0};
    DWORD nfree = 0;
    FATFS* fs = nullptr;
    const uint32_t t0 = millis();
    const FRESULT fr = drv < 0 ? FR_NOT_ENABLED : f_getfree(p, &nfree, &fs);
    const uint32_t getfree_ms = millis() - t0;
    if (fr != FR_OK || !fs) { jcat(",\"getfree_fr\":%d", (int)fr); return; }
    const uint32_t clus = fs->csize * 512u;
    jcat(",\"fs_type\":%u,\"n_fats\":%u,\"cluster\":%u,\"clusters\":%u,\"free_clusters\":%u,\"volbase\":%u,"
                  "\"fatbase\":%u,\"database\":%u,\"fat_secs\":%u,\"total_mb\":%llu,\"free_mb\":%llu,\"getfree_ms\":%u",
                  fs->fs_type, fs->n_fats, (unsigned)clus, (unsigned)(fs->n_fatent - 2), (unsigned)nfree,
                  (unsigned)fs->volbase, (unsigned)fs->fatbase, (unsigned)fs->database, (unsigned)fs->fsize,
                  (unsigned long long)(fs->n_fatent - 2) * clus >> 20, (unsigned long long)nfree * clus >> 20,
                  (unsigned)getfree_ms);
}

void printTree(const Tree& t) {
    jcat("\"files\":%u,\"dirs\":%u,\"bytes\":%llu,\"failed\":%u", (unsigned)t.files, (unsigned)t.dirs,
                  (unsigned long long)t.bytes, (unsigned)t.failed);
}

void cmdInfo() {
    const RawInfo r = rawProbe(g_hz);
    jcat("{\"t\":\"sdinfo\",");
    printRaw(r);
    const uint32_t t0 = millis();
    const bool ok = mount(g_hz, false);
    jcat(",\"mounted\":%d,\"mount_ms\":%u,\"hz\":%u", ok, (unsigned)(millis() - t0), (unsigned)g_hz);
    if (ok) {
        jcat(",\"actual_hz\":%u,\"sd_type\":%d", (unsigned)actualHz(), (int)SD.cardType());
        printVolume();
        jcat(",");
        printTree(walkTree(kMount, false));
    }
    jcat("}");
    jsend();
    unmount();
}

// One clock trial: mount, write 256 KB (32 KB chunks) + fsync, read back and verify.
struct Trial {
    bool mounted = false, verified = false;
    uint32_t write_us = 0, read_us = 0, actual_hz = 0;
};

constexpr size_t kTrialBytes = 256 * 1024;
constexpr size_t kTrialChunk = 32 * 1024;

bool checkPattern(const uint8_t* p, size_t n, uint32_t seed) {
    const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
    for (size_t i = 0; i < n / 4; ++i) if (w[i] != (seed + i) * 2654435761u) return false;
    return true;
}

Trial runTrial(uint32_t hz, uint32_t seed) {
    Trial t;
    t.mounted = mount(hz, false) && mkdirs(kScratch);
    if (!t.mounted) return t;
    t.actual_hz = actualHz();
    const char* path = "/sd/cpa_hwtest/freq.bin";
    uint8_t* buf = scratch();
    uint32_t t0 = micros();
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = fd >= 0;
    for (size_t off = 0; ok && off < kTrialBytes; off += kTrialChunk) {
        fillPattern(buf, kTrialChunk, seed + off / 4);
        ok = write(fd, buf, kTrialChunk) == (ssize_t)kTrialChunk;
    }
    ok = fd >= 0 && fsync(fd) == 0 && close(fd) == 0 && ok;
    t.write_us = micros() - t0;
    t0 = micros();
    fd = ok ? open(path, O_RDONLY) : -1;
    ok = fd >= 0;
    for (size_t off = 0; ok && off < kTrialBytes; off += kTrialChunk) {
        ok = read(fd, buf, kTrialChunk) == (ssize_t)kTrialChunk && checkPattern(buf, kTrialChunk, seed + off / 4);
    }
    if (fd >= 0) close(fd);
    t.read_us = micros() - t0;
    t.verified = ok;
    unlink(path);
    unmount();
    return t;
}

void cmdFreq() {
    static const uint32_t kKhz[] = {4000, 20000, 25000, 26667, 40000};  // 80 MHz APB: 4 / 20 / 20 / 26.7 / 40
    for (uint32_t khz : kKhz) {
        uint32_t mounted = 0, verified = 0, wus = 0, rus = 0, actual = 0;
        for (uint32_t i = 0; i < 5; ++i) {
            const Trial t = runTrial(khz * 1000u, khz * 7919u + i * 104729u);
            if (t.mounted) { mounted++; actual = t.actual_hz; }
            if (t.verified) { verified++; wus += t.write_us; rus += t.read_us; }
            kickUsb();
        }
        const uint32_t kb = verified * kTrialBytes / 1024;
        Serial.printf("{\"t\":\"sdfreq\",\"khz\":%u,\"actual_hz\":%u,\"trials\":5,\"mounted\":%u,"
                      "\"verified\":%u,\"write_kbs\":%u,\"read_kbs\":%u}\n",
                      (unsigned)khz, (unsigned)actual, (unsigned)mounted, (unsigned)verified,
                      (unsigned)(wus ? kb * 1000000ull / wus : 0), (unsigned)(rus ? kb * 1000000ull / rus : 0));
    }
}

void cmdFormat() {
    if (mount(g_hz, false)) {
        Serial.println("{\"t\":\"sdformat\",\"formatted\":0,\"result\":\"already_fat\"}");
        unmount();
        return;
    }
    const uint32_t t0 = millis();
    const bool ok = mount(g_hz, true);  // FR_NO_FILESYSTEM -> f_mkfs(FM_ANY, au=0) in the SD library
    Serial.printf("{\"t\":\"sdformat\",\"formatted\":%d,\"result\":\"%s\",\"ms\":%u}\n", ok, ok ? "ok" : "fail",
                  (unsigned)(millis() - t0));
    unmount();
}

void cmdClear() {
    if (!mount(g_hz, false)) { Serial.println("{\"t\":\"sdclear\",\"result\":\"no_fat\"}"); return; }
    const uint32_t t0 = millis();
    const Tree removed = walkTree(kMount, true);
    const uint32_t ms = millis() - t0;
    const Tree left = walkTree(kMount, false);
    jcat("{\"t\":\"sdclear\",\"removed\":{");
    printTree(removed);
    jcat("},\"left\":{");
    printTree(left);
    jcat("},\"ms\":%u}", (unsigned)ms);
    jsend();
    unmount();
}

void cmdLayout() {
    if (!mount(g_hz, false)) { Serial.println("{\"t\":\"sdlayout\",\"result\":\"no_fat\"}"); return; }
    const Tree scratch_removed = walkTree(kScratch, true, false);
    const bool ok = mkdirs("/sd/cpa/outbox") && mkdirs("/sd/cpa/rec") && mkdirs("/sd/cpa/logs");
    jcat("{\"t\":\"sdlayout\",\"result\":\"%s\",\"scratch_removed\":{", ok ? "ok" : "fail");
    printTree(scratch_removed);
    jcat("}}");
    jsend();
    unmount();
}

void listInto(char* path, size_t len, String& out, int& n) {
    DIR* d = opendir(path);
    if (!d) return;
    while (const dirent* e = readdir(d)) {
        if (n >= 64 || !strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        const size_t nl = strlen(e->d_name);
        if (len + 1 + nl >= 200) continue;
        path[len] = '/';
        memcpy(path + len + 1, e->d_name, nl + 1);
        struct stat st;
        const bool dir = e->d_type == DT_DIR;
        const long size = (!dir && stat(path, &st) == 0) ? (long)st.st_size : 0;
        out += String(n++ ? "," : "") + "[\"" + (path + strlen(kMount)) + "\"," + (dir ? "-1" : String(size)) + "]";
        if (dir) listInto(path, len + 1 + nl, out, n);
        path[len] = '\0';
    }
    closedir(d);
}

void cmdTree() {
    if (!mount(g_hz, false)) { Serial.println("{\"t\":\"sdtree\",\"result\":\"no_fat\"}"); return; }
    char path[200] = "/sd";
    String out;
    int n = 0;
    listInto(path, strlen(path), out, n);
    Serial.printf("{\"t\":\"sdtree\",\"n\":%d,\"entries\":[%s]}\n", n, out.c_str());  // size -1 = directory
    unmount();
}

}  // namespace

bool sdProbeCommand(const String& line) {
    if (line.startsWith("!sdhz ")) {  // kHz, 400..40000
        const long khz = line.substring(6).toInt();
        if (khz >= 400 && khz <= 40000) g_hz = (uint32_t)khz * 1000u;
        Serial.printf("{\"t\":\"sdhz\",\"hz\":%u}\n", (unsigned)g_hz);
    } else if (line == "!sdinfo") cmdInfo();
    else if (line == "!sdfreq") cmdFreq();
    else if (line == "!sdformat") cmdFormat();
    else if (line == "!sdclear") cmdClear();
    else if (line == "!sdlayout") cmdLayout();
    else if (line == "!sdtree") cmdTree();
    else return false;
    return true;
}
