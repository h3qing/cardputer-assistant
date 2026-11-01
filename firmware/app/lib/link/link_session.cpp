#include "link_session.h"

#include <cstdio>

#include "protocol_limits.h"
#include "text_rules.h"

namespace cpa {
namespace {

using proto::Decoded;
using proto::MsgType;
using proto::Outcome;

LinkEffect changed(LinkMode from, LinkMode to, bool rehandshake) {
  LinkEffect e;
  e.kind = LinkEffectKind::Changed;
  e.from = from;
  e.to = to;
  e.rehandshake = rehandshake;
  return e;
}

LinkEffect deliver() {
  LinkEffect e;
  e.kind = LinkEffectKind::Deliver;
  return e;
}

LinkStep with_mode(const LinkCore& c, LinkMode to) {
  LinkStep s{c, {}};
  if (c.info.mode == to) return s;
  s.state.info.mode = to;
  s.effects = s.effects.with(changed(c.info.mode, to, false));
  return s;
}

LinkEffect reply_effect(ReplyKind kind, const MsgId& re) {
  LinkEffect e;
  e.kind = LinkEffectKind::Reply;
  e.reply = kind;
  e.re = re;
  return e;
}

// Adds `e` as a reply with a fresh device id (d<n>).
LinkStep reply_with(const LinkStep& s, const LinkEffect& e) {
  const IdIssue issued = link_issue_id(s.state);
  LinkEffect numbered = e;
  numbered.id = issued.id;
  return LinkStep{issued.core, s.effects.with(numbered)};
}

LinkStep reply(const LinkStep& s, ReplyKind kind, const MsgId& re) { return reply_with(s, reply_effect(kind, re)); }

// Error replies: at most device_errors_per_s per one-second window.
LinkStep error_reply(const LinkStep& s, const MsgId& re, ErrCode code, const proto::FieldPath& field, uint32_t now) {
  LinkStep cur = s;
  if (now - cur.state.err_window_ms >= 1000) {
    cur.state.err_window_ms = now;
    cur.state.err_count = 0;
  }
  if (cur.state.err_count >= limits::kDeviceErrorsPerS) {
    cur.state.counters.errors_dropped++;
    return cur;
  }
  cur.state.err_count++;
  LinkEffect e = reply_effect(ReplyKind::Error, re);
  e.code = code;
  if (proto::is_field_path(field.c_str(), field.size())) e.field = some(field);
  return reply_with(cur, e);
}

LinkStep host_hello(const LinkCore& c, const proto::HostHello& h, const MsgId& re, uint32_t now) {
  LinkStep s{c, {}};
  s.state.info.agent = h.agent;
  s.state.info.cap_spawn = h.cap_spawn;
  s.state.info.cap_reply = h.cap_reply;
  s.state.info.version_mismatch = false;
  s.state.info.mode = LinkMode::Ready;
  s.state.info.last_rx_ms = now;
  s = reply(s, ReplyKind::Hello, re);
  const bool again = c.info.mode == LinkMode::Ready;
  s.effects = s.effects.with(changed(c.info.mode, LinkMode::Ready, again));
  return s;
}

// A host-initiated message while Ready (hello is handled before).
LinkStep ready_request(const LinkStep& s, MsgType t, const MsgId& re, uint32_t now) {
  switch (t) {
    case MsgType::Ping: return reply(s, ReplyKind::Pong, re);
    case MsgType::TimeSet:
    case MsgType::ProjectsSnapshot:
    case MsgType::SessionsSnapshot:
    case MsgType::QueueSnapshot: {
      const LinkStep delivered{s.state, s.effects.with(deliver())};
      return reply(delivered, ReplyKind::Ack, re);
    }
    case MsgType::DebugKey:
    case MsgType::DebugReboot:
    case MsgType::DebugLang: {
      const LinkStep acked = reply(s, ReplyKind::Ack, re);
      return LinkStep{acked.state, acked.effects.with(deliver())};
    }
    case MsgType::DebugState: return reply(s, ReplyKind::DebugState, re);
    default: return error_reply(s, re, ErrCode::UnknownType, proto::FieldPath{}, now);
  }
}

LinkStep valid_line(const LinkCore& c, const Decoded& d, uint32_t now) {
  LinkStep s{c, {}};
  s.state.info.last_rx_ms = now;
  if (!d.reply_to.has) {  // a host reply: route it, never answer
    s.effects = s.effects.with(deliver());
    return s;
  }
  const auto* hello = std::get_if<proto::Hello>(&d.msg.body);
  if (hello && hello->from_host) return host_hello(c, hello->host, d.reply_to.value, now);
  if (c.info.mode != LinkMode::Ready) return error_reply(s, d.reply_to.value, ErrCode::NotReady, {}, now);
  return ready_request(s, proto::msg_type(d.msg), d.reply_to.value, now);
}

LinkStep failed_line(const LinkCore& c, const Decoded& d, uint32_t now) {
  LinkStep s{c, {}};
  if (d.code == ErrCode::Parse) {
    s.state.counters.parse++;
    return s;
  }
  if (d.code == ErrCode::TooLarge) {
    s.state.counters.too_large++;
    return s;
  }
  if (!d.reply_to.has) {  // bad id, or a broken reply: counted, never answered
    s.state.counters.bad++;
    return s;
  }
  if (d.code == ErrCode::Version) s.state.info.version_mismatch = true;
  return error_reply(s, d.reply_to.value, d.code, d.field, now);
}

}  // namespace

LinkStep link_line(const LinkCore& c, const Decoded& d, uint32_t now) {
  switch (d.outcome) {
    case Outcome::Ok: return valid_line(c, d, now);
    case Outcome::Noise: {
      LinkStep s{c, {}};
      s.state.counters.noise++;
      return s;
    }
    case Outcome::Ignored: {
      LinkStep s{c, {}};
      s.state.counters.ignored++;
      return s;
    }
    default: return failed_line(c, d, now);
  }
}

LinkStep link_tick(const LinkCore& c, bool plugged, uint32_t now) {
  if (!plugged) return with_mode(c, LinkMode::Detached);
  if (c.info.mode == LinkMode::Detached) return with_mode(c, LinkMode::Attached);
  if (c.info.mode == LinkMode::Ready && now - c.info.last_rx_ms >= limits::kLinkTimeoutMs) {
    return with_mode(c, LinkMode::Attached);
  }
  return LinkStep{c, {}};
}

IdIssue link_issue_id(const LinkCore& c) {
  char buf[16];
  const int n = std::snprintf(buf, sizeof buf, "d%lu", static_cast<unsigned long>(c.next_id));
  IdIssue out{c, MsgId::from(buf, n > 0 ? static_cast<size_t>(n) : 0)};
  out.core.next_id = c.next_id + 1;
  return out;
}

bool link_ready(const LinkCore& c) { return c.info.mode == LinkMode::Ready; }

bool link_lost_inflight(const LinkEffect& e) {
  return e.kind == LinkEffectKind::Changed && e.from == LinkMode::Ready && (e.to != LinkMode::Ready || e.rehandshake);
}

}  // namespace cpa
