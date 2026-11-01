// MonoId: Crockford base32 EEEEEEE-BBBBB-SSSS (05 §5.9). The boot counter must be
// advanced and persisted before any ID is issued: only a BootTicket (proof of a
// confirmed NVS write) can start a MonoSeq.
#include <unity.h>

#include <cstring>
#include <set>
#include <thread>
#include <vector>

#include "mono_id.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static MonoSeq started(uint32_t epoch, uint32_t boot) {
  const BootRecord rec{epoch, boot};
  const auto ticket = boot_ticket(rec, rec);
  TEST_ASSERT_TRUE(ticket.has);
  return mono_start(ticket.value);
}

static void test_crockford_round_trip() {
  TEST_ASSERT_EQUAL_STRING("0000000", crockford_encode(0, 7).c_str());
  TEST_ASSERT_EQUAL_STRING("3ZZZZZZ", crockford_encode(0xFFFFFFFFu, 7).c_str());
  TEST_ASSERT_EQUAL_STRING("0000C", crockford_encode(12, 5).c_str());
  const auto v = crockford_decode("3QZK1M7", 7);
  TEST_ASSERT_TRUE(v.has);
  TEST_ASSERT_EQUAL_STRING("3QZK1M7", crockford_encode(v.value, 7).c_str());
  TEST_ASSERT_FALSE(crockford_decode("3QZK1I7", 7).has);  // I is excluded
  TEST_ASSERT_FALSE(crockford_decode("3qzk1m7", 7).has);  // upper case only
  TEST_ASSERT_FALSE(crockford_decode("U", 1).has);
}

static void test_format_and_parse() {
  const MonoId id{0x1D7F4C47u, 12, 3};
  const auto s = mono_format(id);
  TEST_ASSERT_EQUAL_UINT(18, s.size());
  TEST_ASSERT_EQUAL_STRING("0EQYK27-0000C-0003", s.c_str());
  const auto back = mono_parse(s.c_str(), s.size());
  TEST_ASSERT_TRUE(back.has);
  TEST_ASSERT_TRUE(back.value == id);
  TEST_ASSERT_EQUAL_STRING("0EQYK27-0000C", boot_id_format(id.epoch, id.boot).c_str());
}

static void test_parse_rejects_malformed() {
  const char* bad[] = {"", "1QFX2HK-0000C", "1QFX2HK-0000C-003", "1QFX2HK_0000C-0003",
                       "1qfx2hk-0000c-0003", "1QFX2HK-0000C-0003X", "4000000-0000C-0003",
                       "1QFX2HL-0000C-0003"};
  for (const char* s : bad) {
    TEST_ASSERT_FALSE_MESSAGE(mono_parse(s, std::strlen(s)).has, s);
  }
}

static void test_string_order_is_numeric_order() {
  const MonoId ids[] = {{5, 1, 0}, {5, 1, 1}, {5, 1, 31}, {5, 1, 32}, {5, 2, 0}, {6, 0, 0}};
  for (size_t i = 0; i + 1 < sizeof ids / sizeof ids[0]; ++i) {
    TEST_ASSERT_TRUE(ids[i] < ids[i + 1]);
    TEST_ASSERT_FALSE(ids[i + 1] < ids[i]);
    TEST_ASSERT_TRUE(std::strcmp(mono_format(ids[i]).c_str(), mono_format(ids[i + 1]).c_str()) < 0);
  }
}

static void test_boot_must_be_advanced_and_persisted() {
  const BootRecord fresh = boot_init(0xABCDu);
  TEST_ASSERT_EQUAL_UINT32(0, fresh.boot);
  const auto next = boot_advance(fresh);
  TEST_ASSERT_TRUE(next.ok);
  TEST_ASSERT_EQUAL_UINT32(1, next.value.boot);
  TEST_ASSERT_EQUAL_UINT32(0xABCDu, next.value.epoch);
  // the NVS read-back must equal what was written, otherwise no ticket
  TEST_ASSERT_FALSE(boot_ticket(next.value, fresh).has);
  TEST_ASSERT_TRUE(boot_ticket(next.value, next.value).has);
  // the 25-bit boot counter cannot wrap
  TEST_ASSERT_FALSE(boot_advance(BootRecord{1, kBootMax}).ok);
}

