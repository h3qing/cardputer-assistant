// Inbox module (txt::kWaiting), a thin shell over inbox_logic.
#pragma once

#include "inbox_logic.h"
#include "module.h"

namespace cpa {

class InboxModule : public Module {
 public:
  const char* id() const override { return "inbox"; }
  bool begin(ModuleContext& ctx) override;
  void onEnter(uint8_t arg) override;
  bool onKey(const KeyEvent& key) override;
  void render(Canvas& c, const Rect& area) const override;
  const char* hint() const override;
  uint8_t contextSlot() const override;
  uint8_t help(const HelpRow** rows) const override;  // the Tab menu's "Here" rows

 private:
  ModuleContext* ctx_ = nullptr;
  InboxUi ui_;
};

}  // namespace cpa
