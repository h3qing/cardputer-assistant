// The settings tab and the WiFi page of 设置 (docs/21 §4.2-§4.4, §4.6, §9.2): the cursor
// starts on the WiFi row at every visit and stops only on the two rows that change
// something; Enter opens the WiFi page or switches the language through the shell; the
// link and sync facts; the words in both languages, inside the screen.
#include <unity.h>

#include <string>
#include <vector>

#include "lang_names.h"
#include "settings_logic.h"
#include "test_system_rig.h"
#include "theme.h"
#include "words.h"

using namespace cpa;
using namespace uitest;

namespace {

struct Page {
  FakeContext ctx = filled();
  SystemModule m;
  Page() {
    m.begin(ctx);
    m.onEnter(0);
  }
  FakeCanvas draw() const {
    FakeCanvas c;
    m.render(c, kContentRect);
    return c;
  }
};

constexpr int16_t kWifiRowY = kContentRect.y + 14;  // y 26
constexpr int16_t kLangRowY = kWifiRowY + 14;       // y 40
constexpr int16_t kLinkY = kContentRect.y + 46;     // y 58
constexpr int16_t kSyncY = kLinkY + 12;

// The Select capsule the cursor is on, by its top.
bool selected_at(const FakeCanvas& c, int16_t y) {
  for (const auto& f : c.fills) {
    if (f.second == Color::Select && f.first.y == y && f.first.h == 14) return true;
  }
  return false;
}

std::vector<std::string> keys_of(const Module& m) {
  const HintKey* k = nullptr;
  const uint8_t n = m.hintKeys(&k);
  std::vector<std::string> out;
  for (uint8_t i = 0; i < n; ++i) out.push_back(std::string(k[i].keys) + " " + tr(k[i].words));
  return out;
}

}  // namespace

void test_settings_tab_opens_first_on_the_wifi_row() {
  Page p;
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Settings);
  TEST_ASSERT_TRUE(p.m.ui().row == SysRow::Wifi);
  const FakeCanvas c = p.draw();
  TEST_ASSERT_TRUE(selected_at(c, kWifiRowY));
  TEST_ASSERT_FALSE(selected_at(c, kLangRowY));
}

// The facts below the divider are never selected: two rows, no wrap, so ". Enter" is the
// language from anywhere on the tab.
void test_dot_and_semicolon_move_between_the_two_rows() {
  Page p;
  TEST_ASSERT_TRUE(p.m.onKey(chr('.')));
  TEST_ASSERT_TRUE(p.m.ui().row == SysRow::Lang);
  TEST_ASSERT_TRUE(selected_at(p.draw(), kLangRowY));
  TEST_ASSERT_TRUE(p.m.onKey(chr('.')));
  TEST_ASSERT_TRUE(p.m.ui().row == SysRow::Lang);
  TEST_ASSERT_TRUE(p.m.onKey(chr(';')));
  TEST_ASSERT_TRUE(p.m.ui().row == SysRow::Wifi);
  TEST_ASSERT_TRUE(p.m.onKey(chr(';')));
  TEST_ASSERT_TRUE(p.m.ui().row == SysRow::Wifi);
  TEST_ASSERT_TRUE(p.m.onKey(nav(Nav::Down)));  // Fn+. as on every list
  TEST_ASSERT_TRUE(p.m.ui().row == SysRow::Lang);
}

// docs/28 S1a: three tabs, round: 设置 / 设备 / 存储 (`,` from 设置 is 存储).
void test_comma_and_slash_switch_tabs() {
  Page p;
  TEST_ASSERT_TRUE(p.m.onKey(chr('/')));
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Device);
  TEST_ASSERT_TRUE(p.m.onKey(chr('/')));
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Storage);
  TEST_ASSERT_TRUE(p.m.onKey(chr('/')));
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Settings);
  TEST_ASSERT_TRUE(p.m.onKey(chr(',')));
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Storage);
  TEST_ASSERT_TRUE(p.m.onKey(nav(Nav::Left)));
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Device);
  TEST_ASSERT_TRUE(p.m.onKey(chr(',')));
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Settings);
  // ` and Backspace are the shell's: they leave the page
  TEST_ASSERT_FALSE(p.m.onKey(chr('`')));
  TEST_ASSERT_FALSE(p.m.onKey(bksp()));
  TEST_ASSERT_FALSE(p.m.onKey(esc()));
}

