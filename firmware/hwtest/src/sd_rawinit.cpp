// Bare SPI-mode card init at 400 kHz, bypassing the SD library, to find out why a card
// will not initialise (docs/gates/sd-card.md §2).
//   !sdrawinit <variants> [gap_ms]   one init pass per variant character (kVariants),
//       reporting the ACMD41 time in ms (-1 = still idle after 3 s) and the polls used
//       -> {"t":"sdrawinit","passes":[[variant,ms,polls],...],"ocr":".."}
#include <SPI.h>

#include <algorithm>

#include "sd_common.h"

using namespace sdt;

namespace {

// How a command is framed. kCsLow: CS stays low. kCsClocked: CS high + 8 clocks + CS
// low before every command (the IDF sdspi host). kCsCore: the core SD library's select,
// CS high then low at once (no clocks while high), read until MISO != 0x00.
enum CsMode : uint8_t { kCsLow, kCsClocked, kCsCore };
CsMode s_cs_mode = kCsLow;
bool s_prebyte = true;  // one 0xFF after CS falls, before the command (kCsLow / kCsClocked)

uint8_t crc7(const uint8_t* p, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t d = p[i];
        for (int b = 0; b < 8; ++b, d <<= 1) {
            crc <<= 1;
            if ((d ^ crc) & 0x80) crc ^= 0x09;
        }
    }
    return (crc << 1) | 1;
}

void select() {
    if (s_cs_mode == kCsClocked) {
        digitalWrite(kCs, HIGH);
        SPI.transfer(0xFF);
        digitalWrite(kCs, LOW);
    } else if (s_cs_mode == kCsCore) {
        digitalWrite(kCs, HIGH);
        digitalWrite(kCs, LOW);
        const uint32_t t0 = millis();
        while (SPI.transfer(0xFF) == 0x00 && millis() - t0 < 500) {}
    }
}

uint8_t rawCmd(uint8_t cmd, uint32_t arg, uint8_t* extra = nullptr, size_t n_extra = 0) {
    select();
    const uint8_t pkt[5] = {(uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8),
                            (uint8_t)arg};
    if (s_cs_mode != kCsCore && s_prebyte) SPI.transfer(0xFF);
    for (uint8_t b : pkt) SPI.transfer(b);
    SPI.transfer(crc7(pkt, 5));
    uint8_t r = 0xFF;
    for (int i = 0; i < 9 && (r & 0x80); ++i) r = SPI.transfer(0xFF);
    for (size_t i = 0; i < n_extra; ++i) extra[i] = SPI.transfer(0xFF);
    return r;
}

// CMD55 + ACMD41 until R1 = 0 (ready); gap_ms of idle bus between polls.
int acmd41(uint32_t arg, uint32_t gap_ms, uint32_t cap_ms, uint32_t& polls) {
    const uint32_t t0 = millis();
    for (polls = 1; millis() - t0 < cap_ms; ++polls) {
        rawCmd(55, 0);
        if (rawCmd(41, arg) == 0) return (int)(millis() - t0);
        if (gap_ms) delay(gap_ms);
    }
    return -1;
}

void busUp() {
    endBus();
    SPI.begin(kSck, kMiso, kMosi, kCs);
    pinMode(kCs, OUTPUT);
    digitalWrite(kCs, HIGH);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; ++i) SPI.transfer(0xFF);  // >= 74 clocks with CS high
    digitalWrite(kCs, LOW);
}

void busDown() {
    digitalWrite(kCs, HIGH);
    SPI.transfer(0xFF);
    SPI.endTransaction();
    SPI.end();
}

