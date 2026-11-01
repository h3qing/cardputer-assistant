#include "text_fit.h"

#include <cstdio>
#include <cstring>

#include "txt.h"

namespace cpa {
namespace {

constexpr uint32_t kReplacement = 0xFFFD;
constexpr char kEllipsis[] = "\xE2\x80\xA6";  // …

uint8_t lead_len(uint8_t b) {
  if (b < 0x80) return 1;
  if (b >= 0xC2 && b <= 0xDF) return 2;
  if (b >= 0xE0 && b <= 0xEF) return 3;
  if (b >= 0xF0 && b <= 0xF4) return 4;
  return 0;
}

bool continuation(uint8_t b) { return (b & 0xC0) == 0x80; }

int16_t width_of(const Canvas& c, const char* s, size_t n) {
  char buf[8] = {};
  const size_t take = n < sizeof buf - 1 ? n : sizeof buf - 1;
  std::memcpy(buf, s, take);
  return c.textWidth(buf);
}

}  // namespace

Utf8Char utf8_at(const char* s, size_t n, size_t i) {
  Utf8Char out{kReplacement, 1};
  const auto* p = reinterpret_cast<const uint8_t*>(s) + i;
  const uint8_t len = lead_len(p[0]);
  if (len == 0 || i + len > n) return out;
  static const uint8_t kMask[] = {0, 0x7F, 0x1F, 0x0F, 0x07};
  uint32_t cp = p[0] & kMask[len];
  for (uint8_t k = 1; k < len; ++k) {
    if (!continuation(p[k])) return out;
    cp = (cp << 6) | (p[k] & 0x3Fu);
  }
  out.cp = cp;
  out.len = len;
  return out;
}

size_t utf8_fit(const Canvas& c, const char* s, size_t n, int16_t max_w) {
  size_t i = 0;
  int16_t w = 0;
  while (i < n) {
    const Utf8Char ch = utf8_at(s, n, i);
    const int16_t cw = width_of(c, s + i, ch.len);
    if (w + cw > max_w) break;
    w = static_cast<int16_t>(w + cw);
    i += ch.len;
  }
  return i;
}

TextLine fit_text(const Canvas& c, const char* s, int16_t max_w) {
  const size_t n = std::strlen(s);
  if (c.textWidth(s) <= max_w) return TextLine::from(s, n);
  const int16_t room = static_cast<int16_t>(max_w - c.textWidth(kEllipsis));
  const size_t keep = room > 0 ? utf8_fit(c, s, n, room) : 0;
  return TextLine::from(s, keep).append(kEllipsis);
}

TextLine fit_tail(const Canvas& c, const char* s, int16_t max_w) {
  const size_t n = std::strlen(s);
  if (c.textWidth(s) <= max_w) return TextLine::from(s, n);
  const int16_t room = static_cast<int16_t>(max_w - c.textWidth(kEllipsis));
  size_t at = 0;  // the first character boundary whose rest fits
  while (at < n && c.textWidth(s + at) > room) at += utf8_at(s, n, at).len;
  return TextLine::from(kEllipsis).append(s + at, n - at);
}

int16_t draw_fit(Canvas& c, int16_t x, int16_t y, int16_t max_w, const char* s, const Style& st) {
  if (max_w <= 0) return 0;
  const TextLine line = fit_text(c, s, max_w);
  c.text(x, y, line.c_str(), st);
  return c.textWidth(line.c_str());
}

int16_t draw_right(Canvas& c, int16_t x_end, int16_t y, const char* s, const Style& st) {
  const int16_t x = static_cast<int16_t>(x_end - c.textWidth(s));
  c.text(x, y, s, st);
  return x;
}

// docs/21 §6.5: English abbreviates, so it needs no plural ("40 min", tight "40m").
constexpr Txt kSeconds{"秒", "s"};
constexpr Txt kMinutes{"分钟", "min"};
constexpr Txt kHours{"小时", "h"};
constexpr Txt kDays{"天", "d"};
constexpr const Txt* kAgeWords[] = {&kSeconds, &kMinutes, &kHours, &kDays};
constexpr const char* kAgeTightEn[] = {"s", "m", "h", "d"};

FixedStr<16> age_text(uint32_t age_s, bool tight) {
  const unsigned long s = static_cast<unsigned long>(age_s);
  const unsigned long minutes = (s + 30) / 60;
  const unsigned long hours = (s + 1800) / 3600;
  uint8_t unit = 3;
  unsigned long n = (s + 43200) / 86400;
  if (s < 60) {
    unit = 0;
    n = s;
  } else if (minutes < 60) {
    unit = 1;
    n = minutes;
  } else if (hours < 24) {
    unit = 2;
    n = hours;
  }
  const char* word = tight && lang() == Lang::En ? kAgeTightEn[unit] : tr(*kAgeWords[unit]);
  char buf[24];
  std::snprintf(buf, sizeof buf, "%lu%s%s", n, tight ? "" : " ", word);
  return FixedStr<16>::from(buf);
}

constexpr Txt kStale{"离线 · %s前的情况", "Offline · as of %s ago"};
constexpr Txt kBg{"后台", "bg"};
constexpr Txt kTty{"终端", "term"};
// Hint strings are plain Txt: brackets over-measure, so test_hint_bars checks each with
// the bar itself (hint_fits) in both languages.
constexpr Txt kHintEmpty{"`返回  Tab菜单", "[`]back  [Tab]menu"};
constexpr Txt kHintOffline{"Enter记给Mac `返回  Tab菜单", "[Enter]note  [`]back  [Tab]menu"};
constexpr Txt kHintReply{"Enter回复 Opt+D忽略 `返回  Tab菜单", "[Enter]reply [Opt+D]hide [`]back [Tab]menu"};
constexpr Txt kHintNote{"Enter记给Mac Opt+D忽略 `返回  Tab菜单", "[Enter]note [Opt+D]hide [`]back [Tab]menu"};
constexpr ToastTxt kGone{"选中的那条已经不在了：按 ; . 选一条", "That row is gone: pick one with ; ."};

FixedStr<48> stale_text(uint32_t age_s) {
  char buf[64];
  std::snprintf(buf, sizeof buf, tr(kStale), age_text(age_s).c_str());
  return FixedStr<48>::from(buf);
}

const char* session_kind_text(SessionKind k) { return tr(k == SessionKind::Bg ? kBg : kTty); }

const char* reply_hint(bool online, bool reaches, bool empty) {
  if (empty) return tr(kHintEmpty);
  if (!online) return tr(kHintOffline);
  return tr(reaches ? kHintReply : kHintNote);
}

const char* selection_gone_text() { return tr(kGone); }

}  // namespace cpa
