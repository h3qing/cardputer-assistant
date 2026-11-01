#include "gate_core.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace gate {

namespace {

constexpr size_t kTok = 16;

// Splits on spaces; reading past the last token yields empty tokens.
struct Tokens {
  const char* p;

  bool next(char* out) {
    while (p && *p == ' ') ++p;
    size_t n = 0;
    while (p && *p && *p != ' ' && n + 1 < kTok) out[n++] = *p++;
    out[n] = '\0';
    while (p && *p && *p != ' ') ++p;  // a token longer than kTok cannot match anything
    return n > 0;
  }
};

bool to_uint(const char* t, uint32_t lo, uint32_t hi, uint32_t& v) {
  if (!*t || std::strlen(t) > 9) return false;
  uint32_t x = 0;
  for (const char* c = t; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    x = x * 10 + static_cast<uint32_t>(*c - '0');
  }
  v = x;
  return x >= lo && x <= hi;
}

// Optional trailing number: absent keeps the default, present must be in range.
bool opt_uint(Tokens& tk, uint32_t lo, uint32_t hi, uint32_t& v) {
  char t[kTok];
  return !tk.next(t) || to_uint(t, lo, hi, v);
}

bool no_more(Tokens& tk) {
  char t[kTok];
  return !tk.next(t);
}

template <typename T>
Parsed<T> fail(const char* what) {
  Parsed<T> p;
  std::memset(&p, 0, sizeof p);
  p.ok = false;
  p.err = what;
  return p;
}

template <typename T>
Parsed<T> done(const T& a) {
  Parsed<T> p;
  p.ok = true;
  p.args = a;
  p.err = nullptr;
  return p;
}

bool parse_enc(const char* t, Enc& e) {
  if (!std::strcmp(t, "b64")) e = Enc::B64;
  else if (!std::strcmp(t, "bin")) e = Enc::Bin;
  else return false;
  return true;
}

bool parse_src(const char* t, Src& s) {
  if (!std::strcmp(t, "ram")) s = Src::Ram;
  else if (!std::strcmp(t, "sd")) s = Src::Sd;
  else if (!std::strcmp(t, "sdkeep")) s = Src::SdKeep;
  else return false;
  return true;
}

bool parse_ring(const char* t, uint8_t& kb) {
  uint32_t v = 0;
  if (!to_uint(t, 4, 16, v) || (v != 4 && v != 8 && v != 16)) return false;
  kb = static_cast<uint8_t>(v);
  return true;
}

bool parse_pattern(const char* t, Pattern& p) {
  if (!*t || !std::strcmp(t, "memo")) p = Pattern::Memo;
  else if (!std::strcmp(t, "meet")) p = Pattern::Meet;
  else if (!std::strcmp(t, "silent")) p = Pattern::Silent;
  else return false;
  return true;
}

bool parse_hammer(const char* t, Hammer& h) {
  if (!*t || !std::strcmp(t, "off")) h = Hammer::Off;
  else if (!std::strcmp(t, "nolock")) h = Hammer::NoLock;
  else if (!std::strcmp(t, "lock")) h = Hammer::Lock;
  else if (!std::strcmp(t, "lockyield")) h = Hammer::LockYield;
  else return false;
  return true;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hex_byte(const char* p, uint8_t& v) {
  const int hi = hex_digit(p[0]);
  const int lo = hi < 0 ? -1 : hex_digit(p[1]);
  if (lo < 0) return false;
  v = static_cast<uint8_t>(hi * 16 + lo);
  return true;
}

// "RR=VV", exactly.
bool parse_reg_eq(const char* t, Reg& r) {
  return std::strlen(t) == 5 && t[2] == '=' && hex_byte(t, r.reg) && hex_byte(t + 3, r.val);
}

// "<ms>:RR=VV", ms 0..kWarmMaxMs.
bool parse_timed(const char* t, TimedReg& w) {
  const char* colon = std::strchr(t, ':');
  if (!colon || colon == t || colon - t > 5) return false;
  char ms[6];
  std::memcpy(ms, t, static_cast<size_t>(colon - t));
  ms[colon - t] = '\0';
  uint32_t v = 0;
  Reg r = {0, 0};
  if (!to_uint(ms, 0, kWarmMaxMs, v) || !parse_reg_eq(colon + 1, r)) return false;
  w.at_ms = static_cast<uint16_t>(v);
  w.reg = r.reg;
  w.val = r.val;
  return true;
}

}  // namespace

Parsed<WarmArgs> parse_warm(const char* s) {
  Tokens tk = {s};
  char t[kTok];
  WarmArgs a;
  std::memset(&a, 0, sizeof a);
  uint32_t v = 0;
  if (!tk.next(t) || !to_uint(t, 100, kWarmMaxMs, v)) return fail<WarmArgs>("rec_ms");
  a.rec_ms = static_cast<uint16_t>(v);
  tk.next(t);
  if (std::strcmp(t, "end") != 0 && std::strcmp(t, "keep") != 0) return fail<WarmArgs>("mode");
  a.keep = t[0] == 'k';
  a.rate = kWarmRate;
  bool first = true;
  while (tk.next(t)) {
    if (first && !std::strncmp(t, "hz=", 3)) {
      if (!to_uint(t + 3, 8000, 64000, a.rate)) return fail<WarmArgs>("hz");
      first = false;
      continue;
    }
    first = false;
    if (a.n == kMaxRegWrites) return fail<WarmArgs>("writes");
    TimedReg& w = a.w[a.n];
    if (!parse_timed(t, w)) return fail<WarmArgs>("write");
    if (a.n > 0 && w.at_ms < a.w[a.n - 1].at_ms) return fail<WarmArgs>("order");
    ++a.n;
  }
  if (static_cast<uint64_t>(a.rec_ms) * a.rate / 1000 > kWarmMaxSamples) return fail<WarmArgs>("rec_ms");
  return done(a);
}

Parsed<RegWrites> parse_regs(const char* s) {
  Tokens tk = {s};
  char t[kTok];
  RegWrites a;
  std::memset(&a, 0, sizeof a);
  while (tk.next(t)) {
    if (a.n == kMaxRegWrites) return fail<RegWrites>("writes");
    if (!parse_reg_eq(t, a.r[a.n])) return fail<RegWrites>("write");
    ++a.n;
  }
  return a.n ? done(a) : fail<RegWrites>("write");
}

size_t leading_zeros(const int16_t* s, size_t n) {
  size_t i = 0;
  while (i < n && s[i] == 0) ++i;
  return i;
}

Parsed<XferArgs> parse_xfer(const char* s) {
  Tokens tk = {s};
  char t[kTok];
  XferArgs a = {Enc::B64, 0, 0, 4, Src::Ram, kXferDefaultSecs};
  uint32_t v = 0;
  if (!tk.next(t) || !parse_enc(t, a.enc)) return fail<XferArgs>("enc");
  if (!tk.next(t) || !to_uint(t, 1, 4096, v)) return fail<XferArgs>("len");
  a.len = static_cast<uint16_t>(v);
  if (!tk.next(t) || !to_uint(t, 0, 16, v)) return fail<XferArgs>("depth");
  a.depth = static_cast<uint8_t>(v);
  if (!tk.next(t) || !parse_ring(t, a.ring_kb)) return fail<XferArgs>("txring");
  if (!tk.next(t) || !parse_src(t, a.src)) return fail<XferArgs>("src");
  v = a.secs;
  if (!opt_uint(tk, 1, 120, v)) return fail<XferArgs>("secs");
  a.secs = static_cast<uint16_t>(v);
  if (!no_more(tk)) return fail<XferArgs>("extra");
  return done(a);
}

Parsed<CueArgs> parse_cue(const char* s) {
  Tokens tk = {s};
  char t[kTok];
  CueArgs a = {0, 0, Pattern::Memo, Hammer::Off, kCueDefaultRecMs};
  uint32_t v = 0;
  if (!tk.next(t) || !to_uint(t, 20, 1000, v)) return fail<CueArgs>("ms");
  a.tone_ms = static_cast<uint16_t>(v);
  if (!tk.next(t) || !to_uint(t, 0, 255, v)) return fail<CueArgs>("vol");
  a.vol = static_cast<uint8_t>(v);
  tk.next(t);
  if (!parse_pattern(t, a.pattern)) return fail<CueArgs>("pattern");
  tk.next(t);
  if (!parse_hammer(t, a.hammer)) return fail<CueArgs>("hammer");
  v = a.rec_ms;
  if (!opt_uint(tk, 200, 1000, v)) return fail<CueArgs>("rec_ms");
  a.rec_ms = static_cast<uint16_t>(v);
  if (!no_more(tk)) return fail<CueArgs>("extra");
  return done(a);
}

Parsed<MicArgs> parse_mic(const char* s) {
  Tokens tk = {s};
  char t[kTok];
  MicArgs a = {0, 0, 0};
  uint32_t v = 0;
  if (!tk.next(t) || !to_uint(t, 1, 64, v)) return fail<MicArgs>("mag");
  a.mag = static_cast<uint8_t>(v);
  if (!tk.next(t) || !to_uint(t, 0, 10, v)) return fail<MicArgs>("pga");
  a.pga = static_cast<uint8_t>(v);
  if (!tk.next(t) || !to_uint(t, 1, 300, v)) return fail<MicArgs>("secs");
  a.secs = static_cast<uint16_t>(v);
  if (!no_more(tk)) return fail<MicArgs>("extra");
  return done(a);
}

uint8_t es8311_reg14(uint8_t pga) { return static_cast<uint8_t>(0x10 | (pga > 10 ? 10 : pga)); }

size_t frame_head(char* out, size_t cap, Enc enc, const FrameMeta& m) {
  const char* const fmt_b64 =
      "{\"v\":0,\"t\":\"file.data\",\"id\":\"d%lu\",\"re\":\"h%lu\",\"fid\":\"K3M9Q2T-00042-0A1C\","
      "\"off\":%lu,\"n\":%lu,\"size\":%lu,\"crc\":%lu,\"b64\":\"";
  const char* const fmt_bin =
      "{\"v\":0,\"t\":\"file.data\",\"id\":\"d%lu\",\"re\":\"h%lu\",\"fid\":\"K3M9Q2T-00042-0A1C\","
      "\"off\":%lu,\"n\":%lu,\"size\":%lu,\"crc\":%lu,\"bin\":%lu}\n";
  const unsigned long seq = m.seq, off = m.off, n = m.n, size = m.size, crc = m.crc;
  const int w = enc == Enc::B64 ? std::snprintf(out, cap, fmt_b64, seq, seq, off, n, size, crc)
                                : std::snprintf(out, cap, fmt_bin, seq, seq, off, n, size, crc, n);
  return (w < 0 || static_cast<size_t>(w) >= cap) ? 0 : static_cast<size_t>(w);
}

size_t b64_encode(const uint8_t* in, size_t n, char* out, size_t cap) {
  static const char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t need = b64_len(n);
  if (need > cap) return 0;
  size_t o = 0, i = 0;
  for (; i + 3 <= n; i += 3) {
    const uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
    out[o++] = kAlpha[v >> 18];
    out[o++] = kAlpha[(v >> 12) & 63];
    out[o++] = kAlpha[(v >> 6) & 63];
    out[o++] = kAlpha[v & 63];
  }
  if (i < n) {
    const uint32_t v = (uint32_t(in[i]) << 16) | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0u);
    out[o++] = kAlpha[v >> 18];
    out[o++] = kAlpha[(v >> 12) & 63];
    out[o++] = i + 1 < n ? kAlpha[(v >> 6) & 63] : '=';
    out[o++] = '=';
  }
  return o;
}

