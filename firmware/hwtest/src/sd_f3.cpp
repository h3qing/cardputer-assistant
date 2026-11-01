// F3-style capacity probe and the single-file write repro (docs/gates/sd-card.md §11).
//   !sdf3 [n] [first] [seed]   n (<= 160, default 128) sectors tagged with their LBA, at
//       the LBAs of sdd::f3_plan from first (default 131072 = 64 MiB: past this card's
//       FAT and /cpa), written with CMD24 in ascending order; then the card is
//       re-initialised (CMD0 + init) and every sector read back       -> {"t":"sdf3"}
//   !sdfile [KB] [reps]   reps x (default 9375 KB = one 5-minute segment, 3 reps):
//       /sd/cpa_hwtest/file.bin written in 16 KB writes, fsync, close, read back and
//       compared; deleted at the end   -> {"t":"sdfile_rep"} per rep, then {"t":"sdfile"}
#include <esp_random.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "sd_common.h"
#include "sd_diag.h"
#include "sd_diag_core.h"
#include "sd_diskio.h"

#ifndef CPA_SD_DIAG  // the core's own SD library (env cardputer-adv-p54): no diagnostics
bool sdF3Command(const String&) { return false; }
#else

using namespace sdt;

namespace {

// ---- !sdf3 -------------------------------------------------------------------------------

constexpr size_t kF3Max = 160;
constexpr size_t kBadMax = 12;
uint32_t s_lba[kF3Max];

struct F3Tally {
    uint16_t ok = 0, rfail = 0;
    uint16_t kinds[7] = {0};  // by sdd::F3
    long first_bad = -1;
    uint32_t max_ok = 0;
    uint8_t listed = 0;
};

uint16_t writePass(uint8_t pdrv, size_t n, uint32_t seed, long* first_fail) {
    uint16_t fails = 0;
    for (size_t i = 0; i < n; ++i) {
        sdd::f3_fill(scratch(), s_lba[i], seed);
        if (!sd_write_raw(pdrv, scratch(), s_lba[i])) {
            if (!fails++) *first_fail = (long)s_lba[i];
        }
        kickUsb();
    }
    return fails;
}

// Reads every planned sector back; lists up to kBadMax bad ones as [lba,kind,found,bytes].
F3Tally readPass(uint8_t pdrv, size_t n, uint32_t seed) {
    F3Tally t;
    jcat(",\"bad\":[");
    for (size_t i = 0; i < n; ++i) {
        const bool io = sd_read_raw(pdrv, scratch(), s_lba[i]);
        const sdd::F3Check c = io ? sdd::f3_check(scratch(), s_lba[i], seed) : sdd::F3Check{sdd::F3::Foreign, 0, 0};
        kickUsb();
        if (io && c.kind == sdd::F3::Ok) { t.ok++; t.max_ok = s_lba[i]; continue; }
        if (io) t.kinds[static_cast<uint8_t>(c.kind)]++;
        else t.rfail++;
        if (t.first_bad < 0) t.first_bad = (long)s_lba[i];
        if (t.listed++ < kBadMax) {
            jcat("%s[%u,\"%s\",%u,%u]", t.listed > 1 ? "," : "", (unsigned)s_lba[i], io ? sdd::f3_name(c.kind) : "rderr",
                 (unsigned)c.found_lba, c.bad_bytes);
        }
    }
    jcat("]");
    return t;
}

void printTally(const F3Tally& t) {
    using sdd::F3;
    const uint16_t* k = t.kinds;
    jcat(",\"ok\":%u,\"rfail\":%u,\"alias\":%u,\"stale\":%u,\"zeros\":%u,\"ones\":%u,\"damaged\":%u,\"foreign\":%u,"
         "\"first_bad\":%ld,\"max_ok_lba\":%u",
         t.ok, t.rfail, k[(int)F3::OtherLba], k[(int)F3::OtherSeed], k[(int)F3::Zeros], k[(int)F3::Ones],
         k[(int)F3::Damaged], k[(int)F3::Foreign], t.first_bad, (unsigned)t.max_ok);
}

void cmdF3(const String& args) {
    uint32_t v[3] = {128, 131072, 0};
    sdd::parse_uints(args.c_str(), v, 3);
    const uint32_t seed = v[2] ? v[2] : (esp_random() | 1u);
    sdcard_diag_reset();
    sdx::Raw r;
    const bool init = sdx::rawOpen(r, g_hz);
    const size_t n = init ? sdd::f3_plan(r.sectors, v[1], v[0], s_lba, kF3Max) : 0;
    jcat("{\"t\":\"sdf3\",\"init\":%d,\"sectors\":%u,\"first\":%u,\"seed\":%u,\"n\":%u,\"last_lba\":%u", init,
         (unsigned)r.sectors, (unsigned)v[1], (unsigned)seed, (unsigned)n, (unsigned)(n ? s_lba[n - 1] : 0));
    long first_wfail = -1;
    uint32_t t0 = millis();
    const uint16_t wfail = n ? writePass(r.pdrv, n, seed, &first_wfail) : 0;
    jcat(",\"wfail\":%u,\"first_wfail\":%ld,\"write_ms\":%u", wfail, first_wfail, (unsigned)(millis() - t0));
    sdx::rawClose(r);
    const bool reinit = n && sdx::rawOpen(r, g_hz);
    jcat(",\"reinit\":%d", reinit);
    t0 = millis();
    if (reinit) printTally(readPass(r.pdrv, n, seed));
    jcat(",\"read_ms\":%u,", (unsigned)(millis() - t0));
    sdx::printDiag();
    jcat("}");
    jsend();
    sdx::rawClose(r);
}

// ---- !sdfile -----------------------------------------------------------------------------

constexpr const char* kFilePath = "/sd/cpa_hwtest/file.bin";
constexpr size_t kChunk = 16384;  // scratch: [0,16K) data, [16K,32K) expected

struct FileRep {
    size_t wrote = 0;
    int err = 0, rerr = 0;
    uint32_t write_ms = 0, read_ms = 0;
    bool verified = false;
    long bad_off = -1;
};

FileRep writeFile(size_t total, uint32_t seed) {
    FileRep r;
    uint8_t* buf = scratch();
    const uint32_t t0 = millis();
    const int fd = open(kFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    r.err = fd >= 0 ? 0 : errno;
    while (r.err == 0 && r.wrote < total) {
        const size_t n = std::min(kChunk, total - r.wrote);
        fillPattern(buf, n, seed + r.wrote / 4);
        const ssize_t w = write(fd, buf, n);
        if (w == (ssize_t)n) r.wrote += n;
        else r.err = w < 0 ? errno : -1;
        kickUsb();
    }
    if (fd >= 0 && (fsync(fd) != 0 || close(fd) != 0) && r.err == 0) r.err = errno;
    r.write_ms = millis() - t0;
    return r;
}

long firstDiff(const uint8_t* a, const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return (long)i;
    return -1;
}

void verifyFile(FileRep& r, uint32_t seed) {
    uint8_t* got = scratch();
    uint8_t* want = scratch() + kChunk;
    const uint32_t t0 = millis();
    const int fd = open(kFilePath, O_RDONLY);
    r.rerr = fd >= 0 ? 0 : errno;
    size_t off = 0;
    while (r.rerr == 0 && r.bad_off < 0 && off < r.wrote) {
        const size_t n = std::min(kChunk, r.wrote - off);
        const ssize_t got_n = read(fd, got, n);
        if (got_n != (ssize_t)n) { r.rerr = got_n < 0 ? errno : -1; break; }
        fillPattern(want, n, seed + off / 4);
        const long d = firstDiff(got, want, n);
        if (d >= 0) r.bad_off = (long)off + d;
        off += n;
        kickUsb();
    }
    if (fd >= 0) close(fd);
    r.verified = r.rerr == 0 && r.bad_off < 0 && off == r.wrote && r.wrote > 0;
    r.read_ms = millis() - t0;
}

void cmdFile(const String& args) {
    uint32_t v[2] = {9375, 3};
    sdd::parse_uints(args.c_str(), v, 2);
    if (v[0] == 0 || v[0] > 16384 || v[1] == 0 || v[1] > 10) { Serial.println("{\"t\":\"sdfile\",\"err\":\"args\"}"); return; }
    sdcard_diag_reset();
    const bool mounted = mount(g_hz) && mkdirs(kScratch);
    const size_t total = v[0] * 1024u;
    uint32_t good = 0;
    for (uint32_t rep = 0; mounted && rep < v[1]; ++rep) {
        const uint32_t seed = esp_random();
        FileRep r = writeFile(total, seed);
        if (r.wrote) verifyFile(r, seed);
        const bool ok = r.err == 0 && r.wrote == total && r.verified;
        good += ok;
        Serial.printf("{\"t\":\"sdfile_rep\",\"rep\":%u,\"ok\":%d,\"wrote\":%u,\"errno\":%d,\"write_ms\":%u,"
                      "\"verified\":%d,\"rerrno\":%d,\"bad_off\":%ld,\"read_ms\":%u}\n",
                      (unsigned)rep, ok, (unsigned)r.wrote, r.err, (unsigned)r.write_ms, r.verified, r.rerr, r.bad_off,
                      (unsigned)r.read_ms);
    }
    const bool removed = mounted && unlink(kFilePath) == 0;
    unmount();
    jcat("{\"t\":\"sdfile\",\"mounted\":%d,\"kb\":%u,\"reps\":%u,\"good\":%u,\"removed\":%d,", mounted, (unsigned)v[0],
         (unsigned)v[1], (unsigned)good, removed);
    sdx::printDiag();
    jcat("}");
    jsend();
}

}  // namespace

bool sdF3Command(const String& line) {
    if (line == "!sdf3" || line.startsWith("!sdf3 ")) cmdF3(line.substring(5));
    else if (line == "!sdfile" || line.startsWith("!sdfile ")) cmdFile(line.substring(7));
    else return false;
    return true;
}

#endif  // CPA_SD_DIAG
