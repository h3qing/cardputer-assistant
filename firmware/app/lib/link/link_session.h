// LinkSession (05 §5.8, §6.2-§6.4): pure link state machine of the device.
//   Detached: no USB host (isPlugged() false; debounced by plug_filter.h) -> sends nothing
//   Attached: host present, no valid hello yet or 15 s without valid data -> answers hello only
//   Ready:    host hello (v=0) received and answered -> everything
// The device never starts a handshake. Every host-initiated message (no re) gets
// exactly one reply (hello / pong / ack / error, or the debug.state summary), error
// replies are limited to 5 per second. A hello while Ready means the daemon restarted:
// it is answered and reported as a re-handshake (in-flight requests are lost).
// Host replies (outbox.ack, agent.result, ack, error) are delivered, never answered.
#pragma once

#include <cstdint>

#include "app_state.h"
#include "codec.h"
#include "step.h"

namespace cpa {

enum class LinkEffectKind : uint8_t {
  Reply,    // send `reply` with our `id`, answering `re`
  Deliver,  // apply the decoded message to its slice (snapshot, time, ack, result, debug)
  Changed,  // link mode changed (from -> to) or the host re-handshook
};

enum class ReplyKind : uint8_t { Hello, Pong, Ack, Error, DebugState };

struct LinkEffect {
  LinkEffectKind kind = LinkEffectKind::Deliver;
  ReplyKind reply = ReplyKind::Ack;
  MsgId id;  // our message id (d<n>) for a Reply
  MsgId re;
  ErrCode code = ErrCode::None;  // Error replies
  Opt<proto::FieldPath> field;   // only when it fits error.field (^[!-~]{1,48}$)
  LinkMode from = LinkMode::Detached;
  LinkMode to = LinkMode::Detached;
  bool rehandshake = false;
};

struct LinkCounters {
  uint32_t noise = 0;
  uint32_t parse = 0;
  uint32_t too_large = 0;
  uint32_t bad = 0;  // malformed lines that get no reply (bad id, broken replies)
  uint32_t ignored = 0;
  uint32_t errors_dropped = 0;  // error replies over the 5/s limit
};

struct LinkCore {
  LinkInfo info;
  uint32_t next_id = 1;
  uint32_t err_window_ms = 0;
  uint8_t err_count = 0;
  LinkCounters counters;
};

using LinkStep = Step<LinkCore, LinkEffect>;

struct IdIssue {
  LinkCore core;
  MsgId id;
};

LinkStep link_line(const LinkCore& c, const proto::Decoded& d, uint32_t now);
LinkStep link_tick(const LinkCore& c, bool plugged, uint32_t now);
IdIssue link_issue_id(const LinkCore& c);  // ids for device requests (agent.*, queue.done, ...)
bool link_ready(const LinkCore& c);
// True when a Changed effect means in-flight requests are lost: outbox items go back
// to pending and an agent request becomes "result unknown".
bool link_lost_inflight(const LinkEffect& e);

}  // namespace cpa
