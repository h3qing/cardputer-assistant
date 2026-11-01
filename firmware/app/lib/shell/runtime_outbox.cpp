// Runtime: outbox items (write, send, ack), online requests and their results, and the
// /cfg/projects.json cache (05 §5.3, §5.6, §5.9).
#include <cstdio>

#include "errors.h"
#include "outbox_io.h"
#include "protocol_limits.h"
#include "reducers.h"
#include "runtime.h"
#include "store_commit.h"
#include "txt.h"

namespace cpa {
namespace {

constexpr char kCfgDir[] = "/cfg";
constexpr char kCacheTmp[] = "/cfg/projects.tmp";
constexpr char kCacheJson[] = "/cfg/projects.json";

char g_line[limits::kLineMax + 1];  // one stored line read back (outbox item, cache)

// docs/21: what a request's failure says. In English "<what>: <code>, queued" is whole in
// the 224 px toast with the longest code (E_UNKNOWN_SESSION), so <what> is 10 or fewer.
constexpr Txt kNoAgent{"Mac 上未找到 Claude Code", "No Claude Code"};
constexpr Txt kSpawnFailed{"Claude 启动失败", "Claude failed"};
constexpr Txt kNoProject{"项目不存在", "No project"};
constexpr Txt kNoSession{"session 不存在", "No session"};
constexpr Txt kCantReply{"无法回复", "Can't reply"};
constexpr Txt kReplyFailed{"回复失败", "Reply failed"};
constexpr Txt kBadText{"内容不合法", "Bad text"};
constexpr Txt kBusy{"已有请求在途", "Busy"};
constexpr Txt kFailed{"执行失败", "Failed"};
constexpr Txt kFailedCode{"%s（%s）%s", "%s: %s%s"};
constexpr Txt kQueuedTail{"，已排队", ", queued"};
constexpr Txt kRefused{"Mac 拒收了一条记录（%s）", "Mac refused it (%s)"};
constexpr Txt kStarted{"已开始", "Started"};
constexpr Txt kSent{"已发送", "Sent"};
constexpr Txt kUnknownResult{"结果未知，请在看板确认%s", "Unknown result: check board%s"};
constexpr Txt kSavedQueued{"（已存为排队）", " (queued)"};
constexpr Txt kRequestFailed{"操作失败（%s）", "Failed (%s)"};

const char* failure_text(ErrCode code) {
  switch (code) {
    case ErrCode::NoAgent: return tr(kNoAgent);
    case ErrCode::Spawn: return tr(kSpawnFailed);
    case ErrCode::UnknownProject: return tr(kNoProject);
    case ErrCode::UnknownSession: return tr(kNoSession);
    case ErrCode::NotReplyable: return tr(kCantReply);
    case ErrCode::ReplyForked: return tr(kReplyFailed);
    case ErrCode::BadField: return tr(kBadText);
    case ErrCode::Busy: return tr(kBusy);
    default: return tr(kFailed);
  }
}

}  // namespace

SaveResult Runtime::save_note(const NoteBody& body) { return save_item(ItemKind::Note, &body, nullptr); }
SaveResult Runtime::save_queue(const QueueBody& body) { return save_item(ItemKind::Queue, nullptr, &body); }

// A latched card (docs/20 U16) is checked before Full too: no sync frees a dead card. Its
// capture goes to the internal cpafs when there is one (save_spare).
SaveResult Runtime::save_item(ItemKind kind, const NoteBody* note, const QueueBody* queue) {
  if (sys_.store == Store::None) return SaveResult::NoStorage;  // before Full: never touch the store
  const bool lost = card_lost();
  if (lost) store_failed();
  if (!lost && !outbox_can_accept(outbox_, free_estimate())) return SaveResult::Full;
  const Opt<MonoId> next = ids_.next();  // shared with the audio task (docs/11 §4.4)
  if (!next.has) return SaveResult::Failed;
  auto& item = out_.body.emplace<proto::OutboxItem>();
  item.kind = kind;
  item.up_ms = now_;
  item.wall_ms = wall_ms(state().time, now_);
  if (note) item.note = *note;
  if (queue) item.queue = *queue;
  out_.id = MsgId::from(mono_format(next.value).c_str());
  out_.re = Opt<MsgId>{};
  if (lost || !encode_persist(next.value)) return card_lost() ? save_spare(next.value) : SaveResult::Failed;
  track_added(next.value);  // only now is it "saved" (05 §5.9)
  return SaveResult::Saved;
}

// out_ -> one outbox item in cpafs, which the next boot migrates to the card (09 §3.7)
// or sends itself when it boots without one.
SaveResult Runtime::save_spare(const MonoId& id) {
  FileStore* fs = spare_ ? spare_->mount() : nullptr;
  if (!fs) return SaveResult::CardLost;
  const auto line = codec_.encode(out_);
  if (!line.ok || !outbox_persist(*fs, id, line.value.c_str(), line.value.size())) return SaveResult::CardLost;
  return SaveResult::Spared;
}

// out_ -> one line -> committed as /outbox/<id>.json (store_commit.h: tmp -> rename on
// LittleFS, the final name and fsync on FAT).
bool Runtime::encode_persist(const MonoId& id) {
  const auto line = codec_.encode(out_);
  if (!line.ok) return false;
  if (outbox_persist(fs_, id, line.value.c_str(), line.value.size())) return true;
  store_failed();
  return false;
}

void Runtime::track_added(const MonoId& id) { apply_outbox(outbox_added(outbox_, id)); }

void Runtime::track_ack(const MonoId& id, proto::AckStatus status, ErrCode code) {
  apply_outbox(outbox_acked(outbox_, id, status, code));
}

void Runtime::track_link_lost() { apply_outbox(outbox_link_lost(outbox_)); }

void Runtime::pump_outbox(uint32_t now) {
  if (!link_ready(link_) || outbox_.n == 0) return;
  const Effects<OutboxEffect> sends = tick_outbox(now);
  for (uint8_t i = 0; i < sends.count; ++i) send_item(sends.items[i].id);
  if (sends.count > 0) update([&](AppState& s) { s.outbox = outbox_info(outbox_, free_estimate()); });
}

// outbox_tick() alone, so its large frame is gone before items are read and sent.
Effects<OutboxEffect> Runtime::tick_outbox(uint32_t now) {
  const OutboxStep s = outbox_tick(outbox_, now, true);
  outbox_ = s.state;
  return s.effects;
}

void Runtime::send_item(const MonoId& id) {
  const long n = outbox_read(fs_, id, g_line, sizeof g_line);
  if (n > 0) tx_.send(g_line, static_cast<size_t>(n), TxClass::Resendable);
}

void Runtime::apply_outbox(const OutboxStep& s) {
  outbox_ = s.state;
  for (uint8_t i = 0; i < s.effects.count; ++i) {
    const OutboxEffect& e = s.effects.items[i];
    if (e.kind == OutboxEffectKind::Send) {
      send_item(e.id);
    } else if (e.kind == OutboxEffectKind::Rejected) {
      char buf[64];
      std::snprintf(buf, sizeof buf, tr(kRefused), err_name(e.code));
      shell_.toast(ToastLevel::Error, buf);
    } else if (!outbox_apply(fs_, e)) {
      store_failed();
    }
  }
  update([&](AppState& s2) { s2.outbox = outbox_info(outbox_, free_estimate()); });
}

bool Runtime::send_agent(const AgentDraft& draft) {
  flush_agent_save();  // never let a new draft replace one still to be saved
  if (!link_ready(link_) || state().agent.phase == AgentPhase::Inflight) return false;
  const IdIssue id = link_issue_id(link_);
  link_ = id.core;
  agent_events(agent_send(state().agent, draft, id.id, now_), now_);
  return true;
}

void Runtime::agent_events(const AgentStep& s, uint32_t now) {
  if (s.effects.count == 0 && s.state.phase == state().agent.phase) return;
  update([&](AppState& st) { st.agent = s.state; });
  for (uint8_t i = 0; i < s.effects.count; ++i) agent_event(s.effects.items[i], now);
}

void Runtime::agent_event(const AgentEvent& e, uint32_t now) {
  (void)now;
  const AgentReqInfo& a = state().agent;
  char buf[96];
  switch (e.kind) {
    case AgentEventKind::Send: {
      out_.id = a.rid;
      out_.re = Opt<MsgId>{};
      if (a.draft.kind == AgentKind::Spawn) {
        auto& sp = out_.body.emplace<proto::AgentSpawn>();
        sp.project = a.draft.project;
        sp.text = a.draft.text;
        sp.qid = a.draft.qid;
      } else {
        auto& rp = out_.body.emplace<proto::AgentReply>();
        rp.sid = a.draft.sid;
        rp.text = a.draft.text;
      }
      transmit(TxClass::Reply);  // requests are never dropped either
      return;
    }
    case AgentEventKind::Started:
      std::snprintf(buf, sizeof buf, "%s · %s", tr(a.draft.kind == AgentKind::Spawn ? kStarted : kSent),
                    e.short_id.c_str());
      return shell_.toast(ToastLevel::Ok, buf);
    case AgentEventKind::Failed:
    case AgentEventKind::Unknown:
      agent_save_ = true;  // saved by flush_agent_save() from loop()
      agent_save_kind_ = e.kind;
      agent_save_code_ = e.code;
      return;
    default: return;
  }
}

void Runtime::flush_agent_save() {
  if (!agent_save_) return;
  agent_save_ = false;
  const AgentReqInfo& a = state().agent;
  // A spawn from a queue item leaves that item on the Mac: saving it again would
  // duplicate it (the daemon only marks it spawned on success).
  const bool from_item = a.draft.qid.has;
  const bool keep = !from_item && save_queue(agent_queue_draft(a)) == SaveResult::Saved;
  char buf[96];
  if (agent_save_kind_ == AgentEventKind::Unknown) {
    std::snprintf(buf, sizeof buf, tr(kUnknownResult), keep ? tr(kSavedQueued) : "");
  } else {
    std::snprintf(buf, sizeof buf, tr(kFailedCode), failure_text(agent_save_code_), err_name(agent_save_code_),
                  keep ? tr(kQueuedTail) : "");
  }
  shell_.toast(agent_save_kind_ == AgentEventKind::Unknown ? ToastLevel::Warn : ToastLevel::Error, buf);
}

bool Runtime::send_request(const OnlineRequest& r) {
  if (!link_ready(link_) || small_.inflight) return false;
  const IdIssue id = link_issue_id(link_);
  link_ = id.core;
  out_.id = id.id;
  out_.re = Opt<MsgId>{};
  if (r.kind == RequestKind::QueueDone) {
    out_.body.emplace<proto::QueueDone>().qid = r.qid;
  } else {
    out_.body.emplace<proto::InboxDismiss>().sid = r.sid;
  }
  if (!transmit(TxClass::Reply)) return false;
  small_.inflight = true;
  small_.rid = id.id;
  small_.sent_ms = now_;
  return true;
}

void Runtime::small_reply(const proto::Decoded& d) {
  if (!small_.inflight || d.msg.re.value != small_.rid) return;
  small_.inflight = false;
  const auto* err = std::get_if<proto::ErrorMsg>(&d.msg.body);
  if (!err) return;
  char buf[64];
  std::snprintf(buf, sizeof buf, tr(kRequestFailed), err_name(err->code));
  shell_.toast(ToastLevel::Error, buf);
}

// Slot -> key / name cache for offline targets (04 US-02 AC3): the projects.snapshot
// line itself, with counts and rev zeroed, committed by commit_file (tmp -> rename on
// LittleFS; written in place on FAT, where a torn file only means no cache: 09 §3.6).
void Runtime::save_projects_cache() {
  if (sys_.store == Store::None) return;
  auto& p = out_.body.emplace<ProjectsData>();
  p = state().projects.data;
  p.rev = 0;
  p.other = OtherCount{};
  for (uint8_t i = 0; i < p.n; ++i) p.rows[i].run = p.rows[i].need = p.rows[i].queued = 0;
  out_.id = MsgId::from("cache");
  out_.re = Opt<MsgId>{};
  const auto line = codec_.encode(out_);
  if (!line.ok) return;
  fs_.mkdir(kCfgDir);
  if (!commit_file(fs_, kCacheJson, kCacheTmp, line.value.c_str(), line.value.size())) store_failed();
}

void Runtime::load_projects_cache() {
  const long n = fs_.read(kCacheJson, g_line, sizeof g_line);
  if (n <= 0) return;
  const proto::Decoded& d = codec_.decode(g_line, static_cast<size_t>(n));
  if (d.outcome != proto::Outcome::Ok || proto::msg_type(d.msg) != proto::MsgType::ProjectsSnapshot) return;
  update([&](AppState& s) {
    s.projects.live = false;
    s.projects.rx_ms = 0;
    s.projects.data = std::get<ProjectsData>(d.msg.body);
    s.projects.data.rev = 0;  // the daemon must push a live snapshot
  });
}

}  // namespace cpa
