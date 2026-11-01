// Sustained audio-style write and file pull.
//   !sdrec [secs] [buf_samples] [nbuf] [sync_ms]   (defaults 60 2048 4 1000)
//       16 kHz 16-bit mono from the mic into /sd/cpa_hwtest/rec.wav. The mic's release
//       callback re-queues a free ring buffer at once (capture task), the loop task writes
//       full buffers and fsyncs every sync_ms. "!ping <id>" is answered with "pong" meanwhile.
//       -> {"t":"sdrec_prog"} every 5 s, then {"t":"sdrec"}
//   !sdget <path> <off> <len>   one chunk of a file (len <= 12288), base64 + CRC32
//       -> {"t":"chunk","off":..,"n":..,"size":..,"crc":..,"b64":".."}
#include <M5Cardputer.h>
#include <fcntl.h>
#include <mbedtls/base64.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#include "sd_common.h"

using namespace sdt;

namespace {

constexpr uint32_t kRate = 16000;
constexpr size_t kMaxBufs = 4;
constexpr const char* kWavPath = "/sd/cpa_hwtest/rec.wav";

int16_t* s_buf[kMaxBufs];
size_t s_nbuf = 0, s_len = 0;  // ring size, samples per buffer
QueueHandle_t s_full = nullptr, s_free = nullptr;
std::atomic<int> s_missing{0};         // mic request slots the callback could not refill
std::atomic<uint32_t> s_idle_since{0};  // micros() when both slots ran dry
void (*s_after_begin)() = nullptr;      // sdRecordWav: e.g. a codec register write

struct RecStats {
    uint32_t buffers = 0, write_max_us = 0, sync_max_us = 0, syncs = 0, full_max = 0;
    uint32_t drop_events = 0, drop_us = 0, pings = 0, write_fail = 0;
    uint64_t write_us = 0;
};

int indexOf(const void* p) {
    for (size_t i = 0; i < s_nbuf; ++i) if (s_buf[i] == p) return (int)i;
    return -1;
}

void markMissing() {
    if (s_missing.fetch_add(1) + 1 >= 2) s_idle_since.store(micros());
}

// Capture task: hand the full buffer to the writer and queue the next free one.
void onRelease(void*, void* data, size_t) {
    const int idx = indexOf(data);
    if (idx >= 0) xQueueSend(s_full, &idx, 0);
    int next;
    if (xQueueReceive(s_free, &next, 0) != pdTRUE) { markMissing(); return; }
    if (!M5Cardputer.Mic.record(s_buf[next], s_len)) {
        xQueueSendToFront(s_free, &next, 0);
        markMissing();
    }
}

// Loop task: refill slots the callback could not, and account for a dry mic.
void refillMissing(RecStats& st) {
    int idx;
    while (s_missing.load() > 0 && xQueueReceive(s_free, &idx, 0) == pdTRUE) {
        const bool dry = s_missing.load() >= 2;
        if (!M5Cardputer.Mic.record(s_buf[idx], s_len)) { xQueueSendToFront(s_free, &idx, 0); return; }
        if (dry) { st.drop_events++; st.drop_us += micros() - s_idle_since.load(); }
        s_missing.fetch_sub(1);
    }
}

void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

void wavHeader(uint8_t* h, uint32_t data_bytes) {
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
    put16(h + 20, 1); put16(h + 22, 1); put32(h + 24, kRate); put32(h + 28, kRate * 2);
    put16(h + 32, 2); put16(h + 34, 16);
    memcpy(h + 36, "data", 4); put32(h + 40, data_bytes);
}

void servicePing(RecStats& st, uint32_t t0_ms) {
    static String line;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (line.length() < 32) line += c; continue; }
        if (line.startsWith("!ping")) {
            st.pings++;
            Serial.printf("{\"t\":\"pong\",\"id\":%ld,\"up_ms\":%lu,\"rec_ms\":%lu}\n", line.substring(5).toInt(),
                          millis(), millis() - t0_ms);
        }
        line = "";
    }
}

