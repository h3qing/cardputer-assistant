// G-A5: cue timing, lead_ms and I2C concurrency (docs/gates/m3-device-gates.md).
//   !cue <ms> <vol> [memo|meet|silent] [off|nolock|lock] [rec_ms]  (defaults: memo off 1000)
//       Runs the start path of 11 §4.2 in a task shaped like rec_task (core 0, priority 1,
//       6 KB stack), from the idle state (mic and speaker off): Speaker.begin ->
//       setVolume -> tone(s) (memo: 1320 Hz; meet: 660/880/1320 Hz, 30 ms gaps) -> wait
//       until !isPlaying -> Speaker.end -> Mic.begin -> first 10 ms buffer -> capture rec_ms.
//       silent is v0.3.1's start: no speaker at all, Mic.begin straight from the idle state
//       (ms and vol unused; the speaker fields stay 0).
//       After each begin the ES8311 registers are read back against M5Unified's tables.
//       Meanwhile the loop (core 1) records its longest step and, unless hammer is off,
//       reads the TCA8418 continuously: CFG must keep its configured value, the event
//       count must stay 0 (nobody touches the keyboard) and no key may appear. With
//       "lock" both sides take one shared I2C mutex (the app's lock, 11 §4.2): the task
//       around every Speaker/Mic begin/end, the loop around every keyboard read. A FreeRTOS
//       mutex is not handed over across cores: a loop that releases and retakes at once can
//       starve the task for seconds (measured). "lockyield" adds the hand-off: the task raises
//       a want flag before taking, the loop yields one tick after releasing while it is up.
//       -> {"t":"cue", timings (us), read-back, counters}  then  {"t":"cue_bins","rms":[..],"peak":[..]}
//          (RMS / peak of each 10 ms bin after Mic.begin; int16 full scale 32767)
#include <M5Cardputer.h>

#include <algorithm>
#include <atomic>

#include "gate_cmds.h"
#include "gate_core.h"
#include "sd_common.h"

