#include "mono_id.h"

namespace cpa {
namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr uint8_t kEpochDigits = 7;
constexpr uint8_t kBootDigits = 5;
constexpr uint8_t kSeqDigits = 4;

int crockford_value(char c) {
  for (int i = 0; i < 32; ++i) {
    if (kAlphabet[i] == c) return i;
  }
  return -1;
}

Opt<uint32_t> field(const char* s, uint8_t digits, uint64_t max) {
  const auto v = crockford_decode(s, digits);
  if (!v.has || v.value > max) return {};
  return some(static_cast<uint32_t>(v.value));
}

}  // namespace

FixedStr<13> crockford_encode(uint64_t value, uint8_t digits) {
  char out[13] = {};
  const uint8_t n = digits > 13 ? 13 : digits;
  uint64_t v = value;
  for (int i = n - 1; i >= 0; --i) {
    out[i] = kAlphabet[v & 31u];
    v >>= 5;
  }
  return FixedStr<13>::from(out, n);
}

Opt<uint64_t> crockford_decode(const char* s, size_t n) {
  if (n == 0 || n > 12) return {};
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    const int d = crockford_value(s[i]);
    if (d < 0) return {};
    v = (v << 5) | static_cast<uint64_t>(d);
  }
  return some(v);
}

MonoIdStr mono_format(const MonoId& id) {
  const auto boot = boot_id_format(id.epoch, id.boot);
  const auto seq = crockford_encode(id.seq, kSeqDigits);
  return MonoIdStr::from(boot.c_str(), boot.size()).append("-").append(seq.c_str(), seq.size());
}

BootIdStr boot_id_format(uint32_t epoch, uint32_t boot) {
  const auto e = crockford_encode(epoch, kEpochDigits);
  const auto b = crockford_encode(boot, kBootDigits);
  return BootIdStr::from(e.c_str(), e.size()).append("-").append(b.c_str(), b.size());
}

Opt<MonoId> mono_parse(const char* s, size_t n) {
  if (n != kMonoIdLen || s[7] != '-' || s[13] != '-') return {};
  const auto epoch = field(s, kEpochDigits, 0xFFFFFFFFull);
  const auto boot = field(s + 8, kBootDigits, kBootMax);
  const auto seq = field(s + 14, kSeqDigits, kSeqMax);
  if (!epoch.has || !boot.has || !seq.has) return {};
  MonoId id;
  id.epoch = epoch.value;
  id.boot = boot.value;
  id.seq = seq.value;
  return some(id);
}

BootRecord boot_init(uint32_t epoch) {
  BootRecord r;
  r.epoch = epoch;
  return r;
}

Result<BootRecord, BootError> boot_advance(const BootRecord& stored) {
  if (stored.boot >= kBootMax) return Result<BootRecord, BootError>::failure(BootError::CounterExhausted);
  BootRecord next = stored;
  next.boot = stored.boot + 1;
  return Result<BootRecord, BootError>::success(next);
}

uint32_t epoch_mix(uint32_t random32, uint64_t mac48) {
  // Fold the MAC in with a multiplicative hash so two boards never share an epoch
  // just because their RNG produced the same first word.
  const uint64_t h = (mac48 ^ (mac48 >> 29)) * 0x9E3779B97F4A7C15ull;
  return random32 ^ static_cast<uint32_t>(h >> 32) ^ static_cast<uint32_t>(h);
}

Opt<BootTicket> boot_ticket(const BootRecord& advanced, const BootRecord& read_back) {
  if (!(advanced == read_back)) return {};
  BootTicket t;
  t.rec_ = advanced;
  t.valid_ = true;
  return some(t);
}

MonoSeq mono_start(const BootTicket& ticket) {
  MonoSeq s;
  s.ticket_ = ticket;
  return s;
}

MonoNext mono_next(const MonoSeq& seq) {
  MonoNext out;
  out.seq = seq;
  if (seq.exhausted()) return out;
  MonoId id;
  id.epoch = seq.ticket_.epoch();
  id.boot = seq.ticket_.boot();
  id.seq = seq.next_;
  out.seq.next_ = seq.next_ + 1;
  out.id = some(id);
  return out;
}

BootIdStr mono_boot_id(const MonoSeq& seq) { return boot_id_format(seq.ticket_.epoch(), seq.ticket_.boot()); }

MonoSeq mono_seek_for_test(const MonoSeq& seq, uint32_t next) {
  MonoSeq s = seq;
  s.next_ = next;
  return s;
}

void MonoCounter::start(const BootTicket& ticket, uint32_t first) {
  ticket_ = ticket;
  next_.store(first);
}

Opt<MonoId> MonoCounter::peek() const {
  const uint32_t seq = next_.load();
  if (!ticket_.valid() || seq > kSeqMax) return {};
  MonoId id;
  id.epoch = ticket_.epoch();
  id.boot = ticket_.boot();
  id.seq = seq;
  return some(id);
}

Opt<MonoId> MonoCounter::next() {
  if (!ticket_.valid()) return {};
  uint32_t seq = next_.load();
  do {
    if (seq > kSeqMax) return {};
  } while (!next_.compare_exchange_weak(seq, seq + 1));
  MonoId id;
  id.epoch = ticket_.epoch();
  id.boot = ticket_.boot();
  id.seq = seq;
  return some(id);
}

bool MonoCounter::exhausted() const { return !ticket_.valid() || next_.load() > kSeqMax; }

BootIdStr MonoCounter::boot_id() const { return boot_id_format(ticket_.epoch(), ticket_.boot()); }

}  // namespace cpa
