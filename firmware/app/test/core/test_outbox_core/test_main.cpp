// OutboxCore (05 §5.9): pure reliable-delivery state. Sends in ID order with at most 4
// in flight, resends after 3/6/12/30 s, handles stored/duplicate/rejected, requeues
// in-flight items on link loss, refuses new items at 200 or < 64 KB free.
// Plus the power-loss test: a crash injected before every store operation of a
// scripted session loses no item that showed "saved" and duplicates nothing.
#include <unity.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "codec.h"
#include "outbox_core.h"
#include "outbox_io.h"
#include "support/fake_store.h"

using namespace cpa;
using proto::AckStatus;
using testsupport::FakeStore;

void setUp() {}
void tearDown() {}

static MonoId mid(uint32_t seq) {
  MonoId id;
  id.epoch = 7;
  id.boot = 3;
  id.seq = seq;
  return id;
}

static OutboxCore with_items(std::initializer_list<uint32_t> seqs) {
  OutboxCore c;
  for (uint32_t s : seqs) c = outbox_added(c, mid(s)).state;
  return c;
}

static std::vector<uint32_t> sends(const OutboxStep& s) {
  std::vector<uint32_t> out;
  for (uint8_t i = 0; i < s.effects.count; ++i) {
    if (s.effects.items[i].kind == OutboxEffectKind::Send) out.push_back(s.effects.items[i].id.seq);
  }
  return out;
}

static void test_added_keeps_id_order() {
  const OutboxCore c = with_items({5, 2, 9});
  TEST_ASSERT_EQUAL_UINT16(3, c.n);
  TEST_ASSERT_EQUAL_UINT32(2, c.ids[0].seq);
  TEST_ASSERT_EQUAL_UINT32(9, c.ids[2].seq);
  TEST_ASSERT_EQUAL_UINT16(3, outbox_info(c, 1 << 20).pending);
  TEST_ASSERT_EQUAL_UINT16(3, outbox_added(c, mid(5)).state.n);  // same id twice: no-op
}

static void test_sends_only_when_ready_in_order_at_most_four() {
  const OutboxCore c = with_items({1, 2, 3, 4, 5, 6});
  TEST_ASSERT_EQUAL_UINT8(0, outbox_tick(c, 0, false).effects.count);
  const auto s = outbox_tick(c, 0, true);
  const std::vector<uint32_t> want = {1, 2, 3, 4};
  TEST_ASSERT_TRUE(sends(s) == want);
  TEST_ASSERT_EQUAL_UINT8(4, s.state.n_inflight);
  TEST_ASSERT_EQUAL_UINT8(0, outbox_tick(s.state, 100, true).effects.count);
  const auto acked = outbox_acked(s.state, mid(2), AckStatus::Stored, ErrCode::None);
  TEST_ASSERT_TRUE(acked.effects.items[0].kind == OutboxEffectKind::Remove);
  const std::vector<uint32_t> next = {5};
  TEST_ASSERT_TRUE(sends(outbox_tick(acked.state, 200, true)) == next);
}

static void test_resend_backoff_3_6_12_30() {
  OutboxCore c = outbox_tick(with_items({1}), 0, true).state;
  const uint32_t at[] = {3000, 9000, 21000, 51000, 81000};
  uint32_t prev = 0;
  for (uint32_t t : at) {
    TEST_ASSERT_EQUAL_UINT8(0, outbox_tick(c, t - 1, true).effects.count);
    const auto s = outbox_tick(c, t, true);
    TEST_ASSERT_EQUAL_UINT8(1, s.effects.count);
    TEST_ASSERT_EQUAL_UINT8(1, s.state.n_inflight);  // a resend is not a new in-flight item
    c = s.state;
    prev = t;
  }
  TEST_ASSERT_EQUAL_UINT32(81000, prev);
  TEST_ASSERT_EQUAL_UINT32(3000, outbox_retry_delay_ms(1));
  TEST_ASSERT_EQUAL_UINT32(30000, outbox_retry_delay_ms(200));
}

