#include "confirm.h"

#include <cstdio>

#include "keymap.h"
#include "reducers.h"
#include "txt.h"

namespace cpa {
namespace {

// docs/21: a reason is short, so "<reason>: <what happens>" fits one 232 px row and the
// 224 px toast (test_text_width draws each with the longest reason).
constexpr Txt kOffline{"离线", "Offline"};
constexpr Txt kNoAgent{"没有 Claude Code", "No Claude Code"};
constexpr Txt kInteractive{"终端里的 session", "Terminal session"};
constexpr Txt kNotIdle{"session 正在运行", "Session busy"};
constexpr Txt kNoReply{"回复功能未开启", "Replies off"};
constexpr Txt kSessionGone{"session 已不在列表", "Session gone"};
constexpr Txt kWaitingAnswer{"session 在等权限或回答", "Session waiting"};
constexpr Txt kSpawnIn{"在 %s 开 session", "New session in %s"};
constexpr Txt kSendTo{"直接发送 → %s", "Send now → %s"};
constexpr Txt kCantStart{"%s：开不了，仍在队列", "%s: not run, queued"};
constexpr Txt kQueueOnly{"%s：只存到 Mac 队列", "%s: Mac queue only"};
constexpr LineTxt kMaybeOpen{"可能已开过，先看 Sessions", "May have started: see Sessions"};
constexpr LineTxt kCloseKeys{"Enter 关闭    ` 取消", "[Enter]close    [`]cancel"};
constexpr LineTxt kStartKeys{"Enter 开始    ` 取消", "[Enter]start    [`]cancel"};
constexpr LineTxt kStartEditKeys{"Enter 开始    ` 返回修改", "[Enter]start    [`]edit"};
constexpr LineTxt kQueueKeys{"Enter 存为排队    ` 取消", "[Enter]save as queued    [`]cancel"};
constexpr LineTxt kQueueEditKeys{"Enter 存为排队    ` 返回修改", "[Enter]save as queued    [`]edit"};
constexpr ToastTxt kQueuedNotSent{"已存到 Mac 队列（不会发给 session）", "Queued on the Mac (not sent)"};
constexpr Txt kQueuedWhy{"%s：已存到 Mac 队列", "%s: queued on Mac"};
constexpr Txt kUnrun{"%s：未执行，仍在队列", "%s: not run, queued"};

Decision decide(Route route, QueueReason reason) { return Decision{route, reason}; }

const SessionRow* find_session(const AppState& s, const Sid& sid) {
  for (uint8_t i = 0; i < s.sessions.data.n; ++i) {
    if (s.sessions.data.rows[i].sid == sid) return &s.sessions.data.rows[i];
  }
  return nullptr;
}

// docs/20 U25: the item was saved after an Opt+S whose result never came back.
bool item_uncertain(const AppState& s, const Opt<Qid>& qid) {
  for (uint8_t i = 0; qid.has && i < s.queue.data.n; ++i) {
    if (s.queue.data.rows[i].qid == qid.value) return s.queue.data.rows[i].uncertain;
  }
  return false;
}

Decision decide_spawn(const ConfirmSpec& spec, const AppState& s) {
  const bool ready = s.link.mode == LinkMode::Ready;
  const bool agent = s.link.agent == AgentStatus::Ok && s.link.cap_spawn;
  if (ready && agent) return Decision{Route::Execute, QueueReason::None, item_uncertain(s, spec.qid)};
  const QueueReason why = ready ? QueueReason::NoAgent : QueueReason::Offline;
  // An existing queue item stays on the Mac: queueing it again would duplicate it.
  return decide(spec.qid.has ? Route::Unavailable : Route::Queue, why);
}

Decision decide_reply(const ConfirmSpec& spec, const AppState& s) {
  if (s.link.mode != LinkMode::Ready) return decide(Route::Queue, QueueReason::Offline);
  const SessionRow* row = spec.sid.has ? find_session(s, spec.sid.value) : nullptr;
  if (!row) return decide(Route::Queue, QueueReason::SessionGone);
  if (row->kind != SessionKind::Bg) return decide(Route::Queue, QueueReason::Interactive);
  if (row->st == SessionState::Waiting) return decide(Route::Queue, QueueReason::Waiting);
  if (row->st != SessionState::Idle) return decide(Route::Queue, QueueReason::NotIdle);
  if (!s.link.cap_reply) return decide(Route::Queue, QueueReason::NoReply);
  return decide(Route::Execute, QueueReason::None);
}

}  // namespace

Decision confirm_decide(const ConfirmSpec& spec, const AppState& s) {
  return spec.kind == ConfirmKind::Spawn ? decide_spawn(spec, s) : decide_reply(spec, s);
}

// docs/20 U7: short (a title is "<reason>：<what happens>" in one 240 px row, its toast
// whole in the 224 px toast capsule, B6), and true for this version: a terminal session
// takes no reply from the device at all.
const char* queue_reason_text(QueueReason r) {
  switch (r) {
    case QueueReason::Offline: return tr(kOffline);
    case QueueReason::NoAgent: return tr(kNoAgent);
    case QueueReason::Interactive: return tr(kInteractive);
    case QueueReason::NotIdle: return tr(kNotIdle);
    case QueueReason::NoReply: return tr(kNoReply);
    case QueueReason::SessionGone: return tr(kSessionGone);
    case QueueReason::Waiting: return tr(kWaitingAnswer);
    default: return "";
  }
}

ConfirmText confirm_title(ConfirmKind k, const Decision& d, const char* target) {
  char line[160];
  if (d.route == Route::Execute && k == ConfirmKind::Spawn) {
    std::snprintf(line, sizeof line, tr(kSpawnIn), target);
  } else if (d.route == Route::Execute) {
    std::snprintf(line, sizeof line, tr(kSendTo), target);
  } else if (d.route == Route::Unavailable) {
    std::snprintf(line, sizeof line, tr(kCantStart), queue_reason_text(d.reason));
  } else {  // what the queue path does, plainly: nothing reaches the session
    std::snprintf(line, sizeof line, tr(kQueueOnly), queue_reason_text(d.reason));
  }
  return ConfirmText::from(line);
}

// docs/20 U25: a row of its own, so the title still says what Enter does, and where.
const char* confirm_note(const Decision& d) { return d.maybe_open ? tr(kMaybeOpen) : ""; }

const char* confirm_keys(const Decision& d, bool draft) {
  if (d.route == Route::Unavailable) return tr(kCloseKeys);  // a queue item: no draft
  if (d.route == Route::Execute) return tr(draft ? kStartEditKeys : kStartKeys);
  return tr(draft ? kQueueEditKeys : kQueueKeys);
}

ConfirmText confirm_queued_text(ConfirmKind k, const char* why) {
  if (k == ConfirmKind::Feedback) return ConfirmText::from(tr(kQueuedNotSent));
  char buf[160];
  std::snprintf(buf, sizeof buf, tr(kQueuedWhy), why);
  return ConfirmText::from(buf);
}

ConfirmText confirm_unrun_text(QueueReason r) {
  char buf[96];
  std::snprintf(buf, sizeof buf, tr(kUnrun), queue_reason_text(r));
  return ConfirmText::from(buf);
}

ConfirmResult confirm_key(const ConfirmState& s, const KeyEvent& ev, uint32_t now) {
  if (now - s.opened_ms < kConfirmGuardMs) return ConfirmResult::Ignored;
  const Resolved r = keymap_lookup(Layer::Confirm, ev);
  if (r.action == Action::Confirm) return ConfirmResult::Accepted;
  if (r.action == Action::Cancel) return ConfirmResult::Cancelled;
  return ConfirmResult::None;
}

Opt<QueueBody> confirm_queue_body(const ConfirmSpec& spec, const AppState& s) {
  const Opt<ProjectRow> project = project_by_slot(s.projects, spec.slot);
  if (!project.has) return {};
  QueueBody q;
  q.text = spec.text;
  q.project = project.value.key;
  q.intent = spec.kind == ConfirmKind::Spawn ? Intent::Spawn : Intent::Feedback;
  if (spec.kind == ConfirmKind::Feedback) q.sid = spec.sid;
  return some(q);
}

}  // namespace cpa
