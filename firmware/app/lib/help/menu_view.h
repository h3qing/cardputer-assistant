// Tab menu keys and drawing (docs/13 §6.1), pure. The shell keeps a MenuState while
// the menu is open, builds the MenuContent of state.page with menu_content() and:
//   ; .            move the selection (clamped; the list scrolls to keep it visible)
//   , /            previous / next page (wrapping; no 去哪 over an overlay)
//   Enter          Replay(row.ev), or None on a row that is shown only
//   a letter       on 去哪 and 控制: Replay(Opt+letter) when a row there replays it;
//                  on 本页 not over an overlay: Replay(Opt+letter) when the Global or
//                  System layer binds it (docs/13 §3.3: Tab, then a letter)
//   G0 tap / hold  Replay(the same key): the menu closes and G0 does what it does
//                  under it (★ in a meeting; §6.1)
//   Tab ` 退格 Fn+`  Close
//   anything else  None: the menu eats it
// Replay and Close both mean "close the menu"; Replay then dispatches ev through the
// normal key path (§5.4 step 9).
#pragma once

#include <cstdint>

#include "canvas.h"
#include "menu_model.h"
#include "theme_anim.h"
#include "txt.h"

namespace cpa {

constexpr uint8_t kMenuVisibleRows = 6;
constexpr int16_t kMenuRowH = 14;
constexpr int16_t kMenuKeyColW = 54;  // labels start at most this far into a row

// The menu's hint bar. mockup 03: letters still jump (menu_key), but 字母直达 is left out of
// the hint, which it pushed to the bar's tightest spacing. English brackets its keys.
constexpr Txt kMenuHint{";.选 Enter做 ,/换页 Tab关", "[;.]pick [Enter]do [,/]page [Tab]close"};

struct MenuState {
  MenuPage page = MenuPage::Here;
  uint8_t sel = 0;  // selected row
  uint8_t top = 0;  // first row on screen
};

enum class MenuOut : uint8_t { None, Replay, Close };

struct MenuStep {
  MenuState state;
  MenuOut out = MenuOut::None;
  KeyEvent ev;  // Replay: the key to dispatch
};

MenuStep menu_key(const MenuState& st, const KeyEvent& ev, const MenuContent& m);

// v0.5 (docs/19 §6.5): the menu as a sheet from the right over the page, which the caller
// has drawn and which is dimmed here; the status bar (y < kBarH, ● and the timer while
// recording) is never touched. On the sheet: the pages as a segmented control (本页 / 去哪 /
// 控制, the current one on a caramel knob), up to kMenuVisibleRows rows of [key] label, a
// scroll bar when rows are off screen; the menu's hint as keycaps in the hint bar.
// v0.5 S3 (docs/19 §6.5): when the menu opens the sheet slides in from the right, and when
// , or / changes the page the knob slides to it, in theme::kSlideFrames frames: frame 0 is
// drawn by the full redraw, the last is at rest.
enum class MenuSlide : uint8_t { None, Sheet, Knob };
struct MenuMotion {
  MenuSlide slide = MenuSlide::None;
  MenuPage from = MenuPage::Here;  // Knob: the page it leaves
  uint8_t frame = theme::kSlideFrames - 1;
};

void menu_render(Canvas& c, const MenuState& st, const MenuContent& m, const MenuMotion& mo = MenuMotion{});
// A later frame of a slide, drawn over the one before: the sheet where it is now (Sheet) or
// the segmented control (Knob). Returns the rectangle to push.
Rect menu_render_slide(Canvas& c, const MenuState& st, const MenuContent& m, const MenuMotion& mo);

// The sheet's left edge: 66 px, or further left (to 6) when the page's widest row needs it.
int16_t menu_sheet_x(const Canvas& c, const MenuContent& m);

}  // namespace cpa
