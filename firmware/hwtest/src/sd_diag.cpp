// SD write-failure diagnostics (docs/gates/sd-card.md §11): what the driver saw, the
// card's registers, and raw sector writes at one LBA with either write command.
//   !sddiag [reset]  driver diagnostics (third_party/SD CPA patch 4)   -> {"t":"sddiag"}
//   !sdid            CID, CSD, SD Status (ACMD13), SCR (ACMD51)        -> {"t":"sdid"}
//   !sdblk <lba> <count> <s|m> [kHz]   count tagged sectors written with CMD24 each (s)
//                    or one CMD25 (m), read back with CMD17 each and one CMD18
//                                                                      -> {"t":"sdblk"}
//   !sdfatscan       counts free / bad / used clusters straight from the FAT32 table and
//                    compares with the FSINFO free count f_getfree trusts -> {"t":"sdfatscan"}
// No file system is mounted by !sdid / !sdblk; they write only the sectors named.
#include <esp_random.h>

#include <algorithm>
#include <cstring>

#include <SPI.h>

#include "ff.h"  // before diskio.h, which uses its types
#include "diskio.h"
#include "sd_common.h"
#include "sd_diag.h"
#include "sd_diag_core.h"
#include "sd_diskio.h"

#ifndef CPA_SD_DIAG  // the core's own SD library (env cardputer-adv-p54): no diagnostics
bool sdDiagCommand(const String&) { return false; }
#else

using namespace sdt;

