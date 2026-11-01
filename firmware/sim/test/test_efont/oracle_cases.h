// The draw cases of the M5GFX oracle (firmware/sim/oracle): each is drawn on a black
// 240 x 135 8-bpp sprite by M5GFX itself and by FbCanvas. oracle_golden.h holds what
// M5GFX drew, so test_efont checks FbCanvas against M5GFX without M5GFX in the build.
// Plain C++ only: the oracle's M5GFX side includes this too.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oracle {

struct Case {
  bool fill;    // fillRect instead of text
  int x, y;     // text: the top left, as Canvas::text
  int w, h;     // fill only
  const char* text;
  int fg, bg;  // cpa::Color values (0 Bg, 1 Fg, 2 Dim, 3 Accent, 4 Ok, 5 Warn, 6 Error, 7 Select,
               // 9 Card, 15 Ink2, 17 OnAccent, 21 RecBg, 23 RecDot): palette entries on the sprite
};

// 32 CJK characters: 96 bytes, the device's longest drawString run (M5Display::walk).
#define ORACLE_CJK32 "一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二"

constexpr Case kCases[] = {
    {false, 0, 0, 0, 0, "Cardputer 助手", 1, 0},
    {false, 3, 5, 0, 0, "Cardputer 助手", 1, 7},
    {false, 0, 0, 0, 0, "● 在线 ◎ 同步 ○ 离线 ◇ 无响应", 4, 0},
    {false, 0, 0, 0, 0, "● 在线 ◎ 同步 ○ 离线 ◇ 无响应", 4, 7},
    {false, 200, 60, 0, 0, "94% tty 队2", 2, 0},
    {false, 210, 60, 0, 0, "94% tty 队2 overflow", 1, 7},
    {false, -5, 30, 0, 0, "jgpqy|Ag 助", 1, 7},  // starts left of the screen: bg clipped at 0
    {false, -14, 30, 0, 0, "Wj 已保存 · 待同步 1", 3, 7},
    {false, 10, 125, 0, 0, "Enter 确认  ` 返回  Tab 菜单", 2, 0},
    {false, 10, 128, 0, 0, "bottom clip 助手 gjy", 1, 6},
    {false, 0, -4, 0, 0, "top clip 助手 gjy", 1, 6},
    {false, 0, 40, 0, 0, "✓ done ⌛ wait \xF0\x9F\x98\x80 emoji", 1, 7},  // no glyph, above U+FFFF
    {false, 0, 40, 0, 0, "tab\there nl\nend", 1, 7},
    {false, 0, 40, 0, 0, "trunc \xE5\x8A", 1, 7},
    {false, 0, 40, 0, 0, "lone \x80 cont \xC3( bad", 1, 7},
    {false, 0, 40, 0, 0, "\xEF\xBF\xBD replacement", 1, 7},
    {false, 0, 40, 0, 0, ORACLE_CJK32 ORACLE_CJK32 " long run over 96 bytes", 1, 7},
    {false, -300, 60, 0, 0, ORACLE_CJK32 "jgA 助手 split here", 1, 7},  // the split on screen
    {false, -301, 90, 0, 0, ORACLE_CJK32 "\xF0\x9F\x98\x80" ORACLE_CJK32 "end", 5, 0},
    {false, 0, 40, 0, 0, "…←→↑↓ □■ ★☆ ①② “引号” 「括号」 —— ～", 1, 7},
    {false, 0, 40, 0, 0,
     "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 !@#$%^&*()_+-=[]{};':\",./<>?`~\\|", 1, 7},
    {false, 0, 40, 0, 0, "é ü ñ ß ° ± × ÷ ¥ € £", 1, 7},
    {false, 0, 40, 0, 0, "same fg bg", 1, 1},
    {true, 10, 10, 50, 20, nullptr, 2, 0},
    {true, 100, 100, -30, -10, nullptr, 3, 0},
    {true, 230, 130, 50, 50, nullptr, 6, 0},
    {true, -10, -10, 20, 20, nullptr, 5, 0},
    // v0.5 (docs/19 §3.1): entries past v0.4's eight, and fg == bg (theme::over: glyphs only).
    {false, 3, 40, 0, 0, "Card 卡片 Ink2", 15, 9},
    {false, 3, 60, 0, 0, "●12:31 over 透明", 6, 6},
    {false, 3, 80, 0, 0, "OnAccent 焦糖", 17, 21},
    {true, 60, 100, 30, 9, nullptr, 23, 0},
};
constexpr int kCaseCount = static_cast<int>(sizeof(kCases) / sizeof(kCases[0]));

// Single glyphs drawn white on black at (0, 0), kept as rows of '#' (ink) and '.'.
constexpr const char* kGlyphs[] = {"A", "g", "助", "●", "□"};
constexpr int kGlyphCount = static_cast<int>(sizeof(kGlyphs) / sizeof(kGlyphs[0]));
constexpr int kGlyphRows = 12;

// FNV-1a, 64 bits, of a frame's bytes.
inline uint64_t fnv1a(const uint8_t* p, size_t n) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
  return h;
}

}  // namespace oracle
