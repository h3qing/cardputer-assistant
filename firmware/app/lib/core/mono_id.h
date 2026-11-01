// MonoId (05 §5.9): EEEEEEE-BBBBB-SSSS in upper-case Crockford base32.
//   E = 32-bit epoch (random + MAC, fixed when NVS is first initialised)
//   B = 25-bit boot counter kept in NVS, S = 20-bit sequence within one boot.
// Within one epoch, string order == numeric order == creation order.
//
// Boot rule: the boot counter is incremented AND persisted before any ID is issued.
// The interface enforces it: a MonoSeq can only be started from a BootTicket, and a
// BootTicket only exists when the NVS read-back equals the advanced record.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "fixed_str.h"
#include "opt.h"
#include "result.h"

namespace cpa {

constexpr uint32_t kBootMax = (1u << 25) - 1;
constexpr uint32_t kSeqMax = (1u << 20) - 1;
constexpr size_t kMonoIdLen = 18;
constexpr size_t kBootIdLen = 13;

struct MonoId {
  uint32_t epoch = 0;
  uint32_t boot = 0;
  uint32_t seq = 0;

  bool operator==(const MonoId& o) const { return epoch == o.epoch && boot == o.boot && seq == o.seq; }
  bool operator!=(const MonoId& o) const { return !(*this == o); }
  bool operator<(const MonoId& o) const {
    if (epoch != o.epoch) return epoch < o.epoch;
    if (boot != o.boot) return boot < o.boot;
    return seq < o.seq;
  }
};

using MonoIdStr = FixedStr<kMonoIdLen>;
using BootIdStr = FixedStr<kBootIdLen>;

// Crockford base32 with a fixed number of digits (most significant first).
FixedStr<13> crockford_encode(uint64_t value, uint8_t digits);
Opt<uint64_t> crockford_decode(const char* s, size_t n);

MonoIdStr mono_format(const MonoId& id);
Opt<MonoId> mono_parse(const char* s, size_t n);  // strict: exact layout and field ranges
BootIdStr boot_id_format(uint32_t epoch, uint32_t boot);

// What NVS stores.
struct BootRecord {
  uint32_t epoch = 0;
  uint32_t boot = 0;
  bool operator==(const BootRecord& o) const { return epoch == o.epoch && boot == o.boot; }
};

enum class BootError : uint8_t { None, CounterExhausted };

BootRecord boot_init(uint32_t epoch);                           // first NVS initialisation
Result<BootRecord, BootError> boot_advance(const BootRecord& stored);  // boot + 1, to be persisted
uint32_t epoch_mix(uint32_t random32, uint64_t mac48);

// A default-constructed ticket is invalid; only boot_ticket() can make a valid one.
class BootTicket {
 public:
  BootTicket() = default;
  bool valid() const { return valid_; }
  uint32_t epoch() const { return rec_.epoch; }
  uint32_t boot() const { return rec_.boot; }

 private:
  BootRecord rec_{};
  bool valid_ = false;
  friend Opt<BootTicket> boot_ticket(const BootRecord& advanced, const BootRecord& read_back);
};

// Proof that `advanced` is durable: the shell writes it to NVS and passes the read-back.
Opt<BootTicket> boot_ticket(const BootRecord& advanced, const BootRecord& read_back);

struct MonoNext;

// Issues IDs for one boot. A default-constructed MonoSeq issues nothing.
class MonoSeq {
 public:
  MonoSeq() = default;
  bool exhausted() const { return !ticket_.valid() || next_ > kSeqMax; }

 private:
  BootTicket ticket_{};
  uint32_t next_ = 0;
  friend MonoSeq mono_start(const BootTicket& ticket);
  friend MonoNext mono_next(const MonoSeq& seq);
  friend BootIdStr mono_boot_id(const MonoSeq& seq);
  friend MonoSeq mono_seek_for_test(const MonoSeq& seq, uint32_t next);
};

struct MonoNext {
  MonoSeq seq;
  Opt<MonoId> id;  // absent once the 20-bit sequence is used up
};

MonoSeq mono_start(const BootTicket& ticket);
MonoNext mono_next(const MonoSeq& seq);
BootIdStr mono_boot_id(const MonoSeq& seq);
MonoSeq mono_seek_for_test(const MonoSeq& seq, uint32_t next);

// v0.3 (docs/11 §4.4): the sequence of one boot shared by the loop (outbox items) and the
// audio task (recording segments). next() is one atomic compare-and-swap, so two tasks
// never get the same id; it issues the same ids, in the same order, as MonoSeq. The
// counter is the one mutable value here: it lives in the runtime and is handed to both.
class MonoCounter {
 public:
  MonoCounter() = default;
  MonoCounter(const MonoCounter&) = delete;
  MonoCounter& operator=(const MonoCounter&) = delete;

  void start(const BootTicket& ticket, uint32_t first = 0);  // before either task takes an id
  Opt<MonoId> next();                                       // absent without a ticket or once used up
  bool exhausted() const;
  BootIdStr boot_id() const;
  // docs/28 S1a: the id the next next() would issue (absent without a ticket or once used
  // up); taking it changes nothing. The plug-in hint's offline mark.
  Opt<MonoId> peek() const;
  uint32_t epoch() const { return ticket_.epoch(); }

 private:
  BootTicket ticket_{};
  std::atomic<uint32_t> next_{0};
};

}  // namespace cpa
