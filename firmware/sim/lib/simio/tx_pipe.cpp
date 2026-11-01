#include "tx_pipe.h"

namespace cpasim {

bool TxPipe::write_now(const char* line, size_t n) {
  if (room() < n) return false;
  ring_.append(line, n);
  return true;
}

void TxPipe::flush(const WriteFn& write) {
  while (!ring_.empty()) {
    const size_t took = write(ring_.data(), ring_.size());
    if (took == 0) return;
    ring_.erase(0, took);
  }
}

void TxPipe::pump(const WriteFn& write) {
  flush(write);
  while (const Queue::Line* line = q_.front()) {
    if (!write_now(line->data, line->n)) break;
    q_.pop();
    flush(write);
  }
}

bool TxPipe::send(const char* line, size_t n, cpa::TxClass cls, const WriteFn& write) {
  pump(write);
  if (q_.empty() && write_now(line, n)) {
    flush(write);
    return true;
  }
  return q_.push(line, n, cls);
}

void TxPipe::reset() {
  ring_.clear();
  q_ = Queue{};
}

}  // namespace cpasim
