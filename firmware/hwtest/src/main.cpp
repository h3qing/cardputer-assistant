// Cardputer ADV hardware probe.
//
// Device -> Mac (one JSON object per line):
//   {"t":"hello", ...}          boot / reply to "!info" (uptime, reset reason, heap, battery)
//   {"t":"keys", ...}           every change of the set of held keys (for ghosting tests)
//   {"t":"line","text":...}     text typed on the device, sent on Enter
//   {"t":"rx","len":N}          ack for a line received from the Mac
//   {"t":"mic_start"} / {"t":"mic","seq":i,"b64":...} / {"t":"mic_end", stats}
//   {"t":"beep", ...}           reply to "!beep" (speaker begin result, mic restart result)
//   {"t":"sd", ...}             reply to "!sdtest" (write/rename/verify timings, or result "no_sd")
//   {"t":"sd<cmd>", ...}        SD bring-up commands, see sd_probe.cpp / sd_bench.cpp / sd_rec.cpp
// Mac -> device (plain text lines):
//   "!info"  -> hello,   "!mic" -> 2 s mic capture,   "!beep" -> 1 kHz 300 ms tone,
//   "!sdtest" -> 1 MB SD write + rename + size check,  "!reboot" -> esp_restart(),
//   "!reboot quiet" -> esp_restart() and the next boot skips setup()'s Mic.begin (the app's boot),
//   "!sd<cmd> ..." -> SD bring-up and write-failure diagnostics (docs/gates/sd-card.md),
//   "!xfer" "!xfermk" "!cue" "!mic" -> M3 device gates G-A1 / G-A5 / G-A10
//       (gate_xfer.cpp, gate_cue.cpp, gate_mic.cpp; docs/gates/m3-device-gates.md),
//   "!warm" "!cw" "!cdump" "!mend" -> the ES8311 warm-up bench of issue #31 (gate_warm.cpp),
//   anything else -> shown on screen
// Device keys: G0 = mic capture, Enter = send typed line.

#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <mbedtls/base64.h>
#include <esp_system.h>

#include "gate_cmds.h"
#include "sd_common.h"
#include "sd_diag.h"

// 16 KB like the app's loop (05 §5.13): FatFs keeps long file names on the stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static constexpr uint32_t kMicRate      = 16000;
static constexpr size_t kMicChunk       = 240;
static constexpr size_t kMicChunks      = 134;  // 134 * 240 = 32160 samples ~= 2.0 s
static constexpr size_t kMicSamples     = kMicChunk * kMicChunks;
static constexpr size_t kB64Samples     = 512;  // samples per "mic" line

static int16_t g_mic[kMicSamples];
// !reboot quiet: survives esp_restart(), read and cleared once in setup().
static constexpr uint32_t kQuietBoot = 0x51E7B007;
RTC_NOINIT_ATTR static uint32_t s_quietBoot;
static String g_typed;
static String g_rx;
static String g_lastKeys = "-";
static String g_micStatus = "G0 = mic test";

static const char* const kKeyLabel[4][14] = {
    {"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "bksp"},
    {"tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\"},
    {"fn", "shift", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "enter"},
    {"ctrl", "opt", "alt", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "space"},
};

static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += c;
    }
    return out;
}

static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT: return "brownout";
        default: return "other";
    }
}

static void sendHello() {
    Serial.printf(
        "{\"t\":\"hello\",\"fw\":\"hwtest\",\"ver\":\"0.1\",\"up_ms\":%lu,\"rst\":\"%s\",\"rst_code\":%d,"
        "\"board\":%d,\"heap\":%u,\"min_heap\":%u,\"max_block\":%u,\"bat_mv\":%d,"
        "\"arduino\":\"%d.%d.%d\",\"idf\":\"%s\",\"mic_on\":%d}\n",
        (unsigned long)millis(), resetReasonName(esp_reset_reason()), (int)esp_reset_reason(), (int)M5.getBoard(),
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
        (int)M5Cardputer.Power.getBatteryVoltage(), ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR,
        ESP_ARDUINO_VERSION_PATCH, ESP.getSdkVersion(), M5Cardputer.Mic.isRunning() ? 1 : 0);
}