// One init pass = CMD0, then the steps of a variant. "base" (0) is the minimal SPI-mode
// init: CMD8, ACMD41(HCS). "full" (1) is what the core SD library and the IDF sdspi host
// send: CRC on (CMD59), CMD8, CMD58, ACMD41(HCS + 3.2-3.3 V), CS toggled with clocks.
// Every other variant changes one thing in base or full.
struct Variant {
    CsMode cs;
    bool prebyte, crc, cmd8, cmd58, cmd5, cmd52;
    uint32_t arg, gap_ms;
};
constexpr uint32_t kHcs = 0x40000000, kHcs33 = 0x40100000, kHcsAll = 0x40FF8000;
constexpr Variant kVariants[] = {
    {kCsLow, true, false, true, false, false, false, kHcs, 0},        // 0 base
    {kCsClocked, true, true, true, true, false, false, kHcs33, 0},    // 1 full
    {kCsCore, false, true, true, true, false, false, kHcs33, 0},      // 2 full, core library select
    {kCsLow, true, false, false, false, false, false, kHcs, 0},       // 3 base, no CMD8 (v1 host)
    {kCsLow, true, true, true, false, false, false, kHcs, 0},         // 4 base + CRC on
    {kCsLow, true, false, true, true, false, false, kHcs, 0},         // 5 base + CMD58
    {kCsLow, true, false, true, false, false, false, kHcs33, 0},      // 6 base + 3.2-3.3 V bit
    {kCsClocked, true, false, true, false, false, false, kHcs, 0},    // 7 base + CS toggled
    {kCsClocked, true, true, true, true, false, false, kHcs33, 10},   // 8 full + 10 ms between polls
    {kCsClocked, true, true, true, true, true, false, kHcs33, 0},     // 9 full + CMD5 (SDIO probe)
    {kCsClocked, true, true, true, true, false, true, kHcs33, 0},     // a full + CMD52 (SDIO reset)
    {kCsClocked, true, true, true, true, false, false, kHcsAll, 0},   // b full, 2.7-3.6 V mask (IDF)
    {kCsClocked, false, true, true, true, false, false, kHcs33, 0},   // c full, no byte before cmd
    {kCsLow, false, false, true, false, false, false, kHcs, 0},       // d base, no byte before cmd
};
constexpr int kVariantCount = sizeof(kVariants) / sizeof(kVariants[0]);

int initPass(const Variant& v, uint32_t gap_ms, uint32_t& polls) {
    s_cs_mode = v.cs;
    s_prebyte = v.prebyte;
    uint8_t r7[4];
    if (v.cmd52) rawCmd(52, 0x80000C08, r7, 1);
    rawCmd(0, 0);
    if (v.crc) rawCmd(59, 1);
    if (v.cmd8) rawCmd(8, 0x1AA, r7, 4);
    if (v.cmd5) rawCmd(5, 0, r7, 4);
    if (v.cmd58) rawCmd(58, 0, r7, 4);
    const int ms = acmd41(v.arg, std::max(v.gap_ms, gap_ms), 3000, polls);
    s_prebyte = true;
    if (v.crc) rawCmd(59, 0);
    s_cs_mode = kCsLow;
    return ms;
}

void cmdRawInit(const String& args) {
    const int sp = args.indexOf(' ');
    const String order = sp < 0 ? args : args.substring(0, sp);
    const uint32_t gap_ms = sp < 0 ? 0 : (uint32_t)std::max(0L, std::min(100L, args.substring(sp + 1).toInt()));
    busUp();
    String out;
    for (size_t i = 0; i < order.length(); ++i) {
        const char c = order[i];
        const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : -1;
        if (v < 0 || v >= kVariantCount) continue;
        uint32_t polls = 0;
        const int ms = initPass(kVariants[v], gap_ms, polls);
        out += String(out.length() ? "," : "") + "[" + v + "," + ms + "," + polls + "]";
        kickUsb();
    }
    uint8_t o[4] = {0};
    rawCmd(58, 0, o, 4);
    busDown();
    Serial.printf("{\"t\":\"sdrawinit\",\"gap_ms\":%u,\"passes\":[%s],\"ocr\":\"%02X%02X%02X%02X\"}\n", (unsigned)gap_ms,
                  out.c_str(), o[0], o[1], o[2], o[3]);
}

}  // namespace

bool sdRawCommand(const String& line) {
    if (line.startsWith("!sdrawinit")) cmdRawInit(line.length() > 11 ? line.substring(11) : String("1"));
    else return false;
    return true;
}