size_t frame_tail(Enc enc) { return enc == Enc::B64 ? 3 : 1; }

size_t frame_size(Enc enc, size_t head_len, size_t n) {
  return head_len + (enc == Enc::B64 ? b64_len(n) : n) + frame_tail(enc);
}

Level level(const int16_t* s, size_t n) {
  Level l = {0, 0};
  if (n == 0) return l;
  double acc = 0;
  uint32_t peak = 0;
  for (size_t i = 0; i < n; ++i) {
    const int32_t v = s[i];
    acc += static_cast<double>(v) * v;
    const uint32_t a = static_cast<uint32_t>(v < 0 ? -v : v);
    if (a > peak) peak = a;
  }
  l.rms = static_cast<uint16_t>(std::sqrt(acc / static_cast<double>(n)));
  l.peak = static_cast<uint16_t>(peak > 65535 ? 65535 : peak);
  return l;
}

size_t level_bins(const int16_t* s, size_t n, size_t per_bin, Level* out, size_t cap) {
  if (per_bin == 0) return 0;
  size_t bins = n / per_bin;
  if (bins > cap) bins = cap;
  for (size_t b = 0; b < bins; ++b) out[b] = level(s + b * per_bin, per_bin);
  return bins;
}

uint8_t busy_pct(uint32_t idle, uint32_t idle_ref) {
  if (idle_ref == 0 || idle >= idle_ref) return 0;
  const uint64_t idle_pct = static_cast<uint64_t>(idle) * 100u / idle_ref;
  return static_cast<uint8_t>(100u - idle_pct);
}

size_t count_mismatch(const Reg* want, const uint8_t* got, size_t n) {
  size_t bad = 0;
  for (size_t i = 0; i < n; ++i) bad += want[i].val != got[i] ? 1 : 0;
  return bad;
}

}  // namespace gate