namespace sdx {

namespace {

const char* stageName(uint8_t s) {
    static const char* const kNames[] = {"none", "busy", "no_r1", "r1", "data_resp", "status", "read_token", "read_crc"};
    return s < sizeof(kNames) / sizeof(kNames[0]) ? kNames[s] : "?";
}

void printEvent(const char* key, const sdcard_diag_event_t& e) {
    jcat(",\"%s\":{\"stage\":\"%s\",\"cmd\":%u,\"tok\":%u,\"st\":%u,\"op\":\"%c\",\"sector\":%u,\"count\":%u,"
         "\"block\":%u,\"busy_ms\":%u,\"at_ms\":%u}",
         key, stageName(e.stage), e.cmd, e.token, e.status, e.op ? e.op : '-', (unsigned)e.sector, e.count, e.block,
         (unsigned)e.busy_ms, (unsigned)e.at_ms);
}

}  // namespace

void printDiag() {
    const sdcard_diag_t d = sdcard_diag();
    const uint32_t* h = d.busy_hist;
    jcat("\"diag\":{\"reads\":%u,\"writes\":%u,\"rfail\":%u,\"wfail\":%u,\"events\":%u,"
         "\"busy_max_ms\":%u,\"busy_max_sector\":%u,\"busy_max_op\":\"%c\",\"busy_hist\":[%u,%u,%u,%u,%u,%u,%u]",
         (unsigned)d.reads, (unsigned)d.writes, (unsigned)d.reads_failed,
         (unsigned)d.writes_failed, (unsigned)d.events, (unsigned)d.busy_max_ms, (unsigned)d.busy_max_sector,
         d.busy_max_op ? d.busy_max_op : '-', (unsigned)h[0], (unsigned)h[1], (unsigned)h[2], (unsigned)h[3],
         (unsigned)h[4], (unsigned)h[5], (unsigned)h[6]);
    jcat(",\"rechecks\":%u,\"rechecks_clean\":%u,\"recheck\":[%u,%u],\"stop_waits\":%u,\"stop_busy_max_us\":%u,"
         "\"stop_busy_max_sector\":%u",
         (unsigned)d.rechecks, (unsigned)d.rechecks_clean, d.recheck_r1, d.recheck_st, (unsigned)d.stop_waits,
         (unsigned)d.stop_busy_max_us, (unsigned)d.stop_busy_max_sector);
    if (d.events) {
        printEvent("first", d.first);
        printEvent("last", d.last);
    }
    jcat("}");
}

bool rawOpen(Raw& r, uint32_t hz) {
    unmount();
    beginBus();
    r.pdrv = sdcard_init(kCs, &SPI, hz);
    if (r.pdrv == 0xFF) return false;
    r.ok = !(disk_initialize(r.pdrv) & STA_NOINIT);
    r.sectors = r.ok ? sdcard_num_sectors(r.pdrv) : 0;
    return r.ok;
}

void rawClose(Raw& r) {
    if (r.pdrv != 0xFF) sdcard_uninit(r.pdrv);
    r = Raw();
}

namespace {

void hex(const char* key, const uint8_t* p, size_t n) {
    jcat(",\"%s\":\"", key);
    for (size_t i = 0; i < n; ++i) jcat("%02x", p[i]);
    jcat("\"");
}

void printCid(const uint8_t* r) {
    const sdd::Cid c = sdd::parse_cid(r);
    // psn stays in the raw hex only (gitignored captures), not as a field
    jcat(",\"cid_mid\":%u,\"cid_oid\":\"%s\",\"cid_pnm\":\"%s\",\"cid_prv\":\"%u.%u\",\"cid_date\":\"%u-%02u\","
         "\"cid_crc_ok\":%d",
         c.mid, c.oid, c.pnm, c.prv_major, c.prv_minor, c.year, c.month, c.crc_ok);
}

void printCsd(const uint8_t* r) {
    const sdd::Csd c = sdd::parse_csd(r);
    jcat(",\"csd_ver\":%u,\"csd_sectors\":%llu,\"csd_tran_speed\":%u,\"csd_ccc\":%u,\"csd_read_bl\":%u,"
         "\"csd_write_bl\":%u,\"csd_erase_blk_en\":%d,\"csd_sector_size\":%u,\"csd_wp\":[%d,%d],\"csd_crc_ok\":%d",
         c.structure + 1, (unsigned long long)c.sectors, c.tran_speed, c.ccc, c.read_bl_len, c.write_bl_len,
         c.erase_blk_en, c.sector_size, c.perm_wp, c.tmp_wp, c.crc_ok);
}

void printStatus(const uint8_t* s) {
    const sdd::SdStatus st = sdd::parse_sd_status(s);
    jcat(",\"ss_card_type\":%u,\"ss_speed_class\":%u,\"ss_au_kb\":%u,\"ss_erase_size\":%u,\"ss_erase_timeout\":%u,"
         "\"ss_erase_offset\":%u,\"ss_uhs_grade\":%u,\"ss_uhs_au_kb\":%u,\"ss_video_class\":%u,\"ss_app_perf\":%u",
         st.card_type, st.speed_class, (unsigned)st.au_kb, st.erase_size, st.erase_timeout, st.erase_offset,
         st.uhs_grade, (unsigned)st.uhs_au_kb, st.video_class, st.app_perf);
}

void cmdId() {
    Raw r;
    const bool ok = rawOpen(r, g_hz);
    jcat("{\"t\":\"sdid\",\"init\":%d,\"card_type\":%d,\"sectors\":%u", ok, ok ? (int)sdcard_type(r.pdrv) : 0,
         (unsigned)r.sectors);
    uint8_t* b = scratch();
    if (ok && sdcard_read_register(r.pdrv, 10, false, b, 16)) { hex("cid", b, 16); printCid(b); }
    if (ok && sdcard_read_register(r.pdrv, 9, false, b, 16)) { hex("csd", b, 16); printCsd(b); }
    if (ok && sdcard_read_register(r.pdrv, 13, true, b, 64)) { hex("sd_status", b, 64); printStatus(b); }
    if (ok && sdcard_read_register(r.pdrv, 51, true, b, 8)) hex("scr", b, 8);
    jcat("}");
    jsend();
    rawClose(r);
}

struct BlkRead {
    uint16_t ok = 0;
    int first_bad = -1;
    sdd::F3 kind = sdd::F3::Ok;
    uint32_t found = 0;
};

// Checks count sectors at b against the tags written from lba with seed.
BlkRead checkBlocks(const uint8_t* b, uint32_t lba, uint16_t count, uint32_t seed, bool read_ok) {
    BlkRead out;
    for (uint16_t i = 0; i < count; ++i) {
        const sdd::F3Check c = sdd::f3_check(b + i * 512u, lba + i, seed);
        if (read_ok && c.kind == sdd::F3::Ok) { out.ok++; continue; }
        if (out.first_bad < 0) { out.first_bad = i; out.kind = c.kind; out.found = c.found_lba; }
    }
    return out;
}

void printRead(const char* key, const BlkRead& r, bool io_ok, uint32_t us) {
    jcat(",\"%s\":{\"io\":%d,\"ok\":%u,\"first_bad\":%d,\"kind\":\"%s\",\"found\":%u,\"us\":%u}", key, io_ok, r.ok,
         r.first_bad, r.first_bad < 0 ? "-" : sdd::f3_name(r.kind), (unsigned)r.found, (unsigned)us);
}

bool writeBlocks(uint8_t pdrv, const sdd::BlkArgs& a, const uint8_t* b) {
    if (a.multi) return disk_write(pdrv, b, a.lba, a.count) == RES_OK;
    bool ok = true;
    for (uint16_t i = 0; i < a.count; ++i) ok = sd_write_raw(pdrv, const_cast<uint8_t*>(b) + i * 512u, a.lba + i) && ok;
    return ok;
}

bool readSingles(uint8_t pdrv, const sdd::BlkArgs& a, uint8_t* b) {
    bool ok = true;
    for (uint16_t i = 0; i < a.count; ++i) ok = sd_read_raw(pdrv, b + i * 512u, a.lba + i) && ok;
    return ok;
}

void cmdBlk(const String& args) {
    sdd::BlkArgs a;
    if (!sdd::parse_blk(args.c_str(), &a)) { Serial.println("{\"t\":\"sdblk\",\"err\":\"args\"}"); return; }
    Raw r;
    sdcard_diag_reset();
    const bool ok = rawOpen(r, a.khz ? a.khz * 1000u : g_hz) && a.lba + a.count <= r.sectors;
    const uint32_t seed = esp_random() | 1u;
    uint8_t* b = scratch();
    jcat("{\"t\":\"sdblk\",\"lba\":%u,\"count\":%u,\"how\":\"%c\",\"init\":%d", (unsigned)a.lba, a.count,
         a.multi ? 'm' : 's', ok);
    if (ok) {
        for (uint16_t i = 0; i < a.count; ++i) sdd::f3_fill(b + i * 512u, a.lba + i, seed);
        uint32_t t0 = micros();
        const bool wrote = writeBlocks(r.pdrv, a, b);
        jcat(",\"actual_hz\":%u,\"write_ok\":%d,\"write_us\":%u", (unsigned)actualHz(), wrote, (unsigned)(micros() - t0));
        memset(b, 0, a.count * 512u);
        t0 = micros();
        const bool io17 = readSingles(r.pdrv, a, b);
        printRead("cmd17", checkBlocks(b, a.lba, a.count, seed, io17), io17, micros() - t0);
        memset(b, 0, a.count * 512u);
        t0 = micros();
        const bool io18 = disk_read(r.pdrv, b, a.lba, a.count) == RES_OK;
        printRead("cmd18", checkBlocks(b, a.lba, a.count, seed, io18), io18, micros() - t0);
    }
    jcat(",");
    printDiag();
    jcat("}");
    jsend();
    rawClose(r);
}

// Reads the whole FAT (32 KB per CMD18) and counts its cluster entries.
bool scanFat(const FATFS* fs, sdd::FatCount* c) {
    const uint32_t per = kScratchBytes / 512u;
    const uint32_t need = (fs->n_fatent + 127u) / 128u;  // 128 entries per sector
    for (uint32_t s = 0; s < need; s += per) {
        const uint32_t n = std::min(per, need - s);
        if (disk_read(fs->pdrv, scratch(), fs->fatbase + s, n) != RES_OK) return false;
        sdd::fat32_count(scratch(), n * 128u, s * 128u, fs->n_fatent, c);
        kickUsb();
    }
    return true;
}

void cmdFatScan() {
    const bool mounted = mount(g_hz);
    const int drv = mounted ? ffDrive() : -1;
    char p[3] = {(char)('0' + drv), ':', 0};
    DWORD fsinfo_free = 0;
    FATFS* fs = nullptr;
    const bool fat32 = drv >= 0 && f_getfree(p, &fsinfo_free, &fs) == FR_OK && fs && fs->fs_type == FS_FAT32;
    sdd::FatCount c = {0, 0, 0};
    const uint32_t t0 = millis();
    const bool io = fat32 && scanFat(fs, &c);
    jcat("{\"t\":\"sdfatscan\",\"mounted\":%d,\"fat32\":%d,\"io\":%d,\"clusters\":%u,\"fsinfo_free\":%u,"
         "\"scan_free\":%u,\"bad\":%u,\"used\":%u,\"match\":%d,\"ms\":%u}",
         mounted, fat32, io, fat32 ? (unsigned)(fs->n_fatent - 2) : 0u, (unsigned)fsinfo_free, (unsigned)c.free,
         (unsigned)c.bad, (unsigned)c.used, io && c.free == fsinfo_free, (unsigned)(millis() - t0));
    jsend();
    unmount();
}

}  // namespace

}  // namespace sdx

bool sdDiagCommand(const String& line) {
    if (line == "!sddiag" || line == "!sddiag reset") {
        if (line.endsWith("reset")) sdcard_diag_reset();
        jcat("{\"t\":\"sddiag\",");
        sdx::printDiag();
        jcat("}");
        jsend();
    } else if (line == "!sdid") {
        sdx::cmdId();
    } else if (line == "!sdfatscan") {
        sdx::cmdFatScan();
    } else if (line.startsWith("!sdblk ")) {
        sdx::cmdBlk(line.substring(7));
    } else {
        return false;
    }
    return true;
}

#endif  // CPA_SD_DIAG
