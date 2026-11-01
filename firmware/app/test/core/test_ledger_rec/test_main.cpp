// The SENT.L record (docs/28 §3.4): 28 B little-endian, magic 0xA5, CRC-32 over the first
// 24 B. A reader resyncs on the magic after a bad record and reports a torn tail, so a
// power cut costs at most the last record and never shifts the ones after a bad one.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "ledger_rec.h"
#include "rec_file_port.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

namespace {

LedgerRec deliv(uint32_t seq, uint32_t host) {
  LedgerRec r;
  r.op = LedgerOp::Deliv;
  r.flags = kLfLast;
  r.boot = 12;
  r.seq = seq;
  r.seg = 3;
  r.back = 7;
  r.wall_s = 1761500000;
  r.host = host;
  return r;
}

std::string bytes_of(const std::vector<LedgerRec>& rs) {
  std::string out;
  for (const auto& r : rs) {
    uint8_t b[kLedgerRecBytes];
    ledger_encode(r, b, crc32_ieee);
    out.append(reinterpret_cast<const char*>(b), sizeof b);
  }
  return out;
}

struct Collect : LedgerVisitor {
  std::vector<LedgerRec> got;
  void record(const LedgerRec& r) override { got.push_back(r); }
};

LedgerParse parse_all(const std::string& s, Collect& c) {
  return ledger_parse(reinterpret_cast<const uint8_t*>(s.data()), s.size(), true, crc32_ieee, c);
}

}  // namespace

static void test_round_trip_is_28_bytes_little_endian() {
  uint8_t b[kLedgerRecBytes];
  ledger_encode(deliv(0x01020304, 0xAABBCCDD), b, crc32_ieee);
  TEST_ASSERT_EQUAL_HEX8(0xA5, b[0]);
  TEST_ASSERT_EQUAL_HEX8(1, b[1]);  // DELIV
  TEST_ASSERT_EQUAL_HEX8(0x04, b[8]);  // seq, low byte first
  TEST_ASSERT_EQUAL_HEX8(0xDD, b[20]);
  const Opt<LedgerRec> r = ledger_decode(b, crc32_ieee);
  TEST_ASSERT_TRUE(r.has);
  TEST_ASSERT_EQUAL_UINT32(0x01020304, r.value.seq);
  TEST_ASSERT_EQUAL_UINT32(0xAABBCCDD, r.value.host);
  TEST_ASSERT_EQUAL_UINT16(3, r.value.seg);
  TEST_ASSERT_EQUAL_UINT16(7, r.value.back);
  TEST_ASSERT_EQUAL_UINT8(kLfLast, r.value.flags);
}

static void test_every_flipped_byte_fails() {
  uint8_t b[kLedgerRecBytes];
  ledger_encode(deliv(5, 9), b, crc32_ieee);
  for (size_t i = 0; i < sizeof b; ++i) {
    uint8_t c[kLedgerRecBytes];
    std::memcpy(c, b, sizeof b);
    c[i] ^= 0x10;
    TEST_ASSERT_FALSE_MESSAGE(ledger_decode(c, crc32_ieee).has, "a flipped byte passed");
  }
}

static void test_unknown_op_fails_even_with_a_good_crc() {
  LedgerRec r = deliv(5, 9);
  r.op = static_cast<LedgerOp>(9);
  uint8_t b[kLedgerRecBytes];
  ledger_encode(r, b, crc32_ieee);
  TEST_ASSERT_FALSE(ledger_decode(b, crc32_ieee).has);
}

static void test_empty_ledger_is_clean() {
  Collect c;
  const LedgerParse p = parse_all("", c);
  TEST_ASSERT_EQUAL_UINT32(0, p.good);
  TEST_ASSERT_FALSE(p.dirty);
}