static void test_sequence_issues_increasing_ids() {
  const MonoSeq s0 = started(7, 3);
  const auto a = mono_next(s0);
  const auto b = mono_next(a.seq);
  TEST_ASSERT_TRUE(a.id.has);
  TEST_ASSERT_TRUE(b.id.has);
  TEST_ASSERT_TRUE(a.id.value < b.id.value);
  TEST_ASSERT_EQUAL_UINT32(7, a.id.value.epoch);
  TEST_ASSERT_EQUAL_UINT32(3, a.id.value.boot);
  // a later boot always sorts after every ID of an earlier boot
  const auto c = mono_next(started(7, 4));
  TEST_ASSERT_TRUE(b.id.value < c.id.value);
  TEST_ASSERT_EQUAL_STRING(boot_id_format(7, 3).c_str(), mono_boot_id(s0).c_str());
}

static void test_sequence_exhaustion_is_reported() {
  MonoSeq s = started(1, 1);
  const auto last = mono_next(mono_seek_for_test(s, kSeqMax));
  TEST_ASSERT_TRUE(last.id.has);
  TEST_ASSERT_FALSE(mono_next(last.seq).id.has);
}

static void test_epoch_mix_uses_mac() {
  const uint32_t a = epoch_mix(0x12345678u, 0x020000000001ull);
  const uint32_t b = epoch_mix(0x12345678u, 0x020000000002ull);
  TEST_ASSERT_NOT_EQUAL(a, b);
  TEST_ASSERT_EQUAL_UINT32(a, epoch_mix(0x12345678u, 0x020000000001ull));
}

// v0.3 (docs/11 §4.4): the boot sequence is shared by the loop (outbox items) and the
// audio task (recording segments), so it becomes one atomic counter.
static BootTicket ticket_of(uint32_t epoch, uint32_t boot) {
  const BootRecord rec{epoch, boot};
  return boot_ticket(rec, rec).value;
}

static void test_counter_without_ticket_issues_nothing() {
  MonoCounter c;
  TEST_ASSERT_TRUE(c.exhausted());
  TEST_ASSERT_FALSE(c.next().has);
}

static void test_counter_issues_the_same_ids_as_the_sequence() {
  MonoCounter c;
  c.start(ticket_of(7, 3));
  MonoSeq s = started(7, 3);
  for (int i = 0; i < 5; ++i) {
    const MonoNext n = mono_next(s);
    s = n.seq;
    const Opt<MonoId> id = c.next();
    TEST_ASSERT_TRUE(id.has);
    TEST_ASSERT_TRUE(id.value == n.id.value);
  }
  TEST_ASSERT_EQUAL_STRING(boot_id_format(7, 3).c_str(), c.boot_id().c_str());
  TEST_ASSERT_FALSE(c.exhausted());
}

static void test_counter_exhaustion() {
  MonoCounter c;
  c.start(ticket_of(1, 1), kSeqMax);
  TEST_ASSERT_TRUE(c.next().has);
  TEST_ASSERT_TRUE(c.exhausted());
  TEST_ASSERT_FALSE(c.next().has);
  TEST_ASSERT_FALSE(c.next().has);  // stays exhausted, never wraps
}

static void test_counter_is_shared_by_two_threads_without_duplicates() {
  MonoCounter c;
  c.start(ticket_of(9, 2));
  constexpr int kEach = 20000;
  std::vector<uint32_t> a, b;
  std::thread ta([&] { for (int i = 0; i < kEach; ++i) a.push_back(c.next().value.seq); });
  std::thread tb([&] { for (int i = 0; i < kEach; ++i) b.push_back(c.next().value.seq); });
  ta.join();
  tb.join();
  std::set<uint32_t> all(a.begin(), a.end());
  all.insert(b.begin(), b.end());
  TEST_ASSERT_EQUAL_UINT(2 * kEach, all.size());
  TEST_ASSERT_EQUAL_UINT32(2 * kEach - 1, *all.rbegin());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crockford_round_trip);
  RUN_TEST(test_format_and_parse);
  RUN_TEST(test_parse_rejects_malformed);
  RUN_TEST(test_string_order_is_numeric_order);
  RUN_TEST(test_boot_must_be_advanced_and_persisted);
  RUN_TEST(test_sequence_issues_increasing_ids);
  RUN_TEST(test_sequence_exhaustion_is_reported);
  RUN_TEST(test_epoch_mix_uses_mac);
  RUN_TEST(test_counter_without_ticket_issues_nothing);
  RUN_TEST(test_counter_issues_the_same_ids_as_the_sequence);
  RUN_TEST(test_counter_exhaustion);
  RUN_TEST(test_counter_is_shared_by_two_threads_without_duplicates);
  return UNITY_END();
}
