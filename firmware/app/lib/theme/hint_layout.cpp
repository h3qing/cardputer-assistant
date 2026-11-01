#include "hint_layout.h"

#include <cstring>

namespace cpa {
namespace theme {
namespace {

bool visible_ascii(char c) { return c >= '!' && c <= '~'; }

// 退格 and 空格: Han key names (rule 5); the bytes they take at the start of w, or 0.
size_t han_key(const char* w, size_t len) {
  static const char* const kKeys[] = {"\xE9\x80\x80\xE6\xA0\xBC", "\xE7\xA9\xBA\xE6\xA0\xBC"};  // 退格 空格
  for (const char* k : kKeys) {
    const size_t kl = std::strlen(k);
    if (len >= kl && std::memcmp(w, k, kl) == 0) return kl;
  }
  return 0;
}

struct Cutter {
  const char* hint;
  HintItems out;
  bool open = false;  // the last item is a key still waiting for its words

  bool add(size_t key_at, size_t key_len, size_t text_at, size_t text_len) {
    if (out.n == kHintItemsMax) {
      out.overflow = true;
      return false;
    }
    HintItem it;
    it.key_at = static_cast<uint8_t>(key_at);
    it.key_len = static_cast<uint8_t>(key_len);
    it.text_at = static_cast<uint8_t>(text_at);
    it.text_len = static_cast<uint8_t>(text_len);
    out.items[out.n++] = it;
    open = key_len > 0 && text_len == 0;
    return true;
  }

  // One word [at, at + len); false once the items are full.
  bool word(size_t at, size_t len) {
    size_t ascii = 0;
    while (ascii < len && visible_ascii(hint[at + ascii])) ++ascii;
    HintItem* last = out.n > 0 ? &out.items[out.n - 1] : nullptr;
    if (ascii == len && open) {  // rule 4: more of the same key
      last->key_len = static_cast<uint8_t>(at + len - last->key_at);
      return true;
    }
    if (ascii > 0) return add(at, ascii, at + ascii, len - ascii);  // rule 2
    const size_t hk = han_key(hint + at, len);
    if (hk > 0) return add(at, hk, at + hk, len - hk);  // rule 5's key names
    if (open) {  // rule 3
      last->text_at = static_cast<uint8_t>(at);
      last->text_len = static_cast<uint8_t>(len);
      open = false;
      return true;
    }
    return add(at, 0, at, len);  // rule 5
  }
};

// A "[" that starts a word (docs/21 §6.6: an English hint brackets its keys).
bool key_open(const char* hint, size_t i) { return hint[i] == '[' && (i == 0 || hint[i - 1] == ' '); }

size_t next_key(const char* hint, size_t from, size_t n) {
  while (from < n && !key_open(hint, from)) ++from;
  return from;
}

bool bracketed(const char* hint, size_t n) {
  const size_t k = next_key(hint, 0, n);
  return k < n && std::strchr(hint + k, ']') != nullptr;
}

size_t trim_end(const char* hint, size_t from, size_t to) {
  while (to > from && hint[to - 1] == ' ') --to;
  return to;
}

// "[Enter]open [`]back": each key in brackets, its words up to the next "[" at a word start;
// text before the first key, or from an unclosed "[", is plain text.
HintItems bracket_items(const char* hint, size_t n) {
  Cutter cut{hint, HintItems{}};
  size_t at = 0;
  while (at < n && hint[at] == ' ') ++at;
  while (at < n && n <= 255) {
    const size_t k = next_key(hint, at, n);
    if (k > at && !cut.add(at, 0, at, trim_end(hint, at, k) - at)) break;
    const char* close = k < n ? std::strchr(hint + k, ']') : nullptr;
    if (k < n && close == nullptr) {
      cut.add(k, 0, k, trim_end(hint, k, n) - k);
      break;
    }
    if (k >= n) break;
    const size_t e = static_cast<size_t>(close - hint);
    const size_t next = next_key(hint, e + 1, n);
    if (!cut.add(k + 1, e - k - 1, e + 1, trim_end(hint, e + 1, next) - e - 1)) break;
    at = next;
  }
  return cut.out;
}

}  // namespace

HintItems hint_items(const char* hint) {
  const size_t n = std::strlen(hint);
  if (bracketed(hint, n)) return bracket_items(hint, n);
  Cutter cut{hint, HintItems{}};
  for (size_t i = 0; i < n;) {
    if (hint[i] == ' ') {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < n && hint[j] != ' ') ++j;
    if (j > 255 || !cut.word(i, j - i)) break;  // byte offsets are uint8_t
    i = j;
  }
  return cut.out;
}

namespace {

// Appends s to line.text at *at; false when it does not fit.
bool put(HintLine& line, size_t& at, const char* s, size_t n) {
  if (at + n > kHintTextMax) return false;
  std::memcpy(line.text + at, s, n);
  at += n;
  return true;
}

}  // namespace

HintLine hint_line(const HintPart* parts, uint8_t n) {
  HintLine line;
  size_t at = 0;
  for (uint8_t i = 0; i < n; ++i) {
    const size_t kl = std::strlen(parts[i].keys);
    const size_t wl = std::strlen(parts[i].words);
    // a space between parts, so the text reads as words when drawn as one line
    const bool room = line.items.n < kHintItemsMax && (i == 0 || put(line, at, " ", 1));
    const size_t key_at = at;
    if (!room || !put(line, at, parts[i].keys, kl) || !put(line, at, parts[i].words, wl)) {
      line.items.overflow = true;
      break;
    }
    line.items.items[line.items.n++] = HintItem{static_cast<uint8_t>(key_at), static_cast<uint8_t>(kl),
                                                static_cast<uint8_t>(key_at + kl), static_cast<uint8_t>(wl)};
  }
  line.text[at] = '\0';
  return line;
}

}  // namespace theme
}  // namespace cpa
