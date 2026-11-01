// PtyTransport: the USB link of `cpa-sim serve` on a real pseudo-terminal. The test plays
// the host: it opens the slave the way pyserial does (non-blocking, no controlling tty)
// and puts it in raw mode. Device bytes wait while nobody has the port open in raw mode,
// so the line discipline can never echo them back as host input.
#include <unity.h>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "pty_transport.h"

using namespace cpasim;
using cpa::TxClass;

void setUp() {}
void tearDown() {}

namespace {

int open_host(const PtyTransport& t) { return ::open(t.path().c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK); }

void make_raw(int fd) {
  termios tio;
  tcgetattr(fd, &tio);
  cfmakeraw(&tio);
  tcsetattr(fd, TCSANOW, &tio);
}

std::string host_read(int fd) {
  char buf[8192];
  const ssize_t n = ::read(fd, buf, sizeof buf);
  return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
}

std::string dev_read(PtyTransport& t) {
  char buf[512];
  std::string out;
  for (int i = 0; i < 20; ++i) {
    const size_t n = t.read(buf, sizeof buf);
    out.append(buf, n);
    if (n == 0 && !out.empty()) break;
    usleep(1000);
  }
  return out;
}

bool send(PtyTransport& t, const std::string& s) { return t.send(s.data(), s.size(), TxClass::Reply); }

}  // namespace

static void test_opens_a_tty() {
  PtyTransport t;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(t.open(err), err.c_str());
  TEST_ASSERT_EQUAL_INT(0, t.path().rfind("/dev/", 0));
  TEST_ASSERT_TRUE(t.plugged());
}

static void test_lines_wait_until_a_raw_host_opens_the_port() {
  PtyTransport t;
  std::string err;
  t.open(err);
  TEST_ASSERT_TRUE(send(t, "{\"t\":\"a\"}\n"));  // nobody on the port: kept in the ring
  TEST_ASSERT_EQUAL_size_t(kTxRing - 10, t.tx_room());
  const int host = open_host(t);
  TEST_ASSERT_TRUE(host >= 0);
  t.tick(0);  // the port is open but still cooked (echo on): nothing is written
  TEST_ASSERT_EQUAL_STRING("", host_read(host).c_str());
  make_raw(host);
  t.tick(10);
  usleep(2000);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"a\"}\n", host_read(host).c_str());
  TEST_ASSERT_EQUAL_size_t(kTxRing, t.tx_room());
  TEST_ASSERT_EQUAL_STRING("", dev_read(t).c_str());  // no echo came back
  ::close(host);
}

static void test_host_lines_reach_the_device() {
  PtyTransport t;
  std::string err;
  t.open(err);
  const int host = open_host(t);
  make_raw(host);
  const std::string line = "{\"v\":0,\"t\":\"hello\",\"id\":\"h1\"}\n";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(line.size()), static_cast<int>(::write(host, line.data(), line.size())));
  TEST_ASSERT_EQUAL_STRING(line.c_str(), dev_read(t).c_str());
  ::close(host);
}

static void test_unplugged_drops_both_ways() {
  PtyTransport t;
  std::string err;
  t.open(err);
  const int host = open_host(t);
  make_raw(host);
  t.set_plugged(false);
  TEST_ASSERT_FALSE(t.plugged());
  TEST_ASSERT_FALSE(send(t, "{\"t\":\"a\"}\n"));
  TEST_ASSERT_EQUAL_INT(4, static_cast<int>(::write(host, "xyz\n", 4)));
  TEST_ASSERT_EQUAL_STRING("", dev_read(t).c_str());
  t.set_plugged(true);
  TEST_ASSERT_TRUE(send(t, "{\"t\":\"b\"}\n"));
  usleep(2000);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"b\"}\n", host_read(host).c_str());
  ::close(host);
}

static void test_reset_drops_what_waits() {
  PtyTransport t;
  std::string err;
  t.open(err);
  send(t, "{\"t\":\"a\"}\n");
  t.reset();
  const int host = open_host(t);
  make_raw(host);
  t.tick(0);
  usleep(2000);
  TEST_ASSERT_EQUAL_STRING("", host_read(host).c_str());
  ::close(host);
}

// The device's RX queue holds 8 KB (usb_transport.h): what the host sends beyond that
// while the loop is not reading is lost there, not pushed back on the host, so a host
// that overruns it finds out here too. take() is the USB interrupt taking bytes in.
static void test_rx_overflow_is_dropped_and_counted() {
  PtyTransport t;
  std::string err;
  t.open(err);
  const int host = open_host(t);
  make_raw(host);
  auto take = [&t] {
    char none[1];
    t.read(none, 0);
  };
  const std::string chunk(256, 'x');
  size_t sent = 0;
  for (int i = 0; i < 400 && sent < kRxBuffer + 4096; ++i) {
    const ssize_t w = ::write(host, chunk.data(), chunk.size());
    sent += w > 0 ? static_cast<size_t>(w) : 0;
    take();
    usleep(100);
  }
  TEST_ASSERT_TRUE(sent >= kRxBuffer + 4096);  // the host was never held back
  for (int i = 0; i < 20; ++i, usleep(1000)) take();
  std::string got(kRxBuffer * 2, '\0');
  got.resize(t.read(&got[0], got.size()));
  TEST_ASSERT_EQUAL_size_t(kRxBuffer, got.size());
  TEST_ASSERT_EQUAL_UINT32(sent - kRxBuffer, t.rx_dropped());
  t.reset();
  TEST_ASSERT_EQUAL_UINT32(sent - kRxBuffer, t.rx_dropped());  // a count for serve, not RAM
  ::close(host);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_opens_a_tty);
  RUN_TEST(test_lines_wait_until_a_raw_host_opens_the_port);
  RUN_TEST(test_host_lines_reach_the_device);
  RUN_TEST(test_unplugged_drops_both_ways);
  RUN_TEST(test_reset_drops_what_waits);
  RUN_TEST(test_rx_overflow_is_dropped_and_counted);
  return UNITY_END();
}