static void test_retry_survives_millis_wrap() {
  const OutboxCore c = outbox_tick(with_items({1}), 0xFFFFFF00u, true).state;
  TEST_ASSERT_EQUAL_UINT8(0, outbox_tick(c, 0x00000010u, true).effects.count);
  TEST_ASSERT_EQUAL_UINT8(1, outbox_tick(c, 0xFFFFFF00u + 3000u, true).effects.count);
}

static void test_duplicate_and_unknown_acks() {
  const OutboxCore c = outbox_tick(with_items({1, 2}), 0, true).state;
  const auto dup = outbox_acked(c, mid(1), AckStatus::Duplicate, ErrCode::None);
  TEST_ASSERT_EQUAL_UINT16(1, dup.state.n);
  TEST_ASSERT_TRUE(dup.effects.items[0].kind == OutboxEffectKind::Remove);
  const auto unknown = outbox_acked(dup.state, mid(77), AckStatus::Stored, ErrCode::None);
  TEST_ASSERT_EQUAL_UINT8(0, unknown.effects.count);
  TEST_ASSERT_EQUAL_UINT16(1, unknown.state.n);
}

static void test_rejected_goes_to_bad_and_keeps_twenty() {
  OutboxCore c = with_items({1});
  c = outbox_tick(c, 0, true).state;
  const auto r = outbox_acked(c, mid(1), AckStatus::Rejected, ErrCode::BadField);
  TEST_ASSERT_EQUAL_UINT16(0, r.state.n);
  TEST_ASSERT_TRUE(r.effects.items[0].kind == OutboxEffectKind::MoveToBad);
  TEST_ASSERT_TRUE(r.effects.items[1].kind == OutboxEffectKind::Rejected);
  TEST_ASSERT_TRUE(r.effects.items[1].code == ErrCode::BadField);
  TEST_ASSERT_EQUAL_UINT16(1, outbox_info(r.state, 1 << 20).bad);
  OutboxCore full;
  for (uint32_t s = 100; s < 120; ++s) full = outbox_bad_added(full, mid(s)).state;
  full = outbox_added(full, mid(1)).state;
  const auto evict = outbox_acked(full, mid(1), AckStatus::Rejected, ErrCode::BadField);
  TEST_ASSERT_TRUE(evict.effects.items[1].kind == OutboxEffectKind::RemoveBad);
  TEST_ASSERT_EQUAL_UINT32(1, evict.effects.items[1].id.seq);  // the oldest: the rejected one itself
  TEST_ASSERT_EQUAL_UINT8(kOutboxBadKeep, evict.state.n_bad);
}

static void test_link_loss_requeues_without_counting_a_retry() {
  const OutboxCore sent = outbox_tick(with_items({1, 2}), 0, true).state;
  const auto lost = outbox_link_lost(sent);
  TEST_ASSERT_EQUAL_UINT8(0, lost.state.n_inflight);
  TEST_ASSERT_EQUAL_UINT16(2, lost.state.n);
  const auto again = outbox_tick(lost.state, 5000, true);
  const std::vector<uint32_t> want = {1, 2};
  TEST_ASSERT_TRUE(sends(again) == want);
  TEST_ASSERT_EQUAL_UINT8(0, outbox_tick(again.state, 7999, true).effects.count);
  TEST_ASSERT_EQUAL_UINT8(2, outbox_tick(again.state, 8000, true).effects.count);
}

