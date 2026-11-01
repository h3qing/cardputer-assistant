#include "key_names.h"

#include <cstring>

namespace cpa {
namespace proto {
namespace {

constexpr const char* kNames[kKeyCount] = {
    "`",    "1",     "2",   "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "bksp",
    "tab",  "q",     "w",   "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\",
    "fn",   "shift", "a",   "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "enter",
    "ctrl", "opt",   "alt", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "space",
    "g0",
};

}  // namespace

const char* key_name(uint8_t index) { return index < kKeyCount ? kNames[index] : ""; }

Opt<uint8_t> key_index(const char* s, size_t n) {
  for (uint8_t i = 0; i < kKeyCount; ++i) {
    if (std::strlen(kNames[i]) == n && std::memcmp(kNames[i], s, n) == 0) return some(i);
  }
  return {};
}

}  // namespace proto
}  // namespace cpa
