// Tab menu content (docs/13 §6.1-§6.3), pure: the rows of one page, built from kKeymap,
// a module's help() rows, AppState and SysInfo. Every row carries the key Enter
// replays (v0.5: the board's and the inbox's rows also their counts); a row whose ev.kind
// is None is shown only:
//   - a run of digit bindings merged into one row ("Opt+1-9");
//   - a hold (Action::RecMemo): a replay has no G0 release, so it would never stop;
//   - a hands-free memo's end (Action::RecStop, gh #51): Tab, Enter must not end it;
//   - a fact on Control, or a module row the module marked so.
// Pages:
//   Here     the module's help() rows, or else the bindings of the layer under the menu;
//            then the System-layer keys not listed yet (none over a confirm box, §5.3).
//            Never Tab: in the menu it closes the menu;
//   Go       the Global layer: places first (Home, Projects, Inbox, Project N, Settings),
//            then the rest in kKeymap order; Lock goes to Control instead;
//   Control  the Lock key, then Sound (read-only: the device has no sound since v0.3.1),
//            Bat, SD, Mac; then the status bar's legend (docs/20 U21: ↑N, !N, RN, ※kN).
// v0.5 (docs/19 §6.5): the key column is a keycap, so a range row's label no longer ends in
// "(press 1-9)" and the lock row is just "Lock" (the lock card says how to unlock).
// Over an overlay (compose, confirm, meeting page) there is no Go page: asking for it
// gives Here.
#pragma once

#include <cstddef>
#include <cstdint>

#include "key_label.h"
#include "keymap.h"
#include "module.h"

namespace cpa {

enum class MenuPage : uint8_t { Here, Goto, Control };  // Here / Go / Control

constexpr uint8_t kMenuRowsMax = 16;

using MenuText = FixedStr<63>;

// v0.5 (docs/19 §6.5, mockup 03): the count a row carries at its right end, as the home
// tiles do: runN on the row that opens the board, !N on the one that opens the inbox.
enum class MenuBadge : uint8_t { None, Running, NeedsYou };

struct MenuRow {
  KeyEvent ev;           // replayed by Enter; kind None: shown only
  int16_t binding = -1;  // kKeymap index (the first of a range); -1: module row or fact
  uint8_t span = 1;      // bindings the row stands for (a digit range: 9 or 10)
  MenuBadge badge = MenuBadge::None;  // only when its count is not zero
  uint32_t badge_n = 0;
  KeyLabel keys;         // UTF-8, the key column
  MenuText label;        // UTF-8, what it does
  bool fact = false;     // a fact on Control (Bat, the legend): it explains no key
};

// What the menu is opened over. title and help must outlive the content built from it.
struct MenuSource {
  Layer layer = Layer::List;      // the layer under the menu: its bindings make Here
  const HelpRow* help = nullptr;  // the module's own rows, used instead of the layer's
  uint8_t help_n = 0;
  bool overlay = false;           // compose, confirm or a recording page: no Go page
  const char* title = "";         // the page under the menu ("Meeting"); "" = Here
};

struct MenuContent {
  MenuPage page = MenuPage::Here;  // never Goto when overlay
  bool overlay = false;
  const char* title = "";
  uint8_t n = 0;
  bool overflow = false;  // a row did not fit in kMenuRowsMax
  MenuRow rows[kMenuRowsMax];
};

// The key table the menu reads; tests pass their own.
struct KeymapView {
  const Binding* b = kKeymap;
  size_t n = kKeymapCount;
};

MenuContent menu_content(MenuPage page, const MenuSource& src, const AppState& s, const SysInfo& sys,
                         const KeymapView& km = KeymapView{});

const char* menu_page_name(MenuPage p);  // Here / Go / Control
int menu_battery_pct(uint16_t mv);       // the status bar's curve; -1 when unknown

}  // namespace cpa
