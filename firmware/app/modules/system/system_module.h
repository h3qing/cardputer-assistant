// Settings (Opt+M; docs/21 §4): the settings tab first (WiFi, the language, the
// link and the last sync), the storage tab (docs/28 S1a: the kept copies) and the device
// tab with today's diagnostics (04 US-16) one `/` each away, and the WiFi page under the
// WiFi row. The language is switched through the shell
// (ModuleContext::setLanguage), never stored here. The module id stays "system": the HIL
// and debug.state.mod find the page by it.
#pragma once

#include "module.h"
#include "settings_logic.h"
#include "system_logic.h"

namespace cpa {

class SystemModule : public Module {
 public:
  const char* id() const override { return "system"; }
  bool begin(ModuleContext& ctx) override;
  void onEnter(uint8_t arg) override;
  bool onKey(const KeyEvent& key) override;
  void render(Canvas& c, const Rect& area) const override;
  uint8_t help(const HelpRow** rows) const override;  // the Tab menu's "Here" rows, by page
  uint8_t hintKeys(const HintKey** keys) const override;
  const SystemUi& ui() const { return ui_; }

 private:
  void kept_cmd(SysCmd cmd, bool busy);
  void render_tabs(Canvas& c, const Rect& area) const;
  void render_settings(Canvas& c, const Rect& area) const;
  void render_facts(Canvas& c, const Rect& area) const;
  void render_device(Canvas& c, const Rect& area) const;
  void render_wifi(Canvas& c, const Rect& area) const;
  void render_storage(Canvas& c, const Rect& area) const;  // system_storage.cpp (docs/28)
  void render_clear_box(Canvas& c, const Rect& area) const;

  ModuleContext* ctx_ = nullptr;
  SystemUi ui_;
};

}  // namespace cpa
