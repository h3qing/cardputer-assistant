// TxPipe: the device's USB send path (usb_transport/) on the host. A line is written only
// when all of it fits in the 4 KB TX ring, else it waits in the 4-line queue with the
// device's drop rule; the ring drains as fast as the host side takes bytes.
#include <unity.h>

#include <string>

#include "tx_pipe.h"

using namespace cpasim;
using cpa::TxClass;

void setUp() {}
void tearDown() {}

namespace {

// The host side: takes at most `take` bytes per call (0 = not reading).
struct Host {
  std::string got;
  size_t take = static_cast<size_t>(-1);
  int calls = 0;
  WriteFn fn() {
    return [this](const char* p, size_t n) {
      ++calls;
      const size_t k = n < take ? n : take;
      got.append(p, k);
      return k;
    };
  }
};

std::string line(char c, size_t n) { return std::string(n - 1, c) + "\n"; }

bool send(TxPipe& t, const std::string& s, TxClass cls, Host& h) { return t.send(s.data(), s.size(), cls, h.fn()); }

}  // namespace

static void test_line_goes_straight_through() {
  TxPipe t;
  Host h;
  TEST_ASSERT_TRUE(send(t, "{\"t\":\"pong\"}\n", TxClass::Reply, h));
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"pong\"}\n", h.got.c_str());
  TEST_ASSERT_EQUAL_size_t(kTxRing, t.room());
  TEST_ASSERT_EQUAL_UINT8(0, t.queued());
}

static void test_ring_holds_what_the_host_does_not_read() {
  TxPipe t;
  Host h;
  h.take = 0;
  TEST_ASSERT_TRUE(send(t, line('a', 1000), TxClass::Reply, h));
  TEST_ASSERT_EQUAL_size_t(kTxRing - 1000, t.room());
  TEST_ASSERT_EQUAL_size_t(1000, t.buffered());
  h.take = static_cast<size_t>(-1);
  t.pump(h.fn());
  TEST_ASSERT_EQUAL_size_t(1000, h.got.size());
  TEST_ASSERT_EQUAL_size_t(kTxRing, t.room());
}

static void test_partial_reads_keep_the_order() {
  TxPipe t;
  Host h;
  h.take = 7;
  send(t, line('a', 30), TxClass::Reply, h);
  send(t, line('b', 30), TxClass::Resendable, h);
  for (int i = 0; i < 20; ++i) t.pump(h.fn());
  TEST_ASSERT_EQUAL_STRING((line('a', 30) + line('b', 30)).c_str(), h.got.c_str());
}

static void test_a_line_that_does_not_fit_waits_whole() {
  TxPipe t;
  Host h;
  h.take = 0;
  send(t, line('a', 2048), TxClass::Reply, h);
  send(t, line('b', 2000), TxClass::Reply, h);  // 4048 bytes in the ring
  TEST_ASSERT_TRUE(send(t, line('c', 100), TxClass::Reply, h));
  TEST_ASSERT_EQUAL_UINT8(1, t.queued());  // 48 bytes free: c waits in the queue
  TEST_ASSERT_EQUAL_size_t(48, t.room());
  h.take = 2048;
  t.pump(h.fn());  // a leaves, c moves into the ring behind b
  TEST_ASSERT_EQUAL_UINT8(0, t.queued());
  h.take = static_cast<size_t>(-1);
  t.pump(h.fn());
  TEST_ASSERT_EQUAL_STRING((line('a', 2048) + line('b', 2000) + line('c', 100)).c_str(), h.got.c_str());
}

static void test_full_queue_drops_resendable_and_keeps_replies() {
  TxPipe t;
  Host h;
  h.take = 0;
  send(t, line('a', 2048), TxClass::Reply, h);
  send(t, line('b', 2048), TxClass::Reply, h);  // the ring is full
  for (int i = 0; i < 4; ++i) TEST_ASSERT_TRUE(send(t, line('i', 50), TxClass::Resendable, h));
  TEST_ASSERT_FALSE(send(t, line('j', 50), TxClass::Resendable, h));
  TEST_ASSERT_EQUAL_UINT32(1, t.dropped_resendable());
  TEST_ASSERT_TRUE(send(t, line('r', 50), TxClass::Reply, h));  // evicts the newest item
  TEST_ASSERT_EQUAL_UINT32(2, t.dropped_resendable());
  TEST_ASSERT_EQUAL_UINT8(4, t.queued());
}

static void test_reset_forgets_everything() {
  TxPipe t;
  Host h;
  h.take = 0;
  send(t, line('a', 2048), TxClass::Reply, h);
  send(t, line('b', 2048), TxClass::Reply, h);
  send(t, line('c', 50), TxClass::Reply, h);
  t.reset();
  TEST_ASSERT_EQUAL_size_t(kTxRing, t.room());
  TEST_ASSERT_EQUAL_UINT8(0, t.queued());
  h.take = static_cast<size_t>(-1);
  t.pump(h.fn());
  TEST_ASSERT_EQUAL_size_t(0, h.got.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_line_goes_straight_through);
  RUN_TEST(test_ring_holds_what_the_host_does_not_read);
  RUN_TEST(test_partial_reads_keep_the_order);
  RUN_TEST(test_a_line_that_does_not_fit_waits_whole);
  RUN_TEST(test_full_queue_drops_resendable_and_keeps_replies);
  RUN_TEST(test_reset_forgets_everything);
  return UNITY_END();
}
