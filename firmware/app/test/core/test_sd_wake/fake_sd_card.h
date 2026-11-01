// FakeSdCard: a powered SD card in SPI mode as sd_wake sees it, one byte each way per
// xfer at the 400 kHz init clock (20 us). The model (SD Physical Layer Simplified Spec,
// SPI mode, as the driver in third_party/SD uses it):
//   - commands are 6 bytes, byte aligned (01xxxxxx first), CRC7 checked (CMD59 turned
//     the check on at the first init and a reset keeps it); R1 after one byte (Ncr);
//     CMD0 -> idle (R1 0x01), CMD12 ends a read stream (one stuff byte, R1 0, busy) and
//     is refused elsewhere (illegal command);
//   - a multi-block write between blocks takes only tokens: 0xFC starts a block, 0xFD
//     ends the write (one byte Nbr, then busy); a block takes 512 + 2 bytes, answers
//     with a data response (CRC error: the bytes were junk), is busy, then waits for the
//     next token (multi) or is done (single);
//   - a multi-block read streams gap, 0xFE, 512 data bytes of 0x01 (they look like an
//     idle R1 to a careless reader) and 2 CRC bytes until CMD12; no other command;
//   - busy drives 0x00; an absent card is a fixed level;
//   - a command started while the card is busy or still answering the last one breaks
//     the protocol: it is counted in `violations`.
// The states a reset leaves the card in are set up by the test.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "sd_wake.h"

namespace sdtest {

class FakeSdCard : public cpa::rec::SdSpi {
 public:
  enum class St : uint8_t { Ready, Idle, WaitToken, Block, Busy, Stream, Absent };

  static FakeSdCard ready() { return FakeSdCard(St::Ready); }
  static FakeSdCard write_wait_token() { return FakeSdCard(St::WaitToken); }
  static FakeSdCard write_in_block(uint16_t left, bool multi) {
    FakeSdCard c(St::Block);
    c.left_ = left;
    c.multi_ = multi;
    return c;
  }
  static FakeSdCard busy(uint32_t bytes, St then) {
    FakeSdCard c(St::Busy);
    c.left_ = bytes;
    c.then_ = then;
    return c;
  }
  static FakeSdCard read_stream(uint16_t pos) {
    FakeSdCard c(St::Stream);
    c.pos_ = pos;
    return c;
  }
  static FakeSdCard absent(uint8_t level) {
    FakeSdCard c(St::Absent);
    c.level_ = level;
    return c;
  }

  uint8_t xfer(uint8_t in) override {
    ++clocks_;
    const bool answering = st_ == St::Busy || !out_.empty();  // in this very byte too
    const uint8_t out = drive();
    if (st_ != St::Absent && n_ == 0 && (in & 0xC0) == 0x40 && answering) ++violations;
    take(in);
    return out;
  }
  uint32_t ms() override { return static_cast<uint32_t>(clocks_ * 20 / 1000); }

  St state() const { return st_; }
  uint64_t clocks() const { return clocks_; }
  uint32_t busy_bytes = 2300;  // ~46 ms, the longest Stop Tran busy seen (sd-card.md §11)
  uint32_t violations = 0;

 private:
  static constexpr uint16_t kBlock = 512 + 2;
  static constexpr uint16_t kStreamCycle = 1 + 1 + kBlock;  // gap, 0xFE, data, CRC

  explicit FakeSdCard(St st) : st_(st) {}

  uint8_t drive() {
    if (st_ == St::Absent) return level_;
    if (!out_.empty()) {
      const uint8_t b = out_.front();
      out_.pop_front();
      return b;
    }
    if (st_ == St::Busy) {
      if (--left_ == 0) st_ = then_;
      return 0x00;
    }
    if (st_ == St::Stream) return stream_byte();
    return 0xFF;
  }

  uint8_t stream_byte() {
    const uint16_t p = pos_++ % kStreamCycle;
    if (p == 0) return 0xFF;
    if (p == 1) return 0xFE;
    return p < 2 + 512 ? 0x01 : 0x00;
  }

  void take(uint8_t in) {
    switch (st_) {
      case St::WaitToken: return token(in);
      case St::Block: return block_byte();
      case St::Ready:
      case St::Idle:
      case St::Stream: return command_byte(in);
      default: return;
    }
  }

  void go_busy(St then) {
    st_ = St::Busy;
    left_ = busy_bytes;
    then_ = then;
  }

  void token(uint8_t in) {
    if (in == 0xFC) {
      st_ = St::Block;
      left_ = kBlock;
      multi_ = true;
    } else if (in == 0xFD) {
      out_.push_back(0xFF);  // Nbr
      go_busy(St::Ready);
    }
  }

  void block_byte() {
    if (--left_ > 0) return;
    out_.push_back(0xEB);  // data response: CRC error
    go_busy(multi_ ? St::WaitToken : St::Ready);
  }

  void command_byte(uint8_t in) {
    if (n_ == 0 && (in & 0xC0) != 0x40) return;
    cmd_[n_++] = in;
    if (n_ < 6) return;
    n_ = 0;
    execute();
  }

  static uint8_t crc7(const uint8_t* d, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; ++i) {
      for (int b = 7; b >= 0; --b) {
        const bool bit = ((d[i] >> b) & 1) != ((crc >> 6) & 1);
        crc = static_cast<uint8_t>((crc << 1) & 0x7F);
        if (bit) crc ^= 0x09;
      }
    }
    return crc;
  }

  void respond(uint8_t r1) {
    out_.push_back(0xFF);  // Ncr
    out_.push_back(r1);
  }

  void execute() {
    const uint8_t index = cmd_[0] & 0x3F;
    const uint8_t idle = st_ == St::Idle ? 0x01 : 0x00;
    if (crc7(cmd_, 5) != (cmd_[5] >> 1)) return st_ == St::Stream ? void() : respond(0x08 | idle);
    if (st_ == St::Stream) {
      if (index != 12) return;  // a read stream hears only CMD12
      out_.push_back(0x01);     // stuff byte: still data
      out_.push_back(0x00);     // R1
      return go_busy(St::Ready);
    }
    if (index == 0) {
      st_ = St::Idle;
      return respond(0x01);
    }
    respond(0x04 | idle);  // illegal command (CMD12 outside a read included)
  }

  St st_;
  St then_ = St::Ready;
  uint32_t left_ = 0;
  bool multi_ = false;
  uint16_t pos_ = 0;
  uint8_t level_ = 0xFF;
  uint8_t cmd_[6] = {};
  uint8_t n_ = 0;
  uint64_t clocks_ = 0;
  std::deque<uint8_t> out_;
};

}  // namespace sdtest