// docs/28 §9: each op's last record cut by 1..27 B, more appended after: all of those come back.
static void test_torn_tail_at_every_byte_then_appends_read_back() {
  const std::string head = bytes_of({deliv(1, 7), deliv(2, 7)});
  const std::string last = bytes_of({deliv(3, 7)});
  const std::string after = bytes_of({deliv(4, 8), deliv(5, 8)});
  for (size_t cut = 1; cut < kLedgerRecBytes; ++cut) {
    Collect c;
    const LedgerParse p = parse_all(head + last.substr(0, cut) + after, c);
    TEST_ASSERT_EQUAL_UINT32(4, p.good);
    TEST_ASSERT_TRUE(p.dirty);
    TEST_ASSERT_EQUAL_UINT32(4, c.got[2].seq);
    TEST_ASSERT_EQUAL_UINT32(5, c.got[3].seq);
  }
}

static void test_torn_tail_alone_is_dirty_and_drops_only_it() {
  const std::string all = bytes_of({deliv(1, 7), deliv(2, 7)});
  for (size_t cut = 1; cut < kLedgerRecBytes; ++cut) {
    Collect c;
    const LedgerParse p = parse_all(all.substr(0, all.size() - cut), c);
    TEST_ASSERT_EQUAL_UINT32(1, p.good);
    TEST_ASSERT_TRUE(p.dirty);
  }
}

static void test_bad_crc_in_the_middle_resyncs_on_the_magic() {
  std::string s = bytes_of({deliv(1, 7), deliv(2, 7), deliv(3, 7)});
  s[kLedgerRecBytes + 13] ^= 0x40;
  Collect c;
  const LedgerParse p = parse_all(s, c);
  TEST_ASSERT_EQUAL_UINT32(2, p.good);
  TEST_ASSERT_EQUAL_UINT32(3, c.got[1].seq);
  TEST_ASSERT_TRUE(p.dirty);
}

static void test_garbage_with_magic_bytes_never_passes() {
  std::string s(200, static_cast<char>(0xA5));
  s += bytes_of({deliv(8, 1)});
  Collect c;
  const LedgerParse p = parse_all(s, c);
  TEST_ASSERT_EQUAL_UINT32(1, p.good);
  TEST_ASSERT_EQUAL_UINT32(8, c.got[0].seq);
}

// The streaming reader: a chunk that ends inside a record leaves it for the next one.
static void test_chunked_parse_matches_whole() {
  const std::string s = bytes_of({deliv(1, 7), deliv(2, 7), deliv(3, 7), deliv(4, 7)});
  for (size_t chunk = 1; chunk <= 60; ++chunk) {
    Collect c;
    LedgerStream st;
    for (size_t at = 0; at < s.size(); at += chunk) {
      const size_t n = s.size() - at < chunk ? s.size() - at : chunk;
      st.feed(reinterpret_cast<const uint8_t*>(s.data() + at), n, crc32_ieee, c);
    }
    const LedgerParse p = st.finish(crc32_ieee, c);
    TEST_ASSERT_EQUAL_UINT32(4, p.good);
    TEST_ASSERT_FALSE(p.dirty);
    TEST_ASSERT_EQUAL_UINT32(4, c.got.back().seq);
  }
}

static void test_names_are_not_segment_names() {
  TEST_ASSERT_EQUAL_STRING("SENT.L", ledger_name(0));
  TEST_ASSERT_EQUAL_STRING("SENT.K", ledger_name(1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_is_28_bytes_little_endian);
  RUN_TEST(test_every_flipped_byte_fails);
  RUN_TEST(test_unknown_op_fails_even_with_a_good_crc);
  RUN_TEST(test_empty_ledger_is_clean);
  RUN_TEST(test_torn_tail_at_every_byte_then_appends_read_back);
  RUN_TEST(test_torn_tail_alone_is_dirty_and_drops_only_it);
  RUN_TEST(test_bad_crc_in_the_middle_resyncs_on_the_magic);
  RUN_TEST(test_garbage_with_magic_bytes_never_passes);
  RUN_TEST(test_chunked_parse_matches_whole);
  RUN_TEST(test_names_are_not_segment_names);
  return UNITY_END();
}