void test_device_tab_ignores_the_row_keys() {
  Page p;
  p.m.onKey(chr('/'));
  TEST_ASSERT_TRUE(p.m.onKey(chr('.')));
  TEST_ASSERT_TRUE(p.m.onKey(enter()));
  TEST_ASSERT_TRUE(p.ctx.languages.empty());
  TEST_ASSERT_FALSE(p.m.ui().wifi);
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Device);
  TEST_ASSERT_FALSE(p.m.onKey(chr('`')));
}

// Enter on the WiFi row only shows words: a stray press changes nothing. ` (or Backspace)
// on the WiFi page goes back to the settings tab, the cursor still on WiFi.
void test_enter_on_wifi_opens_the_wifi_page() {
  Page p;
  TEST_ASSERT_TRUE(p.m.onKey(enter()));
  TEST_ASSERT_TRUE(p.m.ui().wifi);
  TEST_ASSERT_TRUE(p.ctx.languages.empty());
  TEST_ASSERT_TRUE(p.draw().has("这个固件不带 WiFi"));
  TEST_ASSERT_TRUE(p.m.onKey(chr('.')));  // nothing to choose here
  TEST_ASSERT_TRUE(p.m.onKey(chr('/')));
  TEST_ASSERT_TRUE(p.m.onKey(enter()));
  TEST_ASSERT_TRUE(p.m.ui().wifi);
  TEST_ASSERT_TRUE(p.m.onKey(chr('`')));
  TEST_ASSERT_FALSE(p.m.ui().wifi);
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Settings && p.m.ui().row == SysRow::Wifi);
  p.m.onKey(enter());
  TEST_ASSERT_TRUE(p.m.onKey(bksp()));
  TEST_ASSERT_FALSE(p.m.ui().wifi);
  p.m.onKey(enter());
  TEST_ASSERT_TRUE(p.m.onKey(esc()));
  TEST_ASSERT_FALSE(p.m.ui().wifi);
}

// docs/21 §4.3: Opt+M . Enter: the other language, through the shell; the cursor stays on
// the language row, so "Enter again" in the shell's toast switches back.
void test_enter_on_language_switches_through_the_shell() {
  Page p;
  p.m.onKey(chr('.'));
  TEST_ASSERT_TRUE(p.m.onKey(enter()));
  TEST_ASSERT_EQUAL_UINT32(1, p.ctx.languages.size());
  TEST_ASSERT_TRUE(p.ctx.languages[0] == Lang::En);
  TEST_ASSERT_TRUE(p.m.ui().row == SysRow::Lang);
  TEST_ASSERT_TRUE(p.draw().has("English"));
  TEST_ASSERT_TRUE(p.m.onKey(enter()));
  TEST_ASSERT_TRUE(p.ctx.languages[1] == Lang::Zh);
  TEST_ASSERT_TRUE(lang() == Lang::Zh);
}

void test_every_visit_starts_on_the_wifi_row() {
  Page p;
  p.m.onKey(chr('.'));
  p.m.onKey(chr('/'));
  p.m.onEnter(0);
  TEST_ASSERT_TRUE(p.m.ui().tab == SysTab::Settings && p.m.ui().row == SysRow::Wifi);
  p.m.onKey(enter());
  p.m.onEnter(0);
  TEST_ASSERT_FALSE(p.m.ui().wifi);
}

