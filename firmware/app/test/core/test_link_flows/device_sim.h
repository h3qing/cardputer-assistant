// DeviceSim: the pure cores (LinkSession, OutboxCore, agent slice, reducers) wired
// the way the WP4 shell will wire them, driven by the flow vectors. Every line the
// device "sends" goes through Codec::encode and back through decode, so only
// contract-valid lines can come out.
#pragma once

#include <string>
#include <vector>

#include "app_state.h"
#include "codec.h"
#include "link_session.h"
#include "mono_id.h"
#include "outbox_core.h"
#include "outbox_io.h"
#include "reducers.h"
#include "support/fake_store.h"

namespace flowsim {

using namespace cpa;
using namespace cpa::proto;

struct Out {
  Message msg;
  uint32_t t = 0;
  bool matched = false;
  bool from_flow = false;  // a seeded outbox item: compare its id too
};

class DeviceSim {
 public:
  std::vector<Out> outs;
  OutboxCore outbox;
  LinkCore link;
  AppState app;
  bool delivered[3] = {};  // projects, sessions, queue snapshots received since boot

  explicit DeviceSim(bool ready) {
    boot(12);
    link = link_tick(LinkCore{}, true, 0).state;
    if (ready) {
      link.info.mode = LinkMode::Ready;
      link.info.agent = AgentStatus::Ok;
      link.info.cap_spawn = true;
    }
  }

  // Timers run only when time advances. Flows leave out the 5 s pings, so the 15 s
  // link timeout is not simulated here (test_link_session covers it).
  void tick(uint32_t now) {
    if (ticked_ && now <= last_tick_) return;
    ticked_ = true;
    last_tick_ = now;
    apply_link(link_tick(link, plugged_, link.info.last_rx_ms), now);
    agent_events(agent_tick(app.agent, now), now);
    pump_outbox(now);
  }

  void pump_outbox(uint32_t now) {
    const OutboxStep s = outbox_tick(outbox, now, link_ready(link));
    outbox = s.state;
    for (uint8_t i = 0; i < s.effects.count; ++i) send_item(s.effects.items[i].id, now);
  }

  void host_line(const std::string& raw, uint32_t now) {
    const Decoded d = codec_.decode(raw.data(), raw.size());
    const LinkStep s = link_line(link, d, now);
    link = s.state;
    for (uint8_t i = 0; i < s.effects.count; ++i) link_effect(s.effects.items[i], d, now);
  }

  void user_spawn(const char* project, const char* text, uint32_t now) {
    AgentDraft draft;
    draft.project = ProjectKey::from(project);
    draft.text = UserText::from(text);
    if (!link_ready(link) || link.info.agent != AgentStatus::Ok || !link.info.cap_spawn) {
      QueueBody q;  // not Ready: the confirm box stores a queue item instead (05 §5.6)
      q.text = draft.text;
      q.project = draft.project;
      q.intent = Intent::Spawn;
      save_queue(q, now);
      return;
    }
    const IdIssue rid = link_issue_id(link);
    link = rid.core;
    const AgentStep s = agent_send(app.agent, draft, rid.id, now);
    app.agent = s.state;
    AgentSpawn spawn;
    spawn.project = draft.project;
    spawn.text = draft.text;
    emit(make_message(rid.id, {}, spawn), now);
  }

  void reboot(uint32_t now) {
    boot(boot_n_ + 1);
    link = link_tick(LinkCore{}, plugged_, now).state;
    app = AppState{};
    delivered[0] = delivered[1] = delivered[2] = false;
  }

  void detach(uint32_t now) {
    plugged_ = false;
    apply_link(link_tick(link, plugged_, now), now);
  }

  void seed(const Message& item, uint32_t now) {
    const auto line = codec_.encode(item);
    const auto id = mono_parse(item.id.c_str(), item.id.size());
    if (!line.ok || !id.has || !outbox_persist(store_, id.value, line.value.c_str(), line.value.size())) return;
    outbox = outbox_added(outbox, id.value).state;
    seeded_.push_back(item.id.c_str());
    pump_outbox(now);  // the loop sends a fresh item right away
  }

  bool is_seeded(const MsgId& id) const {
    for (const auto& s : seeded_) {
      if (id.equals(s.c_str())) return true;
    }
    return false;
  }

 private:
  testsupport::FakeStore store_;
  Codec codec_;
  MonoSeq seq_;
  uint32_t boot_n_ = 0;
  bool plugged_ = true;
  bool ticked_ = false;
  uint32_t last_tick_ = 0;
  std::vector<std::string> seeded_;

  void boot(uint32_t n) {
    boot_n_ = n;
    const BootRecord rec{mono_parse("3QZK1M7-0000C-0000", 18).value.epoch, n};
    seq_ = mono_seek_for_test(mono_start(boot_ticket(rec, rec).value), 100);
    outbox = outbox_boot_scan(store_, codec_).core;
  }

