// MigrateScreen (docs/09-m2-design.md §3.1, §8): the MigrateObserver setup() hands to
// Runtime::begin while cpafs's pending items move to the SD card. setup() answers no
// host line meanwhile, so every item kicks the USB transport; once the migration has
// run 500 ms the screen shows "正在迁移到 SD 卡 i/n", redrawn at most every 200 ms (a
// frame push costs ~10-20 ms of the ~40 ms an item takes) and always for the last item.
// A boot-time I/O shell: its clock reading and redraw time are its only state.
#pragma once

#include <cstdint>

#include "outbox_migrate.h"
#include "platform.h"
#include "transport.h"

namespace cpa {

constexpr uint32_t kMigrateShowAfterMs = 500;
constexpr uint32_t kMigrateRedrawMs = 200;

class MigrateScreen : public MigrateObserver {
 public:
  using Clock = uint32_t (*)();
  MigrateScreen(Transport& tx, Display& disp, Clock clock);
  void on_item(uint16_t done, uint16_t total) override;

 private:
  void draw(uint16_t done, uint16_t total);

  Transport& tx_;
  Display& disp_;
  Clock clock_;
  uint32_t start_ms_;
  uint32_t drawn_ms_ = 0;
  bool drawn_ = false;
};

}  // namespace cpa