namespace {

using gate::Hammer;
using gate::Pattern;

constexpr uint8_t kEs8311 = 0x18, kTca8418 = 0x34;
constexpr uint8_t kTcaCfg = 0x01, kTcaKeyLckEc = 0x03;
constexpr uint32_t kCodecHz = 100000, kTcaHz = 400000;
constexpr size_t kBinSamples = 160;  // 10 ms at 16 kHz
constexpr size_t kMaxBins = 100;
constexpr uint32_t kTaskTimeoutMs = 8000;

// M5Unified 0.2.26 _speaker_enabled_cb_cardputer_adv / _microphone_enabled_cb_cardputer_adv.
const gate::Reg kSpkRegs[] = {{0x00, 0x80}, {0x01, 0xB5}, {0x02, 0x18}, {0x0D, 0x01},
                              {0x12, 0x00}, {0x13, 0x10}, {0x32, 0xBF}, {0x37, 0x08}};
const gate::Reg kMicRegs[] = {{0x00, 0x80}, {0x01, 0xBA}, {0x02, 0x18}, {0x0D, 0x01},
                              {0x0E, 0x02}, {0x14, 0x10}, {0x17, 0xBF}, {0x1C, 0x6A}};
constexpr size_t kNRegs = sizeof(kSpkRegs) / sizeof(kSpkRegs[0]);

struct Result {
  uint32_t spk_begin_us, tone_us, spk_end_us, mic_begin_us, first_buf_us, start_us, lock_max_us;
  uint32_t lock_wait_us, lock_wait_max_us;  // the cue task waiting for the loop to release the lock
  uint32_t loop_max_us, loop_iters, i2c_err, bad_reads, bad_keys, rb_err;
  bool spk_ok, mic_ok;
  uint8_t spk_rb[kNRegs], mic_rb[kNRegs];
  size_t bins;
};

struct Ctx {
  gate::CueArgs a;
  volatile bool done;
  Result r;
};

SemaphoreHandle_t s_lock = nullptr;
std::atomic<bool> s_want{false};  // lockyield: the cue task is waiting for the lock
gate::Level s_bins[kMaxBins];

bool usesLock(const Ctx& c) { return c.a.hammer == Hammer::Lock || c.a.hammer == Hammer::LockYield; }
void take(const Ctx& c) {
  if (usesLock(c)) xSemaphoreTake(s_lock, portMAX_DELAY);
}
void give(const Ctx& c) {
  if (usesLock(c)) xSemaphoreGive(s_lock);
}

// Runs f under the app lock (when enabled); returns its duration and tracks the longest hold.
template <typename F>
uint32_t locked(Ctx& c, F f) {
  const uint32_t w = micros();
  s_want = c.a.hammer == Hammer::LockYield;
  take(c);
  s_want = false;
  const uint32_t t = micros();
  c.r.lock_wait_us += t - w;
  c.r.lock_wait_max_us = std::max(c.r.lock_wait_max_us, t - w);
  f();
  const uint32_t dt = micros() - t;
  give(c);
  c.r.lock_max_us = std::max(c.r.lock_max_us, dt);
  return dt;
}

void readBack(Ctx& c, const gate::Reg* want, uint8_t* got) {
  locked(c, [&] {
    for (size_t i = 0; i < kNRegs; ++i) {
      if (!M5.In_I2C.readRegister(kEs8311, want[i].reg, &got[i], 1, kCodecHz)) c.r.rb_err++;
    }
  });
}

void waitPlayed() {
  const uint32_t t0 = millis();
  while (!M5Cardputer.Speaker.isPlaying() && millis() - t0 < 50) vTaskDelay(1);
  while (M5Cardputer.Speaker.isPlaying() && millis() - t0 < 3000) vTaskDelay(1);
}

void playPattern(const gate::CueArgs& a) {
  static const float kMeet[3] = {660.0f, 880.0f, 1320.0f};
  if (a.pattern == Pattern::Memo) {
    M5Cardputer.Speaker.tone(1320.0f, a.tone_ms);
    waitPlayed();
    return;
  }
  for (int i = 0; i < 3; ++i) {
    if (i) vTaskDelay(pdMS_TO_TICKS(30));
    M5Cardputer.Speaker.tone(kMeet[i], a.tone_ms);
    waitPlayed();
  }
}

// rec_ms after Mic.begin in 10 ms chunks into scratch (<= 32,000 B); times the first chunk.
void capture(Ctx& c, uint32_t t_mic) {
  int16_t* buf = reinterpret_cast<int16_t*>(sdt::scratch());
  const size_t chunks = std::min<size_t>(kMaxBins, c.a.rec_ms / 10);
  M5Cardputer.Mic.record(buf, kBinSamples, 16000);
  while (M5Cardputer.Mic.isRecording()) vTaskDelay(1);
  c.r.first_buf_us = micros() - t_mic;
  for (size_t i = 1; i < chunks; ++i) {
    while (!M5Cardputer.Mic.record(buf + i * kBinSamples, kBinSamples, 16000)) vTaskDelay(1);
  }
  while (M5Cardputer.Mic.isRecording()) vTaskDelay(1);
  c.r.bins = gate::level_bins(buf, chunks * kBinSamples, kBinSamples, s_bins, kMaxBins);
}

// Speaker.begin -> tone(s) -> Speaker.end: v0.3.0's cue before Mic.begin.
void speakerCue(Ctx& c) {
  c.r.spk_begin_us = locked(c, [&] { c.r.spk_ok = M5Cardputer.Speaker.begin(); });
  M5Cardputer.Speaker.setVolume(c.a.vol);
  readBack(c, kSpkRegs, c.r.spk_rb);
  const uint32_t t_tone = micros();
  playPattern(c.a);
  c.r.tone_us = micros() - t_tone;
  c.r.spk_end_us = locked(c, [&] { M5Cardputer.Speaker.end(); });
}

void cueTask(void* arg) {
  Ctx& c = *static_cast<Ctx*>(arg);
  const uint32_t t0 = micros();
  if (c.a.pattern != Pattern::Silent) speakerCue(c);
  c.r.mic_begin_us = locked(c, [&] { c.r.mic_ok = M5Cardputer.Mic.begin(); });
  const uint32_t t_mic = micros();
  readBack(c, kMicRegs, c.r.mic_rb);
  capture(c, t_mic);
  c.r.start_us = (t_mic - t0) + c.r.first_buf_us;
  locked(c, [&] { M5Cardputer.Mic.end(); });
  c.done = true;
  vTaskDelete(nullptr);
}

// One keyboard read as the loop would do it: TCA8418 CFG and event count, then keyList.
void hammerOnce(Ctx& c, uint8_t cfg_ref) {
  uint8_t cfg = 0, ec = 0;
  take(c);
  const bool ok = M5.In_I2C.readRegister(kTca8418, kTcaCfg, &cfg, 1, kTcaHz) &&
                  M5.In_I2C.readRegister(kTca8418, kTcaKeyLckEc, &ec, 1, kTcaHz);
  M5Cardputer.Keyboard.updateKeyList();
  give(c);
  if (s_want) vTaskDelay(1);  // lockyield: let the waiting task on core 0 take it
  if (!ok) c.r.i2c_err++;
  else if (cfg != cfg_ref) c.r.bad_reads++;
  if (ok && (ec & 0x0F) != 0) c.r.bad_keys++;
  if (!M5Cardputer.Keyboard.keyList().empty()) c.r.bad_keys++;
}

void loopWhileCue(Ctx& c, uint8_t cfg_ref) {
  const uint32_t t0 = millis();
  uint32_t last = micros();
  while (!c.done && millis() - t0 < kTaskTimeoutMs) {
    const uint32_t now = micros();
    c.r.loop_max_us = std::max(c.r.loop_max_us, now - last);
    last = now;
    c.r.loop_iters++;
    sdt::kickUsb();
    if (c.a.hammer == Hammer::Off) vTaskDelay(1);
    else hammerOnce(c, cfg_ref);
  }
}

void hex(const uint8_t* p, size_t n, char* out) {
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = kHex[p[i] >> 4];
    out[2 * i + 1] = kHex[p[i] & 15];
  }
  out[2 * n] = '\0';
}

