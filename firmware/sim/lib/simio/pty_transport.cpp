#include "pty_transport.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace cpasim {
namespace {

// tcgetattr on the master reads the slave's settings: raw once the host configured it.
bool host_raw(int fd) {
  termios t;
  if (tcgetattr(fd, &t) != 0) return false;
  return (t.c_lflag & (ECHO | ICANON)) == 0;
}

bool fail(std::string& err, const char* what, int fd) {
  err = std::string(what) + ": " + std::strerror(errno);
  if (fd >= 0) ::close(fd);
  return false;
}

}  // namespace

PtyTransport::~PtyTransport() {
  if (fd_ >= 0) ::close(fd_);
}

bool PtyTransport::open(std::string& err) {
  const int fd = posix_openpt(O_RDWR | O_NOCTTY);
  if (fd < 0) return fail(err, "posix_openpt", fd);
  if (grantpt(fd) != 0 || unlockpt(fd) != 0) return fail(err, "grantpt", fd);
  const char* name = ptsname(fd);
  if (name == nullptr) return fail(err, "ptsname", fd);
  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != 0) return fail(err, "fcntl", fd);
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  fd_ = fd;
  path_ = name;
  return true;
}

// Everything the host sent is taken; what does not fit in the 8 KB queue is lost.
void PtyTransport::fill_rx() {
  char buf[1024];
  while (fd_ >= 0) {
    const ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n <= 0) return;      // EAGAIN: nothing yet; 0 / EIO: nobody has the port open
    if (!plugged_) continue;  // no cable: nothing arrives
    const size_t keep = std::min(static_cast<size_t>(n), kRxBuffer - rx_.size());
    rx_.append(buf, keep);
    rx_dropped_ += static_cast<uint32_t>(static_cast<size_t>(n) - keep);
  }
}

size_t PtyTransport::read(char* buf, size_t cap) {
  fill_rx();
  const size_t n = std::min(cap, rx_.size());
  rx_.copy(buf, n);
  rx_.erase(0, n);
  return n;
}

size_t PtyTransport::write_some(const char* data, size_t n) {
  if (fd_ < 0 || !host_raw(fd_)) return 0;
  const ssize_t w = ::write(fd_, data, n);
  return w > 0 ? static_cast<size_t>(w) : 0;  // EAGAIN: the host's buffer is full; EIO: no host
}

WriteFn PtyTransport::writer() {
  return [this](const char* data, size_t n) { return write_some(data, n); };
}

bool PtyTransport::send(const char* line, size_t n, cpa::TxClass cls) {
  if (!plugged_) return false;
  return tx_.send(line, n, cls, writer());
}

void PtyTransport::tick(uint32_t) {
  if (plugged_) tx_.pump(writer());
}

void PtyTransport::set_plugged(bool on) {
  plugged_ = on;
  if (!on) reset();
}

void PtyTransport::reset() {
  tx_.reset();
  rx_.clear();
}

}  // namespace cpasim
