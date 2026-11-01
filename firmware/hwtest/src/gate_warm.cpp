// Issue #31: the ES8311's warm-up after a codec power-on (bench only, never in the app).
//   !cdump                  -> {"t":"cdump","regs":"<0x00..0x45, two hex digits each>","id":"<FD FE FF>"}
//   !cw RR=VV [RR=VV]...    -> each written then read back -> {"t":"cw","n":..,"ok":..,"rb":"<hex>"}
//   !mend                   -> Mic.end, the app's idle (M5Unified writes REG0D=FC, 0E=6A, 00=00)
//   !warm <rec_ms> <end|keep> [hz=<rate>] [<at_ms>:RR=VV]...
//       The app's start (MicRing::start) from whatever state the codec is in: over_sampling 1,
//       magnification 2, Mic.begin, REG14 = 0x19 and REG17 = 0xC4 (kMicGain {9, 0xC4, 2}),
//       then rec_ms of mono at rate (16 kHz by default) into RAM while the timed writes go out at
//       their at_ms after Mic.begin returned (the I2S clock runs by then). Mic.end afterwards
//       unless keep; the port's sample rate goes back to 16 kHz with it.
//       Refused while the mic runs (Mic.end would change the state under test: send !mend).
//       -> {"t":"warm","ok":1,"begin_us":..,"n":..,"rate":..,"lead0":..,"zeros":..,"late_ms":..,"rb":..,"rb_end":..}
//          {"t":"warm_pcm","seq":i,"b64":..} for each 512 samples, then {"t":"warm_end","lines":k}
//   lead0 counts the samples that are exactly 0 from the first one; rb / rb_end read back
//   REG 00 01 02 0D 0E 14 17 1C right after the gain writes / before Mic.end.
#include <M5Cardputer.h>
#include <esp_heap_caps.h>

#include "gate_cmds.h"
#include "gate_core.h"

