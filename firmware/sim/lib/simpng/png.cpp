#include "png.h"

#include <fstream>

#include "rec_file_port.h"

namespace cpasim {
namespace {

constexpr size_t kStoredMax = 65535;

void put32(std::string& s, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) s.push_back(static_cast<char>((v >> shift) & 0xFF));
}

std::string chunk(const char* type, const std::string& data) {
  std::string body(type, 4);
  body += data;
  std::string out;
  put32(out, static_cast<uint32_t>(data.size()));
  out += body;
  put32(out, cpa::rec::crc32_ieee(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
  return out;
}

// zlib stream (RFC 1950) of stored deflate blocks (RFC 1951 §3.2.4).
std::string zlib_stored(const std::string& raw) {
  std::string z("\x78\x01", 2);
  size_t at = 0;
  do {
    const size_t len = raw.size() - at < kStoredMax ? raw.size() - at : kStoredMax;
    const bool last = at + len == raw.size();
    z.push_back(static_cast<char>(last ? 1 : 0));
    z.push_back(static_cast<char>(len & 0xFF));
    z.push_back(static_cast<char>(len >> 8));
    z.push_back(static_cast<char>(~len & 0xFF));
    z.push_back(static_cast<char>((~len >> 8) & 0xFF));
    z.append(raw, at, len);
    at += len;
  } while (at < raw.size());
  put32(z, adler32(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));
  return z;
}

std::string rows_of(const Frame& f) {
  std::string raw;
  raw.reserve(static_cast<size_t>(f.w() + 1) * f.h());
  for (int y = 0; y < f.h(); ++y) {
    raw.push_back(0);  // filter: none
    const auto row = f.pixels().begin() + static_cast<long>(y) * f.w();
    raw.append(row, row + f.w());
  }
  return raw;
}

}  // namespace

Palette panel_palette() {
  Palette p;
  for (size_t i = 0; i < p.size(); ++i) p[i] = panel_rgb(static_cast<uint8_t>(i));
  return p;
}

uint32_t adler32(const uint8_t* p, size_t n) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (size_t i = 0; i < n; ++i) {
    a = (a + p[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

std::string png_indexed(const Frame& f, const Palette& pal) {
  std::string ihdr;
  put32(ihdr, static_cast<uint32_t>(f.w()));
  put32(ihdr, static_cast<uint32_t>(f.h()));
  ihdr += std::string("\x08\x03\x00\x00\x00", 5);  // depth 8, indexed, deflate, no filter / interlace
  std::string plte;
  for (const Rgb& c : pal) {
    plte.push_back(static_cast<char>(c.r));
    plte.push_back(static_cast<char>(c.g));
    plte.push_back(static_cast<char>(c.b));
  }
  return std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("PLTE", plte) +
         chunk("IDAT", zlib_stored(rows_of(f))) + chunk("IEND", "");
}

bool write_file(const std::string& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

}  // namespace cpasim