const char* patternName(Pattern p) {
  static const char* const kNames[] = {"memo", "meet", "silent"};
  return kNames[static_cast<int>(p)];
}

const char* hammerName(Hammer h) {
  static const char* const kNames[] = {"off", "nolock", "lock", "lockyield"};
  return kNames[static_cast<int>(h)];
}

void report(const Ctx& c) {
  const Result& r = c.r;
  char spk[2 * kNRegs + 1], mic[2 * kNRegs + 1];
  hex(r.spk_rb, kNRegs, spk);
  hex(r.mic_rb, kNRegs, mic);
  sdt::jcat("{\"t\":\"cue\",\"done\":%d,\"pattern\":\"%s\",\"ms\":%u,\"vol\":%u,\"hammer\":\"%s\",\"rec_ms\":%u,",
            c.done ? 1 : 0, patternName(c.a.pattern), c.a.tone_ms, c.a.vol,
            hammerName(c.a.hammer), c.a.rec_ms);
  sdt::jcat("\"spk_begin_us\":%u,\"tone_us\":%u,\"spk_end_us\":%u,\"mic_begin_us\":%u,\"first_buf_us\":%u,"
            "\"start_us\":%u,\"lock_max_us\":%u,\"lock_wait_us\":%u,\"lock_wait_max_us\":%u,\"loop_max_us\":%u,"
            "\"loop_iters\":%u,",
            (unsigned)r.spk_begin_us, (unsigned)r.tone_us, (unsigned)r.spk_end_us, (unsigned)r.mic_begin_us,
            (unsigned)r.first_buf_us, (unsigned)r.start_us, (unsigned)r.lock_max_us, (unsigned)r.lock_wait_us,
            (unsigned)r.lock_wait_max_us, (unsigned)r.loop_max_us, (unsigned)r.loop_iters);
  sdt::jcat("\"spk_ok\":%d,\"mic_ok\":%d,\"spk_rb\":\"%s\",\"mic_rb\":\"%s\",\"spk_rb_bad\":%u,\"mic_rb_bad\":%u,"
            "\"rb_err\":%u,\"i2c_err\":%u,\"bad_reads\":%u,\"bad_keys\":%u}",
            r.spk_ok ? 1 : 0, r.mic_ok ? 1 : 0, spk, mic, (unsigned)gate::count_mismatch(kSpkRegs, r.spk_rb, kNRegs),
            (unsigned)gate::count_mismatch(kMicRegs, r.mic_rb, kNRegs), (unsigned)r.rb_err, (unsigned)r.i2c_err,
            (unsigned)r.bad_reads, (unsigned)r.bad_keys);
  sdt::jsend();
}

void reportBins(const Result& r) {
  sdt::jcat("{\"t\":\"cue_bins\",\"bin_ms\":10,\"rms\":[");
  for (size_t i = 0; i < r.bins; ++i) sdt::jcat(i ? ",%u" : "%u", s_bins[i].rms);
  sdt::jcat("],\"peak\":[");
  for (size_t i = 0; i < r.bins; ++i) sdt::jcat(i ? ",%u" : "%u", s_bins[i].peak);
  sdt::jcat("]}");
  sdt::jsend();
}

void cmdCue(const gate::CueArgs& a) {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  static Ctx c;
  c = Ctx();
  c.a = a;
  M5Cardputer.Mic.end();  // idle state of the app: mic and speaker off (11 §4.2)
  M5Cardputer.Speaker.end();
  delay(300);
  const uint8_t cfg_ref = M5.In_I2C.readRegister8(kTca8418, kTcaCfg, kTcaHz);
  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCore(cueTask, "cue", 6144, &c, 1, &task, 0) != pdPASS) {
    Serial.println("{\"t\":\"cue\",\"error\":\"task\"}");
    return;
  }
  loopWhileCue(c, cfg_ref);
  report(c);
  reportBins(c.r);
  if (c.done) M5Cardputer.Mic.begin();  // back to the state setup() left it in
}

}  // namespace

bool gateCueCommand(const String& line) {
  if (!line.startsWith("!cue ")) return false;
  const gate::Parsed<gate::CueArgs> p = gate::parse_cue(line.c_str() + 5);
  if (!p.ok) Serial.printf("{\"t\":\"cue\",\"error\":\"args\",\"field\":\"%s\"}\n", p.err);
  else cmdCue(p.args);
  return true;
}
