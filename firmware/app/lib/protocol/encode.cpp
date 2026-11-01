// Encoders: write the payload fields of each message type after the envelope.
// Absent optionals are omitted, except outbox.item at.wall_ms which is sent as
// null when the device does not know the time (05 §6.6).
#include "codec_internal.h"
#include "errors.h"

namespace cpa {
namespace proto {
namespace {

template <size_t N>
void put(JsonObject out, const char* key, const FixedStr<N>& s) {
  out[key] = JsonString(s.data(), s.size());
}

template <size_t N>
void put(JsonObject out, const char* key, const Opt<FixedStr<N>>& s) {
  if (s.has) put(out, key, s.value);
}

void put_revs(JsonObject out, const Revs& r) {
  JsonObject o = out["revs"].to<JsonObject>();
  o["projects"] = r.projects;
  o["sessions"] = r.sessions;
  o["queue"] = r.queue;
}

void put_err(JsonObject out, const Opt<ErrCode>& code) {
  if (code.has) out["code"] = err_name(code.value);
}

void encode_hello(const Hello& h, JsonObject out) {
  out["role"] = h.from_host ? "host" : "device";
  if (h.from_host) {
    put(out, "app", h.host.app);
    out["agent"] = wire_name(kAgentStatusNames, h.host.agent);
    JsonObject caps = out["caps"].to<JsonObject>();
    caps["spawn"] = h.host.cap_spawn;
    caps["reply"] = h.host.cap_reply;
    if (h.host.cap_recs.has) caps["recs"] = h.host.cap_recs.value;  // v0.4.1: unset = v0.4.0 bytes
    if (h.host.cap_todo.has) caps["todo"] = h.host.cap_todo.value;
    put(out, "host_id", h.host.host_id);
    return put(out, "host_label", h.host.host_label);
  }
  put(out, "fw", h.device.fw);
  put(out, "dev", h.device.dev);
  put(out, "boot", h.device.boot);
  out["up_ms"] = h.device.up_ms;
  put(out, "rst", h.device.rst);
  out["time"] = h.device.time;
  out["pending"] = h.device.pending;
  out["bat_mv"] = h.device.bat_mv;
  put_revs(out, h.device.revs);
  if (h.device.store.has) out["store"] = wire_name(kStoreKindNames, h.device.store.value);
  if (h.device.launcher.has) out["launcher"] = h.device.launcher.value;
  put_files(out, h.device.files);
}

void encode_projects(const ProjectsData& d, JsonObject out) {
  out["rev"] = d.rev;
  JsonArray arr = out["projects"].to<JsonArray>();
  for (uint8_t i = 0; i < d.n; ++i) {
    const ProjectRow& r = d.rows[i];
    JsonObject p = arr.add<JsonObject>();
    p["slot"] = r.slot;
    put(p, "key", r.key);
    put(p, "name", r.name);
    p["run"] = r.run;
    p["need"] = r.need;
    p["queued"] = r.queued;
    p["ok"] = r.ok;
  }
  JsonObject other = out["other"].to<JsonObject>();
  other["run"] = d.other.run;
  other["n"] = d.other.n;
}

void encode_sessions(const SessionsData& d, JsonObject out) {
  out["rev"] = d.rev;
  out["total"] = d.total;
  JsonArray arr = out["sessions"].to<JsonArray>();
  for (uint8_t i = 0; i < d.n; ++i) {
    const SessionRow& r = d.rows[i];
    JsonObject s = arr.add<JsonObject>();
    put(s, "sid", r.sid);
    s["slot"] = r.slot;
    put(s, "name", r.name);
    s["kind"] = wire_name(kSessionKindNames, r.kind);
    s["st"] = wire_name(kSessionStateNames, r.st);
    s["need"] = r.need;
    s["age_s"] = r.age_s;
    put(s, "short", r.short_id);
  }
}

void encode_queue(const QueueData& d, JsonObject out) {
  out["rev"] = d.rev;
  JsonArray arr = out["items"].to<JsonArray>();
  for (uint8_t i = 0; i < d.n; ++i) {
    const QueueRow& r = d.rows[i];
    JsonObject q = arr.add<JsonObject>();
    put(q, "qid", r.qid);
    q["slot"] = r.slot;
    put(q, "text", r.text);
    q["intent"] = wire_name(kIntentNames, r.intent);
    put(q, "sid", r.sid);
    if (r.uncertain) q["uncertain"] = true;  // v0.4.1: unset = the v0.4.0 bytes
  }
}

void encode_item_body(const OutboxItem& item, JsonObject body) {
  if (item.kind == ItemKind::Note) {
    put(body, "text", item.note.text);
    put(body, "project", item.note.project);
    return put(body, "dest", item.dest);  // v0.4.1: unset = the v0.3 bytes
  }
  put(body, "text", item.queue.text);
  put(body, "project", item.queue.project);
  body["intent"] = wire_name(kIntentNames, item.queue.intent);
  put(body, "sid", item.queue.sid);
  if (item.queue.uncertain) body["uncertain"] = true;
}

void encode_outbox_item(const OutboxItem& item, JsonObject out) {
  out["kind"] = wire_name(kItemKindNames, item.kind);
  JsonObject at = out["at"].to<JsonObject>();
  at["up_ms"] = item.up_ms;
  if (item.wall_ms.has) {
    at["wall_ms"] = item.wall_ms.value;
  } else {
    at["wall_ms"] = nullptr;
  }
  encode_item_body(item, out["body"].to<JsonObject>());
}

void encode_small(const Body& body, JsonObject out) {
  if (const auto* p = std::get_if<Pong>(&body)) {
    out["up_ms"] = p->up_ms;
    out["pending"] = p->pending;
    put_revs(out, p->revs);
    return put_files(out, p->files);
  }
  if (const auto* e = std::get_if<ErrorMsg>(&body)) {
    out["code"] = err_name(e->code);
    put(out, "field", e->field);
    return put(out, "msg", e->msg);
  }
  if (const auto* t = std::get_if<TimeData>(&body)) {
    out["epoch_ms"] = t->epoch_ms;
    out["tz_min"] = t->tz_min;
    return;
  }
  if (const auto* a = std::get_if<OutboxAck>(&body)) {
    out["status"] = wire_name(kAckStatusNames, a->status);
    return put_err(out, a->code);
  }
  if (const auto* q = std::get_if<QueueDone>(&body)) return put(out, "qid", q->qid);
  if (const auto* d = std::get_if<InboxDismiss>(&body)) return put(out, "sid", d->sid);
}

void encode_agent(const Body& body, JsonObject out) {
  if (const auto* s = std::get_if<AgentSpawn>(&body)) {
    put(out, "project", s->project);
    put(out, "text", s->text);
    return put(out, "qid", s->qid);
  }
  if (const auto* r = std::get_if<AgentReply>(&body)) {
    put(out, "sid", r->sid);
    return put(out, "text", r->text);
  }
  if (const auto* res = std::get_if<AgentResult>(&body)) {
    out["ok"] = res->ok;
    put_err(out, res->code);
    put(out, "name", res->name);
    put(out, "short", res->short_id);
  }
}

}  // namespace

void encode_payload(const Body& body, JsonObject out) {
  if (const auto* h = std::get_if<Hello>(&body)) return encode_hello(*h, out);
  if (const auto* p = std::get_if<ProjectsData>(&body)) return encode_projects(*p, out);
  if (const auto* s = std::get_if<SessionsData>(&body)) return encode_sessions(*s, out);
  if (const auto* q = std::get_if<QueueData>(&body)) return encode_queue(*q, out);
  if (const auto* i = std::get_if<OutboxItem>(&body)) return encode_outbox_item(*i, out);
  encode_small(body, out);
  encode_agent(body, out);
  encode_file(body, out);
  encode_rec(body, out);
  encode_dest(body, out);
#if CPA_TEST_HOOKS
  encode_debug(body, out);
  encode_debug_rec(body, out);
  encode_debug_mic(body, out);
  encode_debug_codec(body, out);
#endif
}

}  // namespace proto
}  // namespace cpa
