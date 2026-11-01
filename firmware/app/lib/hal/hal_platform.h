// HalPlatform: the Platform port on the Cardputer ADV (stats, backlight, keyboard
// re-init, scheduled restart) plus the device identity. Device only.
#pragma once

#include <cstdint>

#include "fixed_str.h"
#include "m5_display.h"
#include "platform.h"

namespace cpa {

constexpr uint32_t kRebootDelayMs = 200;  // let the debug.reboot ack leave first

class HalPlatform : public Platform {
 public:
  explicit HalPlatform(M5Display& display) : display_(display) {}
  DeviceStats stats() override;
  void reinit_keyboard() override;
  void set_backlight(bool on) override { display_.set_backlight(on); }
  void set_backlight_level(Backlight lv) override { display_.set_backlight_level(lv); }
  size_t tx_room() override;  // Serial.availableForWrite()
  void reboot_soon() override;
  uint32_t micros() override;  // esp_timer, for the test hooks' render timing
  void tick(uint32_t now);  // restarts once the scheduled time has come
  Settings load_settings() override;  // NVS (nvs_settings.h, docs/21 §6.8)
  bool save_settings(const Settings& s) override;
  Opt<OfflineMark> load_offline_mark() override;
  bool save_offline_mark(const OfflineMark& m) override;
#if CPA_TEST_HOOKS
  // debug.state.diag (gh #21): src/main.cpp times every loop() pass with micros(). Out of
  // line, so the LoopStats copies stay off loop()'s own frame (the base of every chain).
  void loop_timed(uint32_t start_us, uint32_t end_us, uint32_t now_ms);
  LoopStats loop_stats() const override { return loop_; }
#endif

 private:
  M5Display& display_;
  uint32_t reboot_at_ = 0;
  bool reboot_ = false;
#if CPA_TEST_HOOKS
  LoopStats loop_;
#endif
};

FixedStr<12> hal_dev_id();         // MAC, 12 lower-case hex digits
FixedStr<16> hal_reset_reason();   // poweron / software / panic / watchdog / ...

}  // namespace cpa
