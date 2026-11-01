// RecIndex (docs/11 §4.6-§4.7): the committed segments waiting for sync, at most 256
// entries of {id, size} in id order, plus rev. rev is 0 until the boot recovery scan has
// finished; after that every commit, delete and recovery adds one.
#include <unity.h>

#include <string>

#include "rec_index.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

static MonoId seq(uint32_t s) {
  MonoId id;
  id.epoch = 7;
  id.boot = 3;
  id.seq = s;
  return id;
}

static void test_starts_empty_and_not_ready() {
  RecIndex x;
  TEST_ASSERT_FALSE(x.ready());
  TEST_ASSERT_EQUAL_UINT32(0, x.rev());
  TEST_ASSERT_EQUAL_UINT16(0, x.count());
  TEST_ASSERT_NULL(x.find(seq(1)));
}

static void test_keeps_id_order_and_rejects_duplicates() {
  RecIndex x;
  TEST_ASSERT_TRUE(x.insert(seq(5), 1044));
  TEST_ASSERT_TRUE(x.insert(seq(2), 2044));
  TEST_ASSERT_TRUE(x.insert(seq(9), 3044));
  TEST_ASSERT_FALSE(x.insert(seq(2), 99));
  TEST_ASSERT_EQUAL_UINT16(3, x.count());
  TEST_ASSERT_TRUE(x.at(0).id == seq(2) && x.at(1).id == seq(5) && x.at(2).id == seq(9));
  TEST_ASSERT_EQUAL_UINT32(2044, x.find(seq(2))->size);
  TEST_ASSERT_TRUE(x.remove(seq(5)));
  TEST_ASSERT_FALSE(x.remove(seq(5)));
  TEST_ASSERT_EQUAL_UINT16(2, x.count());
  TEST_ASSERT_TRUE(x.at(1).id == seq(9));
}

static void test_rev_counts_changes_once_ready() {
  RecIndex x;
  x.insert(seq(1), 44);
  TEST_ASSERT_EQUAL_UINT32(0, x.rev());  // the scan's own inserts are not counted
  x.set_ready(3);  // three recoveries during the scan
  TEST_ASSERT_TRUE(x.ready());
  TEST_ASSERT_EQUAL_UINT32(4, x.rev());
  x.insert(seq(2), 44);
  TEST_ASSERT_EQUAL_UINT32(5, x.rev());
  x.remove(seq(1));
  TEST_ASSERT_EQUAL_UINT32(6, x.rev());
  x.remove(seq(1));  // nothing removed: no change
  TEST_ASSERT_EQUAL_UINT32(6, x.rev());
}

static void test_holds_256_at_most() {
  RecIndex x;
  for (uint32_t i = 0; i < kIndexMax; ++i) TEST_ASSERT_TRUE(x.insert(seq(1000 - i), 44 + i));
  TEST_ASSERT_TRUE(x.full());
  TEST_ASSERT_FALSE(x.insert(seq(5000), 44));
  TEST_ASSERT_EQUAL_UINT16(256, x.count());
  TEST_ASSERT_TRUE(x.at(0).id == seq(745));
  // part of the ~38 KB allocated at boot; docs/28 S1a: +1 KB for each entry's excl and first
  TEST_ASSERT_TRUE(sizeof(RecIndex) <= 5200);
}

static void test_first_after_for_paging() {
  RecIndex x;
  for (uint32_t s : {10u, 20u, 30u}) x.insert(seq(s), 44);
  TEST_ASSERT_EQUAL_UINT16(0, x.first_after(nullptr));
  const MonoIdStr s10 = mono_format(seq(10));
  TEST_ASSERT_EQUAL_UINT16(1, x.first_after(s10.c_str()));
  const MonoIdStr s15 = mono_format(seq(15));  // not listed itself: the next one
  TEST_ASSERT_EQUAL_UINT16(1, x.first_after(s15.c_str()));
  TEST_ASSERT_EQUAL_UINT16(3, x.first_after(mono_format(seq(30)).c_str()));
  TEST_ASSERT_EQUAL_UINT16(0, x.first_after("0000000-00000-0000"));
  TEST_ASSERT_EQUAL_UINT16(3, x.first_after("ZZZZZZZ-ZZZZZ-ZZZZ"));  // another, later epoch
}

// docs/28 S1a: an entry kept from one Mac; at most 4 Macs, a fifth takes the oldest slot.
static void test_entries_kept_from_one_mac() {
  RecIndex x;
  x.insert(seq(1), 44, 0xA1, true);
  x.insert(seq(2), 44);
  TEST_ASSERT_FALSE(x.for_host(*x.find(seq(1)), 0xA1));
  TEST_ASSERT_TRUE(x.for_host(*x.find(seq(1)), 0xB2));
  TEST_ASSERT_TRUE(x.for_host(*x.find(seq(1)), 0));  // an unknown Mac
  TEST_ASSERT_TRUE(x.find(seq(1))->first && !x.find(seq(2))->first);
  x.set_ready(0);
  const uint32_t rev = x.rev();
  TEST_ASSERT_EQUAL_UINT16(1, x.exclude_from(seq(2), 0xB2));
  TEST_ASSERT_EQUAL_UINT32(rev + 1, x.rev());
  TEST_ASSERT_FALSE(x.for_host(*x.find(seq(2)), 0xB2));
  TEST_ASSERT_EQUAL_UINT16(0, x.exclude_from(seq(2), 0xB2));  // nothing new: rev stays
  TEST_ASSERT_EQUAL_UINT32(rev + 1, x.rev());
}

static void test_a_fifth_mac_takes_the_oldest_slot() {
  RecIndex x;
  x.insert(seq(1), 44, 0x11);
  for (uint32_t h : {0x22u, 0x33u, 0x44u}) TEST_ASSERT_TRUE(x.host_slot(h) > 0);
  TEST_ASSERT_EQUAL_UINT8(1, x.host_slot(0x55));  // 0x11's slot
  TEST_ASSERT_TRUE(x.for_host(*x.find(seq(1)), 0x11));  // freed
  TEST_ASSERT_EQUAL_HEX32(0x55, x.host_at(1));
  TEST_ASSERT_EQUAL_UINT8(0, x.host_slot(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_empty_and_not_ready);
  RUN_TEST(test_keeps_id_order_and_rejects_duplicates);
  RUN_TEST(test_rev_counts_changes_once_ready);
  RUN_TEST(test_holds_256_at_most);
  RUN_TEST(test_first_after_for_paging);
  RUN_TEST(test_entries_kept_from_one_mac);
  RUN_TEST(test_a_fifth_mac_takes_the_oldest_slot);
  return UNITY_END();
}
