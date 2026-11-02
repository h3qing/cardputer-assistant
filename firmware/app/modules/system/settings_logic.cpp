#include "settings_logic.h"

#include <cstdio>

#include "keymap.h"
#include "system_logic.h"
#include "system_texts.h"
#include "text_fit.h"

namespace cpa {
namespace {

SystemStep handled(const SystemUi& ui, SysCmd cmd = SysCmd::None) { return SystemStep{ui, cmd, true}; }

SystemUi with_tab(const SystemUi& ui, SysTab tab) {
  SystemUi out = ui;
  out.tab = tab;
  out.wifi = false;
  return out;
}

SystemUi with_row(const SystemUi& ui, SysRow row) {
  SystemUi out = ui;
  out.row = row;
  return out;
}

SystemUi with_wifi(const SystemUi& ui, bool wifi) {
  SystemUi out = ui;
  out.wifi = wifi;
  return out;
}

bool is_back(const KeyEvent& ev) {
  return keymap_lookup(Layer::List, ev).action == Action::Back ||
         keymap_lookup(Layer::Global, ev).action == Action::Escape;
}

bool is_list_key(Action a) {
  return a == Action::Up || a == Action::Down || a == Action::TabPrev || a == Action::TabNext ||
         a == Action::Primary;
}

// The WiFi page has nothing to choose: back goes to the settings tab, the list keys do nothing.
SystemStep wifi_next(const SystemUi& ui, const KeyEvent& ev) {
  if (is_back(ev)) return handled(with_wifi(ui, false));
  if (is_list_key(keymap_lookup(Layer::List, ev).action)) return handled(ui);
  return SystemStep{ui, SysCmd::None, false};
}

SystemStep settings_next(const SystemUi& ui, Action a) {
  switch (a) {
    case Action::Up: return handled(with_row(ui, SysRow::Wifi));
    case Action::Down: return handled(with_row(ui, SysRow::Lang));
    case Action::Primary:
      if (ui.row == SysRow::Wifi) return handled(with_wifi(ui, true));
      return handled(ui, SysCmd::SwitchLang);
    default: return SystemStep{ui, SysCmd::None, false};
  }
}

SystemUi with_store(const SystemUi& ui, StoreRow row, bool confirm, bool yes) {
  SystemUi out = ui;
  out.store = row;
  out.confirm = confirm;
  out.yes = yes;
  return out;
}

// "Clear kept files?" / "Send to another Mac?": any move flips the cursor, Enter does what it
// is on, back is Cancel.
SystemStep confirm_next(const SystemUi& ui, const KeyEvent& ev) {
  if (is_back(ev)) return handled(with_store(ui, ui.store, false, false));
  const Action a = keymap_lookup(Layer::List, ev).action;
  if (a == Action::Primary) {
    const SysCmd yes = ui.store == StoreRow::Again ? SysCmd::DeliverAgain : SysCmd::ClearKept;
    return handled(with_store(ui, ui.store, false, false), ui.yes ? yes : SysCmd::None);
  }
  if (is_list_key(a)) return handled(with_store(ui, ui.store, true, !ui.yes));
  return handled(ui);  // nothing else leaves the box by accident
}

// Enter on a row: its box (Again, Clear), the ask (Give), or why there is nothing to do.
SystemStep storage_enter(const SystemUi& ui, const KeptView& k) {
  switch (ui.store) {
    case StoreRow::Again:
      if (k.batch_host == 0 || k.batch_recs == 0) return handled(ui, SysCmd::NoBatch);
      return handled(with_store(ui, ui.store, true, false));
    case StoreRow::Give: return handled(ui, k.held_here == 0 ? SysCmd::NoHeld : SysCmd::GiveHere);
    default:
      if (k.segs == 0) return handled(ui, SysCmd::NoKept);
      return handled(with_store(ui, ui.store, true, false));
  }
}

SystemStep storage_next(const SystemUi& ui, Action a, const KeptView& k) {
  const uint8_t row = static_cast<uint8_t>(ui.store);
  switch (a) {
    case Action::Up: return handled(with_store(ui, static_cast<StoreRow>(row > 0 ? row - 1 : 0), false, false));
    case Action::Down: return handled(with_store(ui, static_cast<StoreRow>(row < 2 ? row + 1 : 2), false, false));
    case Action::Primary: return storage_enter(ui, k);
    default: return SystemStep{ui, SysCmd::None, false};
  }
}

SysTab next_tab(SysTab t, bool forward) {
  const uint8_t n = 3;
  const uint8_t i = static_cast<uint8_t>(t);
  return static_cast<SysTab>(forward ? (i + 1) % n : (i + n - 1) % n);
}

}  // namespace

SystemStep system_next(const SystemUi& ui, const KeyEvent& ev, const KeptView& kept) {
  if (ui.wifi) return wifi_next(ui, ev);
  if (ui.confirm) return confirm_next(ui, ev);
  const Action a = keymap_lookup(Layer::List, ev).action;
  if (a == Action::TabPrev || a == Action::TabNext) return handled(with_tab(ui, next_tab(ui.tab, a == Action::TabNext)));
  if (ui.tab == SysTab::Settings) return settings_next(ui, a);
  if (ui.tab == SysTab::Storage) return storage_next(ui, a, kept);
  return is_list_key(a) ? handled(ui) : SystemStep{ui, SysCmd::None, false};  // the device tab is read only
}

LinkFacts link_facts(const AppState& s, const SysInfo& info) {
  LinkFacts f{tr(txt::kOverUsb), FixedStr<32>::from("--"), tr(txt::kOffline), false};
  if (!info.host_label.empty()) {
    f.name = FixedStr<32>::from(info.host_label.c_str(), info.host_label.size());
  } else if (!info.host_app.empty()) {
    f.name = info.host_app;
  }
  f.online = s.link.mode == LinkMode::Ready;
  if (f.online) f.state = tr(txt::kOnline);
  return f;
}

FixedStr<64> sync_text(const AppState& s, uint32_t now) {
  char when[32];
  if (s.link.last_rx_ms == 0) {
    std::snprintf(when, sizeof when, "%s", tr(txt::kNever));
  } else {
    const FixedStr<16> age = age_text((now - s.link.last_rx_ms) / 1000);
    std::snprintf(when, sizeof when, tr(txt::kAgo), age.c_str());
  }
  char buf[80];
  const int n = std::snprintf(buf, sizeof buf, tr(txt::kToSync), when, static_cast<unsigned>(s.outbox.pending));
  if (s.rec.avail && n > 0 && static_cast<size_t>(n) < sizeof buf) {
    const int m = std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), tr(txt::kRecsToSync),
                                static_cast<unsigned>(s.rec.files_n));
    const size_t at = static_cast<size_t>(n) + (m > 0 ? static_cast<size_t>(m) : 0);
    if (s.rec.kept.segs > 0 && at < sizeof buf) {  // docs/28: the kept copies are plain too
      std::snprintf(buf + at, sizeof buf - at, tr(txt::kKeptToSync), static_cast<unsigned>(s.rec.kept.segs));
    }
  }
  return FixedStr<64>::from(buf);
}

