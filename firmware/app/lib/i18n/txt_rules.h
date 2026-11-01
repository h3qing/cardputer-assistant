// The rules every zh/en pair is held to at build time (docs/21 §6.3), as constexpr
// predicates over UTF-8. txt.h runs them in Txt's constexpr constructor; tests run the
// same functions at run time. Widths are efontCN_12's (firmware/sim test_i18n_font checks
// this table against the real glyphs): ASCII and 11 symbols 6 px, everything else counted
// as 12 px, which can only over-measure.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cpa {
namespace i18n {

constexpr int16_t kNoLimit = 0x7FFF;

struct Cp {
  uint32_t cp;
  uint8_t len;
};

// The code point at s[i]; a malformed sequence is U+FFFD, one byte.
constexpr Cp cp_at(const char* s, size_t i) {
  const uint8_t b = static_cast<uint8_t>(s[i]);
  if (b < 0x80) return Cp{b, 1};
  const uint8_t len = b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : (b >= 0xC0 ? 2 : 1));
  if (len == 1) return Cp{0xFFFD, 1};
  uint32_t cp = b & (0xFFu >> (len + 1));
  for (uint8_t k = 1; k < len; ++k) {
    const uint8_t c = static_cast<uint8_t>(s[i + k]);
    if ((c & 0xC0) != 0x80) return Cp{0xFFFD, 1};  // also stops at the NUL
    cp = (cp << 6) | (c & 0x3Fu);
  }
  return Cp{cp, len};
}

// efontCN_12's narrow symbols: ● … ★ ↑ ← → ◎ ◇ ○ · ×
constexpr uint32_t kNarrow[] = {0x25CF, 0x2026, 0x2605, 0x2191, 0x2190, 0x2192,
                                0x25CE, 0x25C7, 0x25CB, 0x00B7, 0x00D7};
// Common symbols efontCN_12 has no glyph for (drawn as □): — – ✓ ✗ ⚠ ▶ ▸ ▼ ‹ › « » ½ ⏎ ↵ ⌫ ⇧ ⌘ ⌥ ♪
constexpr uint32_t kNoGlyph[] = {0x2014, 0x2013, 0x2713, 0x2717, 0x26A0, 0x25B6, 0x25B8,
                                 0x25BC, 0x2039, 0x203A, 0x00AB, 0x00BB, 0x00BD, 0x23CE,
                                 0x21B5, 0x232B, 0x21E7, 0x2318, 0x2325, 0x266A};

template <size_t N>
constexpr bool listed(const uint32_t (&list)[N], uint32_t cp) {
  for (uint32_t x : list) {
    if (x == cp) return true;
  }
  return false;
}

constexpr int16_t cp_width(uint32_t cp) { return cp < 0x80 || listed(kNarrow, cp) ? 6 : 12; }

// Han, kana and CJK / full-width punctuation: U+3000-U+9FFF, U+FF00-U+FFEF.
constexpr bool is_han(uint32_t cp) { return (cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xFF00 && cp <= 0xFFEF); }
constexpr bool is_no_glyph(uint32_t cp) { return listed(kNoGlyph, cp); }
constexpr bool is_control(uint32_t cp) { return cp < 0x20 || cp == 0x7F; }

constexpr bool any_cp(const char* s, bool (*pred)(uint32_t)) {
  for (size_t i = 0; s[i] != '\0';) {
    const Cp c = cp_at(s, i);
    if (pred(c.cp)) return true;
    i += c.len;
  }
  return false;
}

constexpr int32_t text_width(const char* s) {
  int32_t w = 0;
  for (size_t i = 0; s[i] != '\0';) {
    const Cp c = cp_at(s, i);
    w += cp_width(c.cp);
    i += c.len;
  }
  return w;
}

constexpr bool has_han(const char* s) { return any_cp(s, is_han); }
constexpr bool has_missing_glyph(const char* s) { return any_cp(s, is_no_glyph); }
constexpr bool has_control(const char* s) { return any_cp(s, is_control); }
constexpr bool has_percent(const char* s) {
  for (size_t i = 0; s[i] != '\0'; ++i) {
    if (s[i] == '%') return true;
  }
  return false;
}

// printf conversions: a spec runs from % to its conversion character ("%%" included).
constexpr bool conv_end(char c) {
  for (const char* p = "diouxXcspfFeEgGaAn%"; *p != '\0'; ++p) {
    if (*p == c) return true;
  }
  return false;
}

struct Span {
  size_t at;  // s[at] == '\0': no further conversion
  size_t end;
};

constexpr Span next_conv(const char* s, size_t i) {
  while (s[i] != '\0' && s[i] != '%') ++i;
  if (s[i] == '\0') return Span{i, i};
  size_t j = i + 1;
  while (s[j] != '\0' && !conv_end(s[j])) ++j;
  return Span{i, s[j] == '\0' ? j : j + 1};
}

constexpr bool same_span(const char* a, Span x, const char* b, Span y) {
  if (x.end - x.at != y.end - y.at) return false;
  for (size_t k = 0; k < x.end - x.at; ++k) {
    if (a[x.at + k] != b[y.at + k]) return false;
  }
  return true;
}

// The same conversions in the same order: the arguments cannot be swapped by a translation.
constexpr bool same_conversions(const char* a, const char* b) {
  Span x = next_conv(a, 0);
  Span y = next_conv(b, 0);
  while (a[x.at] != '\0' && b[y.at] != '\0') {
    if (!same_span(a, x, b, y)) return false;
    x = next_conv(a, x.end);
    y = next_conv(b, y.end);
  }
  return a[x.at] == '\0' && b[y.at] == '\0';
}

enum class Problem : uint8_t { None, Empty, Control, Han, Glyph, Format, Wide };

// A format string's width is left to its tests, which fill in the longest arguments.
constexpr bool too_wide(const char* s, int16_t max_w) { return !has_percent(s) && text_width(s) > max_w; }

constexpr Problem problem_of(const char* zh, const char* en, int16_t max_w) {
  if (zh[0] == '\0' || en[0] == '\0') return Problem::Empty;
  if (has_control(zh) || has_control(en)) return Problem::Control;
  if (has_han(en)) return Problem::Han;
  if (has_missing_glyph(en)) return Problem::Glyph;
  if (!same_conversions(zh, en)) return Problem::Format;
  if (too_wide(zh, max_w) || too_wide(en, max_w)) return Problem::Wide;
  return Problem::None;
}

constexpr const char* problem_name(Problem p) {
  switch (p) {
    case Problem::Empty: return "empty";
    case Problem::Control: return "control";
    case Problem::Han: return "han in english";
    case Problem::Glyph: return "missing glyph";
    case Problem::Format: return "format";
    case Problem::Wide: return "too wide";
    default: return nullptr;
  }
}

constexpr const char* pair_problem(const char* zh, const char* en, int16_t max_w) {
  return problem_name(problem_of(zh, en, max_w));
}

}  // namespace i18n
}  // namespace cpa