bool startMic(size_t len, size_t nbuf) {
    s_len = len; s_nbuf = nbuf;
    for (size_t i = 0; i < nbuf; ++i) s_buf[i] = reinterpret_cast<int16_t*>(scratch()) + i * len;
    s_full = xQueueCreate(nbuf, sizeof(int));
    s_free = xQueueCreate(nbuf, sizeof(int));
    s_missing.store(0);
    M5Cardputer.Mic.end();
    M5Cardputer.Mic.setBufferReleaseCallback(nullptr, onRelease);
    if (!M5Cardputer.Mic.begin()) return false;
    if (s_after_begin) s_after_begin();
    for (int i = 2; i < (int)nbuf; ++i) xQueueSend(s_free, &i, 0);
    return M5Cardputer.Mic.record(s_buf[0], len, kRate) && M5Cardputer.Mic.record(s_buf[1], len, kRate);
}

void stopMic() {
    M5Cardputer.Mic.end();
    M5Cardputer.Mic.setBufferReleaseCallback(nullptr, nullptr);
    vQueueDelete(s_full); vQueueDelete(s_free);
    s_full = s_free = nullptr;
    M5Cardputer.Mic.begin();  // back to the state setup() left it in
}

void writeFull(int fd, int idx, RecStats& st) {
    const size_t bytes = s_len * sizeof(int16_t);
    const uint32_t t = micros();
    if (write(fd, s_buf[idx], bytes) != (ssize_t)bytes) st.write_fail++;
    const uint32_t dt = micros() - t;
    st.write_us += dt;
    st.write_max_us = std::max(st.write_max_us, dt);
    st.buffers++;
    st.full_max = std::max<uint32_t>(st.full_max, uxQueueMessagesWaiting(s_full) + 1);
    xQueueSend(s_free, &idx, 0);
    refillMissing(st);
}

void maybeSync(int fd, uint32_t sync_ms, uint32_t& last, RecStats& st) {
    if (millis() - last < sync_ms) return;
    const uint32_t t = micros();
    fsync(fd);
    const uint32_t dt = micros() - t;
    st.sync_max_us = std::max(st.sync_max_us, dt);
    st.syncs++;
    last = millis();
}

void printRec(const RecStats& st, uint32_t secs, uint32_t wall_ms, bool ok) {
    const uint32_t samples = st.buffers * s_len;
    Serial.printf("{\"t\":\"sdrec\",\"result\":\"%s\",\"hz\":%u,\"secs\":%u,\"buf_samples\":%u,\"nbuf\":%u,"
                  "\"buffers\":%u,\"samples\":%u,\"wall_ms\":%u,\"expected_samples\":%u,\"drop_events\":%u,"
                  "\"drop_ms\":%u,\"write_fail\":%u,\"write_max_us\":%u,\"write_avg_us\":%u,\"sync_max_us\":%u,"
                  "\"syncs\":%u,\"ring_max_used\":%u,\"pings\":%u,\"bytes\":%u}\n",
                  ok ? "ok" : "fail", (unsigned)g_hz, (unsigned)secs, (unsigned)s_len, (unsigned)s_nbuf,
                  (unsigned)st.buffers, (unsigned)samples, (unsigned)wall_ms,
                  (unsigned)((uint64_t)wall_ms * kRate / 1000), (unsigned)st.drop_events, (unsigned)(st.drop_us / 1000),
                  (unsigned)st.write_fail, (unsigned)st.write_max_us,
                  (unsigned)(st.buffers ? st.write_us / st.buffers : 0), (unsigned)st.sync_max_us, (unsigned)st.syncs,
                  (unsigned)st.full_max, (unsigned)st.pings, (unsigned)(44 + samples * 2));
}

// The capture loop: write full buffers, fsync every sync_ms, answer pings, report every 5 s.
void recordLoop(int fd, uint32_t secs, uint32_t target, uint32_t sync_ms, RecStats& st) {
    const uint32_t t0 = millis();
    uint32_t last_sync = t0, last_prog = t0;
    while (st.buffers < target && millis() - t0 < secs * 1000 + 5000) {
        int idx;
        if (xQueueReceive(s_full, &idx, pdMS_TO_TICKS(5)) == pdTRUE) writeFull(fd, idx, st);
        maybeSync(fd, sync_ms, last_sync, st);
        refillMissing(st);
        servicePing(st, t0);
        kickUsb();
        if (millis() - last_prog >= 5000) {
            last_prog = millis();
            Serial.printf("{\"t\":\"sdrec_prog\",\"ms\":%lu,\"buffers\":%u,\"drops\":%u}\n", last_prog - t0,
                          (unsigned)st.buffers, (unsigned)st.drop_events);
        }
    }
}

