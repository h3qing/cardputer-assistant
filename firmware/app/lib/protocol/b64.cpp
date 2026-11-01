#include "b64.h"

#include "protocol_limits.h"

namespace cpa {
namespace proto {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int value_of(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  return c == '/' ? 63 : -1;
}

// The number of '=' at the end of a quad-aligned input, or -1 when '=' appears elsewhere.
int padding(const char* in, size_t n) {
  int pad = 0;
  if (n >= 1 && in[n - 1] == '=') ++pad;
  if (n >= 2 && in[n - 2] == '=') ++pad;
  for (size_t i = 0; i + static_cast<size_t>(pad) < n; ++i) {
    if (in[i] == '=') return -1;
  }
  return pad;
}

}  // namespace

size_t b64_encode(const uint8_t* in, size_t n, char* out) {
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t b0 = in[i];
    const uint32_t b1 = i + 1 < n ? in[i + 1] : 0;
    const uint32_t b2 = i + 2 < n ? in[i + 2] : 0;
    const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
    out[o++] = kAlphabet[(v >> 18) & 63];
    out[o++] = kAlphabet[(v >> 12) & 63];
    out[o++] = i + 1 < n ? kAlphabet[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? kAlphabet[v & 63] : '=';
  }
  return o;
}

long b64_decode(const char* in, size_t n, uint8_t* out, size_t cap) {
  const int pad = n % 4 == 0 ? padding(in, n) : -1;
  if (pad < 0) return -1;
  const size_t bytes = n / 4 * 3 - static_cast<size_t>(pad);
  if (bytes > cap) return -1;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    uint32_t v = 0;
    for (size_t k = 0; k < 4; ++k) {
      const int d = in[i + k] == '=' ? 0 : value_of(in[i + k]);
      if (d < 0) return -1;
      v = (v << 6) | static_cast<uint32_t>(d);
    }
    for (size_t k = 0; k < 3 && o < bytes; ++k) out[o++] = static_cast<uint8_t>(v >> (16 - 8 * k));
  }
  return static_cast<long>(bytes);
}

bool is_b64(const char* s, size_t n) {
  if (n < 4 || n > limits::kB64Max || n % 4 != 0 || padding(s, n) < 0) return false;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] != '=' && value_of(s[i]) < 0) return false;
  }
  return true;
}

}  // namespace proto
}  // namespace cpa
