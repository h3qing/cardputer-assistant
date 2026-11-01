#include "agent_req.h"

namespace cpa {
namespace {

AgentEvent event(AgentEventKind kind) {
  AgentEvent e;
  e.kind = kind;
  return e;
}

AgentStep finish(const AgentReqInfo& s, AgentOutcome outcome, const AgentEvent& ev) {
  AgentStep out{s, {}};
  out.state.phase = AgentPhase::Idle;
  out.state.outcome = outcome;
  out.effects = out.effects.with(ev);
  return out;
}

}  // namespace

AgentStep agent_send(const AgentReqInfo& s, const AgentDraft& draft, const MsgId& rid, uint32_t now) {
  AgentStep out{s, {}};
  if (s.phase == AgentPhase::Inflight) {
    out.effects = out.effects.with(event(AgentEventKind::Busy));
    return out;
  }
  out.state.phase = AgentPhase::Inflight;
  out.state.rid = rid;
  out.state.sent_ms = now;
  out.state.outcome = AgentOutcome::None;
  out.state.draft = draft;
  out.effects = out.effects.with(event(AgentEventKind::Send));
  return out;
}

AgentStep agent_result(const AgentReqInfo& s, const MsgId& re, bool ok, ErrCode code, const ShortId& short_id) {
  if (s.phase != AgentPhase::Inflight || s.rid != re) return AgentStep{s, {}};
  AgentEvent ev = event(ok ? AgentEventKind::Started : AgentEventKind::Failed);
  ev.code = ok ? ErrCode::None : code;
  ev.short_id = short_id;
  return finish(s, ok ? AgentOutcome::Started : AgentOutcome::Failed, ev);
}

AgentStep agent_tick(const AgentReqInfo& s, uint32_t now) {
  // Unsigned subtraction keeps the timeout correct across millis() wrap-around.
  if (s.phase != AgentPhase::Inflight || now - s.sent_ms < kAgentTimeoutMs) return AgentStep{s, {}};
  return finish(s, AgentOutcome::Unknown, event(AgentEventKind::Unknown));
}

AgentStep agent_link_lost(const AgentReqInfo& s) {
  if (s.phase != AgentPhase::Inflight) return AgentStep{s, {}};
  return finish(s, AgentOutcome::Unknown, event(AgentEventKind::Unknown));
}

uint8_t agent_inflight(const AgentReqInfo& s) { return s.phase == AgentPhase::Inflight ? 1 : 0; }

QueueBody agent_queue_draft(const AgentReqInfo& s) {
  QueueBody q;
  q.text = s.draft.text;
  q.project = s.draft.project;
  q.intent = s.draft.kind == AgentKind::Reply ? Intent::Feedback : Intent::Spawn;
  if (s.draft.kind == AgentKind::Reply) q.sid = some(s.draft.sid);
  q.uncertain = s.outcome != AgentOutcome::Failed;
  return q;
}

}  // namespace cpa
