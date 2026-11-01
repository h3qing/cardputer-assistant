// FixedStr<N>: a fixed-capacity byte string held by value (no heap; 05 §1.2).
// N is the maximum number of bytes, excluding the terminating NUL. Content may
// contain NUL bytes; size() is authoritative. Every operation returns a new value.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cpa {

template <size_t N>
class FixedStr {
  static_assert(N > 0 && N < 65535, "FixedStr capacity must fit uint16_t");

 public:
  constexpr FixedStr() = default;

  // Copies up to N bytes of s[0..n); longer input is truncated (callers validate first).
  static FixedStr from(const char* s, size_t n) {
    FixedStr out;
    const size_t take = n < N ? n : N;
    if (take > 0) std::memcpy(out.buf_, s, take);
    out.buf_[take] = '\0';
    out.len_ = static_cast<uint16_t>(take);
    return out;
  }

  static FixedStr from(const char* s) { return from(s, s ? std::strlen(s) : 0); }

  // Builds a new value in place: fill(buf, N + 1) writes the content into buf and
  // returns its length (clamped to N). Spares a second large buffer on the stack.
  template <class Fill>
  static FixedStr build(Fill fill) {
    FixedStr out;
    const size_t n = fill(out.buf_, N + 1);
    out.len_ = static_cast<uint16_t>(n < N ? n : N);
    out.buf_[out.len_] = '\0';
    return out;
  }

  static constexpr bool fits(size_t n) { return n <= N; }
  static constexpr size_t capacity() { return N; }

  const char* c_str() const { return buf_; }
  const char* data() const { return buf_; }
  size_t size() const { return len_; }
  bool empty() const { return len_ == 0; }

  bool equals(const char* s, size_t n) const { return n == len_ && std::memcmp(buf_, s, n) == 0; }
  bool equals(const char* s) const { return equals(s, std::strlen(s)); }
  bool operator==(const FixedStr& o) const { return equals(o.buf_, o.len_); }
  bool operator!=(const FixedStr& o) const { return !(*this == o); }

  // New value with s[0..n) appended (truncated at capacity).
  FixedStr append(const char* s, size_t n) const {
    FixedStr out = *this;
    const size_t room = N - len_;
    const size_t take = n < room ? n : room;
    if (take > 0) std::memcpy(out.buf_ + len_, s, take);
    out.len_ = static_cast<uint16_t>(len_ + take);
    out.buf_[out.len_] = '\0';
    return out;
  }

  FixedStr append(const char* s) const { return append(s, std::strlen(s)); }

 private:
  char buf_[N + 1] = {};
  uint16_t len_ = 0;
};

}  // namespace cpa
