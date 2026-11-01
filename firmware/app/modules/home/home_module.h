// Home launcher module (docs/13 §5.6, v0.4.0), a thin shell over home_logic. Opt+H
// opens it once the shell registers it as the root page (V40-F, V40-H). v0.5 (docs/19
// §6.3, mockup 01): Mando, a greeting, the most urgent session as a card, the tiles.
#pragma once

#include "home_logic.h"
#include "module.h"
#include "theme_anim.h"

namespace cpa {

class HomeModule : public Module {
 public:
  const char* id() const override { return "home"; }
  bool begin(ModuleContext& ctx) override;
  void onEnter(uint8_t arg) override;
  bool onKey(const KeyEvent& key) override;
  void render(Canvas& c, const Rect& area) const override;
  // docs/19 §6.3: G0 in the hint; the Tab menu is the ninth tile. docs/20 D8, U16: wait for
  // ● REC, or why G0 cannot record now; gh #51: the double tap.
  const char* hint() const override { return home_hint(ctx_->state()); }
  uint8_t help(const HelpRow** rows) const override;
  Rect animate(Canvas& c, const Rect& area, uint32_t now) override;  // v0.5 S3: Mando

 private:
  void render_greeting(Canvas& c, const Rect& area) const;
  void render_tiles(Canvas& c, const Rect& area) const;
  void render_urgent(Canvas& c, const Rect& area) const;
  void render_pat(Canvas& c, const Rect& area) const;  // v0.5.0 (docs/04, hidden keys)

  ModuleContext* ctx_ = nullptr;
  HomeUi ui_;
  theme::MandoMotion motion_;  // as the last animation tick left him
  uint32_t pat_seen_ = 0;      // the pats his last tick knew of: a new one hops again
};

}  // namespace cpa
