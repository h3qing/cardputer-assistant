// How 设置 / Settings is drawn (docs/21 §4.2, §4.4, §4.5; the parts are docs/19's):
//   the tab pills at the top of the content area, the current one Accent;
//   the settings tab: the rows that change something (14 px, the selected one in a Select
//   capsule), a Line divider, then the facts (12 px, never selected);
//   the WiFi page: its title, a None pill on the right, then label / words rows;
//   the device tab: 8 label / value rows, a divider after who the device is.
#include <cstdio>

#include "lang_names.h"
#include "system_module.h"
#include "system_texts.h"
#include "text_fit.h"
#include "theme.h"

namespace cpa {
namespace {

constexpr int16_t kTabH = 12;
constexpr int16_t kTabGap = 4;
constexpr int16_t kRowH = 14;           // a row the cursor stops on
constexpr int16_t kRowsTop = 14;        // below the tabs: y 26
constexpr int16_t kListX = 8;           // as the board's lists
constexpr int16_t kSettingValueX = 100;
constexpr int16_t kDividerTop = 43;     // y 55
constexpr int16_t kFactsTop = 46;       // y 58
constexpr int16_t kStoreDividerTop = kRowsTop + 3 * kRowH + 1;  // the storage tab's 3 rows: y 69
constexpr int16_t kStoreFactsTop = kStoreDividerTop + 3;        // y 72, then a row apart
constexpr int16_t kFactValueX = 44;
constexpr int16_t kFactGap = 6;         // between the Mac's name and 在线
constexpr int16_t kWifiRowsTop = 14;
constexpr int16_t kIdentityRows = 3;    // 固件 / dev / 启动: who the device is
constexpr int16_t kDividerGap = 3;      // a pixel, the divider, a pixel
constexpr int16_t kValueX = 46;         // after "outbox", the widest label in either language

int16_t at(const Rect& area, int16_t dy) { return static_cast<int16_t>(area.y + dy); }
int16_t right_of(const Rect& area) { return static_cast<int16_t>(area.x + area.w - theme::kGutter); }

Style row_style(bool selected, Color fg) {
  if (selected && fg == Color::Ink3) fg = Color::Ink2;
  return selected ? theme::over(fg) : Style{fg, Color::Bg};
}

void divider(Canvas& c, const Rect& area, int16_t y) {
  const int16_t lx = static_cast<int16_t>(area.x + theme::kGutter);
  c.fill(Rect{lx, y, static_cast<int16_t>(right_of(area) - lx), 1}, Color::Line);
}

// One row the cursor can stop on: its label at x 8, its value at x 100.
void setting_row(Canvas& c, const Rect& area, int16_t y, const char* label, const char* value, bool sel) {
  if (sel) {
    c.fillRound(Rect{static_cast<int16_t>(area.x + 2), y, static_cast<int16_t>(area.w - 4), kRowH}, kRowH / 2,
                Color::Select, Color::Bg);
  }
  const int16_t ty = static_cast<int16_t>(y + 1);
  c.text(static_cast<int16_t>(area.x + kListX), ty, label, row_style(sel, Color::Ink3));
  const int16_t vx = static_cast<int16_t>(area.x + kSettingValueX);
  draw_fit(c, vx, ty, static_cast<int16_t>(right_of(area) - vx), value, row_style(sel, Color::Ink));
}

// s without its leading spaces, every run of spaces as one: values are read, not aligned.
SystemLine single_spaced(const char* s) {
  char buf[SystemLine::capacity() + 1];
  size_t n = 0;
  while (*s == ' ') ++s;
  for (; *s != '\0' && n < SystemLine::capacity(); ++s) {
    if (*s == ' ' && n > 0 && buf[n - 1] == ' ') continue;
    buf[n++] = *s;
  }
  return SystemLine::from(buf, n);
}

}  // namespace

void SystemModule::render_tabs(Canvas& c, const Rect& area) const {
  const Txt* names[] = {&txt::kSettings, &txt::kDevice, &txt::kStorage};
  int16_t x = static_cast<int16_t>(area.x + theme::kGutter);
  for (uint8_t i = 0; i < 3; ++i) {
    const theme::Tone tone = static_cast<uint8_t>(ui_.tab) == i ? theme::Tone::Accent : theme::Tone::Neutral;
    x = static_cast<int16_t>(x + kTabGap + theme::pill(c, x, area.y, tr(*names[i]), tone, Color::Bg));
  }
}

// docs/21 §4.2: WiFi and 语言 Language (both scripts, so a wrong switch is found again),
// a divider, then the link and the last sync.
void SystemModule::render_settings(Canvas& c, const Rect& area) const {
  const BothText language = both(txt::kLanguage);
  setting_row(c, area, at(area, kRowsTop), "WiFi", tr(txt::kWifiOff), ui_.row == SysRow::Wifi);
  setting_row(c, area, at(area, kRowsTop + kRowH), language.c_str(), lang_name(lang()), ui_.row == SysRow::Lang);
  divider(c, area, at(area, kDividerTop));
  render_facts(c, area);
}

// 连接: "USB 线 · " + the Mac's name, cut to fit, and 在线 / 离线 pinned on the right;
// 同步: how long ago and what waits.
void SystemModule::render_facts(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const LinkFacts link = link_facts(s, ctx_->sys());
  const Style label{Color::Ink3, Color::Bg};
  const Style value{Color::Ink, Color::Bg};
  const int16_t lx = static_cast<int16_t>(area.x + kListX);
  const int16_t vx = static_cast<int16_t>(area.x + kFactValueX);
  const int16_t right = right_of(area);
  int16_t y = at(area, kFactsTop);
  c.text(lx, y, tr(txt::kLink), label);
  const int16_t sx = draw_right(c, right, y, link.state, Style{link.online ? Color::Ok : Color::Ink3, Color::Bg});
  c.text(vx, y, link.head, value);
  const int16_t nx = static_cast<int16_t>(vx + c.textWidth(link.head));
  draw_fit(c, nx, y, static_cast<int16_t>(sx - kFactGap - nx), link.name.c_str(), value);
  y = static_cast<int16_t>(y + c.lineHeight());
  c.text(lx, y, tr(txt::kSync), label);
  draw_fit(c, vx, y, static_cast<int16_t>(right - vx), sync_text(s, ctx_->nowMs()).c_str(), value);
}

// docs/21 §4.4 (W0): only words. The value column starts after the widest label.
void SystemModule::render_wifi(Canvas& c, const Rect& area) const {
  const int16_t lx = static_cast<int16_t>(area.x + theme::kGutter);
  const int16_t right = right_of(area);
  theme::title(c, lx, area.y, "WiFi");
  const char* tag = tr(txt::kNoneTag);
  theme::pill(c, static_cast<int16_t>(right - theme::pill_width(c, tag)), area.y, tag, theme::Tone::Neutral, Color::Bg);
  const WifiRows w = wifi_rows(ctx_->sys().launcher);
  int16_t label_w = 0;
  for (const WifiRow& r : w.rows) {
    if (r.label && c.textWidth(tr(*r.label)) > label_w) label_w = c.textWidth(tr(*r.label));
  }
  const int16_t vx = static_cast<int16_t>(lx + label_w + 8);
  for (uint8_t i = 0; i < kWifiRows; ++i) {
    const int16_t y = at(area, static_cast<int16_t>(kWifiRowsTop + i * c.lineHeight()));
    if (w.rows[i].label) c.text(lx, y, tr(*w.rows[i].label), Style{Color::Ink3, Color::Bg});
    draw_fit(c, vx, y, static_cast<int16_t>(right - vx), tr(*w.rows[i].words), Style{Color::Ink, Color::Bg});
  }
}

// docs/21 §4.5 (docs/19 §6.9): label / value rows under the tabs, the label in Ink3 at x 4,
// the value in Ink at x 46, single-spaced; a divider after who the device is; the warning
// row a sentence, whole, in Warn.
void SystemModule::render_device(Canvas& c, const Rect& area) const {
  const SystemPage p = system_page(ctx_->state(), ctx_->sys());
  const int16_t h = c.lineHeight();
  const int16_t lx = static_cast<int16_t>(area.x + theme::kGutter);
  const int16_t right = right_of(area);
  const int16_t top = at(area, kTabH);
  for (uint8_t i = 0; i < p.n; ++i) {
    const int16_t y = static_cast<int16_t>(top + i * h + (i >= kIdentityRows ? kDividerGap : 0));
    const Color ink = p.warn[i] ? Color::Warn : Color::Ink;
    if (p.key_len[i] == 0) {
      draw_fit(c, lx, y, static_cast<int16_t>(right - lx), p.lines[i].c_str(), Style{ink, Color::Bg});
      continue;
    }
    c.text(lx, y, FixedStr<16>::from(p.lines[i].c_str(), p.key_len[i]).c_str(), Style{Color::Ink3, Color::Bg});
    const SystemLine value = single_spaced(p.lines[i].c_str() + p.key_len[i]);
    const int16_t vx = static_cast<int16_t>(area.x + kValueX);
    draw_fit(c, vx, y, static_cast<int16_t>(right - vx), value.c_str(), Style{ink, Color::Bg});
  }
  if (p.n > kIdentityRows) divider(c, area, static_cast<int16_t>(top + kIdentityRows * h + 1));
}

// docs/28 S1a: 再交一次, 给这台 Mac and 清掉留底 (rows the cursor stops on), a divider, then
// the kept copies, what waits and the card's free space, and (while some are kept) that they
// are plain on the card, a row apart (review 8: 12 px Chinese lines touch). With a confirm
// box open, the box over the rows and no facts.
void SystemModule::render_storage(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const KeptView& k = s.rec.kept;
  const FixedStr<48> values[] = {again_value(k), give_value(k), clear_value(k)};
  const Txt* labels[] = {&txt::kAgainRow, &txt::kGiveRow, &txt::kClearRow};
  for (uint8_t i = 0; i < 3; ++i) {
    const bool sel = !ui_.confirm && static_cast<uint8_t>(ui_.store) == i;
    setting_row(c, area, at(area, static_cast<int16_t>(kRowsTop + i * kRowH)), tr(*labels[i]), values[i].c_str(), sel);
  }
  divider(c, area, at(area, kStoreDividerTop));
  if (ui_.confirm) return render_clear_box(c, area);  // it covers the rows; the facts would peek out below
  const int16_t lx = static_cast<int16_t>(area.x + kListX);
  const int16_t w = static_cast<int16_t>(right_of(area) - lx);
  int16_t y = at(area, kStoreFactsTop);
  const FixedStr<64> kept = kept_fact(s, ctx_->nowMs());
  draw_fit(c, lx, y, w, kept.c_str(), Style{Color::Ink, Color::Bg});
  y = static_cast<int16_t>(y + kRowH);
  const FixedStr<64> waiting = waiting_fact(s);
  draw_fit(c, lx, y, w, waiting.c_str(), Style{Color::Ink, Color::Bg});
  y = static_cast<int16_t>(y + kRowH);
  if (k.segs > 0) draw_fit(c, lx, y, w, tr(txt::kPlainNote), Style{Color::Ink3, Color::Bg});
}

// "清掉 12 段留底？" or "把 2 次再交给别的 Mac？" and what it means; [取消] [清掉 / 再交], the
// cursor on 取消 first.
void SystemModule::render_clear_box(Canvas& c, const Rect& area) const {
  const Rect box{static_cast<int16_t>(area.x + 4), at(area, 16), static_cast<int16_t>(area.w - 8), 66};
  theme::card(c, box);
  const Color under = theme::card_fill(theme::CardKind::Plain);
  const KeptView& k = ctx_->state().rec.kept;
  const bool again = ui_.store == StoreRow::Again;
  char ask[64];
  char sub[64];
  if (again) {
    std::snprintf(ask, sizeof ask, tr(txt::kAgainAsk), static_cast<unsigned>(k.batch_recs));
    const FixedStr<8> tag = host_tag(k.batch_host);
    std::snprintf(sub, sizeof sub, tr(txt::kAgainSub), tag.c_str());
  } else {
    std::snprintf(ask, sizeof ask, trn(txt::kClearAsk, k.segs), static_cast<unsigned>(k.segs));
    std::snprintf(sub, sizeof sub, "%s", tr(txt::kClearSub));
  }
  const int16_t tx = static_cast<int16_t>(box.x + 8);
  theme::title(c, tx, static_cast<int16_t>(box.y + 6), ask, under);
  c.text(tx, static_cast<int16_t>(box.y + 22), sub, Style{Color::Ink2, under});
  const int16_t by = static_cast<int16_t>(box.y + box.h - 20);
  int16_t bx = static_cast<int16_t>(box.x + box.w - 8);
  const Txt* buttons[] = {again ? &txt::kAgainBtn : &txt::kClearBtn, &txt::kCancelBtn};  // right to left
  for (uint8_t i = 0; i < 2; ++i) {
    const bool on = (i == 0) == ui_.yes;
    const char* word = tr(*buttons[i]);
    bx = static_cast<int16_t>(bx - theme::pill_width(c, word));
    theme::pill(c, bx, by, word, on ? (i == 0 ? theme::Tone::Rec : theme::Tone::Accent) : theme::Tone::Neutral, under);
    bx = static_cast<int16_t>(bx - kTabGap * 2);
  }
}

}  // namespace cpa
