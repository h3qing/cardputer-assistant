#include "text_rules.h"

#include <cstring>

#include "protocol_limits.h"

namespace cpa {
namespace proto {
namespace {

bool lower_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
bool hex_lower(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
bool digit(char c) { return c >= '0' && c <= '9'; }

bool crockford(char c) {
  if (digit(c)) return true;
  return c >= 'A' && c <= 'Z' && c != 'I' && c != 'L' && c != 'O' && c != 'U';
}

template <class Pred>
bool all_of(const char* s, size_t n, Pred pred) {
  for (size_t i = 0; i < n; ++i) {
    if (!pred(s[i])) return false;
  }
  return true;
}

bool crockford_run(const char* s, size_t n) { return all_of(s, n, crockford); }

// Length of the UTF-8 sequence starting with lead byte b, or 0 if b cannot start one.
size_t seq_len(uint8_t b) {
  if (b < 0x80) return 1;
  if (b >= 0xC2 && b <= 0xDF) return 2;
  if (b >= 0xE0 && b <= 0xEF) return 3;
  if (b >= 0xF0 && b <= 0xF4) return 4;
  return 0;
}

// Allowed range of the second byte for the lead byte b (rejects overlongs,
// surrogates and code points above U+10FFFF).
bool second_ok(uint8_t b, uint8_t c) {
  if (b == 0xE0) return c >= 0xA0 && c <= 0xBF;
  if (b == 0xED) return c >= 0x80 && c <= 0x9F;
  if (b == 0xF0) return c >= 0x90 && c <= 0xBF;
  if (b == 0xF4) return c >= 0x80 && c <= 0x8F;
  return c >= 0x80 && c <= 0xBF;
}

struct CodePoint {
  size_t len = 0;  // 0 = not decodable
  uint32_t cp = 0;
};

// Decodes the first code point of already validated UTF-8.
CodePoint decode_cp(const uint8_t* p, size_t n) {
  static const uint8_t kMask[] = {0, 0x7F, 0x1F, 0x0F, 0x07};
  CodePoint out;
  const size_t len = seq_len(p[0]);
  if (len == 0 || len > n) return out;
  uint32_t v = p[0] & kMask[len];
  for (size_t i = 1; i < len; ++i) v = (v << 6) | (p[i] & 0x3Fu);
  out.len = len;
  out.cp = v;
  return out;
}

// Python's str.isspace() for code points that are not control characters
// (controls are rejected separately by no_controls).
bool py_space(uint32_t cp) {
  if (cp == 0x20 || cp == 0x85 || cp == 0xA0 || cp == 0x1680) return true;
  if (cp >= 0x2000 && cp <= 0x200A) return true;
  return cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

bool name_rule(const char* s, size_t n, size_t max) { return n >= 1 && n <= max && no_controls(s, n); }

// One dest segment: 1..12 of [a-z0-9-]; the first segment must start with a letter.
bool dest_segment(const char* s, size_t n, bool first) {
  if (n < 1 || n > 12 || (first && !(s[0] >= 'a' && s[0] <= 'z'))) return false;
  return all_of(s, n, [](char c) { return lower_alnum(c) || c == '-'; });
}

}  // namespace

bool utf8_valid(const char* s, size_t n) {
  const auto* p = reinterpret_cast<const uint8_t*>(s);
  size_t i = 0;
  while (i < n) {
    const size_t len = seq_len(p[i]);
    if (len == 0 || i + len > n) return false;
    if (len > 1 && !second_ok(p[i], p[i + 1])) return false;
    for (size_t k = 2; k < len; ++k) {
      if ((p[i + k] & 0xC0) != 0x80) return false;
    }
    i += len;
  }
  return true;
}

bool no_controls(const char* s, size_t n) {
  return all_of(s, n, [](char c) {
    const auto b = static_cast<uint8_t>(c);
    return b >= 0x20 && b != 0x7F;
  });
}

bool ascii_span(const char* s, size_t n, char lo, char hi, size_t min, size_t max) {
  return n >= min && n <= max && all_of(s, n, [lo, hi](char c) { return c >= lo && c <= hi; });
}

bool is_id(const char* s, size_t n) {
  return n >= 1 && n <= limits::kIdMax && all_of(s, n, [](char c) {
           return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || digit(c) || c == '-';
         });
}

bool is_type(const char* s, size_t n) {
  return n >= 2 && n <= 32 && s[0] >= 'a' && s[0] <= 'z' &&
         all_of(s + 1, n - 1, [](char c) { return lower_alnum(c) || c == '_' || c == '.'; });
}

bool is_boot_text(const char* s, size_t n) {
  return n == 13 && s[7] == '-' && crockford_run(s, 7) && crockford_run(s + 8, 5);
}

bool is_monoid_text(const char* s, size_t n) {
  return n == 18 && is_boot_text(s, 13) && s[13] == '-' && crockford_run(s + 14, 4);
}

bool is_sha256(const char* s, size_t n) {
  return n == 64 && all_of(s, n, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool is_rec_cursor(const char* s, size_t n) {
  return n >= 1 && n <= limits::kRecCursorMax && all_of(s, n, [](char c) {
           return (c >= 'A' && c <= 'Z') || lower_alnum(c) || c == '_' || c == '-';
         });
}

bool is_host_id(const char* s, size_t n) {
  return n >= 8 && n <= 32 && all_of(s, n, [](char c) { return lower_alnum(c) || c == '-'; });
}

bool is_dest(const char* s, size_t n) {
  if (n > limits::kDestMax) return false;
  size_t start = 0;
  for (int seg = 0; seg < 3; ++seg) {
    const char* dot = static_cast<const char*>(std::memchr(s + start, '.', n - start));
    const size_t end = dot ? static_cast<size_t>(dot - s) : n;
    if (!dest_segment(s + start, end - start, seg == 0)) return false;
    if (!dot) return true;
    start = end + 1;
  }
  return false;  // a fourth segment
}

bool is_dest_kind(const char* s, size_t n) { return ascii_span(s, n, 'a', 'z', 3, 7); }
bool is_dest_key(const char* s, size_t n) { return ascii_span(s, n, 'a', 'z', 1, 1); }
bool is_dest_flags(const char* s, size_t n) { return ascii_span(s, n, 'a', 'z', 0, 8); }

bool is_dest_row_id(const char* s, size_t n) {
  const char* dot = static_cast<const char*>(std::memchr(s, '.', n));
  const size_t kind = dot ? static_cast<size_t>(dot - s) : n;
  if (!is_dest_kind(s, kind)) return false;
  if (!dot) return true;
  const char* name = dot + 1;
  const size_t len = n - kind - 1;
  return len >= 1 && len <= 12 && lower_alnum(name[0]) &&
         all_of(name + 1, len - 1, [](char c) { return lower_alnum(c) || c == '-'; });
}

bool is_dest_label(const char* s, size_t n) {
  return name_rule(s, n, 24) && all_of(s, n, [](char c) { return c != '"' && c != '\\'; });
}

bool is_project_key(const char* s, size_t n) {
  return n >= 1 && n <= 12 && lower_alnum(s[0]) &&
         all_of(s + 1, n - 1, [](char c) { return lower_alnum(c) || c == '-'; });
}

bool is_sid(const char* s, size_t n) { return n == 8 && all_of(s, n, hex_lower); }
bool is_dev(const char* s, size_t n) { return n == 12 && all_of(s, n, hex_lower); }
bool is_qid(const char* s, size_t n) { return n >= 2 && n <= 11 && s[0] == 'q' && all_of(s + 1, n - 1, digit); }

bool is_fw(const char* s, size_t n) {
  static const char kPrefix[] = "cpa-fw/";
  const size_t p = sizeof kPrefix - 1;
  return n > p && std::memcmp(s, kPrefix, p) == 0 && ascii_span(s + p, n - p, '!', '~', 1, 24);
}

bool is_rst(const char* s, size_t n) {
  return n >= 1 && n <= 16 && all_of(s, n, [](char c) { return lower_alnum(c) || c == '_'; });
}

bool is_module_id(const char* s, size_t n) {
  return n >= 2 && n <= 16 && s[0] >= 'a' && s[0] <= 'z' &&
         all_of(s + 1, n - 1, [](char c) { return lower_alnum(c) || c == '_'; });
}

bool is_app(const char* s, size_t n) { return ascii_span(s, n, '!', '~', 1, 32); }
bool is_short(const char* s, size_t n) { return ascii_span(s, n, '!', '~', 1, limits::kShortMax); }
bool is_field_path(const char* s, size_t n) { return ascii_span(s, n, '!', '~', 1, 48); }
bool is_err_msg(const char* s, size_t n) { return ascii_span(s, n, ' ', '~', 1, 80); }

bool is_project_name(const char* s, size_t n) { return name_rule(s, n, limits::kProjectNameMax); }
bool is_session_name(const char* s, size_t n) { return name_rule(s, n, limits::kSessionNameMax); }
bool is_queue_text(const char* s, size_t n) { return name_rule(s, n, limits::kQueueTextMax); }
bool is_user_text(const char* s, size_t n) { return name_rule(s, n, limits::kTextMax); }
bool is_host_label(const char* s, size_t n) { return name_rule(s, n, limits::kHostLabelMax); }
bool is_rec_title(const char* s, size_t n) { return name_rule(s, n, limits::kRecTitleMax); }

bool is_agent_text(const char* s, size_t n) {
  if (!is_user_text(s, n)) return false;
  const auto* p = reinterpret_cast<const uint8_t*>(s);
  size_t i = 0;
  while (i < n) {
    const CodePoint c = decode_cp(p + i, n - i);
    if (c.len == 0 || !py_space(c.cp)) break;
    i += c.len;
  }
  return i >= n || s[i] != '-';
}

}  // namespace proto
}  // namespace cpa