void test_settings_tab_layout_in_chinese() {
  Page p;
  p.ctx.info.host_label = FixedStr<24>::from("工作 MBP");
  const FakeCanvas c = p.draw();
  const TextOp* tab = c.find("设置");
  TEST_ASSERT_TRUE(tab != nullptr && tab->y == kContentRect.y && tab->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(c.find("设备")->style.fg == Color::Ink2);
  const TextOp* wifi = c.find("WiFi");
  TEST_ASSERT_TRUE(wifi->x == 8 && wifi->y == kWifiRowY + 1);
  const TextOp* off = c.find("无 · 只用 USB 线");
  TEST_ASSERT_TRUE(off->x == 100 && off->y == kWifiRowY + 1);
  const TextOp* language = c.find("语言 Language");
  TEST_ASSERT_TRUE(language->x == 8 && language->y == kLangRowY + 1 && language->style.fg == Color::Ink3);
  TEST_ASSERT_TRUE(c.find("中文")->x == 100);
  TEST_ASSERT_TRUE(c.filled_at(kContentRect.y + 43, Color::Line));
  TEST_ASSERT_TRUE(c.find("连接")->y == kLinkY && c.find("连接")->x == 8);
  TEST_ASSERT_TRUE(c.find("USB 线 · ")->x == 44);
  TEST_ASSERT_TRUE(c.find("工作 MBP")->y == kLinkY);
  const TextOp* online = c.find("在线");
  TEST_ASSERT_TRUE(online->x + 24 == 236 && online->style.fg == Color::Ok);
  TEST_ASSERT_TRUE(c.find("同步")->y == kSyncY);
  TEST_ASSERT_TRUE(c.has("5 秒前 · 待传 ↑3"));
  expect_inside(c);
}

void test_settings_tab_layout_in_english() {
  LangScope en(Lang::En);
  Page p;
  p.ctx.info.host_label = FixedStr<24>::from("Studio Mac");
  const FakeCanvas c = p.draw();
  TEST_ASSERT_TRUE(c.find("Settings")->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(c.has("Device"));
  TEST_ASSERT_TRUE(c.find("None · USB only")->x == 100);
  TEST_ASSERT_TRUE(c.has("语言 Language"));  // the same in both: a wrong switch is found again
  TEST_ASSERT_TRUE(c.find("English")->x == 100);
  TEST_ASSERT_TRUE(c.find("Link")->y == kLinkY);
  TEST_ASSERT_TRUE(c.has("USB · "));
  TEST_ASSERT_TRUE(c.has("Studio Mac"));
  TEST_ASSERT_TRUE(c.find("online")->x + 36 == 236);
  TEST_ASSERT_TRUE(c.has("5 s ago · to sync ↑3"));
  expect_inside(c);
}

void test_link_row_names_the_mac() {
  FakeContext ctx = filled();
  ctx.info.host_label = FixedStr<24>::from("工作 MBP");
  LinkFacts f = link_facts(ctx.app, ctx.info);
  TEST_ASSERT_EQUAL_STRING("工作 MBP", f.name.c_str());
  TEST_ASSERT_EQUAL_STRING("USB 线 · ", f.head);
  TEST_ASSERT_TRUE(f.online);
  ctx.info.host_label = FixedStr<24>{};  // a cpad that sends no name: its version
  TEST_ASSERT_EQUAL_STRING("cpad/0.1.0", link_facts(ctx.app, ctx.info).name.c_str());
  ctx.info.host_app = FixedStr<32>{};  // no hello since boot
  ctx.app.link.mode = LinkMode::Detached;
  f = link_facts(ctx.app, ctx.info);
  TEST_ASSERT_EQUAL_STRING("--", f.name.c_str());
  TEST_ASSERT_EQUAL_STRING("离线", f.state);
}

// host_label is up to 24 bytes (144 px): cut with "…" before the state word, which stays.
void test_link_row_cuts_a_long_name_and_keeps_the_state() {
  for (Lang l : kLangs) {
    LangScope s(l);
    Page p;
    p.ctx.info.host_label = FixedStr<24>::from("八个汉字的主机名字");
    const FakeCanvas c = p.draw();
    TEST_ASSERT_TRUE(c.has(lang_name(l)));
    const TextOp* state = c.find(l == Lang::En ? "online" : "在线");
    TEST_ASSERT_NOT_NULL(state);
    for (const TextOp& t : c.texts) {
      if (t.y != kLinkY || &t == state || t.x < 44) continue;
      TEST_ASSERT_TRUE_MESSAGE(t.x + c.textWidth(t.text.c_str()) + 6 <= state->x, t.text.c_str());
    }
    expect_inside(c);
  }
}

// Pulled: the last Mac's name stays, the state says offline.
void test_link_row_after_the_cable_is_pulled() {
  Page p;
  p.ctx.info.host_label = FixedStr<24>::from("工作 MBP");
  p.ctx.app.link.mode = LinkMode::Detached;
  const FakeCanvas c = p.draw();
  TEST_ASSERT_TRUE(c.has("工作 MBP"));
  TEST_ASSERT_TRUE(c.find("离线")->style.fg == Color::Ink3);
}

void test_sync_row() {
  FakeContext ctx = filled();
  TEST_ASSERT_EQUAL_STRING("5 秒前 · 待传 ↑3", sync_text(ctx.app, ctx.now).c_str());
  ctx.app.rec.avail = true;  // the device can record: what waits to sync
  ctx.app.rec.files_n = 2;
  ctx.now = 10000 + 3 * 60 * 1000;
  TEST_ASSERT_EQUAL_STRING("3 分钟前 · 待传 ↑3 录2", sync_text(ctx.app, ctx.now).c_str());
  ctx.app.link.last_rx_ms = 0;
  ctx.app.outbox.pending = 0;
  TEST_ASSERT_EQUAL_STRING("从未 · 待传 ↑0 录2", sync_text(ctx.app, ctx.now).c_str());
  LangScope en(Lang::En);
  TEST_ASSERT_EQUAL_STRING("never · to sync ↑0 R2", sync_text(ctx.app, ctx.now).c_str());
  ctx.app.link.last_rx_ms = 10000;
  TEST_ASSERT_EQUAL_STRING("3 min ago · to sync ↑0 R2", sync_text(ctx.app, ctx.now).c_str());
}

// The widest: "59 min ago · to sync ↑99 R99" at x 44 (docs/21 §4.2); the counts' own
// widest (5 digits) still inside the row.
void test_sync_row_widest_fits() {
  FakeContext ctx = filled();
  ctx.app.rec.avail = true;
  ctx.app.rec.files_n = 99;
  ctx.app.outbox.pending = 99;
  ctx.now = 10000 + (59 * 60 + 20) * 1000;
  for (Lang l : kLangs) {
    LangScope s(l);
    TEST_ASSERT_TRUE(44 + i18n::text_width(sync_text(ctx.app, ctx.now).c_str()) <= 212);
  }
  ctx.app.rec.files_n = 65535;
  ctx.app.outbox.pending = 65535;
  ctx.now = 10000 + 23 * 3600 * 1000;
  for (Lang l : kLangs) {
    LangScope s(l);
    TEST_ASSERT_TRUE(44 + i18n::text_width(sync_text(ctx.app, ctx.now).c_str()) <= 236);
  }
}

void test_wifi_page_words() {
  Page p;
  p.ctx.info.launcher = true;
  p.m.onKey(enter());
  FakeCanvas c = p.draw();
  const char* zh[] = {"这个固件不带 WiFi", "只用 USB 线连你的 Mac，不上网", "出厂固件有 WiFi：",
                      "关开电源进 Launcher 切过去", "它会把 WiFi 密码明文打到串口", "还没排期"};
  for (const char* t : zh) TEST_ASSERT_TRUE_MESSAGE(c.has(t), t);
  TEST_ASSERT_EQUAL_INT16(36, c.find("这个固件不带 WiFi")->x);
  TEST_ASSERT_TRUE(c.find("现在")->style.fg == Color::Ink3);
  TEST_ASSERT_TRUE(c.find("无")->style.fg == Color::Ink2);  // the state pill, not a switch
  set_lang(Lang::En);
  c = p.draw();
  const char* en[] = {"No WiFi in this firmware", "USB to your Mac, never online", "Factory firmware has WiFi:",
                      "power-cycle into the Launcher", "Prints WiFi passwords to serial", "not planned yet"};
  for (const char* t : en) TEST_ASSERT_TRUE_MESSAGE(c.has(t), t);
  TEST_ASSERT_EQUAL_INT16(42, c.find("No WiFi in this firmware")->x);
  TEST_ASSERT_TRUE(c.has("None"));
}

// Flashed whole, without the Launcher, the factory firmware is not one power cycle away.
void test_wifi_page_without_the_launcher() {
  Page p;
  p.ctx.info.launcher = false;
  p.m.onKey(enter());
  TEST_ASSERT_TRUE(p.draw().has("先装 Launcher（见 README）"));
  TEST_ASSERT_FALSE(p.draw().has("关开电源进 Launcher 切过去"));
  LangScope en(Lang::En);
  TEST_ASSERT_TRUE(p.draw().has("Needs the Launcher (README)"));
}

void test_wifi_page_fits_in_both_languages() {
  for (Lang l : kLangs) {
    LangScope s(l);
    for (bool launcher : {true, false}) {
      Page p;
      p.ctx.info.launcher = launcher;
      p.m.onKey(enter());
      expect_inside(p.draw());
    }
  }
}

// docs/21 §4.6: the 本页 rows by page; Enter is among them, so the menu switches the
// language too. Every row's words fit the menu in both languages.
void test_help_rows_by_page() {
  Page p;
  std::vector<HelpRow> rows = help_rows(p.m);
  TEST_ASSERT_EQUAL_UINT32(7, rows.size());
  TEST_ASSERT_TRUE(rows[0].ev == chr(';') && rows[1].ev == chr('.') && rows[2].ev == enter());
  TEST_ASSERT_TRUE(rows[3].ev == chr(',') && rows[4].ev == chr('/') && rows[5].ev == chr('`'));
  TEST_ASSERT_TRUE(rows[6].ev == opt('h'));
  TEST_ASSERT_EQUAL_STRING("改这一行", help_label(rows[2]));
  p.m.onKey(chr('/'));
  TEST_ASSERT_EQUAL_UINT32(4, help_rows(p.m).size());
  p.m.onKey(chr('/'));
  TEST_ASSERT_EQUAL_UINT32(7, help_rows(p.m).size());  // docs/28: the storage tab
  p.m.onKey(chr('/'));
  p.m.onKey(enter());
  rows = help_rows(p.m);
  TEST_ASSERT_EQUAL_UINT32(2, rows.size());
  TEST_ASSERT_EQUAL_STRING("回到设置", help_label(rows[0]));
  for (Lang l : kLangs) {
    LangScope s(l);
    for (const HelpRow& r : help_rows(Page{}.m)) {
      TEST_ASSERT_TRUE_MESSAGE(i18n::text_width(help_label(r)) <= kMenuTxtW, help_label(r));
    }
  }
}

// docs/21 §4.6: the hint bars (the shell adds [Tab] 菜单 / menu), within the bar in both.
void test_hint_keys_by_page() {
  Page p;
  std::vector<std::string> keys = keys_of(p.m);
  TEST_ASSERT_EQUAL_UINT32(3, keys.size());
  TEST_ASSERT_EQUAL_STRING(";. 选", keys[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Enter 改", keys[1].c_str());
  TEST_ASSERT_EQUAL_STRING("` 返回", keys[2].c_str());
  p.m.onKey(chr('/'));
  keys = keys_of(p.m);
  TEST_ASSERT_EQUAL_UINT32(2, keys.size());
  TEST_ASSERT_EQUAL_STRING(",/ 换页", keys[0].c_str());
  p.m.onKey(chr('/'));
  TEST_ASSERT_EQUAL_UINT32(3, keys_of(p.m).size());  // docs/28: the storage tab
  p.m.onKey(chr('/'));
  p.m.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, keys_of(p.m).size());
  LangScope en(Lang::En);
  p.m.onKey(chr('`'));
  keys = keys_of(p.m);
  TEST_ASSERT_EQUAL_STRING("Enter change", keys[1].c_str());
  for (Lang l : kLangs) {
    LangScope s(l);
    Page q;
    for (int page = 0; page < 3; ++page) {
      const HintKey* k = nullptr;
      const uint8_t n = q.m.hintKeys(&k);
      theme::HintPart parts[theme::kHintItemsMax + 1];
      for (uint8_t i = 0; i < n; ++i) parts[i] = theme::HintPart{k[i].keys, tr(k[i].words)};
      parts[n] = theme::HintPart{"Tab", tr(txt::kMenu)};
      FakeCanvas c;
      TEST_ASSERT_TRUE(theme::hint_fits(c, theme::hint_line(parts, static_cast<uint8_t>(n + 1))));
      q.m.onKey(page == 0 ? chr('/') : chr(','));
      if (page == 1) q.m.onKey(enter());
    }
  }
}

// The tab pills at one y on both tabs (docs/21 §4.2), the current one Accent.
void test_tabs_share_one_y() {
  Page p;
  FakeCanvas c = p.draw();
  TEST_ASSERT_EQUAL_INT16(kContentRect.y, c.find("设备")->y);
  p.m.onKey(chr('/'));
  c = p.draw();
  TEST_ASSERT_EQUAL_INT16(kContentRect.y, c.find("设置")->y);
  TEST_ASSERT_TRUE(c.find("设备")->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(c.find("设置")->style.fg == Color::Ink2);
}