namespace {

constexpr uint8_t kEs8311 = 0x18;
constexpr uint32_t kCodecHz = 100000;
constexpr size_t kLineSamples = 512;
constexpr uint8_t kReg14 = 0x19, kReg17 = 0xC4;  // the app's kMicGain {9, 0xC4, 2}
const uint8_t kWatch[] = {0x00, 0x01, 0x02, 0x0D, 0x0E, 0x14, 0x17, 0x1C};
constexpr size_t kNWatch = sizeof(kWatch);

uint8_t readReg(uint8_t reg) {
  uint8_t v = 0;
  return M5.In_I2C.readRegister(kEs8311, reg, &v, 1, kCodecHz) ? v : 0xEE;
}

bool writeReg(uint8_t reg, uint8_t val) { return M5.In_I2C.writeRegister8(kEs8311, reg, val, kCodecHz); }

// n registers as hex into out (2n + 1 bytes).
void hexRegs(const uint8_t* regs, size_t n, char* out) {
  for (size_t i = 0; i < n; ++i) snprintf(out + 2 * i, 3, "%02X", readReg(regs[i]));
}

void cmdDump() {
  uint8_t all[0x46];
  for (uint8_t i = 0; i < sizeof(all); ++i) all[i] = i;
  static char regs[2 * sizeof(all) + 1];
  hexRegs(all, sizeof(all), regs);
  const uint8_t ids[] = {0xFD, 0xFE, 0xFF};
  char id[7];
  hexRegs(ids, 3, id);
  Serial.printf("{\"t\":\"cdump\",\"regs\":\"%s\",\"id\":\"%s\",\"mic_on\":%d}\n", regs, id,
                M5Cardputer.Mic.isRunning() ? 1 : 0);
}

void cmdWrite(const gate::RegWrites& w) {
  char rb[2 * gate::kMaxRegWrites + 1] = "";
  size_t ok = 0;
  for (size_t i = 0; i < w.n; ++i) {
    const bool wrote = writeReg(w.r[i].reg, w.r[i].val);
    const uint8_t back = readReg(w.r[i].reg);
    snprintf(rb + 2 * i, 3, "%02X", back);
    ok += wrote && back == w.r[i].val ? 1 : 0;
  }
  Serial.printf("{\"t\":\"cw\",\"n\":%u,\"ok\":%u,\"rb\":\"%s\"}\n", (unsigned)w.n, (unsigned)ok, rb);
}

struct Run {
  const gate::WarmArgs* a;
  uint32_t t0;   // millis() when Mic.begin returned
  uint8_t sent;  // timed writes done
  uint32_t late_ms;
};

void dueWrites(Run& r) {
  const uint32_t now = millis() - r.t0;
  while (r.sent < r.a->n && r.a->w[r.sent].at_ms <= now) {
    writeReg(r.a->w[r.sent].reg, r.a->w[r.sent].val);
    const uint32_t late = now - r.a->w[r.sent].at_ms;
    r.late_ms = late > r.late_ms ? late : r.late_ms;
    ++r.sent;
  }
}

// chunks of 10 ms at the run's rate.
void capture(Run& r, int16_t* buf, size_t chunks) {
  const size_t chunk = r.a->rate / 100;
  for (size_t i = 0; i < chunks; ++i) {
    while (!M5Cardputer.Mic.record(buf + i * chunk, chunk, r.a->rate)) {
      dueWrites(r);
      delay(1);
    }
    dueWrites(r);
  }
  while (M5Cardputer.Mic.isRecording()) {
    dueWrites(r);
    delay(1);
  }
}

bool sendLine(const char* p, size_t n) {
  const uint32_t t0 = millis();
  while (Serial.availableForWrite() < static_cast<int>(n)) {
    if (millis() - t0 > 3000) return false;
    gate_io::tickUsb(millis(), INT32_MAX);  // bytes wait in the ring: flush and re-arm
    delay(1);
  }
  return gate_io::writeWhole(p, n);
}

void stream(const int16_t* buf, size_t n) {
  static char line[64 + gate::b64_len(kLineSamples * 2)];
  size_t seq = 0;
  for (size_t off = 0; off < n; off += kLineSamples, ++seq) {
    const size_t k = n - off < kLineSamples ? n - off : kLineSamples;
    int head = snprintf(line, sizeof(line), "{\"t\":\"warm_pcm\",\"seq\":%u,\"b64\":\"", (unsigned)seq);
    head += gate::b64_encode(reinterpret_cast<const uint8_t*>(buf + off), k * 2, line + head, sizeof(line) - head);
    head += snprintf(line + head, sizeof(line) - head, "\"}\n");
    sendLine(line, head);
  }
  Serial.printf("{\"t\":\"warm_end\",\"lines\":%u}\n", (unsigned)seq);
}

bool beginLikeApp(uint32_t rate) {
  auto cfg = M5Cardputer.Mic.config();
  cfg.sample_rate = rate;  // the port clock is set up at begin
  cfg.over_sampling = 1;
  cfg.magnification = 2;
  M5Cardputer.Mic.config(cfg);
  if (!M5Cardputer.Mic.begin()) return false;
  writeReg(0x14, kReg14);
  writeReg(0x17, kReg17);
  return true;
}

void report(const Run& r, bool ok, uint32_t begin_us, const int16_t* buf, size_t n, const char* rb,
            const char* rb_end) {
  size_t zeros = 0;
  for (size_t i = 0; i < n; ++i) zeros += buf[i] == 0 ? 1 : 0;
  Serial.printf("{\"t\":\"warm\",\"ok\":%d,\"begin_us\":%u,\"n\":%u,\"rate\":%u,\"lead0\":%u,\"zeros\":%u,"
                "\"late_ms\":%u,\"writes\":%u,\"rb\":\"%s\",\"rb_end\":\"%s\",\"keep\":%d}\n",
                ok ? 1 : 0, (unsigned)begin_us, (unsigned)n, (unsigned)r.a->rate, (unsigned)gate::leading_zeros(buf, n),
                (unsigned)zeros, (unsigned)r.late_ms, (unsigned)r.sent, rb, rb_end, r.a->keep ? 1 : 0);
}

void cmdWarm(const gate::WarmArgs& a) {
  if (M5Cardputer.Mic.isRunning()) {
    Serial.println("{\"t\":\"warm\",\"ok\":0,\"error\":\"running\"}");
    return;
  }
  const size_t chunks = a.rec_ms / 10, n = chunks * (a.rate / 100);
  int16_t* buf = static_cast<int16_t*>(heap_caps_calloc(n, 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!buf) {
    Serial.println("{\"t\":\"warm\",\"ok\":0,\"error\":\"memory\"}");
    return;
  }
  const uint32_t b0 = micros();
  const bool ok = beginLikeApp(a.rate);
  const uint32_t begin_us = micros() - b0;
  Run r = {&a, millis(), 0, 0};
  char rb[2 * kNWatch + 1], rb_end[2 * kNWatch + 1] = "";
  hexRegs(kWatch, kNWatch, rb);
  if (ok) capture(r, buf, chunks);
  if (ok) hexRegs(kWatch, kNWatch, rb_end);
  if (ok && !a.keep) M5Cardputer.Mic.end();
  if (!a.keep) {
    auto cfg = M5Cardputer.Mic.config();  // the other commands record at 16 kHz
    cfg.sample_rate = gate::kWarmRate;
    M5Cardputer.Mic.config(cfg);
  }
  report(r, ok, begin_us, buf, ok ? n : 0, rb, rb_end);
  if (ok) stream(buf, n);
  heap_caps_free(buf);
}

}  // namespace

bool gateWarmCommand(const String& line) {
  if (line == "!cdump") {
    cmdDump();
    return true;
  }
  if (line == "!mend") {
    const uint32_t t0 = millis();
    M5Cardputer.Mic.end();
    Serial.printf("{\"t\":\"mend\",\"ms\":%u}\n", (unsigned)(millis() - t0));
    return true;
  }
  if (line.startsWith("!cw ")) {
    const gate::Parsed<gate::RegWrites> p = gate::parse_regs(line.c_str() + 4);
    if (!p.ok) Serial.printf("{\"t\":\"cw\",\"error\":\"args\",\"field\":\"%s\"}\n", p.err);
    else cmdWrite(p.args);
    return true;
  }
  if (!line.startsWith("!warm ")) return false;
  const gate::Parsed<gate::WarmArgs> p = gate::parse_warm(line.c_str() + 6);
  if (!p.ok) Serial.printf("{\"t\":\"warm\",\"error\":\"args\",\"field\":\"%s\"}\n", p.err);
  else cmdWarm(p.args);
  return true;
}
