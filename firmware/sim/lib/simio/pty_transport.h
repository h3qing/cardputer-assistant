// PtyTransport (gh #32): the device's USB link for `cpa-sim serve`, on a pseudo-terminal.
// The host (cpad, a HIL rig, `screen`) opens the slave path() as it would the Cardputer's
// port. Like UsbTransport, a line goes into the 4 KB TX ring only when all of it fits,
// else into the 4-line queue (TxPipe); received bytes are taken into an 8 KB RX buffer,
// and, as in the HWCDC RX queue, bytes that arrive while it is full are lost (counted
// in rx_dropped()), never pushed back on the host.
//   - A pty has no USB cable: plugged() is the simulator's own switch (`usb on|off`).
//     While unplugged nothing moves, and both directions are dropped.
//   - Bytes are written only while the host has the port open in raw mode. macOS gives
//     a newly opened slave the default termios (echo, canonical), so anything written
//     before pyserial sets raw mode would be echoed back as host input; until then, and
//     while nobody has the port open (EIO), lines wait in the ring as they would on the
//     device while the host is not reading.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "transport.h"
#include "tx_pipe.h"

namespace cpasim {

constexpr size_t kRxBuffer = cpa::kUsbRxBuffer;

class PtyTransport : public cpa::Transport {
 public:
  PtyTransport() = default;
  PtyTransport(const PtyTransport&) = delete;
  PtyTransport& operator=(const PtyTransport&) = delete;
  ~PtyTransport() override;

  bool open(std::string& err);  // a new pty; path() is its slave
  const std::string& path() const { return path_; }
  int fd() const { return fd_; }  // the master, for poll()

  bool plugged() const override { return plugged_; }
  size_t read(char* buf, size_t cap) override;
  bool send(const char* line, size_t n, cpa::TxClass cls) override;
  void tick(uint32_t now_ms) override;

  void set_plugged(bool on);
  void reset();  // a reboot: the TX ring, its queue and the RX buffer are RAM
  size_t tx_room() const { return tx_.room(); }
  uint32_t dropped_replies() const { return tx_.dropped_replies(); }
  uint32_t rx_dropped() const { return rx_dropped_; }  // since open(); reset() keeps it

 private:
  void fill_rx();
  size_t write_some(const char* data, size_t n);
  WriteFn writer();

  int fd_ = -1;
  std::string path_;
  bool plugged_ = true;
  std::string rx_;
  uint32_t rx_dropped_ = 0;
  TxPipe tx_;
};

}  // namespace cpasim