static void test_capacity_rules() {
  OutboxCore c;
  TEST_ASSERT_TRUE(outbox_can_accept(c, 64 * 1024));
  TEST_ASSERT_FALSE(outbox_can_accept(c, 64 * 1024 - 1));
  TEST_ASSERT_TRUE(outbox_info(c, 1000).full);
  for (uint32_t s = 0; s < kOutboxCapacity; ++s) c = outbox_added(c, mid(s)).state;
  TEST_ASSERT_FALSE(outbox_can_accept(c, 1 << 20));
  TEST_ASSERT_TRUE(outbox_info(c, 1 << 20).full);
  const auto over = outbox_added(c, mid(9999));
  TEST_ASSERT_EQUAL_UINT16(kOutboxCapacity, over.state.n);
  TEST_ASSERT_TRUE(over.effects.items[0].kind == OutboxEffectKind::Full);
}

// In-place primitives behind outbox_added / outbox_bad_added / outbox_acked. The boot
// scan applies them to its static state so it never copies an OutboxCore (05 §5.13).
static void test_in_place_primitives() {
  OutboxCore c;
  TEST_ASSERT_TRUE(outbox_insert_pending(c, mid(5)));
  TEST_ASSERT_TRUE(outbox_insert_pending(c, mid(2)));
  TEST_ASSERT_TRUE(outbox_insert_pending(c, mid(5)));  // already tracked
  TEST_ASSERT_EQUAL_UINT16(2, c.n);
  TEST_ASSERT_EQUAL_UINT32(2, c.ids[0].seq);
  c = outbox_tick(c, 0, true).state;  // both in flight
  outbox_forget(c, mid(2));
  TEST_ASSERT_EQUAL_UINT16(1, c.n);
  TEST_ASSERT_EQUAL_UINT8(1, c.n_inflight);
  TEST_ASSERT_EQUAL_UINT32(5, c.inflight[0].id.seq);
  for (uint32_t s = 100; s < 120; ++s) TEST_ASSERT_FALSE(outbox_insert_bad(c, mid(s)).has);
  const auto older = outbox_insert_bad(c, mid(1));  // older than all 20 kept: goes itself
  TEST_ASSERT_TRUE(older.has && older.value == mid(1));
  const auto newer = outbox_insert_bad(c, mid(200));  // evicts the oldest kept
  TEST_ASSERT_TRUE(newer.has && newer.value == mid(100));
  TEST_ASSERT_EQUAL_UINT8(kOutboxBadKeep, c.n_bad);
  TEST_ASSERT_TRUE(c.bad[0] == mid(101) && c.bad[kOutboxBadKeep - 1] == mid(200));
  TEST_ASSERT_FALSE(outbox_insert_bad(c, mid(200)).has);  // already tracked
  OutboxCore full;
  for (uint32_t s = 0; s < kOutboxCapacity; ++s) TEST_ASSERT_TRUE(outbox_insert_pending(full, mid(s)));
  TEST_ASSERT_FALSE(outbox_insert_pending(full, mid(9999)));
  TEST_ASSERT_EQUAL_UINT16(kOutboxCapacity, full.n);
}

// ---- power-loss injection ---------------------------------------------------------

struct Daemon {  // dedupes by id like cpad (05 §7.3)
  std::map<std::string, std::string> stored;
  proto::Codec codec;
  AckStatus receive(const std::string& line, std::string& id) {
    const auto d = codec.decode(line.data(), line.size());
    if (d.outcome != proto::Outcome::Ok) return AckStatus::Rejected;
    id = d.msg.id.c_str();
    if (stored.count(id)) return AckStatus::Duplicate;
    stored[id] = std::get<proto::OutboxItem>(d.msg.body).note.text.c_str();
    return AckStatus::Stored;
  }
};

struct Device {
  FakeStore& store;
  proto::Codec codec;
  OutboxCore core;
  MonoSeq seq;
  uint32_t now = 0;
  std::vector<std::string> saved;  // texts that showed "saved"

  Device(FakeStore& s, uint32_t boot) : store(s) {
    const BootRecord rec{7, boot};
    seq = mono_start(boot_ticket(rec, rec).value);
    core = outbox_boot_scan(store, codec).core;
  }

