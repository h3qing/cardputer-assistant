// G-A10: speech level at a given gain (docs/gates/m3-device-gates.md, needs the user).
//   !mic <mag> <pga> <secs>
//       Records secs of 16 kHz mono into /sd/cpa_hwtest/mic.wav with the !sdrec pipeline
//       (sd_rec.cpp: release-callback ring, 4 x 2048 samples, fsync every 1 s), with the M5
//       Mic magnification set to mag and the ES8311 PGA (REG14) set to pga x 3 dB right
//       after Mic.begin. Both return to M5Unified's defaults (16, 0 dB) afterwards.
//       The host pulls the file with !sdget and computes the levels.
//       -> {"t":"sdrec",...} then {"t":"mic_done","path":..,"mag":..,"pga":..,"reg14":..}
#include <M5Cardputer.h>

#include "gate_cmds.h"
#include "gate_core.h"
#include "sd_common.h"

namespace {

constexpr uint8_t kEs8311 = 0x18, kReg14 = 0x14;
constexpr uint32_t kCodecHz = 100000;
constexpr const char* kMicPath = "/sd/cpa_hwtest/mic.wav";

uint8_t s_reg14 = 0x10;   // value to write after Mic.begin
int s_reg14_read = -1;    // read back after the write

void applyPga() {
  M5.In_I2C.writeRegister8(kEs8311, kReg14, s_reg14, kCodecHz);
  uint8_t v = 0;
  s_reg14_read = M5.In_I2C.readRegister(kEs8311, kReg14, &v, 1, kCodecHz) ? v : -1;
}

void setMagnification(uint8_t mag) {
  auto cfg = M5Cardputer.Mic.config();
  cfg.magnification = mag;
  M5Cardputer.Mic.config(cfg);
}

void cmdMic(const gate::MicArgs& a) {
  s_reg14 = gate::es8311_reg14(a.pga);
  s_reg14_read = -1;
  M5Cardputer.Mic.end();
  setMagnification(a.mag);
  sdRecordWav(kMicPath, a.secs, applyPga);
  setMagnification(16);
  M5Cardputer.Mic.end();
  M5Cardputer.Mic.begin();  // M5Unified's own REG14 again
  Serial.printf("{\"t\":\"mic_done\",\"path\":\"%s\",\"mag\":%u,\"pga\":%u,\"reg14\":%d}\n", kMicPath, a.mag, a.pga,
                s_reg14_read);
}

}  // namespace

bool gateMicCommand(const String& line) {
  if (!line.startsWith("!mic ")) return false;
  const gate::Parsed<gate::MicArgs> p = gate::parse_mic(line.c_str() + 5);
  if (!p.ok) Serial.printf("{\"t\":\"mic_done\",\"error\":\"args\",\"field\":\"%s\"}\n", p.err);
  else cmdMic(p.args);
  return true;
}
