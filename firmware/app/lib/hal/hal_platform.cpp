#include "hal_platform.h"

#include <M5Cardputer.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdio>

#include "keyboard.h"
#include "nvs_settings.h"

namespace cpa {

DeviceStats HalPlatform::stats() {
  DeviceStats s;
  const int16_t mv = M5Cardputer.Power.getBatteryVoltage();
  s.bat_mv = mv > 0 ? static_cast<uint16_t>(mv) : 0;
  s.heap_free = ESP.getFreeHeap();
  s.heap_min = ESP.getMinFreeHeap();
  s.heap_max_block = ESP.getMaxAllocHeap();
  s.stack_hwm = uxTaskGetStackHighWaterMark(nullptr);  // bytes on ESP-IDF
  return s;
}

void HalPlatform::reinit_keyboard() { hal_keyboard_reinit(); }

size_t HalPlatform::tx_room() {
  const int n = Serial.availableForWrite();
  return n > 0 ? static_cast<size_t>(n) : 0;
}

uint32_t HalPlatform::micros() { return static_cast<uint32_t>(esp_timer_get_time()); }

void HalPlatform::reboot_soon() {
  reboot_ = true;
  reboot_at_ = millis() + kRebootDelayMs;
}

Settings HalPlatform::load_settings() { return nvs_settings_load(); }

bool HalPlatform::save_settings(const Settings& s) { return nvs_settings_save(s); }
Opt<OfflineMark> HalPlatform::load_offline_mark() { return nvs_offline_mark_load(); }
bool HalPlatform::save_offline_mark(const OfflineMark& m) { return nvs_offline_mark_save(m); }

void HalPlatform::tick(uint32_t now) {
  if (reboot_ && static_cast<int32_t>(now - reboot_at_) >= 0) esp_restart();
}

#if CPA_TEST_HOOKS
void HalPlatform::loop_timed(uint32_t start_us, uint32_t end_us, uint32_t now_ms) {
  loop_ = loop_stats_add(loop_, start_us, end_us, now_ms);
}
#endif

FixedStr<12> hal_dev_id() {
  const uint64_t mac = ESP.getEfuseMac();  // byte 0 of the MAC in the low byte
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02x%02x%02x%02x%02x%02x", static_cast<unsigned>(mac & 0xFF),
                static_cast<unsigned>((mac >> 8) & 0xFF), static_cast<unsigned>((mac >> 16) & 0xFF),
                static_cast<unsigned>((mac >> 24) & 0xFF), static_cast<unsigned>((mac >> 32) & 0xFF),
                static_cast<unsigned>((mac >> 40) & 0xFF));
  return FixedStr<12>::from(buf);
}

FixedStr<16> hal_reset_reason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return FixedStr<16>::from("poweron");
    case ESP_RST_SW: return FixedStr<16>::from("software");
    case ESP_RST_PANIC: return FixedStr<16>::from("panic");
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return FixedStr<16>::from("watchdog");
    case ESP_RST_DEEPSLEEP: return FixedStr<16>::from("deepsleep");
    case ESP_RST_BROWNOUT: return FixedStr<16>::from("brownout");
    case ESP_RST_UNKNOWN: return FixedStr<16>::from("unknown");  // USB resets on IDF 4.4 (03 §4)
    default: return FixedStr<16>::from("other");
  }
}

}  // namespace cpa
