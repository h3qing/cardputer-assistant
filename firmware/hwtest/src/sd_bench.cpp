// SD throughput and outbox-like workloads, all under /sd/cpa_hwtest (POSIX via the VFS,
// the same calls LfsStore makes on LittleFS).
//   !sdbench          4 MB sequential write (+fsync) and read-back per chunk size 512 B / 4 KB / 32 KB
//                     -> {"t":"sdbench"} per chunk size
//   !sdoutbox [n]     n (default 200) ~300 B items saved as tmp -> fsync -> close -> rename,
//                     listed, read back and deleted, for three layouts
//                     -> {"t":"sdoutbox"} per layout
//   !sdrename         rename onto an existing name, FAT semantics -> {"t":"sdrename"}
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "ff.h"
#include "sd_common.h"

using namespace sdt;

namespace {

constexpr size_t kBenchBytes = 4 * 1024 * 1024;
constexpr size_t kMaxItems = 400;
constexpr size_t kItemBytes = 300;

uint32_t s_us[kMaxItems];  // per-operation latencies of the current run

void printPct(const char* key, const Pct& p) {
    jcat(",\"%s\":{\"p50\":%u,\"p95\":%u,\"max\":%u}", key, (unsigned)p.p50, (unsigned)p.p95, (unsigned)p.max);
}

// ---- sequential throughput ---------------------------------------------------------

bool checkWords(const uint8_t* p, size_t n, uint32_t seed) {
    const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
    for (size_t i = 0; i < n / 4; ++i) if (w[i] != (seed + i) * 2654435761u) return false;
    return true;
}

void benchChunk(size_t chunk) {
    const char* path = "/sd/cpa_hwtest/bench.bin";
    uint8_t* buf = scratch();
    uint32_t wsum = 0, wmax = 0, rsum = 0, rmax = 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = fd >= 0;
    for (size_t off = 0; ok && off < kBenchBytes; off += chunk) {
        fillPattern(buf, chunk, off / 4);
        const uint32_t t = micros();
        ok = write(fd, buf, chunk) == (ssize_t)chunk;
        const uint32_t dt = micros() - t;
        wsum += dt;
        wmax = std::max(wmax, dt);
        kickUsb();
    }
    const uint32_t tc = micros();
    ok = fd >= 0 && fsync(fd) == 0 && close(fd) == 0 && ok;
    const uint32_t close_us = micros() - tc;
    fd = ok ? open(path, O_RDONLY) : -1;
    bool verified = fd >= 0;
    for (size_t off = 0; verified && off < kBenchBytes; off += chunk) {
        const uint32_t t = micros();
        verified = read(fd, buf, chunk) == (ssize_t)chunk;
        const uint32_t dt = micros() - t;
        rsum += dt;
        rmax = std::max(rmax, dt);
        verified = verified && checkWords(buf, chunk, off / 4);
        kickUsb();
    }
    if (fd >= 0) close(fd);
    unlink(path);
    const uint64_t kb = kBenchBytes / 1024;
    Serial.printf("{\"t\":\"sdbench\",\"hz\":%u,\"chunk\":%u,\"bytes\":%u,\"written\":%d,\"verified\":%d,"
                  "\"write_kbs\":%u,\"read_kbs\":%u,\"write_max_us\":%u,\"read_max_us\":%u,\"fsync_close_us\":%u}\n",
                  (unsigned)g_hz, (unsigned)chunk, (unsigned)kBenchBytes, ok, verified,
                  (unsigned)(kb * 1000000ull / (wsum + close_us)), (unsigned)(rsum ? kb * 1000000ull / rsum : 0),
                  (unsigned)wmax, (unsigned)rmax, (unsigned)close_us);
}

void cmdBench() {
    if (!mount(g_hz, false) || !mkdirs(kScratch)) { Serial.println("{\"t\":\"sdbench\",\"result\":\"no_fat\"}"); return; }
    for (size_t chunk : {(size_t)512, (size_t)4096, (size_t)32768}) benchChunk(chunk);
    unmount();
}

// ---- outbox workload -----------------------------------------------------------------

enum Layout { kFlatLfn, kShard32, kFlat83, kLayouts };
const char* const kLayoutName[kLayouts] = {"flat_lfn", "shard32_lfn", "flat_83"};
const char* const kLayoutDir[kLayouts] = {"/sd/cpa_hwtest/ob_flat", "/sd/cpa_hwtest/ob_shard", "/sd/cpa_hwtest/ob_83"};

// MonoId as the firmware makes it (05 §5.9): one epoch and boot, rising sequence.
void monoId(char* out, uint32_t seq) {
    static const char kB32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    memcpy(out, "K3M9Q2T-00042-", 14);
    for (int i = 3; i >= 0; --i, seq >>= 5) out[14 + i] = kB32[seq & 31];
    out[18] = '\0';
}

void itemPath(char* out, size_t cap, Layout l, uint32_t seq, bool tmp) {
    char id[19];
    monoId(id, seq);
    if (l == kFlatLfn) snprintf(out, cap, "%s/%s.%s", kLayoutDir[l], id, tmp ? "tmp" : "json");
    else if (l == kShard32) snprintf(out, cap, "%s/_%c/%s.%s", kLayoutDir[l], id[17], id, tmp ? "tmp" : "json");
    else snprintf(out, cap, "%s/%08X.%s", kLayoutDir[l], (unsigned)seq, tmp ? "TMP" : "JSN");  // 8.3, no LFN
}

int openCreate(const char* path, bool make_parent) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 && errno == ENOENT && make_parent) {  // first item of this shard
        char dir[96];
        snprintf(dir, sizeof(dir), "%s", path);
        *strrchr(dir, '/') = '\0';
        mkdir(dir, 0755);
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    return fd;
}

