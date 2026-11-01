// Runtime: inbound lines, the replies the link core asks for, and delivery of host
// messages to their AppState slices (05 §5.8, §6.3-§6.5).
#include <cstdio>

#include "mando_eggs.h"
#include "reducers.h"
#include "runtime.h"
#include "txt.h"

namespace cpa {

constexpr Txt kNeedsYou{"%s 等你", "%s needs you"};

using proto::Decoded;
using proto::MsgType;

namespace {

proto::StoreKind store_kind(Store s) {
  switch (s) {
    case Store::Sd: return proto::StoreKind::Sd;
    case Store::Internal: return proto::StoreKind::Int;
    default: return proto::StoreKind::None;
  }
}

}  // namespace

void Runtime::handle_line(const char* line, size_t n, uint32_t now) {
  // Bound by reference: the decode result lives in the codec until the next decode.
  const Decoded& d = codec_.decode(line, n);
  if (rec_request(d, now)) return;  // file.* / debug.rec / debug.mic / debug.codec (runtime_files.cpp)
  const LinkStep s = link_line(link_, d, now);
  link_ = s.state;
  for (uint8_t i = 0; i < s.effects.count; ++i) link_effect(s.effects.items[i], d, now);
  sync_link_slice();
}

void Runtime::link_effect(const LinkEffect& e, const Decoded& d, uint32_t now) {
  switch (e.kind) {
    case LinkEffectKind::Reply: {
      const auto* hello = std::get_if<proto::Hello>(&d.msg.body);
      if (e.reply == ReplyKind::Hello && hello && hello->from_host) kept_host(*hello);  // docs/28 S1a
      return send_reply(e, now);
    }
    case LinkEffectKind::Deliver: return deliver(d, now);
    default: return link_changed(e, &d, now);
  }
}

void Runtime::link_changed(const LinkEffect& e, const Decoded* d, uint32_t now) {
  if (link_lost_inflight(e)) {
    track_link_lost();
    agent_events(agent_link_lost(state().agent), now);
    small_.inflight = false;
    rec_link_lost();
    kept_link_lost();  // docs/28 S1a: the offline mark
  }
  const auto* hello = d ? std::get_if<proto::Hello>(&d->msg.body) : nullptr;
  if (e.to != LinkMode::Ready || !hello || !hello->from_host) return;
  sys_.host_app = hello->host.app;
  const auto& label = hello->host.host_label;  // docs/21 §4.2: the settings page names the Mac
  sys_.host_label = label.has ? label.value : FixedStr<24>{};
  kept_hello(*hello);  // docs/28 S1a: which Mac, and the plug-in hint
}

void Runtime::send_reply(const LinkEffect& e, uint32_t now) {
  out_.id = e.id;
  out_.re = some(e.re);
  fill_reply_body(e, now);
  transmit(TxClass::Reply);
}

void Runtime::fill_reply_body(const LinkEffect& e, uint32_t now) {
  switch (e.reply) {
    case ReplyKind::Hello: {
      auto& h = out_.body.emplace<proto::Hello>();
      h.from_host = false;
      h.device.fw = sys_.fw;
      h.device.dev = sys_.dev;
      h.device.boot = ids_.boot_id();
      h.device.up_ms = now;
      h.device.rst = sys_.rst;
      h.device.time = state().time.known;
      h.device.pending = outbox_.n;
      h.device.bat_mv = state().device.bat_mv;
      h.device.revs = revs_of(state());
      h.device.store = some(store_kind(sys_.store));  // v0.2 (09 §6)
      h.device.launcher = some(sys_.launcher);
      h.device.files = files_info();                  // v0.3, only when recording works
      return;
    }
    case ReplyKind::Pong: {
      auto& p = out_.body.emplace<proto::Pong>();
      p.up_ms = now;
      p.pending = outbox_.n;
      p.revs = revs_of(state());
      p.files = files_info();
      return;
    }
    case ReplyKind::Error: {
      auto& err = out_.body.emplace<proto::ErrorMsg>();
      err.code = e.code;
      err.field = e.field;
      return;
    }
#if CPA_TEST_HOOKS
    case ReplyKind::DebugState: return fill_debug_state(now);
#endif
    default: out_.body.emplace<proto::Ack>(); return;
  }
}

bool Runtime::transmit(TxClass cls) {
  const auto line = codec_.encode(out_);
  return line.ok && tx_.send(line.value.c_str(), line.value.size(), cls);
}

void Runtime::deliver(const Decoded& d, uint32_t now) {
  const proto::Body& b = d.msg.body;
  switch (proto::msg_type(d.msg)) {
    case MsgType::TimeSet: {
      const TimeData& t = std::get<TimeData>(b);
      update([&](AppState& s) { s.time = reduce_time(s.time, t, now); });
      return kept_time();  // docs/28 D16
    }
    case MsgType::ProjectsSnapshot:
    case MsgType::SessionsSnapshot:
    case MsgType::QueueSnapshot: return deliver_snapshot(b, now);
#if CPA_TEST_HOOKS
    case MsgType::DebugKey: inject_ = inject_push(inject_, std::get<proto::DebugKey>(b)); return;
    case MsgType::DebugReboot: return plat_.reboot_soon();
    case MsgType::DebugLang: shell_.setLanguage(std::get<proto::DebugLang>(b).lang); return;  // docs/21 §7
#endif
    default: return deliver_reply(d, now);
  }
}

void Runtime::deliver_snapshot(const proto::Body& b, uint32_t now) {
  if (const auto* p = std::get_if<ProjectsData>(&b)) {
    const auto st = reduce_projects(state().projects, *p, now);
    update([&](AppState& s) { s.projects = st.state; });
    if (st.effects.count > 0) save_projects_cache();
    return;
  }
  if (const auto* q = std::get_if<QueueData>(&b)) {
    const QueueView v = reduce_queue(state().queue, *q, now);
    return update([&](AppState& s) { s.queue = v; });
  }
  const uint16_t need = need_count(state().sessions);
  const auto st = reduce_sessions(state().sessions, std::get<SessionsData>(b), now);
  update([&](AppState& s) { s.sessions = st.state; });
  // docs/20 D7 (U11a): locked at the desk, more sessions need you: the card shows the count
  if (need_count(state().sessions) > need) shell_.lock_flash(now, kNeedsFlashMs);
  // v0.5.0 (mando_eggs.h): none needs you any more, and home is in front: Mando says so once,
  // not over a saved hop or his "?" still on show
  if (all_clear(need, need_count(state().sessions)) && shell_.on_home() && all_clear_may_show(state().ui, now)) {
    set_moment(Moment::AllClear);
  }
  for (uint8_t i = 0; i < st.effects.count; ++i) {
    const SessionRow& row = state().sessions.data.rows[st.effects.items[i].index];
    char buf[80];  // docs/20 U14: "key/slug" names are not given the key twice
    std::snprintf(buf, sizeof buf, tr(kNeedsYou), session_label(state().projects, row).c_str());
    shell_.notice(ToastLevel::Warn, buf);  // behind the user's own results (docs/20 U20)
  }
}

void Runtime::deliver_reply(const Decoded& d, uint32_t now) {
  if (!d.msg.re.has) return;
  const MsgId& re = d.msg.re.value;
  if (const auto* a = std::get_if<proto::OutboxAck>(&d.msg.body)) {
    const Opt<MonoId> id = mono_parse(re.c_str(), re.size());
    if (id.has) track_ack(id.value, a->status, a->code.value_or(ErrCode::None));
    return;
  }
  if (const auto* r = std::get_if<proto::AgentResult>(&d.msg.body)) {
    const ErrCode code = r->code.value_or(ErrCode::Internal);
    return agent_events(agent_result(state().agent, re, r->ok, code, r->short_id.value_or(ShortId{})), now);
  }
  small_reply(d);
}

}  // namespace cpa
