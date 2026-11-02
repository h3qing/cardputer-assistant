// How a hint string becomes keycaps (docs/19 §4.2), pure. Module hint() strings stay as
// they are ("Enter打开 ;.选择 Opt+1-9项目  Tab菜单"); the theme cuts them into items:
//   1. words are split at spaces (a run of spaces only groups them);
//   2. a word's leading visible ASCII is its key, the rest says what it does:
//      "Enter打开" -> [Enter] 打开, ";.选择" -> [;.] 选择;
//   3. a key with nothing after it takes the next word when that starts with a Han
//      character: "Enter 确认" -> [Enter] 确认;
//   4. pure ASCII words in a row are one key: "Opt N/Q/S 类型" -> [Opt N/Q/S] 类型;
//   5. a word starting with a Han character is plain text, except the key names 退格
//      (Backspace) and 空格 (Space): "退格 退出" -> [退格] 退出.
//   6. (docs/21 §6.6) English words are ASCII like keys, so an English hint brackets its
//      keys, and then only brackets count: "[Enter]open [`]keep recording" -> [Enter] open,
//      [`] keep recording. Words run to the next "[" at a word start; text before the first
//      key, or from a "[" never closed, is plain text.
#pragma once

#include <cstdint>

namespace cpa {
namespace theme {

constexpr uint8_t kHintItemsMax = 8;

// Byte ranges of the hint string; key_len == 0: plain text only.
struct HintItem {
  uint8_t key_at = 0;
  uint8_t key_len = 0;
  uint8_t text_at = 0;
  uint8_t text_len = 0;
};

struct HintItems {
  uint8_t n = 0;
  bool overflow = false;  // more items than kHintItemsMax
  HintItem items[kHintItemsMax];
};

HintItems hint_items(const char* hint);

// A structured hint (docs/21 §6.6): each part is a key and its words, already in the shown
// language, taken as given. English words are ASCII like keys, so "Enter open" parsed by
// the rules above would be one key; parts never are. keys "" is words alone, words "" a key
// alone. More parts than kHintItemsMax, or more text than kHintTextMax, set overflow (the
// bar then draws it as one line, as it does an unparseable hint).
struct HintPart {
  const char* keys;
  const char* words;
};

constexpr uint8_t kHintTextMax = 191;

struct HintLine {
  char text[kHintTextMax + 1] = {};  // the parts' bytes, a space between parts; items index into it
  HintItems items;
};

HintLine hint_line(const HintPart* parts, uint8_t n);

}  // namespace theme
}  // namespace cpa