  void save(const std::string& text) {
    const auto next = mono_next(seq);
    seq = next.seq;
    proto::OutboxItem item;
    item.note.text = UserText::from(text.c_str());
    const auto line = codec.encode(proto::make_message(MsgId::from(mono_format(next.id.value).c_str()), {}, item));
    if (!outbox_persist(store, next.id.value, line.value.c_str(), line.value.size())) return;
    core = outbox_added(core, next.id.value).state;
    saved.push_back(text);  // only now is "saved" shown
  }

  // One loop pass: send what is due; `lose_acks` drops every ack of this pass.
  void pump(Daemon& daemon, bool lose_acks) {
    const auto step = outbox_tick(core, now, true);
    core = step.state;
    for (uint8_t i = 0; i < step.effects.count && !store.crashed(); ++i) {
      char buf[2100];
      const long n = outbox_read(store, step.effects.items[i].id, buf, sizeof buf);
      if (n < 0) continue;
      std::string id;
      const AckStatus st = daemon.receive(std::string(buf, static_cast<size_t>(n)), id);
      if (lose_acks) continue;
      const auto acked = outbox_acked(core, step.effects.items[i].id, st, ErrCode::None);
      core = acked.state;
      for (uint8_t k = 0; k < acked.effects.count; ++k) outbox_apply(store, acked.effects.items[k]);
    }
    now += 3000;
  }
};

// The scripted session; it stops as soon as the store "loses power".
static void session(Device& dev, Daemon& daemon) {
  const char* plan[] = {"save", "save", "pump", "save", "pump_lost", "save", "pump", "save", "save", "pump", "pump"};
  int n = 0;
  for (const char* step : plan) {
    if (dev.store.crashed()) return;
    if (std::string(step) == "save") dev.save("note-" + std::to_string(++n));
    if (std::string(step) == "pump") dev.pump(daemon, false);
    if (std::string(step) == "pump_lost") dev.pump(daemon, true);
  }
}

static void drain(Device& dev, Daemon& daemon) {
  for (int i = 0; i < 50 && dev.core.n > 0; ++i) dev.pump(daemon, false);
}

static void test_power_loss_at_every_store_operation() {
  long total = 0;
  {
    FakeStore clean;
    Daemon daemon;
    Device dev(clean, 1);
    session(dev, daemon);
    total = clean.ops();
  }
  TEST_ASSERT_TRUE(total > 20);
  for (long k = 0; k <= total; ++k) {
    FakeStore store;
    Daemon daemon;
    std::vector<std::string> saved;
    {
      Device dev(store, 1);
      store.crash_before(k);
      session(dev, daemon);
      saved = dev.saved;
    }
    store.power_cycle();
    Device rebooted(store, 2);
    drain(rebooted, daemon);
    const std::string at = "crash before op " + std::to_string(k);
    for (const auto& text : saved) {
      bool found = false;
      for (const auto& kv : daemon.stored) found = found || kv.second == text;
      TEST_ASSERT_TRUE_MESSAGE(found, (at + ": lost " + text).c_str());
    }
    std::map<std::string, int> copies;
    for (const auto& kv : daemon.stored) TEST_ASSERT_EQUAL_INT_MESSAGE(1, ++copies[kv.second], (at + ": duplicate " + kv.second).c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, store.count_in("/outbox", ".json"), at.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, store.count_in("/outbox", ".tmp"), at.c_str());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_added_keeps_id_order);
  RUN_TEST(test_sends_only_when_ready_in_order_at_most_four);
  RUN_TEST(test_resend_backoff_3_6_12_30);
  RUN_TEST(test_retry_survives_millis_wrap);
  RUN_TEST(test_duplicate_and_unknown_acks);
  RUN_TEST(test_rejected_goes_to_bad_and_keeps_twenty);
  RUN_TEST(test_link_loss_requeues_without_counting_a_retry);
  RUN_TEST(test_capacity_rules);
  RUN_TEST(test_in_place_primitives);
  RUN_TEST(test_power_loss_at_every_store_operation);
  return UNITY_END();
}
