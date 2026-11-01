// Ports the runtime drives on the device (hal/ implements them; tests use fakes).
#pragma once

#include <cstddef>
#include <cstdint>

#include "canvas.h"
#include "loop_stats.h"
#include "opt.h"
#include "settings.h"

namespace cpa {

struct DeviceStats {
  uint16_t bat_mv = 0;
  uint32_t heap_free = 0;
  uint32_t heap_min = 0;
  uint32_t heap_max_block = 0;
  uint32_t stack_hwm = 0;  // loop task stack never used so far, bytes
};

// v0.3 (docs/11 §4.2): the backlight has a dim level, used while recording on battery.
enum class Backlight : uint8_t { Off, Dim, On };

struct OfflineMark {
  uint32_t boot = 0;
  uint32_t seq = 0;
  bool operator==(const OfflineMark& o) const { return boot == o.boot && seq == o.seq; }
};

class Platform {
 public:
  virtual ~Platform() = default;
  virtual DeviceStats stats() = 0;
  virtual void reinit_keyboard() = 0;  // keyboard watchdog (05 §5.7 rule 7)
  virtual void set_backlight(bool on) = 0;
  virtual void set_backlight_level(Backlight lv) { set_backlight(lv != Backlight::Off); }
  // Free bytes in the USB TX ring right now. file.data is only built when a whole line
  // fits (docs/11 §5.4), never queued behind other lines.
  virtual size_t tx_room() { return static_cast<size_t>(-1); }
  virtual void reboot_soon() = 0;      // debug.reboot: restart after the ack went out
  // Test firmware (debug.state.diag): how long the device's loop() passes took; zeros where
  // nothing times them (device test programs, the host simulator).
  virtual LoopStats loop_stats() const { return LoopStats{}; }
  // v0.5 test hooks (docs/19 §10): a microsecond clock to time the redraws; 0 without one.
  virtual uint32_t micros() { return 0; }
  // docs/21 §6.8: the settings NVS holds (defaults when it holds none or cannot be read),
  // and a write of them; false: not written. The defaults store nothing.
  virtual Settings load_settings() { return Settings{}; }
  virtual bool save_settings(const Settings& s) {
    (void)s;
    return false;
  }
  // docs/28 §3.2 (S1a): the offline mark, the first id issued after the last unplug (boot,
  // seq of this NVS's epoch): what the plug-in hint calls recorded offline. None stored: no
  // hint. false: not written.
  virtual Opt<OfflineMark> load_offline_mark() { return {}; }
  virtual bool save_offline_mark(const OfflineMark& m) {
    (void)m;
    return false;
  }
};

class Display {
 public:
  virtual ~Display() = default;
  virtual Canvas& canvas() = 0;
  virtual void present() = 0;  // push the finished frame to the panel
  // v0.5 (docs/19 §3.4, S3): push only r (cut to the screen), what an animation tick redrew.
  // The default pushes the whole frame.
  virtual void present(const Rect& r) {
    (void)r;
    present();
  }
  // v0.5: the big ● at breath step `step` (0-3, 0 full Rec; theme::breath_dot): only the
  // RecDot and RecDotEdge palette entries change, what was pushed before keeps its colour, so
  // the caller pushes the ●'s rectangle next. The default does nothing.
  virtual void breathe(uint8_t step) { (void)step; }
};

}  // namespace cpa
