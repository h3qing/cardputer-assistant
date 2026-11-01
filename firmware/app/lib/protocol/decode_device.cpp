// Decoders for messages the device sends (pong, ack, error, outbox.item, agent.*,
// queue.done, inbox.dismiss). The device decodes them when it re-reads its own
// outbox files at boot, and the contract test checks them against the vectors.
#include "codec_internal.h"

namespace cpa {
namespace proto {
namespace {

Bad note_body(const Obj& b, NoteBody& n, Opt<DestId>& dest) {
  const auto text = req_text(b, "text", is_user_text);
  const auto project = opt_text(b, "project", is_project_key);
  const auto to = opt_text(b, "dest", is_dest);  // v0.4.1
  if (const FieldPath* bad = first_bad(text, project, to)) return some(*bad);
  place(n.text, text.value);
  n.project = fixed<12>(project.value);
  dest = fixed<24>(to.value);
  return {};
}

Bad queue_body(const Obj& b, QueueBody& q) {
  const auto text = req_text(b, "text", is_user_text);
  const auto project = req_text(b, "project", is_project_key);
  const auto intent = req_enum(b, "intent", kIntentNames, 3);
  const auto sid = opt_text(b, "sid", is_sid);
  const auto uncertain = opt_bool(b, "uncertain");
  if (const FieldPath* bad = first_bad(text, project, intent, sid, uncertain)) return some(*bad);
  if (intent.value == static_cast<uint8_t>(Intent::Feedback) && !sid.value.has) return some(path_key(b.base, "sid"));
  place(q.text, text.value);
  q.project = fixed<12>(project.value);
  q.intent = static_cast<Intent>(intent.value);
  q.sid = fixed<8>(sid.value);
  q.uncertain = uncertain.value.value_or(false);
  return {};
}

}  // namespace

Bad decode_pong(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto up = req_int(o, "up_ms", 0, limits::kU32Max);
  const auto pending = req_int(o, "pending", 0, limits::kCountMax);
  const auto revs = req_revs(o, "revs");
  const auto files = opt_files(o);  // v0.3 (docs/11 §5.2)
  if (const FieldPath* bad = first_bad(up, pending, revs, files)) return some(*bad);
  Pong& p = out.emplace<Pong>();
  p.up_ms = static_cast<uint32_t>(up.value);
  p.pending = static_cast<uint16_t>(pending.value);
  p.revs = revs.value;
  p.files = files.value;
  return {};
}

Bad decode_ack(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (!env.has) out.emplace<Ack>();
  return env;
}

Bad decode_error(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto code = opt_err(o, "code");
  const auto field = opt_text(o, "field", is_field_path);
  const auto msg = opt_text(o, "msg", is_err_msg);
  if (const FieldPath* bad = first_bad(code, field, msg)) return some(*bad);
  if (!code.value.has) return some(path_key(o.base, "code"));
  ErrorMsg& e = out.emplace<ErrorMsg>();
  e.code = code.value.value;
  e.field = fixed<48>(field.value);
  e.msg = fixed<80>(msg.value);
  return {};
}

Bad decode_outbox_item(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto id = req_text(o, "id", is_monoid_text);
  const auto kind = req_enum(o, "kind", kItemKindNames, 2);  // execution kinds are invalid here
  const auto at = req_obj(o, "at");
  const auto b = req_obj(o, "body");
  if (const FieldPath* bad = first_bad(id, kind, at, b)) return some(*bad);
  const auto up = req_int(at.value, "up_ms", 0, limits::kU32Max);
  const auto wall = opt_int(at.value, "wall_ms", limits::kEpochMsMin, limits::kJsonSafeIntMax);
  if (const FieldPath* bad = first_bad(up, wall)) return some(*bad);
  OutboxItem& item = out.emplace<OutboxItem>();
  item.kind = static_cast<ItemKind>(kind.value);
  item.up_ms = static_cast<uint32_t>(up.value);
  item.wall_ms = wall.value;
  return item.kind == ItemKind::Note ? note_body(b.value, item.note, item.dest) : queue_body(b.value, item.queue);
}

Bad decode_agent_spawn(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto project = req_text(o, "project", is_project_key);
  const auto text = req_text(o, "text", is_agent_text);
  const auto qid = opt_text(o, "qid", is_qid);
  if (const FieldPath* bad = first_bad(project, text, qid)) return some(*bad);
  AgentSpawn& s = out.emplace<AgentSpawn>();
  s.project = fixed<12>(project.value);
  place(s.text, text.value);
  s.qid = fixed<11>(qid.value);
  return {};
}

Bad decode_agent_reply(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto sid = req_text(o, "sid", is_sid);
  const auto text = req_text(o, "text", is_agent_text);
  if (const FieldPath* bad = first_bad(sid, text)) return some(*bad);
  AgentReply& r = out.emplace<AgentReply>();
  r.sid = fixed<8>(sid.value);
  place(r.text, text.value);
  return {};
}

Bad decode_queue_done(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto qid = req_text(o, "qid", is_qid);
  if (!qid.ok) return some(qid.error);
  out.emplace<QueueDone>().qid = fixed<11>(qid.value);
  return {};
}

Bad decode_inbox_dismiss(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto sid = req_text(o, "sid", is_sid);
  if (!sid.ok) return some(sid.error);
  out.emplace<InboxDismiss>().sid = fixed<8>(sid.value);
  return {};
}

}  // namespace proto
}  // namespace cpa