static void draw() {
    auto& d = M5Cardputer.Display;
    d.startWrite();
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setCursor(0, 0);
    d.printf("HWTEST  bat %dmV  heap %uK", (int)M5Cardputer.Power.getBatteryVoltage(),
             (unsigned)(ESP.getFreeHeap() / 1024));
    d.setTextSize(2);
    d.setTextColor(TFT_GREEN, TFT_BLACK);
    d.setCursor(0, 14);
    d.print(g_lastKeys.substring(0, 40));
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(0, 56);
    d.printf("TX> %s", g_typed.substring(0, 34).c_str());
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.setCursor(0, 72);
    d.printf("RX< %s", g_rx.substring(0, 34).c_str());
    d.setTextColor(TFT_ORANGE, TFT_BLACK);
    d.setCursor(0, 92);
    d.print(g_micStatus.substring(0, 80));
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setCursor(0, 125);
    d.print("G0=mic  Enter=send line to Mac");
    d.endWrite();
}

// Report every change of the held-key set, with layout (x,y) and TCA8418 matrix (row,col).
static void reportKeys() {
    const auto& list = M5Cardputer.Keyboard.keyList();
    String json = "{\"t\":\"keys\",\"ms\":" + String(millis()) + ",\"n\":" + String(list.size()) + ",\"held\":[";
    String names;
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& p = list[i];
        const char* label = (p.y < 4 && p.x < 14) ? kKeyLabel[p.y][p.x] : "?";
        int tcaRow = p.x / 2;
        int tcaCol = p.y + 4 * (p.x % 2);
        if (i) { json += ","; names += "+"; }
        json += "{\"k\":\"" + jsonEscape(label) + "\",\"x\":" + String(p.x) + ",\"y\":" + String(p.y) +
                ",\"row\":" + String(tcaRow) + ",\"col\":" + String(tcaCol) + "}";
        names += label;
    }
    json += "]}";
    Serial.println(json);
    if (list.size()) g_lastKeys = names;
}

static void handleTyping() {
    auto& st = M5Cardputer.Keyboard.keysState();
    for (char c : st.word) g_typed += c;
    if (st.del && g_typed.length()) g_typed.remove(g_typed.length() - 1);
    if (st.enter && g_typed.length()) {
        Serial.printf("{\"t\":\"line\",\"text\":\"%s\"}\n", jsonEscape(g_typed).c_str());
        g_typed = "";
    }
}

static void micStats(int32_t& mn, int32_t& mx, int32_t& mean, int32_t& rms, uint32_t& zeros) {
    int64_t sum = 0;
    mn = INT16_MAX; mx = INT16_MIN; zeros = 0;
    for (size_t i = 0; i < kMicSamples; ++i) {
        int32_t v = g_mic[i];
        sum += v; mn = min(mn, v); mx = max(mx, v);
        if (v == 0) zeros++;
    }
    mean = (int32_t)(sum / (int64_t)kMicSamples);
    double acc = 0;
    for (size_t i = 0; i < kMicSamples; ++i) { double d = g_mic[i] - mean; acc += d * d; }
    rms = (int32_t)sqrt(acc / kMicSamples);
}

static void streamMic() {
    static unsigned char b64[kB64Samples * 2 * 4 / 3 + 8];
    size_t seq = 0;
    for (size_t off = 0; off < kMicSamples; off += kB64Samples, ++seq) {
        size_t n = min(kB64Samples, kMicSamples - off) * sizeof(int16_t);
        size_t olen = 0;
        mbedtls_base64_encode(b64, sizeof(b64), &olen, (const unsigned char*)&g_mic[off], n);
        b64[olen] = 0;
        Serial.printf("{\"t\":\"mic\",\"seq\":%u,\"b64\":\"%s\"}\n", (unsigned)seq, (const char*)b64);
    }
}

