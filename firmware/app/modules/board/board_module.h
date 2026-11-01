// Board module (home page + project pages), a thin shell over board_logic. v0.4.0: the
// project board behind Opt+P (docs/13 §4.1); help() gives each page's Tab menu rows.
#pragma once

#include "board_logic.h"
#include "module.h"

namespace cpa {

class BoardModule : public Module {
 public:
  const char* id() const override { return "board"; }
  bool begin(ModuleContext& ctx) override;
  void onEnter(uint8_t arg) override;
  bool onKey(const KeyEvent& key) override;
  void render(Canvas& c, const Rect& area) const override;
  const char* hint() const override;
  uint8_t contextSlot() const override { return ui_.slot; }
  uint8_t help(const HelpRow** rows) const override;

 private:
  void run(const BoardStep& st);
  void render_home(Canvas& c, const Rect& area) const;
  void render_project(Canvas& c, const Rect& area) const;
  void render_sessions(Canvas& c, const Rect& area, int16_t y) const;
  void render_queue(Canvas& c, const Rect& area, int16_t y) const;

  ModuleContext* ctx_ = nullptr;
  BoardUi ui_;
};

}  // namespace cpa
