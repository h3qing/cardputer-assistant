// cpa-fw entry point (05 §5.1): one Arduino loop task with a 16 KB stack. setup()
// brings the hardware up in the order that matters (USB RX buffer before anything can
// start the CDC, NVS boot counter before any id), then loop() runs the runtime, which
// never blocks: no delay() anywhere in the loop.
// Storage (docs/09-m2-design.md §3.1): partition facts, one SD mount attempt, cpafs,
// then plan_storage() picks the primary store once for this boot; with the card primary
// the Runtime first moves cpafs's pending items to it, and cpafs is unmounted after.
// Recording (docs/11 §4.7): the audio task's buffers are allocated with the card primary,
// before the runtime starts; the task itself starts after it, with the runtime's id
// counter, and scans the card first. The loop reads the keys under the I2C lock.
#include <Arduino.h>
#include <M5Cardputer.h>

#include "audio_task.h"
#include "hal_platform.h"
#include "i2c_lock.h"
#include "keyboard.h"
#include "lfs_store.h"
#include "m5_display.h"
#include "migrate_screen.h"
#include "module_list.h"
#include "null_store.h"
#include "nvs_boot.h"
#include "part_facts.h"
#include "runtime.h"
#include "sd_card.h"
#include "storage_plan.h"
#include "usb_transport.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

cpa::UsbTransport g_usb;
cpa::LfsStore g_lfs;
cpa::VfsFatPort g_sd_port;
cpa::SdStore g_sd(g_sd_port);
cpa::NullStore g_null;
cpa::M5Display g_display;
cpa::HalPlatform g_platform(g_display);
cpa::AudioTask g_audio;
cpa::Runtime* g_runtime = nullptr;  // built in setup(), once the store is known

uint32_t now_ms() { return millis(); }

// docs/20 U16: cpafs again, mounted on demand once the SD card latched while running, so a
// capture is kept there instead of lost (the next boot's migration moves it to the card).
class LfsSpare : public cpa::SpareStore {
 public:
  cpa::FileStore* mount() override {
    if (!mounted_) mounted_ = g_lfs.begin();
    return mounted_ ? &g_lfs : nullptr;
  }

 private:
  bool mounted_ = false;
};

LfsSpare g_spare;

cpa::FileStore& primary_store(cpa::Store s) {
  switch (s) {
    case cpa::Store::Sd: return g_sd;
    case cpa::Store::Internal: return g_lfs;
    default: return g_null;  // saving answers NoStorage (09 §8)
  }
}

}  // namespace

void setup() {
  g_usb.begin();  // setRxBufferSize(8192) before Serial.begin (05 §5.8)
  cpa::hal_i2c_lock_begin();
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // M5Unified must not start the CDC with a 256 B RX queue
  M5Cardputer.begin(cfg, true);
  g_display.begin();
  cpa::StorageBoot storage;
  storage.part = cpa::hal_part_facts();
  storage.sd = cpa::hal_sd_mount(g_sd);
  const bool lfs = g_lfs.begin();  // only ever cpafs (launcher.md R1, R2)
  storage.plan = cpa::plan_storage(storage.part, storage.sd, lfs);
  // Function-local static, not heap: built once the primary store is chosen.
  static cpa::Runtime runtime(g_usb, primary_store(storage.plan.primary), g_platform, g_display, cpa::kModules,
                              cpa::kModuleCount);
  g_runtime = &runtime;
  cpa::MigrateScreen screen(g_usb, g_display, now_ms);
  storage.migrate_from = storage.plan.migrate ? &g_lfs : nullptr;
  storage.observer = &screen;
  const bool sd = storage.plan.primary == cpa::Store::Sd;
  cpa::RecBoot rec = g_audio.prepare(sd);
  const cpa::Opt<uint64_t> card_free = sd ? cpa::hal_sd_free_bytes() : cpa::Opt<uint64_t>{};
  rec.free_known = card_free.has;
  rec.free_bytes = card_free.value;
  runtime.attach_rec(g_audio, rec);
  if (sd && lfs) runtime.attach_spare(g_spare);
  cpa::BootInfo boot;
  boot.ticket = cpa::nvs_boot_ticket();
  boot.dev = cpa::hal_dev_id();
  boot.rst = cpa::hal_reset_reason();
  runtime.begin(boot, millis(), storage);
  if (sd && lfs) g_lfs.end();  // frees LittleFS's heap
  g_audio.start(runtime.ids(), cpa::FixedStr<31>::from(cpa::kFwVersion));
}

void loop() {
#if CPA_TEST_HOOKS
  const uint32_t t0 = micros();  // debug.state.diag (gh #21): the whole pass, keyboard scan included
#endif
  const cpa::KeySet keys = cpa::hal_update_and_keys();  // M5Cardputer.update() under the I2C lock
  const uint32_t now = millis();
  g_runtime->loop(now, keys);
  g_platform.tick(now);
#if CPA_TEST_HOOKS
  g_platform.loop_timed(t0, micros(), now);
#endif
}