bool saveItem(Layout l, uint32_t seq, const char* body) {
    char tmp[96], fin[96];
    itemPath(tmp, sizeof(tmp), l, seq, true);
    itemPath(fin, sizeof(fin), l, seq, false);
    const int fd = openCreate(tmp, l == kShard32);
    if (fd < 0) return false;
    bool ok = write(fd, body, kItemBytes) == (ssize_t)kItemBytes;
    ok = fsync(fd) == 0 && ok;
    ok = close(fd) == 0 && ok;
    return ok && rename(tmp, fin) == 0;
}

uint32_t countDir(const char* path) {
    DIR* d = opendir(path);
    if (!d) return 0;
    uint32_t n = 0;
    while (const dirent* e = readdir(d)) n += strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    closedir(d);
    return n;
}

// Like LfsStore::list: collect the shard names first, then list each shard.
uint32_t listLayout(Layout l) {
    if (l != kShard32) return countDir(kLayoutDir[l]);
    char shards[32];
    size_t n = 0;
    DIR* d = opendir(kLayoutDir[l]);
    if (!d) return 0;
    while (const dirent* e = readdir(d)) {
        if (e->d_name[0] == '_' && n < sizeof(shards)) shards[n++] = e->d_name[1];
    }
    closedir(d);
    uint32_t total = 0;
    char path[64];
    for (size_t i = 0; i < n; ++i) {
        snprintf(path, sizeof(path), "%s/_%c", kLayoutDir[l], shards[i]);
        total += countDir(path);
    }
    return total;
}

uint32_t readAll(Layout l, uint32_t n) {
    char path[96], buf[kItemBytes];
    uint32_t good = 0;
    for (uint32_t i = 1; i <= n; ++i) {
        itemPath(path, sizeof(path), l, i, false);
        const int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        good += read(fd, buf, sizeof(buf)) == (ssize_t)kItemBytes;
        close(fd);
    }
    return good;
}

uint32_t timeGetfreeUs() {
    char p[3] = {(char)('0' + ffDrive()), ':', 0};
    DWORD nfree;
    FATFS* fs;
    const uint32_t t = micros();
    f_getfree(p, &nfree, &fs);
    return micros() - t;
}

uint32_t avgUs(const uint32_t* v, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; ++i) s += v[i];
    return n ? (uint32_t)(s / n) : 0;
}