// Back-fills the WAV header with the data length, fsyncs and closes.
bool finishWav(int fd, uint32_t data_bytes) {
    uint8_t hdr[44];
    wavHeader(hdr, data_bytes);
    bool ok = lseek(fd, 0, SEEK_SET) == 0 && write(fd, hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr);
    return fsync(fd) == 0 && close(fd) == 0 && ok;
}

void cmdRec(const char* path, uint32_t secs, size_t len, size_t nbuf, uint32_t sync_ms) {
    if (!mount(g_hz, false) || !mkdirs(kScratch)) { Serial.println("{\"t\":\"sdrec\",\"result\":\"no_fat\"}"); return; }
    uint8_t hdr[44];
    wavHeader(hdr, 0);
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = fd >= 0 && write(fd, hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr) && startMic(len, nbuf);
    RecStats st;
    const uint32_t target = secs * kRate / len, t0 = millis();
    if (ok) recordLoop(fd, secs, target, sync_ms, st);
    const uint32_t wall_ms = millis() - t0;
    if (s_full) stopMic();
    if (fd >= 0) ok = finishWav(fd, st.buffers * len * 2) && ok;
    printRec(st, secs, wall_ms, ok && st.buffers >= target && st.write_fail == 0);
    unmount();
}

// ---- file pull -----------------------------------------------------------------------

constexpr size_t kGetMax = 12288;

void cmdGet(const String& args) {
    const int a = args.indexOf(' '), b = args.indexOf(' ', a + 1);
    const String path = args.substring(0, a);
    const long off = args.substring(a + 1, b).toInt();
    const size_t len = (size_t)std::max(0L, std::min((long)kGetMax, args.substring(b + 1).toInt()));
    if (a < 0 || b < 0 || !path.startsWith("/sd/") || path.indexOf("..") >= 0 || off < 0) {
        Serial.println("{\"t\":\"chunk\",\"error\":\"args\"}");
        return;
    }
    if (ffDrive() < 0 && !mount(g_hz, false)) { Serial.println("{\"t\":\"chunk\",\"error\":\"no_fat\"}"); return; }
    uint8_t* raw = scratch();
    uint8_t* b64 = scratch() + kGetMax;
    const int fd = open(path.c_str(), O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) { if (fd >= 0) close(fd); Serial.println("{\"t\":\"chunk\",\"error\":\"open\"}"); return; }
    const ssize_t n = lseek(fd, off, SEEK_SET) == off ? read(fd, raw, len) : -1;
    close(fd);
    size_t olen = 0;
    if (n > 0) mbedtls_base64_encode(b64, kScratchBytes - kGetMax, &olen, raw, (size_t)n);
    Serial.printf("{\"t\":\"chunk\",\"off\":%ld,\"n\":%d,\"size\":%ld,\"crc\":%u,\"b64\":\"", off, (int)n,
                  (long)st.st_size, (unsigned)(n > 0 ? crc32(0, raw, (size_t)n) : 0));
    Serial.write(b64, olen);
    Serial.println("\"}");
}

}  // namespace

bool sdRecCommand(const String& line) {
    if (line == "!sdrec" || line.startsWith("!sdrec ")) {
        long v[4] = {60, 2048, 4, 1000};
        int from = 7;
        for (int i = 0; i < 4 && from > 0 && from < (int)line.length(); ++i) {
            const int sp = line.indexOf(' ', from);
            v[i] = line.substring(from, sp < 0 ? line.length() : sp).toInt();
            from = sp < 0 ? -1 : sp + 1;
        }
        const size_t nbuf = (size_t)std::max(3L, std::min((long)kMaxBufs, v[2]));
        const size_t len = (size_t)std::max(256L, std::min((long)(kScratchBytes / 2 / nbuf), v[1]));
        cmdRec(kWavPath, (uint32_t)std::max(1L, std::min(600L, v[0])), len, nbuf, (uint32_t)std::max(100L, v[3]));
    } else if (line.startsWith("!sdget ")) cmdGet(line.substring(7));
    else return false;
    return true;
}

void sdRecordWav(const char* path, uint32_t secs, void (*after_begin)()) {
    s_after_begin = after_begin;
    cmdRec(path, secs, 2048, 4, 1000);
    s_after_begin = nullptr;
}