  void emit(const Message& m, uint32_t now) {
    const auto line = codec_.encode(m);
    const Decoded back = line.ok ? codec_.decode(line.value.c_str(), line.value.size()) : Decoded{};
    Out o;
    o.msg = back.msg;
    o.t = now;
    o.from_flow = back.outcome == Outcome::Ok && is_seeded(back.msg.id);
    if (back.outcome == Outcome::Ok) outs.push_back(o);
  }

  void send_item(const MonoId& id, uint32_t now) {
    char buf[2100];
    const long n = outbox_read(store_, id, buf, sizeof buf);
    const Decoded d = codec_.decode(buf, n > 0 ? static_cast<size_t>(n) : 0);
    if (d.outcome == Outcome::Ok) emit(d.msg, now);
  }

  void save_queue(const QueueBody& body, uint32_t now) {
    const MonoNext next = mono_next(seq_);
    seq_ = next.seq;
    OutboxItem item;
    item.kind = ItemKind::Queue;
    item.up_ms = now;
    item.queue = body;
    const auto line = codec_.encode(make_message(MsgId::from(mono_format(next.id.value).c_str()), {}, item));
    if (outbox_persist(store_, next.id.value, line.value.c_str(), line.value.size())) {
      outbox = outbox_added(outbox, next.id.value).state;
    }
  }

  void agent_events(const AgentStep& s, uint32_t now) {
    app.agent = s.state;
    for (uint8_t i = 0; i < s.effects.count; ++i) {
      const AgentEventKind k = s.effects.items[i].kind;
      if (k == AgentEventKind::Failed || k == AgentEventKind::Unknown) save_queue(agent_queue_draft(app.agent), now);
    }
  }

  void apply_link(const LinkStep& s, uint32_t now) {
    link = s.state;
    for (uint8_t i = 0; i < s.effects.count; ++i) {
      if (!link_lost_inflight(s.effects.items[i])) continue;
      outbox = outbox_link_lost(outbox).state;
      agent_events(agent_link_lost(app.agent), now);
    }
  }

  void link_effect(const LinkEffect& e, const Decoded& d, uint32_t now) {
    if (e.kind == LinkEffectKind::Changed) return apply_link(LinkStep{link, Effects<LinkEffect>{}.with(e)}, now);
    if (e.kind == LinkEffectKind::Deliver) return deliver(d, now);
    emit(make_message(e.id, some(e.re), reply_body(e, now)), now);
  }

  Body reply_body(const LinkEffect& e, uint32_t now) const {
    switch (e.reply) {
      case ReplyKind::Hello: return device_hello(now);
      case ReplyKind::Pong: {
        Pong p;
        p.up_ms = now;
        p.pending = outbox.n;
        p.revs = revs_of(app);
        return p;
      }
      case ReplyKind::Error: {
        ErrorMsg err;
        err.code = e.code;
        err.field = e.field;
        return err;
      }
      default: return Ack{};
    }
  }

  Body device_hello(uint32_t now) const {
    Hello h;
    h.from_host = false;
    h.device.fw = FixedStr<31>::from("cpa-fw/0.1.0");
    h.device.dev = FixedStr<12>::from("020000000001");
    h.device.boot = mono_boot_id(seq_);
    h.device.up_ms = now;
    h.device.rst = FixedStr<16>::from("poweron");
    h.device.time = app.time.known;
    h.device.pending = outbox.n;
    h.device.bat_mv = 4150;
    h.device.revs = revs_of(app);
    return h;
  }

  void deliver(const Decoded& d, uint32_t now) {
    const Body& b = d.msg.body;
    if (const auto* t = std::get_if<TimeData>(&b)) app.time = reduce_time(app.time, *t, now);
    if (const auto* p = std::get_if<ProjectsData>(&b)) {
      app.projects = reduce_projects(app.projects, *p, now).state;
      delivered[0] = true;
    }
    if (const auto* s = std::get_if<SessionsData>(&b)) {
      app.sessions = reduce_sessions(app.sessions, *s, now).state;
      delivered[1] = true;
    }
    if (const auto* q = std::get_if<QueueData>(&b)) {
      app.queue = reduce_queue(app.queue, *q, now);
      delivered[2] = true;
    }
    if (const auto* a = std::get_if<OutboxAck>(&b)) acked(d.msg.re.value, *a);
    if (const auto* r = std::get_if<AgentResult>(&b)) {
      agent_events(agent_result(app.agent, d.msg.re.value, r->ok, r->code.value_or(ErrCode::None), r->short_id.value), now);
    }
  }

  void acked(const MsgId& re, const OutboxAck& a) {
    const auto id = mono_parse(re.c_str(), re.size());
    if (!id.has) return;
    const OutboxStep s = outbox_acked(outbox, id.value, a.status, a.code.value_or(ErrCode::None));
    outbox = s.state;
    for (uint8_t i = 0; i < s.effects.count; ++i) outbox_apply(store_, s.effects.items[i]);
  }
};

}  // namespace flowsim
