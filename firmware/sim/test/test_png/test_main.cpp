// PNG writer: 8-bit indexed, palette = the panel colour of each RGB332 byte, zlib with
// stored blocks. Checked by parsing the file back here (chunks, CRCs, a stored-block
// inflater, adler32), so the test needs no image library.
#include <unity.h>

#include <string>
#include <vector>

#include "fb.h"
#include "png.h"
#include "rec_file_port.h"

using namespace cpasim;

void setUp() {}
void tearDown() {}

namespace {

uint32_t be32(const std::string& s, size_t at) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(s[at])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[at + 1])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[at + 2])) << 8) | static_cast<uint8_t>(s[at + 3]);
}

struct Chunk {
  std::string type;
  std::string data;
};

// The chunks after the signature; fails the test on a bad CRC.
std::vector<Chunk> chunks(const std::string& png) {
  std::vector<Chunk> out;
  for (size_t at = 8; at + 12 <= png.size();) {
    const uint32_t len = be32(png, at);
    const std::string body = png.substr(at + 4, 4 + len);
    const uint32_t crc = cpa::rec::crc32_ieee(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    TEST_ASSERT_EQUAL_HEX32(crc, be32(png, at + 8 + len));
    out.push_back(Chunk{body.substr(0, 4), body.substr(4)});
    at += 12 + len;
  }
  return out;
}

// zlib with stored blocks only -> the raw bytes; checks the header and adler32.
std::string inflate_stored(const std::string& z) {
  TEST_ASSERT_EQUAL_HEX8(0x78, static_cast<uint8_t>(z[0]));
  TEST_ASSERT_EQUAL(0, ((static_cast<uint8_t>(z[0]) << 8) | static_cast<uint8_t>(z[1])) % 31);
  std::string raw;
  size_t at = 2;
  bool last = false;
  while (!last) {
    const uint8_t head = static_cast<uint8_t>(z[at]);
    TEST_ASSERT_EQUAL(0, (head >> 1) & 3);  // BTYPE 00: stored
    last = head & 1;
    const unsigned len = static_cast<uint8_t>(z[at + 1]) | (static_cast<uint8_t>(z[at + 2]) << 8);
    const unsigned nlen = static_cast<uint8_t>(z[at + 3]) | (static_cast<uint8_t>(z[at + 4]) << 8);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, len ^ nlen);
    raw += z.substr(at + 5, len);
    at += 5 + len;
  }
  TEST_ASSERT_EQUAL_HEX32(adler32(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()), be32(z, at));
  TEST_ASSERT_EQUAL_size_t(at + 4, z.size());
  return raw;
}

std::string idat_of(const std::vector<Chunk>& cs) {
  std::string z;
  for (const auto& c : cs) z += c.type == "IDAT" ? c.data : "";
  return z;
}

}  // namespace

static void test_adler32() {
  const std::string s = "Wikipedia";
  TEST_ASSERT_EQUAL_HEX32(0x11E60398, adler32(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
  TEST_ASSERT_EQUAL_HEX32(1, adler32(nullptr, 0));
}

static void test_signature_header_and_palette() {
  Frame f(3, 2);
  const std::string png = png_indexed(f, panel_palette());
  TEST_ASSERT_EQUAL_STRING_LEN("\x89PNG\r\n\x1a\n", png.data(), 8);
  const auto cs = chunks(png);
  TEST_ASSERT_EQUAL_STRING("IHDR", cs.front().type.c_str());
  TEST_ASSERT_EQUAL_STRING("IEND", cs.back().type.c_str());
  const std::string& ihdr = cs.front().data;
  TEST_ASSERT_EQUAL_UINT32(3, be32(ihdr, 0));
  TEST_ASSERT_EQUAL_UINT32(2, be32(ihdr, 4));
  TEST_ASSERT_EQUAL(8, ihdr[8]);  // bit depth
  TEST_ASSERT_EQUAL(3, ihdr[9]);  // colour type: indexed
  TEST_ASSERT_EQUAL_STRING("PLTE", cs[1].type.c_str());
  TEST_ASSERT_EQUAL_size_t(768, cs[1].data.size());
  const uint8_t dim = entry_of(cpa::Color::Dim);  // ink3 #96897b, as the panel shows it
  TEST_ASSERT_EQUAL_UINT8(148, static_cast<uint8_t>(cs[1].data[dim * 3]));
  TEST_ASSERT_EQUAL_UINT8(138, static_cast<uint8_t>(cs[1].data[dim * 3 + 1]));
  TEST_ASSERT_EQUAL_UINT8(123, static_cast<uint8_t>(cs[1].data[dim * 3 + 2]));
}

static void test_pixels_round_trip() {
  Frame f(4, 2);
  f.fill_rect(1, 0, 2, 1, 0x92);
  f.fill_rect(3, 1, 1, 1, 0xE0);
  const std::string raw = inflate_stored(idat_of(chunks(png_indexed(f, panel_palette()))));
  const std::string want("\0\0\x92\x92\0\0\0\0\0\xE0", 10);  // filter byte 0 per row
  TEST_ASSERT_EQUAL_size_t(want.size(), raw.size());
  TEST_ASSERT_TRUE(raw == want);
}

// More than 65535 raw bytes need several stored blocks.
static void test_large_frame_uses_several_blocks() {
  Frame f(300, 300, 0x1C);
  const std::string z = idat_of(chunks(png_indexed(f, panel_palette())));
  const std::string raw = inflate_stored(z);
  TEST_ASSERT_EQUAL_size_t(300 * 301, raw.size());
  TEST_ASSERT_EQUAL_HEX8(0x1C, static_cast<uint8_t>(raw[300 * 301 - 1]));
  TEST_ASSERT_EQUAL(0, raw[301]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_adler32);
  RUN_TEST(test_signature_header_and_palette);
  RUN_TEST(test_pixels_round_trip);
  RUN_TEST(test_large_frame_uses_several_blocks);
  return UNITY_END();
}
