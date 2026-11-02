// Settings (docs/21 §4.1-§4.4, §4.6): which page and row the user is on, the keys
// that move between them, and the facts the settings page and the WiFi page show. Pure.
//   (Settings) (Device)  , / switch the tabs
//   ▸WiFi             Enter: the WiFi page (only words; ` goes back)
//    语言 Language     Enter: the other language
//    ─────
//    Link / Sync       facts: the cursor never stops on them
// Every visit starts on the WiFi row (docs/18 §5.9): Enter there changes nothing.
// docs/28 S1a: a Storage tab after them (, / go round all three: `,` from Settings):
//   ▸Send again  from ·3F2A: 2   Enter: a box on Cancel; / Enter: the last batch waits again,
//                                never for that Mac
//    To this Mac  4 files        Enter: what was kept from the Mac on the link is for it again
//    Clear kept  12 files        Enter: a box, the cursor on Cancel; / then Enter clears
//    ─────
//    Kept 12 files · oldest 5 d / To sync 3 · free 478G / Kept copies are plain: clear first
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

enum class SysTab : uint8_t { Settings, Device, Storage };
enum class SysRow : uint8_t { Wifi, Lang };
enum class StoreRow : uint8_t { Again, Give, Clear };

struct SystemUi {
  SysTab tab = SysTab::Settings;
  SysRow row = SysRow::Wifi;
  bool wifi = false;  // the WiFi page is open over the settings tab
  StoreRow store = StoreRow::Again;
  bool confirm = false;  // the box of `store` (Again or Clear) is open
  bool yes = false;      // ... its cursor on Clear (it opens on Cancel)
};

// NoKept / NoBatch / NoHeld: Enter on a storage row with nothing to do (the module says so).
enum class SysCmd : uint8_t { None, SwitchLang, DeliverAgain, ClearKept, GiveHere, NoKept, NoBatch, NoHeld };

struct SystemStep {
  SystemUi ui;
  SysCmd cmd = SysCmd::None;
  bool handled = false;  // false: the shell's (` and Backspace leave the page)
};

// kept: the kept copies now (System -> storage's rows act, or open their box, only when they
// have something to do).
SystemStep system_next(const SystemUi& ui, const KeyEvent& ev, const KeptView& kept = KeptView{});

// The Link row: "USB · " + the Mac's name, cut to fit, and "online" / "offline" on the right.
struct LinkFacts {
  const char* head;   // "USB · "
  FixedStr<32> name;  // host_label of the last hello, else host_app, else "--"
  const char* state;  // "online" while Ready, else "offline"
  bool online;
};
LinkFacts link_facts(const AppState& s, const SysInfo& info);

// The Sync row: "3 min ago · to sync ↑0 R2" (RN only when the device can record).
FixedStr<64> sync_text(const AppState& s, uint32_t now);

// docs/28 S1a, the storage tab's words: the rows' values and the facts below them.
FixedStr<48> again_value(const KeptView& k);  // "from ·3F2A: 2", or "none"
FixedStr<48> give_value(const KeptView& k);   // "4 files", or "none"
FixedStr<48> clear_value(const KeptView& k);  // "12 files", or "none"
FixedStr<64> kept_fact(const AppState& s, uint32_t now);    // Kept 12 files · oldest 5 d
FixedStr<64> waiting_fact(const AppState& s);               // To sync 3 · free 478G
FixedStr<8> host_tag(uint32_t host);                        // ·3F2A

// The WiFi page's rows: a label (nullptr: the row goes on from the one above) and words.
struct WifiRow {
  const Txt* label;
  const Txt* words;
};
constexpr uint8_t kWifiRows = 6;
struct WifiRows {
  WifiRow rows[kWifiRows];
};
WifiRows wifi_rows(bool launcher);

}  // namespace cpa
