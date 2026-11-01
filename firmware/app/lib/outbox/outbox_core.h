// OutboxCore (05 §5.9): pure state of the reliable outbox. Items are files
// /outbox/<MonoId>.json written by outbox_persist(); this core only tracks their ids.
//   - sends only when the link is Ready, in ID order, at most 4 in flight;
//   - resends after 3 s, then 6 s, 12 s and every 30 s;
//   - stored / duplicate -> delete the file; rejected -> move to bad/ (20 kept) + toast;
//   - link loss puts in-flight items back to pending (not counted as a retry);
//   - new items are refused at 200 pending or under 64 KB free.
#pragma once

#include <cstdint>

#include "app_state.h"
#include "messages.h"
#include "mono_id.h"
#include "step.h"

namespace cpa {

constexpr uint16_t kOutboxCapacity = 200;
constexpr uint32_t kOutboxMinFreeBytes = 64u * 1024u;
constexpr uint8_t kOutboxBadKeep = 20;
constexpr uint8_t kOutboxInflightMax = 4;  // limits.json device_outbox_inflight_max

struct Inflight {
  MonoId id;
  uint32_t sent_ms = 0;
  uint8_t sends = 0;  // transmissions since it last became pending
};

struct OutboxCore {
  uint16_t n = 0;
  MonoId ids[kOutboxCapacity];  // pending ids, ascending (in-flight ones included)
  uint8_t n_inflight = 0;
  Inflight inflight[kOutboxInflightMax];
  uint8_t n_bad = 0;
  MonoId bad[kOutboxBadKeep];  // ids in /outbox/bad, ascending
};

enum class OutboxEffectKind : uint8_t {
  Send,       // read /outbox/<id>.json and transmit it
  Remove,     // delete /outbox/<id>.json
  MoveToBad,  // rename it to /outbox/bad/<id>.json
  RemoveBad,  // delete /outbox/bad/<id>.json (only the newest 20 are kept)
  Rejected,   // toast: the daemon rejected an item (code)
  Full,       // the item could not be tracked: capacity reached
};

struct OutboxEffect {
  OutboxEffectKind kind = OutboxEffectKind::Send;
  MonoId id;
  ErrCode code = ErrCode::None;
};

using OutboxStep = Step<OutboxCore, OutboxEffect>;

OutboxStep outbox_added(const OutboxCore& c, const MonoId& id);  // after outbox_persist() succeeded
OutboxStep outbox_tick(const OutboxCore& c, uint32_t now, bool ready);
OutboxStep outbox_acked(const OutboxCore& c, const MonoId& id, proto::AckStatus status, ErrCode code);
OutboxStep outbox_link_lost(const OutboxCore& c);
OutboxStep outbox_bad_added(const OutboxCore& c, const MonoId& id);  // track a file in bad/

// In-place primitives behind outbox_added / outbox_bad_added / outbox_acked: they modify
// `c`. The pure functions apply them to their own new copy; the boot scan applies them
// to its static state in the outbox I/O shell, which must not copy a ~2.8 KB OutboxCore
// per directory entry on the 16 KB loop stack (05 §5.13).
bool outbox_insert_pending(OutboxCore& c, const MonoId& id);     // false: capacity reached
void outbox_forget(OutboxCore& c, const MonoId& id);             // drop from pending and in flight
Opt<MonoId> outbox_insert_bad(OutboxCore& c, const MonoId& id);  // the id dropped to keep 20, if any

bool outbox_can_accept(const OutboxCore& c, uint32_t free_bytes);
OutboxInfo outbox_info(const OutboxCore& c, uint32_t free_bytes);
uint32_t outbox_retry_delay_ms(uint8_t sends);
bool outbox_contains(const OutboxCore& c, const MonoId& id);

}  // namespace cpa
