#include "outbox_core.h"

namespace cpa {
namespace {

OutboxEffect effect(OutboxEffectKind kind, const MonoId& id) {
  OutboxEffect e;
  e.kind = kind;
  e.id = id;
  return e;
}

int index_of(const MonoId* ids, uint16_t n, const MonoId& id) {
  for (uint16_t i = 0; i < n; ++i) {
    if (ids[i] == id) return i;
  }
  return -1;
}

bool in_flight(const OutboxCore& c, const MonoId& id) {
  for (uint8_t i = 0; i < c.n_inflight; ++i) {
    if (c.inflight[i].id == id) return true;
  }
  return false;
}

// Inserts id into the ascending ids[0..n), which has room for one more.
template <class Count>
void insert_sorted(MonoId* ids, Count& n, const MonoId& id) {
  Count at = n;
  while (at > 0 && id < ids[at - 1]) {
    ids[at] = ids[at - 1];
    --at;
  }
  ids[at] = id;
  n = static_cast<Count>(n + 1);
}

// Resend every in-flight item whose retry delay has passed.
OutboxStep resend_due(const OutboxStep& s, uint32_t now) {
  OutboxStep out = s;
  for (uint8_t i = 0; i < out.state.n_inflight; ++i) {
    const Inflight& f = s.state.inflight[i];
    if (now - f.sent_ms < outbox_retry_delay_ms(f.sends)) continue;
    out.state.inflight[i].sent_ms = now;
    out.state.inflight[i].sends = static_cast<uint8_t>(f.sends < 255 ? f.sends + 1 : 255);
    out.effects = out.effects.with(effect(OutboxEffectKind::Send, f.id));
  }
  return out;
}

// Start sending the oldest pending items until 4 are in flight.
OutboxStep send_new(const OutboxStep& s, uint32_t now) {
  OutboxStep out = s;
  for (uint16_t i = 0; i < s.state.n && out.state.n_inflight < kOutboxInflightMax; ++i) {
    const MonoId& id = s.state.ids[i];
    if (in_flight(out.state, id)) continue;
    Inflight f;
    f.id = id;
    f.sent_ms = now;
    f.sends = 1;
    out.state.inflight[out.state.n_inflight++] = f;
    out.effects = out.effects.with(effect(OutboxEffectKind::Send, id));
  }
  return out;
}

}  // namespace

OutboxStep outbox_added(const OutboxCore& c, const MonoId& id) {
  OutboxStep out{c, {}};
  if (!outbox_insert_pending(out.state, id)) out.effects = out.effects.with(effect(OutboxEffectKind::Full, id));
  return out;
}

OutboxStep outbox_tick(const OutboxCore& c, uint32_t now, bool ready) {
  const OutboxStep start{c, {}};
  if (!ready) return start;
  return send_new(resend_due(start, now), now);
}

OutboxStep outbox_acked(const OutboxCore& c, const MonoId& id, proto::AckStatus status, ErrCode code) {
  OutboxStep out{c, {}};
  if (index_of(c.ids, c.n, id) < 0) return out;
  outbox_forget(out.state, id);
  if (status != proto::AckStatus::Rejected) {
    out.effects = out.effects.with(effect(OutboxEffectKind::Remove, id));
    return out;
  }
  out.effects = out.effects.with(effect(OutboxEffectKind::MoveToBad, id));
  const auto dropped = outbox_insert_bad(out.state, id);
  if (dropped.has) out.effects = out.effects.with(effect(OutboxEffectKind::RemoveBad, dropped.value));
  OutboxEffect toast = effect(OutboxEffectKind::Rejected, id);
  toast.code = code;
  out.effects = out.effects.with(toast);
  return out;
}

OutboxStep outbox_link_lost(const OutboxCore& c) {
  OutboxStep out{c, {}};
  out.state.n_inflight = 0;
  return out;
}

OutboxStep outbox_bad_added(const OutboxCore& c, const MonoId& id) {
  OutboxStep out{c, {}};
  const auto dropped = outbox_insert_bad(out.state, id);
  if (dropped.has) out.effects = out.effects.with(effect(OutboxEffectKind::RemoveBad, dropped.value));
  return out;
}

bool outbox_insert_pending(OutboxCore& c, const MonoId& id) {
  if (index_of(c.ids, c.n, id) >= 0) return true;
  if (c.n >= kOutboxCapacity) return false;
  insert_sorted(c.ids, c.n, id);
  return true;
}

void outbox_forget(OutboxCore& c, const MonoId& id) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < c.n; ++i) {
    if (c.ids[i] != id) c.ids[n++] = c.ids[i];
  }
  c.n = n;
  uint8_t k = 0;
  for (uint8_t i = 0; i < c.n_inflight; ++i) {
    if (c.inflight[i].id != id) c.inflight[k++] = c.inflight[i];
  }
  c.n_inflight = k;
}

Opt<MonoId> outbox_insert_bad(OutboxCore& c, const MonoId& id) {
  if (index_of(c.bad, c.n_bad, id) >= 0) return {};
  Opt<MonoId> dropped;
  if (c.n_bad >= kOutboxBadKeep) {
    if (id < c.bad[0]) return some(id);  // older than every kept one: it goes itself
    dropped = some(c.bad[0]);
    for (uint8_t i = 1; i < c.n_bad; ++i) c.bad[i - 1] = c.bad[i];
    c.n_bad = static_cast<uint8_t>(c.n_bad - 1);
  }
  insert_sorted(c.bad, c.n_bad, id);
  return dropped;
}

bool outbox_can_accept(const OutboxCore& c, uint32_t free_bytes) {
  return c.n < kOutboxCapacity && free_bytes >= kOutboxMinFreeBytes;
}

OutboxInfo outbox_info(const OutboxCore& c, uint32_t free_bytes) {
  OutboxInfo info;
  info.pending = c.n;
  info.inflight = c.n_inflight;
  info.bad = c.n_bad;
  info.full = !outbox_can_accept(c, free_bytes);
  return info;
}

uint32_t outbox_retry_delay_ms(uint8_t sends) {
  static const uint32_t kDelays[] = {3000, 3000, 6000, 12000};
  return sends < 4 ? kDelays[sends] : 30000;
}

bool outbox_contains(const OutboxCore& c, const MonoId& id) { return index_of(c.ids, c.n, id) >= 0; }

}  // namespace cpa
