// LineQueue drop policy (05 §5.8): <= 4 lines; when full only outbox.item lines are
// dropped; a reply evicts the newest queued outbox.item and is never dropped while
// one is there. Header-only, included by path: usb_transport/ is device-only.
#include <unity.h>

#include <string>

#include "transport.h"  // the link library (found by the dependency finder)
#include "../../../lib/usb_transport/line_queue.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

using Q = LineQueue<4, 16>;

static std::string front(const Q& q) { return std::string(q.front()->data, q.front()->n); }

static void test_fifo_order() {
  Q q;
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_NULL(q.front());
  q.push("a", 1, TxClass::Reply);
  q.push("bb", 2, TxClass::Resendable);
  TEST_ASSERT_EQUAL_UINT8(2, q.size());
  TEST_ASSERT_EQUAL_STRING("a", front(q).c_str());
  q.pop();
  TEST_ASSERT_EQUAL_STRING("bb", front(q).c_str());
  q.pop();
  q.pop();  // empty: no-op
  TEST_ASSERT_TRUE(q.empty());
}

static void test_full_queue_drops_resendable_only() {
  Q q;
  for (int i = 0; i < 4; ++i) TEST_ASSERT_TRUE(q.push("i", 1, TxClass::Resendable));
  TEST_ASSERT_FALSE(q.push("x", 1, TxClass::Resendable));
  TEST_ASSERT_EQUAL_UINT32(1, q.dropped_resendable());
  TEST_ASSERT_TRUE(q.push("r", 1, TxClass::Reply));  // evicts the newest item
  TEST_ASSERT_EQUAL_UINT8(4, q.size());
  TEST_ASSERT_EQUAL_UINT32(2, q.dropped_resendable());
  for (int i = 0; i < 3; ++i) q.pop();
  TEST_ASSERT_EQUAL_STRING("r", front(q).c_str());  // the reply kept its place at the end
}

static void test_replies_only_queue_counts_the_drop() {
  Q q;
  for (int i = 0; i < 4; ++i) q.push("r", 1, TxClass::Reply);
  TEST_ASSERT_FALSE(q.push("r", 1, TxClass::Reply));
  TEST_ASSERT_EQUAL_UINT32(1, q.dropped_replies());
}

static void test_oversized_line_is_refused() {
  Q q;
  TEST_ASSERT_FALSE(q.push("0123456789abcdefX", 17, TxClass::Reply));
  TEST_ASSERT_TRUE(q.empty());
}

static void test_slots_are_reused() {
  Q q;
  const char* names = "abcdefghijklm";
  for (int i = 0; i < 3; ++i) q.push(names + i, 1, TxClass::Reply);
  for (int i = 3; i < 13; ++i) {  // pop one, push one: slots are recycled
    q.pop();
    TEST_ASSERT_TRUE(q.push(names + i, 1, TxClass::Reply));
  }
  TEST_ASSERT_EQUAL_UINT8(3, q.size());
  TEST_ASSERT_EQUAL_STRING("k", front(q).c_str());
  TEST_ASSERT_EQUAL_UINT32(0, q.dropped_replies());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fifo_order);
  RUN_TEST(test_full_queue_drops_resendable_only);
  RUN_TEST(test_replies_only_queue_counts_the_drop);
  RUN_TEST(test_oversized_line_is_refused);
  RUN_TEST(test_slots_are_reused);
  return UNITY_END();
}
