#include "system_module.h"

#include <cstddef>

#include "system_texts.h"

namespace cpa {
namespace {

constexpr KeyEvent key_chr(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }
constexpr KeyEvent kEnter{KeyEventKind::Nav, 0, 0, 0, Nav::Enter};
constexpr KeyEvent kHomeKey{KeyEventKind::Chord, 0, kModOpt, 'h', Nav::None};

// The Tab menu's "Here" rows (docs/21 §4.6): each replays the key it names, so the menu
// switches the language too (Enter on the Language row).
constexpr HelpRow kSettingsHelp[] = {
    {key_chr(';'), ";", "", &txt::kUp},
    {key_chr('.'), ".", "", &txt::kDown},
    {kEnter, "Enter", "", &txt::kChangeRow},
    {key_chr(','), ",", "", &txt::kPrevTab},
    {key_chr('/'), "/", "", &txt::kNextTab},
    {key_chr('`'), "`", "", &txt::kBackRow},
    {kHomeKey, "Opt+H", "", &txt::kHome},
};
constexpr HelpRow kDeviceHelp[] = {
    {key_chr(','), ",", "", &txt::kPrevTab},
    {key_chr('/'), "/", "", &txt::kNextTab},
    {key_chr('`'), "`", "", &txt::kBackRow},
    {kHomeKey, "Opt+H", "", &txt::kHome},
};
// docs/28 S1a: the storage tab, and its "Clear kept files?" box (Enter does what the cursor is on).
constexpr HelpRow kStorageHelp[] = {
    {key_chr(';'), ";", "", &txt::kUp},
    {key_chr('.'), ".", "", &txt::kDown},
    {kEnter, "Enter", "", &txt::kDoRow},
    {key_chr(','), ",", "", &txt::kPrevTab},
    {key_chr('/'), "/", "", &txt::kNextTab},
    {key_chr('`'), "`", "", &txt::kBackRow},
    {kHomeKey, "Opt+H", "", &txt::kHome},
};
constexpr HelpRow kConfirmHelp[] = {
    {key_chr('/'), "/", "", &txt::kOtherButton},
    {kEnter, "Enter", "", &txt::kDoRow},
    {key_chr('`'), "`", "", &txt::kClearNo},
};
constexpr HelpRow kWifiHelp[] = {
    {key_chr('`'), "`", "", &txt::kBackToSettings},
    {kHomeKey, "Opt+H", "", &txt::kHome},
};

// The hint bars (§4.6); the shell adds [Tab] menu. , / are not on the settings
// tab's bar: the tab pills are on the screen and on the Tab menu.
constexpr HintKey kSettingsHint[] = {{";.", txt::kPick}, {"Enter", txt::kChange}, {"`", txt::kBack}};
constexpr HintKey kDeviceHint[] = {{",/", txt::kSwitchPage}, {"`", txt::kBack}};
constexpr HintKey kWifiHint[] = {{"`", txt::kBack}};
constexpr HintKey kStorageHint[] = {{";.", txt::kPick}, {"Enter", txt::kDo}, {"`", txt::kBack}};
constexpr HintKey kConfirmHint[] = {{",/", txt::kPick}, {"Enter", txt::kDo}, {"`", txt::kBack}};

template <class T, size_t N>
uint8_t give(const T (&rows)[N], const T** out) {
  *out = rows;
  return static_cast<uint8_t>(N);
}

}  // namespace

bool SystemModule::begin(ModuleContext& ctx) {
  ctx_ = &ctx;
  return true;
}

// docs/21 §4.2: every visit starts on the settings tab, on the WiFi row.
void SystemModule::onEnter(uint8_t) { ui_ = SystemUi{}; }

bool SystemModule::onKey(const KeyEvent& key) {
  const KeptView& kept = ctx_->state().rec.kept;
  const SystemStep st = system_next(ui_, key, kept);
  ui_ = st.ui;
  if (st.cmd == SysCmd::SwitchLang) ctx_->setLanguage(lang_other(lang()));
  kept_cmd(st.cmd, kept.busy);
  return st.handled;
}

// docs/28 S1a: what a storage row asked: the ledger (one at a time), or why there is nothing.
void SystemModule::kept_cmd(SysCmd cmd, bool busy) {
  switch (cmd) {
    case SysCmd::NoKept: return ctx_->hint(ToastLevel::Info, tr(txt::kNoKept));
    case SysCmd::NoBatch: return ctx_->hint(ToastLevel::Info, tr(txt::kNothingAgain));
    case SysCmd::NoHeld: return ctx_->hint(ToastLevel::Info, tr(txt::kNothingHeld));
    case SysCmd::DeliverAgain:
    case SysCmd::ClearKept:
    case SysCmd::GiveHere: break;
    default: return;
  }
  if (busy) return ctx_->hint(ToastLevel::Info, tr(txt::kWorking));
  const KeptAsk a = cmd == SysCmd::DeliverAgain ? KeptAsk::Again : (cmd == SysCmd::ClearKept ? KeptAsk::Clear : KeptAsk::Give);
  ctx_->kept(a);
}

uint8_t SystemModule::help(const HelpRow** rows) const {
  if (ui_.wifi) return give(kWifiHelp, rows);
  if (ui_.confirm) return give(kConfirmHelp, rows);
  if (ui_.tab == SysTab::Storage) return give(kStorageHelp, rows);
  return ui_.tab == SysTab::Settings ? give(kSettingsHelp, rows) : give(kDeviceHelp, rows);
}

uint8_t SystemModule::hintKeys(const HintKey** keys) const {
  if (ui_.wifi) return give(kWifiHint, keys);
  if (ui_.confirm) return give(kConfirmHint, keys);
  if (ui_.tab == SysTab::Storage) return give(kStorageHint, keys);
  return ui_.tab == SysTab::Settings ? give(kSettingsHint, keys) : give(kDeviceHint, keys);
}

void SystemModule::render(Canvas& c, const Rect& area) const {
  if (ui_.wifi) return render_wifi(c, area);
  render_tabs(c, area);
  if (ui_.tab == SysTab::Settings) return render_settings(c, area);
  if (ui_.tab == SysTab::Storage) return render_storage(c, area);
  render_device(c, area);
}

}  // namespace cpa
