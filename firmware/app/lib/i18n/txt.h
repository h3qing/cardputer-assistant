// Txt: one UI string as a zh/en pair (docs/21 §6.3-§6.5), checked when it is built.
// Write them only as constexpr constants next to the code that draws them:
//   constexpr LineTxt kQueueEmpty{"队列为空", "Queue is empty"};
//   draw_fit(c, x, y, w, tr(kQueueEmpty), st);
// A constexpr Txt that breaks a rule does not compile: the constructor then calls one of
// the txt_* functions below, which are not constexpr, and the compiler names it:
//   txt_empty            a half is ""
//   txt_control_char     a control character (\n, \t, ...) in either half
//   txt_han_in_english   Han, kana or full-width punctuation in the English half
//   txt_missing_glyph    a symbol efontCN_12 draws as □ (— ✓ ▸ ...) in the English half
//   txt_format_mismatch  the halves' % conversions differ (kind, flags or order)
//   txt_too_wide         wider than the class allows (ToastTxt, LineTxt ...), in real font px
// A Txt{...} written inline is not evaluated at compile time and skips these checks;
// tools/i18n/check_texts.py rejects those (docs/21 §6.3).
#pragma once

#include <cstdint>

#include "fixed_str.h"
#include "lang.h"
#include "txt_rules.h"

namespace cpa {

void txt_empty();
void txt_control_char();
void txt_han_in_english();
void txt_missing_glyph();
void txt_format_mismatch();
void txt_too_wide();

constexpr void check_pair(const char* zh, const char* en, int16_t max_w) {
  switch (i18n::problem_of(zh, en, max_w)) {
    case i18n::Problem::Empty: txt_empty(); break;
    case i18n::Problem::Control: txt_control_char(); break;
    case i18n::Problem::Han: txt_han_in_english(); break;
    case i18n::Problem::Glyph: txt_missing_glyph(); break;
    case i18n::Problem::Format: txt_format_mismatch(); break;
    case i18n::Problem::Wide: txt_too_wide(); break;
    default: break;
  }
}

struct Txt {
  const char* zh;
  const char* en;
  constexpr Txt(const char* z, const char* e, int16_t max_w = i18n::kNoLimit) : zh(z), en(e) {
    check_pair(z, e, max_w);
  }
};

// Width classes (docs/21 §6.4), in px of efontCN_12.
constexpr int16_t kToastTxtW = 224;  // theme::kToastTextW, the toast capsule's text
constexpr int16_t kLineTxtW = 232;   // a 240 px row less 4 px each side
constexpr int16_t kMenuTxtW = 120;   // a Tab menu row's words
constexpr int16_t kTileTxtW = 60;    // a first-party home tile's name
constexpr int16_t kSignTxtW = 48;    // Mando's sign
constexpr int16_t kKeyTxtW = 43;     // a key name in the Tab menu's key column

struct ToastTxt : Txt {
  constexpr ToastTxt(const char* z, const char* e) : Txt(z, e, kToastTxtW) {}
};
struct LineTxt : Txt {
  constexpr LineTxt(const char* z, const char* e) : Txt(z, e, kLineTxtW) {}
};
struct MenuTxt : Txt {
  constexpr MenuTxt(const char* z, const char* e) : Txt(z, e, kMenuTxtW) {}
};
struct TileTxt : Txt {
  constexpr TileTxt(const char* z, const char* e) : Txt(z, e, kTileTxtW) {}
};
struct SignTxt : Txt {
  constexpr SignTxt(const char* z, const char* e) : Txt(z, e, kSignTxtW) {}
};
struct KeyTxt : Txt {
  constexpr KeyTxt(const char* z, const char* e) : Txt(z, e, kKeyTxtW) {}
};

// A count: English has a singular (n == 1) and a plural; Chinese one form.
struct CountTxt {
  const char* zh;
  const char* one;
  const char* many;
  constexpr CountTxt(const char* z, const char* e1, const char* en, int16_t max_w = i18n::kNoLimit)
      : zh(z), one(e1), many(en) {
    check_pair(z, e1, max_w);
    check_pair(z, en, max_w);
  }
};

inline const char* tr(const Txt& t) { return lang() == Lang::En ? t.en : t.zh; }
inline const char* tr_in(const Txt& t, Lang l) { return l == Lang::En ? t.en : t.zh; }
inline const char* trn(const CountTxt& t, uint32_t n) {
  if (lang() != Lang::En) return t.zh;
  return n == 1 ? t.one : t.many;
}

// Both halves, "语言 Language": only for the row that must read in either language.
using BothText = FixedStr<63>;
BothText both(const Txt& t);

}  // namespace cpa
