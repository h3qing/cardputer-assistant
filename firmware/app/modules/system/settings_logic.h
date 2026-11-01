// 设置 / Settings (docs/21 §4.1-§4.4, §4.6): which page and row the user is on, the keys
// that move between them, and the facts the settings page and the WiFi page show. Pure.
//   (设置) (设备)        , / switch the tabs
//   ▸WiFi             Enter: the WiFi page (only words; ` goes back)
//    语言 Language     Enter: the other language
//    ─────
//    连接 / 同步        facts: the cursor never stops on them
// Every visit starts on the WiFi row (docs/18 §5.9): Enter there changes nothing.
// docs/28 S1a: a 存储 / Storage tab after them (, / go round all three: `,` from 设置):
//   ▸再交一次  ·3F2A 的 2 次     Enter: a box on 取消; / Enter: the last batch waits again,
//                                never for that Mac
//    给这台 Mac  4 段            Enter: what was kept from the Mac on the link is for it again
//    清掉留底  12 段            Enter: a box, the cursor on 取消; / then Enter clears
//    ─────
//    留底 12 段 · 最旧 5 天 / 待传 3 · 空闲 478G / 留底是明文：去别处前清掉
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
  bool yes = false;      // ... its cursor on 清掉 (it opens on 取消)
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

// The 连接 row: "USB 线 · " + the Mac's name, cut to fit, and 在线 / 离线 on the right.
struct LinkFacts {
  const char* head;   // "USB 线 · " / "USB · "
  FixedStr<32> name;  // host_label of the last hello, else host_app, else "--"
  const char* state;  // 在线 / online while Ready, else 离线 / offline
  bool online;
};
LinkFacts link_facts(const AppState& s, const SysInfo& info);

// The 同步 row: "3 分钟前 · 待传 ↑0 录2" (录N only when the device can record).
FixedStr<64> sync_text(const AppState& s, uint32_t now);

// docs/28 S1a, the storage tab's words: the rows' values and the facts below them.
FixedStr<48> again_value(const KeptView& k);  // "·3F2A 的 2 次", or 没有
FixedStr<48> give_value(const KeptView& k);   // "4 段", or 没有
FixedStr<48> clear_value(const KeptView& k);  // "12 段", or 没有
FixedStr<64> kept_fact(const AppState& s, uint32_t now);    // 留底 12 段 · 最旧 5 天
FixedStr<64> waiting_fact(const AppState& s);               // 待传 3 · 空闲 478G
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