static void runMicTest() {
    if (!M5Cardputer.Mic.isRunning()) M5Cardputer.Mic.begin();  // after !reboot quiet or !warm
    g_micStatus = "REC 2s... speak now";
    draw();
    Serial.println("{\"t\":\"mic_start\",\"rate\":16000,\"secs\":2}");
    uint32_t t0 = millis();
    for (size_t i = 0; i < kMicChunks; ++i) {
        while (!M5Cardputer.Mic.record(&g_mic[i * kMicChunk], kMicChunk, kMicRate)) delay(1);
    }
    while (M5Cardputer.Mic.isRecording()) delay(1);
    uint32_t took = millis() - t0;
    streamMic();
    int32_t mn, mx, mean, rms; uint32_t zeros;
    micStats(mn, mx, mean, rms, zeros);
    Serial.printf("{\"t\":\"mic_end\",\"n\":%u,\"took_ms\":%u,\"min\":%d,\"max\":%d,\"mean\":%d,\"rms\":%d,\"zeros\":%u}\n",
                  (unsigned)kMicSamples, (unsigned)took, mn, mx, mean, rms, (unsigned)zeros);
    char buf[96];
    snprintf(buf, sizeof(buf), "MIC min %d max %d rms %d zeros %u", mn, mx, rms, (unsigned)zeros);
    g_micStatus = buf;
}

// Mic and speaker share the I2S bus on the ADV: stop the mic, beep, then restart the mic.
static void runBeep() {
    uint32_t t0 = millis();
    M5Cardputer.Mic.end();
    bool spk = M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(128);
    M5Cardputer.Speaker.tone(1000, 300);
    delay(20);
    while (M5Cardputer.Speaker.isPlaying() && millis() - t0 < 2000) delay(1);
    M5Cardputer.Speaker.end();
    bool mic = M5Cardputer.Mic.begin();
    Serial.printf("{\"t\":\"beep\",\"spk\":%d,\"mic\":%d,\"ms\":%u}\n", spk, mic, (unsigned)(millis() - t0));
    g_micStatus = spk ? "BEEP done" : "BEEP: speaker begin failed";
}

// SD on the Cardputer SPI pins (same as the M5Cardputer sdcard example).
static constexpr int kSdSck = 40, kSdMiso = 39, kSdMosi = 14, kSdCs = 12;
static constexpr size_t kSdChunk = 4096;
static constexpr size_t kSdBytes = 1024 * 1024;
static const char* const kSdTmp = "/cpa_test.tmp";
static const char* const kSdBin = "/cpa_test.bin";

static size_t sdWriteFile(const char* path) {
    static uint8_t buf[kSdChunk];
    for (size_t i = 0; i < kSdChunk; ++i) buf[i] = (uint8_t)i;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return 0;
    size_t total = 0;
    while (total < kSdBytes && f.write(buf, kSdChunk) == kSdChunk) total += kSdChunk;
    f.close();
    return total;
}

static size_t sdFileSize(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
}

static void runSdTest() {
    sdt::endBus();  // start from a released bus whatever the sd_* commands left behind
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    if (!SD.begin(kSdCs, SPI, 25000000)) {
        Serial.println("{\"t\":\"sd\",\"result\":\"no_sd\"}");
        SPI.end();
        g_micStatus = "SD: no card";
        return;
    }
    SD.remove(kSdTmp);
    SD.remove(kSdBin);
    uint32_t t0 = millis();
    size_t wrote = sdWriteFile(kSdTmp);
    uint32_t t1 = millis();
    bool renamed = SD.rename(kSdTmp, kSdBin);
    uint32_t t2 = millis();
    size_t size = sdFileSize(kSdBin);
    bool removed = SD.remove(kSdBin);
    uint32_t t3 = millis();
    bool ok = wrote == kSdBytes && renamed && size == kSdBytes && removed;
    Serial.printf("{\"t\":\"sd\",\"result\":\"%s\",\"card_type\":%d,\"wrote\":%u,\"renamed\":%d,\"size\":%u,"
                  "\"removed\":%d,\"write_ms\":%u,\"rename_ms\":%u,\"verify_ms\":%u}\n",
                  ok ? "ok" : "fail", (int)SD.cardType(), (unsigned)wrote, renamed, (unsigned)size, removed,
                  (unsigned)(t1 - t0), (unsigned)(t2 - t1), (unsigned)(t3 - t2));
    SD.end();
    SPI.end();
    g_micStatus = ok ? "SD test ok" : "SD test FAILED";
}

