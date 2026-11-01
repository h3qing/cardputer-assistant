#include "migrate_screen.h"

#include <cstdio>

#include "txt.h"

namespace cpa {

constexpr Txt kMoving{"正在迁移到 SD 卡 %u/%u", "Moving to the SD card %u/%u"};

MigrateScreen::MigrateScreen(Transport& tx, Display& disp, Clock clock)
    : tx_(tx), disp_(disp), clock_(clock), start_ms_(clock()) {}

void MigrateScreen::on_item(uint16_t done, uint16_t total) {
  const uint32_t now = clock_();
  tx_.tick(now);
  if (now - start_ms_ < kMigrateShowAfterMs) return;
  const bool last = done >= total;
  if (drawn_ && now - drawn_ms_ < kMigrateRedrawMs && !last) return;
  draw(done, total);
  drawn_ = true;
  drawn_ms_ = now;
}

void MigrateScreen::draw(uint16_t done, uint16_t total) {
  char line[48];
  std::snprintf(line, sizeof line, tr(kMoving), static_cast<unsigned>(done), static_cast<unsigned>(total));
  Canvas& c = disp_.canvas();
  c.fill(Rect{0, 0, kScreenW, kScreenH}, Color::Bg);
  c.text(0, 48, line, Style{Color::Accent, Color::Bg});
  disp_.present();
}

}  // namespace cpa