void runLayout(Layout l, uint32_t n, const char* body) {
    walkTree(kLayoutDir[l], true, false);
    mkdirs(kLayoutDir[l]);
    uint32_t saved = 0;
    for (uint32_t i = 1; i <= n; ++i) {
        const uint32_t t = micros();
        saved += saveItem(l, i, body);
        s_us[i - 1] = micros() - t;
        kickUsb();
    }
    const uint32_t k = std::min<uint32_t>(10, n);
    const uint32_t first10 = avgUs(s_us, k), last10 = avgUs(s_us + n - k, k);
    const Pct save = percentiles(s_us, n);
    uint32_t t = millis();
    const uint32_t listed = listLayout(l);
    const uint32_t list_ms = millis() - t;
    t = millis();
    const uint32_t readable = readAll(l, n);
    const uint32_t read_ms = millis() - t;
    const uint32_t getfree_us = timeGetfreeUs();
    char path[96];
    for (uint32_t i = 1; i <= n; ++i) {
        itemPath(path, sizeof(path), l, i, false);
        const uint32_t t0 = micros();
        unlink(path);
        s_us[i - 1] = micros() - t0;
        kickUsb();
    }
    const Pct del = percentiles(s_us, n);
    walkTree(kLayoutDir[l], true, false);
    jcat("{\"t\":\"sdoutbox\",\"layout\":\"%s\",\"hz\":%u,\"n\":%u,\"saved\":%u", kLayoutName[l], (unsigned)g_hz,
         (unsigned)n, (unsigned)saved);
    printPct("save_us", save);
    jcat(",\"first10_avg_us\":%u,\"last10_avg_us\":%u,\"listed\":%u,\"list_ms\":%u,\"readable\":%u,"
                  "\"read_all_ms\":%u,\"getfree_us\":%u",
                  (unsigned)first10, (unsigned)last10, (unsigned)listed, (unsigned)list_ms, (unsigned)readable,
                  (unsigned)read_ms, (unsigned)getfree_us);
    printPct("delete_us", del);
    jcat("}");
    jsend();
}

void cmdOutbox(uint32_t n) {
    if (!mount(g_hz, false) || !mkdirs(kScratch)) { Serial.println("{\"t\":\"sdoutbox\",\"result\":\"no_fat\"}"); return; }
    static char body[kItemBytes + 1];
    memset(body, 'x', kItemBytes);
    memcpy(body, "{\"t\":\"outbox.item\",\"body\":\"", 27);
    memcpy(body + kItemBytes - 3, "\"}\n", 3);
    for (int l = 0; l < kLayouts; ++l) runLayout(static_cast<Layout>(l), n, body);
    unmount();
}

// ---- rename semantics ----------------------------------------------------------------

bool writeText(const char* path, const char* text) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const bool ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    return close(fd) == 0 && ok;
}

char firstByte(const char* path) {
    char c = '-';
    const int fd = open(path, O_RDONLY);
    if (fd >= 0) { read(fd, &c, 1); close(fd); }
    return c;
}

void cmdRename() {
    if (!mount(g_hz, false) || !mkdirs("/sd/cpa_hwtest/ren")) { Serial.println("{\"t\":\"sdrename\",\"result\":\"no_fat\"}"); return; }
    const char* dst = "/sd/cpa_hwtest/ren/K3M9Q2T-00042-0001.json";
    const char* src = "/sd/cpa_hwtest/ren/K3M9Q2T-00042-0001.tmp";
    const bool setup = writeText(dst, "A") && writeText(src, "B");
    errno = 0;
    const int rc = rename(src, dst);
    const int err = errno;
    struct stat st;
    const bool src_left = stat(src, &st) == 0;
    const char after = firstByte(dst);
    const bool unlinked = unlink(dst) == 0;
    const int rc2 = rename(src, dst);
    const char after2 = firstByte(dst);
    walkTree("/sd/cpa_hwtest/ren", true, false);
    Serial.printf("{\"t\":\"sdrename\",\"setup\":%d,\"over_existing_rc\":%d,\"errno\":%d,\"eexist\":%d,"
                  "\"src_left\":%d,\"dst_content\":\"%c\",\"unlink_then_rename_rc\":%d,\"dst_content_after\":\"%c\","
                  "\"unlinked\":%d}\n",
                  setup, rc, err, err == EEXIST, src_left, after, rc2, after2, unlinked);
    unmount();
}

}  // namespace

bool sdBenchCommand(const String& line) {
    if (line == "!sdbench") cmdBench();
    else if (line == "!sdoutbox" || line.startsWith("!sdoutbox ")) {
        const long n = line.length() > 10 ? line.substring(10).toInt() : 200;
        cmdOutbox((uint32_t)std::max(1L, std::min((long)kMaxItems, n)));
    } else if (line == "!sdrename") cmdRename();
    else return false;
    return true;
}
