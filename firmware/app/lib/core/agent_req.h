// Online agent request slice (05 §5.6, §6.3): agent.spawn / agent.reply.
// At most one in flight; never stored, never retried. No result within 30 s, or a
// link loss, means "result unknown": the text is saved as an uncertain queue item.
#pragma once

#include <cstdint>

#include "domain.h"
#include "err_code.h"
#include "step.h"

namespace cpa {

enum class AgentPhase : uint8_t { Idle, Inflight };
enum class AgentKind : uint8_t { Spawn, Reply };
enum class AgentOutcome : uint8_t { None, Started, Failed, Unknown };

struct AgentDraft {
  AgentKind kind = AgentKind::Spawn;
  ProjectKey project;
  Sid sid;          // Reply only
  Opt<Qid> qid;     // Spawn from a queue item
  UserText text;
};

// The draft is kept after the request ends so the shell can save it to the outbox.
struct AgentReqInfo {
  AgentPhase phase = AgentPhase::Idle;
  MsgId rid;
  uint32_t sent_ms = 0;
  AgentOutcome outcome = AgentOutcome::None;  // of the last request
  AgentDraft draft;
};

enum class AgentEventKind : uint8_t {
  Send,     // transmit the request (agent.spawn / agent.reply with id rid)
  Busy,     // another request is in flight: nothing sent
  Started,  // ok: toast "started · <short>"
  Failed,   // ok=false: save agent_queue_draft() (not uncertain), toast the code
  Unknown,  // timeout or link loss: save agent_queue_draft() (uncertain), toast
};

struct AgentEvent {
  AgentEventKind kind = AgentEventKind::Send;
  ErrCode code = ErrCode::None;
  ShortId short_id;
};

using AgentStep = Step<AgentReqInfo, AgentEvent>;

AgentStep agent_send(const AgentReqInfo& s, const AgentDraft& draft, const MsgId& rid, uint32_t now);
AgentStep agent_result(const AgentReqInfo& s, const MsgId& re, bool ok, ErrCode code, const ShortId& short_id);
AgentStep agent_tick(const AgentReqInfo& s, uint32_t now);
AgentStep agent_link_lost(const AgentReqInfo& s);
uint8_t agent_inflight(const AgentReqInfo& s);

// Queue item body for a failed or unknown request (uncertain unless it failed).
QueueBody agent_queue_draft(const AgentReqInfo& s);

}  // namespace cpa
