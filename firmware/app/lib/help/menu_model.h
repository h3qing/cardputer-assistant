// Tab menu content (docs/13 §6.1-§6.3), pure: the rows of one page, built from kKeymap,
// a module's help() rows, AppState and SysInfo. Every row carries the key Enter
// replays (v0.5: the board's and the inbox's rows also their counts); a row whose ev.kind
// is None is shown only:
//   - a run of digit bindings merged into one row ("Opt+1-9");
//   - a hold (Action::RecMemo): a replay has no G0 release, so it would never stop;
//   - a hands-free memo's end (Action::RecStop, gh #51): Tab, Enter must not end it;
//   - a fact on 控制, or a module row the module marked so.
// Pages:
//   本页  the module's help() rows, or else the bindings of the layer under the menu;
//         then the System-layer keys not listed yet (none over a confirm box, §5.3).
//         Never Tab: in the menu it closes the menu;
//   去哪  the Global layer: places first (主页, 项目, 等你, 第 N 个项目页, 设置), then
//         the rest in kKeymap order; Lock goes to 控制 instead;
//   控制  the Lock key, then 声音 (read-only: the device has no sound since v0.3.1),
//         电池, SD, Mac; then the status bar's legend (docs/20 U21: ↑N, !N, 录N, ※kN).
// v0.5 (docs/19 §6.5): the key column is a keycap, so a range row's label no longer ends in
// "（按 1-9）" and the lock row is just 锁屏 (the lock card says how to unlock).
// Over an overlay (compose, confirm, meeting page) there is no 去哪: asking for it
// gives 本页.
#pragma once

#include <cstddef>
#include <cstdint>

#include "key_label.h"
#include "keymap.h"
#include "module.h"

namespace cpa {

enum class MenuPage : uint8_t { Here, Goto, Control };  // 本页 / 去哪 / 控制

constexpr uint8_t kMenuRowsMax = 16;

using MenuText = FixedStr<63>;

// v0.5 (docs/19 §6.5, mockup 03): the count a row carries at its right end, as the home
// tiles do: 跑N on the row that opens the board, !N on the one that opens the inbox.
enum class MenuBadge : uint8_t { None, Running, NeedsYou };

struct MenuRow {
  KeyEvent ev;           // replayed by Enter; kind None: shown only
  int16_t binding = -1;  // kKeymap index (the first of a range); -1: module row or fact
  uint8_t span = 1;      // bindings the row stands for (a digit range: 9 or 10)
  MenuBadge badge = MenuBadge::None;  // only when its count is not zero
  uint32_t badge_n = 0;
  KeyLabel keys;         // UTF-8, the key column
  MenuText label;        // UTF-8, what it does
  bool fact = false;     // a fact on 控制 (电池, the legend): it explains no key
};

// What the menu is opened over. title and help must outlive the content built from it.
struct MenuSource {
  Layer layer = Layer::List;      // the layer under the menu: its bindings make 本页
  const HelpRow* help = nullptr;  // the module's own rows, used instead of the layer's
  uint8_t help_n = 0;
  bool overlay = false;           // compose, confirm or a recording page: no 去哪
  const char* title = "";         // the page under the menu ("会议录音"); "" = 本页
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

const char* menu_page_name(MenuPage p);  // 本页 / 去哪 / 控制
int menu_battery_pct(uint16_t mv);       // the status bar's curve; -1 when unknown

}  // namespace cpa