FixedStr<8> host_tag(uint32_t host) {
  char buf[12];
  std::snprintf(buf, sizeof buf, "\xC2\xB7%04X", static_cast<unsigned>(host >> 16));  // ·3F2A
  return FixedStr<8>::from(buf);
}

FixedStr<48> again_value(const KeptView& k) {
  if (k.batch_host == 0 || k.batch_recs == 0) return FixedStr<48>::from(tr(txt::kNoneValue));
  char buf[64];
  const FixedStr<8> tag = host_tag(k.batch_host);
  std::snprintf(buf, sizeof buf, tr(txt::kAgainValue), tag.c_str(), static_cast<unsigned>(k.batch_recs));
  return FixedStr<48>::from(buf);
}

FixedStr<48> give_value(const KeptView& k) {
  if (k.held_here == 0) return FixedStr<48>::from(tr(txt::kNoneValue));
  char buf[48];
  std::snprintf(buf, sizeof buf, trn(txt::kSegsValue, k.held_here), static_cast<unsigned>(k.held_here));
  return FixedStr<48>::from(buf);
}

FixedStr<48> clear_value(const KeptView& k) {
  if (k.segs == 0) return FixedStr<48>::from(tr(txt::kNoneValue));
  char buf[48];
  std::snprintf(buf, sizeof buf, trn(txt::kSegsValue, k.segs), static_cast<unsigned>(k.segs));
  return FixedStr<48>::from(buf);
}

FixedStr<64> kept_fact(const AppState& s, uint32_t now) {
  char buf[80];
  const int n = std::snprintf(buf, sizeof buf, trn(txt::kKeptFact, s.rec.kept.segs), static_cast<unsigned>(s.rec.kept.segs));
  const int64_t wall_ms = static_cast<int64_t>(now) + s.time.offset_ms;
  const int64_t age_s = wall_ms / 1000 - static_cast<int64_t>(s.rec.kept.oldest_s);
  if (s.time.known && s.rec.kept.oldest_s && s.rec.kept.segs > 0 && age_s >= 0 && n > 0) {
    std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), tr(txt::kOldestDays),
                  static_cast<unsigned>(age_s / 86400));
  }
  return FixedStr<64>::from(buf);
}

FixedStr<64> waiting_fact(const AppState& s) {
  char buf[80];
  const FixedStr<16> free = s.rec.free_known ? size_text(static_cast<uint64_t>(s.rec.free_mib) << 20) : FixedStr<16>::from("--");
  std::snprintf(buf, sizeof buf, tr(txt::kWaitingFact), static_cast<unsigned>(s.rec.files_n), free.c_str());
  return FixedStr<64>::from(buf);
}

WifiRows wifi_rows(bool launcher) {
  return WifiRows{{
      {&txt::kNow, &txt::kNoWifi},
      {&txt::kData, &txt::kUsbOnly},
      {&txt::kWant, &txt::kFactory},
      {nullptr, launcher ? &txt::kViaLauncher : &txt::kNeedsLauncher},
      {&txt::kMind, &txt::kPrintsPasswords},
      {&txt::kLater, &txt::kNotPlanned},
  }};
}

}  // namespace cpa
