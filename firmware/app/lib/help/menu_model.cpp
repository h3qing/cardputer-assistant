#include "menu_model.h"

#include <cstdio>
#include <cstring>

#include "action_help.h"
#include "reducers.h"
#include "text_fit.h"
#include "words.h"

namespace cpa {
namespace {

// The status bar's battery curve (status_bar.cpp); test_menu_view checks they agree.
constexpr int kBatEmptyMv = 3300;
constexpr int kBatFullMv = 4150;

using Num = FixedStr<15>;

// docs/21 §6.7: the menu's words. Facts on 控制 are a word (KeyTxt) and a line.
constexpr MenuTxt kExitAsks{"退出（有字先问）", "Exit (asks if text)"};
constexpr MenuTxt kLockRecording{"锁屏（录音不停）", "Lock, keep recording"};
constexpr MenuTxt kLockDraft{"锁屏（草稿留着）", "Lock, keep the draft"};
constexpr MenuTxt kCancelLock{"取消并锁屏", "Cancel and lock"};
constexpr Txt kSdFacts{"剩 %s · 待传 录%u · ↑%u", "%s free · R%u · ↑%u"};
constexpr MenuTxt kOnline{"在线", "online"};
constexpr MenuTxt kMismatch{"版本不兼容", "mismatch"};
constexpr MenuTxt kNoReply{"无响应", "no reply"};
constexpr MenuTxt kOffline{"离线", "offline"};
constexpr Txt kSynced{"已同步：都传到 Mac 了", "Synced: all on the Mac"};  // 126 px in Chinese: the row cuts it
constexpr CountTxt kNotesWait{"%u 条笔记等着传到 Mac", "%u note waits for the Mac", "%u notes wait for the Mac",
                              kMenuTxtW};
constexpr CountTxt kSessionsWait{"%u 个 session 等你", "%u session needs you", "%u sessions need you", kMenuTxtW};
constexpr CountTxt kRecsWait{"%u 段录音等着传", "%u recording to sync", "%u recordings to sync", kMenuTxtW};
constexpr Txt kSdBad{"SD 卡不能用：不能录音", "SD unusable: no recording"};  // as kSynced
constexpr Txt kKeysLost{"键盘按太快，丢了 %u 次", "Typed too fast: %u lost", kMenuTxtW};
constexpr Txt kRecSymbol{"录", "R"};  // the status bar's 录N / RN
constexpr KeyTxt kSound{"声音", "Sound"};
constexpr MenuTxt kSoundOff{"关（录音不出声）", "Off (silent)"};
constexpr KeyTxt kBattery{"电池", "Bat"};
constexpr MenuTxt kHerePage{"本页", "Here"};
constexpr MenuTxt kGotoPage{"去哪", "Go"};
constexpr MenuTxt kControlPage{"控制", "Control"};

Num num(unsigned v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%u", v);
  return Num::from(buf);
}

// The longest prefix of s that fits FixedStr<N> and ends between UTF-8 characters.
template <size_t N>
FixedStr<N> utf8_prefix(const char* s) {
  const size_t n = s ? std::strlen(s) : 0;
  size_t at = 0;
  while (at < n) {
    const size_t next = at + utf8_at(s, n, at).len;
    if (next > N) break;
    at = next;
  }
  return FixedStr<N>::from(s, at);
}

void add(MenuContent& m, const MenuRow& r) {
  if (m.n >= kMenuRowsMax) {
    m.overflow = true;
    return;
  }
  m.rows[m.n] = r;
  m.n++;
}

// The digit a Char or Chord key types, or 0.
char digit_of(const KeyEvent& e) {
  const char c = e.kind == KeyEventKind::Chord ? e.key : (e.kind == KeyEventKind::Char ? e.ch : 0);
  return (c >= '0' && c <= '9') ? c : 0;
}

// km.b[i + 1] continues a digit run from km.b[i]: same layer, action and key kind.
bool continues(const KeymapView& km, size_t i) {
  if (i + 1 >= km.n) return false;
  const Binding& a = km.b[i];
  const Binding& b = km.b[i + 1];
  const char d = digit_of(a.trigger);
  return d != 0 && digit_of(b.trigger) == d + 1 && a.layer == b.layer && a.action == b.action &&
         a.trigger.kind == b.trigger.kind && a.trigger.mods == b.trigger.mods;
}

uint8_t run_length(const KeymapView& km, size_t i) {
  uint8_t n = 1;
  while (continues(km, i + n - 1)) n++;
  return n;
}

// One row for km.b[i .. i + span). A range or a hold to record is shown only: a replay
// has no G0 release, so a replayed memo would never stop. So is a hands-free memo's end
// (gh #51): Tab, Enter are two pocket presses, and they must not end it.
MenuRow binding_row(const KeymapView& km, size_t i, uint8_t span, const char* label) {
  const Binding& b = km.b[i];
  MenuRow r;
  r.binding = static_cast<int16_t>(i);
  r.span = span;
  if (span > 1) {
    r.keys = key_range_label(b.trigger, km.b[i + span - 1].trigger);
    r.label = MenuText::from(label);
    return r;
  }
  r.keys = key_label(b.trigger);
  r.label = MenuText::from(label);
  r.ev = b.action == Action::RecMemo || b.action == Action::RecStop ? KeyEvent{} : b.trigger;
  return r;
}

// Rows for the bindings of `layer` that pick() labels (nullptr: skip), in kKeymap order.
template <class Pick>
void add_layer(MenuContent& m, const KeymapView& km, Layer layer, Pick pick) {
  for (size_t i = 0; i < km.n;) {
    const Binding& b = km.b[i];
    const char* label = b.layer == layer ? pick(b) : nullptr;
    const uint8_t span = label ? run_length(km, i) : 1;
    if (label) add(m, binding_row(km, i, span, label));
    i += span;
  }
}

// A module's row (a range row is shown only: its ev is None).
MenuRow help_row(const HelpRow& h) {
  MenuRow r;
  r.ev = h.ev;
  r.keys = utf8_prefix<KeyLabel::capacity()>(h.keys);
  r.label = utf8_prefix<MenuText::capacity()>(help_label(h));
  return r;
}

const char* system_label(Action a, Layer under) {
  if (a == Action::Cancel && under == Layer::Compose) return tr(kExitAsks);  // docs/20 D5
  if (a != Action::Lock) return action_help(a);
  if (under == Layer::RecPage || under == Layer::RecLatch) return tr(kLockRecording);
  if (under == Layer::Compose) return tr(kLockDraft);
  if (under == Layer::RecConfirm) return tr(kCancelLock);  // docs/20 D3: the start box (the end box has its own row)
  return action_help(a);
}

bool listed(const MenuContent& m, const KeyEvent& ev) {
  for (uint8_t i = 0; i < m.n; ++i) {
    if (m.rows[i].ev == ev) return true;
  }
  return false;
}

// 本页: the module's rows or the layer's, then the System keys not listed yet (docs/13
// §6.2). Never Tab: it closes the menu. Over a confirm box Opt+L does nothing (§5.3).
void add_here(MenuContent& m, const MenuSource& src, const KeymapView& km) {
  const Layer under = src.layer;
  const auto label = [under](const Binding& b) {
    return b.action == Action::Menu ? nullptr : system_label(b.action, under);
  };
  if (src.help && src.help_n > 0) {
    for (uint8_t i = 0; i < src.help_n; ++i) add(m, help_row(src.help[i]));
  } else {
    add_layer(m, km, under, label);
  }
  if (under == Layer::Confirm) return;
  add_layer(m, km, Layer::System, [&m, &label](const Binding& b) { return listed(m, b.trigger) ? nullptr : label(b); });
}

// 去哪 lists places first (docs/13 §6.3), then the rest; -1: on 控制 instead.
int goto_rank(Action a) {
  switch (a) {
    case Action::Home: return 0;
    case Action::Projects: return 1;
    case Action::Inbox: return 2;
    case Action::OpenProject: return 3;
    case Action::System: return 4;
    case Action::Lock: return -1;
    default: return 5;
  }
}

void add_goto(MenuContent& m, const KeymapView& km) {
  for (int rank = 0; rank <= 5; ++rank) {
    add_layer(m, km, Layer::Global,
              [rank](const Binding& b) { return goto_rank(b.action) == rank ? action_help(b.action) : nullptr; });
  }
}

MenuRow fact(const char* keys, const MenuText& label) {
  MenuRow r;
  r.keys = KeyLabel::from(keys);
  r.label = label;
  r.fact = true;
  return r;
}

MenuText battery_text(uint16_t mv) {
  const int pct = menu_battery_pct(mv);
  if (pct < 0) return MenuText::from("--");
  return MenuText::from(num(static_cast<unsigned>(pct)).c_str()).append("% · ").append(num(mv).c_str()).append("mV");
}

// "900M", "1.5G", "412G".
Num mib_text(uint32_t mib) {
  if (mib < 1024) return num(mib).append("M");
  const uint64_t tenths = static_cast<uint64_t>(mib) * 10 / 1024;
  const Num whole = num(static_cast<unsigned>(tenths / 10));
  if (tenths >= 100) return whole.append("G");
  return whole.append(".").append(num(static_cast<unsigned>(tenths % 10)).c_str()).append("G");
}

// 剩 412G · 待传 录2 · ↑0 / 412G free · R2 · ↑0
MenuText sd_text(const AppState& s) {
  const Num free = s.rec.free_known ? mib_text(s.rec.free_mib) : Num::from("--");
  char buf[MenuText::capacity() + 1];
  std::snprintf(buf, sizeof buf, tr(kSdFacts), free.c_str(), static_cast<unsigned>(s.rec.files_n),
                static_cast<unsigned>(s.outbox.pending));
  return MenuText::from(buf);
}

const char* link_word(const LinkInfo& l) {
  switch (l.mode) {
    case LinkMode::Ready: return tr(kOnline);
    case LinkMode::Attached: return tr(l.version_mismatch ? kMismatch : kNoReply);
    default: return tr(kOffline);
  }
}

MenuText mac_text(const AppState& s, const SysInfo& sys) {
  const MenuText word = MenuText::from(link_word(s.link));
  return sys.host_app.empty() ? word : word.append(" · ").append(sys.host_app.c_str());
}

// Whichever layer binds Lock (System from V40-F); the lock card says how to unlock.
void add_lock_rows(MenuContent& m, const KeymapView& km) {
  for (size_t i = 0; i < km.n; ++i) {
    const Binding& b = km.b[i];
    if (b.action != Action::Lock || (b.layer != Layer::Global && b.layer != Layer::System)) continue;
    add(m, binding_row(km, i, 1, tr(txt::kLock)));
  }
}

// One legend row: the status bar's symbol with its count, then the sentence (a format
// with one %u, the count).
MenuRow legend(const char* symbol, unsigned n, const char* sentence) {
  const KeyLabel keys = KeyLabel::from(symbol).append(num(n).c_str());
  char buf[MenuText::capacity() + 1];
  std::snprintf(buf, sizeof buf, sentence, n);
  return fact(keys.c_str(), MenuText::from(buf));
}

// docs/20 U21: what the status bar's counts mean, in whole sentences, one name each
// (↑ notes, ! sessions, 录 recordings, ※SD the card, ※k the keyboard), each when not zero
// as the bar draws them (status_bar.cpp; ※k stays here after the bar's 10 s). B3: once no
// note or recording waits and the Mac is there, "√ 已同步" says so.
void add_legend(MenuContent& m, const AppState& s) {
  const bool synced = s.link.mode == LinkMode::Ready && s.outbox.pending == 0 && s.rec.files_n == 0;
  if (synced) add(m, fact("\xE2\x88\x9A", MenuText::from(tr(kSynced))));  // √ (no ✓ in efontCN_12)
  const unsigned notes = s.outbox.pending;
  if (notes > 0) add(m, legend("\xE2\x86\x91", notes, trn(kNotesWait, notes)));  // ↑
  const unsigned need = need_count(s.sessions);
  if (need > 0) add(m, legend("!", need, trn(kSessionsWait, need)));
  const unsigned recs = s.rec.files_n;
  if (recs > 0) add(m, legend(tr(kRecSymbol), recs, trn(kRecsWait, recs)));
  if (s.rec.sd_bad) add(m, fact("\xE2\x80\xBBSD", MenuText::from(tr(kSdBad))));  // ※SD
  const unsigned k = static_cast<unsigned>(s.device.ghost_n) + s.device.kb_reinit_n;
  if (k > 0) add(m, legend("\xE2\x80\xBBk", k, tr(kKeysLost)));  // ※k
}

// 控制: the Lock keys (not over a confirm box, §5.3), then the facts and the status bar's
// legend, shown only.
void add_control(MenuContent& m, const MenuSource& src, const AppState& s, const SysInfo& sys,
                 const KeymapView& km) {
  if (src.layer != Layer::Confirm) add_lock_rows(m, km);
  add(m, fact(tr(kSound), MenuText::from(tr(kSoundOff))));
  add(m, fact(tr(kBattery), battery_text(s.device.bat_mv)));
  add(m, fact("SD", sd_text(s)));
  add(m, fact("Mac", mac_text(s, sys)));
  add_legend(m, s);
}

}  // namespace

int menu_battery_pct(uint16_t mv) {
  if (mv == 0) return -1;
  const int pct = (static_cast<int>(mv) - kBatEmptyMv) * 100 / (kBatFullMv - kBatEmptyMv);
  if (pct < 0) return 0;
  return pct > 100 ? 100 : pct;
}

const char* menu_page_name(MenuPage p) {
  switch (p) {
    case MenuPage::Goto: return tr(kGotoPage);
    case MenuPage::Control: return tr(kControlPage);
    default: return tr(kHerePage);
  }
}

namespace {

// The action a row's key has on the Global layer of km, where the board and inbox keys are.
Action global_action(const KeymapView& km, const KeyEvent& ev) {
  if (ev.kind == KeyEventKind::None) return Action::None;
  for (size_t i = 0; i < km.n; ++i) {
    if (km.b[i].layer == Layer::Global && km.b[i].trigger == ev) return km.b[i].action;
  }
  return Action::None;
}

void add_badges(MenuContent& m, const AppState& s, const KeymapView& km) {
  const uint32_t run = running_count(s.projects);
  const uint32_t need = need_count(s.sessions);
  for (uint8_t i = 0; i < m.n; ++i) {
    MenuRow& r = m.rows[i];
    const Action a = global_action(km, r.ev);
    if (a == Action::Projects && run > 0) {
      r.badge = MenuBadge::Running;
      r.badge_n = run;
    } else if (a == Action::Inbox && need > 0) {
      r.badge = MenuBadge::NeedsYou;
      r.badge_n = need;
    }
  }
}

}  // namespace

MenuContent menu_content(MenuPage page, const MenuSource& src, const AppState& s, const SysInfo& sys,
                         const KeymapView& km) {
  MenuContent m;
  m.page = (src.overlay && page == MenuPage::Goto) ? MenuPage::Here : page;
  m.overlay = src.overlay;
  m.title = src.title ? src.title : "";
  switch (m.page) {
    case MenuPage::Goto: add_goto(m, km); break;
    case MenuPage::Control: add_control(m, src, s, sys, km); break;
    default: add_here(m, src, km); break;
  }
  add_badges(m, s, km);
  return m;
}

}  // namespace cpa
