#include "mic_source.h"

#include <algorithm>
#include <cstdint>

namespace cpasim {
namespace {

constexpr uint32_t kRate = 16000;
constexpr size_t kHeader = 12;  // "RIFF" size "WAVE"

uint32_t le(const std::string& s, size_t at, int bytes) {
  uint32_t v = 0;
  for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | static_cast<uint8_t>(s[at + i]);
  return v;
}

struct Chunks {
  bool fmt = false;
  bool data = false;
  uint32_t format = 0, channels = 0, rate = 0, bits = 0;
  std::string pcm;
};

void take(Chunks& c, const std::string& id, const std::string& body) {
  if (id == "fmt " && body.size() >= 16) {
    c.fmt = true;
    c.format = le(body, 0, 2);
    c.channels = le(body, 2, 2);
    c.rate = le(body, 4, 4);
    c.bits = le(body, 14, 2);
  } else if (id == "data") {
    c.data = true;
    c.pcm = body.substr(0, body.size() & ~static_cast<size_t>(1));  // whole samples only
  }
}

// Walks the chunks after the header; returns "" or what is wrong.
std::string walk(const std::string& b, Chunks& c) {
  for (size_t at = kHeader; at + 8 <= b.size();) {
    const uint32_t n = le(b, at + 4, 4);
    if (n > b.size() - at - 8) return "chunk runs past the end";
    take(c, b.substr(at, 4), b.substr(at + 8, n));
    at += 8 + n + (n & 1);
  }
  return "";
}

}  // namespace

Wav parse_wav(const std::string& bytes) {
  Wav w;
  if (bytes.size() < kHeader || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) {
    w.error = "not a RIFF/WAVE file";
    return w;
  }
  Chunks c;
  w.error = walk(bytes, c);
  if (!w.error.empty()) return w;
  if (!c.fmt) w.error = "no fmt chunk";
  else if (c.format != 1) w.error = "not PCM";
  else if (c.channels != 1 || c.rate != kRate || c.bits != 16) w.error = "need mono, 16000 Hz, 16-bit";
  else if (c.pcm.empty()) w.error = "no audio";
  else w.pcm = c.pcm;
  return w;
}

std::string MicSource::block(size_t n) {
  if (pcm_.empty()) return std::string(n, '\0');
  std::string out;
  out.reserve(n);
  while (out.size() < n) {
    const size_t k = std::min(n - out.size(), pcm_.size() - at_);
    out.append(pcm_, at_, k);
    at_ = (at_ + k) % pcm_.size();
  }
  return out;
}

}  // namespace cpasim