static bool runSdCommand(const String& line) {
    g_micStatus = "SD " + line.substring(1, 40);
    draw();
    const bool known = sdProbeCommand(line) || sdBenchCommand(line) || sdRecCommand(line) || sdRawCommand(line) ||
                       sdDiagCommand(line) || sdF3Command(line);
    g_micStatus = known ? "SD done: " + line.substring(1, 30) : "SD: unknown command";
    return known;
}

static bool gateCommand(const String& line) {
    return gateXferCommand(line) || gateCueCommand(line) || gateMicCommand(line) || gateWarmCommand(line);
}

// quiet: the next boot skips setup()'s Mic.begin, as the app's boot does (issue #31).
static void restart(bool quiet) {
    if (quiet) s_quietBoot = kQuietBoot;
    Serial.printf("{\"t\":\"bye\",\"quiet\":%d}\n", quiet ? 1 : 0);
    Serial.flush();
    delay(100);
    esp_restart();
}

static void handleSerialIn() {
    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (line.length() < 200) line += c; continue; }
        if (line == "!info") sendHello();
        else if (line == "!mic") runMicTest();
        else if (line == "!beep") runBeep();
        else if (line == "!sdtest") runSdTest();
        else if (line.startsWith("!sd") && runSdCommand(line)) {}
        else if (gateCommand(line)) {}
        else if (line == "!reboot" || line == "!reboot quiet") restart(line.endsWith("quiet"));
        else { g_rx = line; Serial.printf("{\"t\":\"rx\",\"len\":%u}\n", (unsigned)line.length()); }
        line = "";
        draw();
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    gate_io::beginSerial(4096);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Speaker.end();  // mic and speaker share the I2S bus on the ADV
    const bool quiet = s_quietBoot == kQuietBoot;
    s_quietBoot = 0;
    if (!quiet) M5Cardputer.Mic.begin();
    draw();
    sendHello();
}

// Keyboard_Class::isChange() only compares the number of held keys, so compare the full set.
static bool heldKeysChanged() {
    static std::vector<Point2D_t> prev;
    const auto& now = M5Cardputer.Keyboard.keyList();
    bool same = now.size() == prev.size();
    for (size_t i = 0; same && i < now.size(); ++i) {
        same = std::find(prev.begin(), prev.end(), now[i]) != prev.end();
    }
    if (!same) prev = now;
    return !same;
}

// Core 2.0.17 HWCDC decides "host present" from USB SOFs; when macOS idles the bus it
// marks itself disconnected and parks queued TX until the next host write. Evaluating
// Serial (isCDC_Connected) re-arms the IN_EMPTY interrupt so parked data goes out.
// G2 (docs/adr/0001-toolchain.md): this kick does not release every parked reply on either core.
static void kickUsbTx() {
    static uint32_t last = 0;
    if (millis() - last < 20) return;
    last = millis();
    (void)static_cast<bool>(Serial);
}

void loop() {
    M5Cardputer.update();
    kickUsbTx();
    if (heldKeysChanged()) {
        reportKeys();
        if (M5Cardputer.Keyboard.isPressed()) handleTyping();
        draw();
    }
    if (M5Cardputer.BtnA.wasPressed()) {
        runMicTest();
        draw();
    }
    handleSerialIn();
}
