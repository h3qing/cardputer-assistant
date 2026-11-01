// The shared In_I2C lock (docs/11 §4.2; G-A5, docs/gates/m3-device-gates.md §4.2). The
// ES8311 codec and the TCA8418 keyboard share one bus: the loop holds the lock around
// M5Cardputer.update() and the key list, the audio task around every Mic begin and end
// (the speaker is never begun, v0.3.1). A FreeRTOS mutex is not handed over across
// cores (measured: the audio task waited up to 3.7 s while the loop kept reading keys),
// so the audio task raises a want flag before it waits and the loop, after releasing,
// yields one tick while the flag is up ("lockyield": 40 runs, 0 bad keys, 0 codec
// failures, wait <= 0.6 ms).
// CPA_I2C_LOCK=0 compiles it out (M5GFX already serializes single transactions).
// Device only.
#pragma once

#ifndef CPA_I2C_LOCK
#define CPA_I2C_LOCK 1
#endif

namespace cpa {

void hal_i2c_lock_begin();  // once, in setup(), before the audio task starts
void hal_i2c_loop_take();
void hal_i2c_loop_give();   // gives, then yields one tick while the audio task waits
void hal_i2c_audio_take();
void hal_i2c_audio_give();

// Scoped audio-task hold around one begin()/end().
class I2cAudioHold {
 public:
  I2cAudioHold() { hal_i2c_audio_take(); }
  ~I2cAudioHold() { hal_i2c_audio_give(); }
  I2cAudioHold(const I2cAudioHold&) = delete;
  I2cAudioHold& operator=(const I2cAudioHold&) = delete;
};

}  // namespace cpa
