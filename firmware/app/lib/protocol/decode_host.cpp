// Decoders for messages the host sends to the device (hello in both directions,
// time.set, the three snapshots, outbox.ack, agent.result). Schemas: protocol/schema/v0.
#include "codec_internal.h"

namespace cpa {
namespace proto {
namespace {

const char* const kRoleNames[2] = {"host", "device"};

// The optional v0.4.1 fields (docs/13 §11.1), read after the v0.1 ones; unknown caps are
// ignored. Not inlined, like device_hello_v02, to keep decode_host_hello's frame small.
__attribute__((noinline)) Bad host_hello_v041(const Obj& o, const Obj& caps, HostHello& h) {
  const auto recs = opt_bool(caps, "recs");
  const auto todo = opt_bool(caps, "todo");
  const auto host_id = opt_text(o, "host_id", is_host_id);
  const auto label = opt_text(o, "host_label", is_host_label);
  if (const FieldPath* bad = first_bad(recs, todo, host_id, label)) return some(*bad);
  h.cap_recs = recs.value;
  h.cap_todo = todo.value;
  h.host_id = fixed<32>(host_id.value);
  h.host_label = fixed<24>(label.value);
  return {};
}

Bad decode_host_hello(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto app = req_text(o, "app", is_app);
  const auto agent = req_enum(o, "agent", kAgentStatusNames, 3);
  const auto caps = req_obj(o, "caps");
  if (const FieldPath* bad = first_bad(app, agent, caps)) return some(*bad);
  const auto spawn = req_bool(caps.value, "spawn");
  const auto reply = req_bool(caps.value, "reply");
  if (const FieldPath* bad = first_bad(spawn, reply)) return some(*bad);
  Hello& h = out.emplace<Hello>();
  h.from_host = true;
  h.host.app = fixed<32>(app.value);
  h.host.agent = static_cast<AgentStatus>(agent.value);
  h.host.cap_spawn = spawn.value;
  h.host.cap_reply = reply.value;
  return host_hello_v041(o, caps.value, h.host);
}

// The optional v0.2 fields (09 §6), read after the v0.1 ones. Not inlined: their results
// would otherwise sit in decode_device_hello's frame, on the stack gate's hello chain.
__attribute__((noinline)) Bad device_hello_v02(const Obj& o, DeviceHello& d) {
  const auto store = opt_enum(o, "store", kStoreKindNames, 3);
  const auto launcher = opt_bool(o, "launcher");
  const auto files = opt_files(o);  // v0.3 (docs/11 §5.2)
  if (const FieldPath* bad = first_bad(store, launcher, files)) return some(*bad);
  if (store.value.has) d.store = some(static_cast<StoreKind>(store.value.value));
  d.launcher = launcher.value;
  d.files = files.value;
  return {};
}

Bad decode_device_hello(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto fw = req_text(o, "fw", is_fw);
  const auto dev = req_text(o, "dev", is_dev);
  const auto boot = req_text(o, "boot", is_boot_text);
  const auto up = req_int(o, "up_ms", 0, limits::kU32Max);
  const auto rst = req_text(o, "rst", is_rst);
  const auto time = req_bool(o, "time");
  const auto pending = req_int(o, "pending", 0, limits::kCountMax);
  const auto bat = req_int(o, "bat_mv", 0, limits::kCountMax);
  const auto revs = req_revs(o, "revs");
  if (const FieldPath* bad = first_bad(fw, dev, boot, up, rst, time, pending, bat, revs)) return some(*bad);
  Hello& h = out.emplace<Hello>();
  h.from_host = false;
  h.device.fw = fixed<31>(fw.value);
  h.device.dev = fixed<12>(dev.value);
  h.device.boot = fixed<kBootIdLen>(boot.value);
  h.device.up_ms = static_cast<uint32_t>(up.value);
  h.device.rst = fixed<16>(rst.value);
  h.device.time = time.value;
  h.device.pending = static_cast<uint16_t>(pending.value);
  h.device.bat_mv = static_cast<uint16_t>(bat.value);
  h.device.revs = revs.value;
  return device_hello_v02(o, h.device);
}

Dec<ProjectRow> project_row(const Obj& o) {
  const auto slot = req_int(o, "slot", kSlotMin, kSlotMax);
  const auto key = req_text(o, "key", is_project_key);
  const auto name = req_text(o, "name", is_project_name);
  const auto run = req_int(o, "run", 0, limits::kCountMax);
  const auto need = req_int(o, "need", 0, limits::kCountMax);
  const auto queued = req_int(o, "queued", 0, limits::kCountMax);
  const auto ok = req_bool(o, "ok");
  if (const FieldPath* bad = first_bad(slot, key, name, run, need, queued, ok)) return fail_at<ProjectRow>(*bad);
  ProjectRow r;
  r.slot = static_cast<uint8_t>(slot.value);
  r.key = fixed<12>(key.value);
  r.name = fixed<24>(name.value);
  r.run = static_cast<uint16_t>(run.value);
  r.need = static_cast<uint16_t>(need.value);
  r.queued = static_cast<uint16_t>(queued.value);
  r.ok = ok.value;
  return Dec<ProjectRow>::success(r);
}

Dec<SessionRow> session_row(const Obj& o) {
  const auto sid = req_text(o, "sid", is_sid);
  const auto slot = req_int(o, "slot", kSlotMin, kSlotMax);
  const auto name = req_text(o, "name", is_session_name);
  const auto kind = req_enum(o, "kind", kSessionKindNames, 2);
  const auto st = req_enum(o, "st", kSessionStateNames, 5);
  const auto need = req_bool(o, "need");
  const auto age = req_int(o, "age_s", 0, limits::kU32Max);
  const auto short_id = opt_text(o, "short", is_short);
  if (const FieldPath* bad = first_bad(sid, slot, name, kind, st, need, age, short_id)) return fail_at<SessionRow>(*bad);
  SessionRow r;
  r.sid = fixed<8>(sid.value);
  r.slot = static_cast<uint8_t>(slot.value);
  r.name = fixed<32>(name.value);
  r.kind = static_cast<SessionKind>(kind.value);
  r.st = static_cast<SessionState>(st.value);
  r.need = need.value;
  r.age_s = static_cast<uint32_t>(age.value);
  r.short_id = fixed<16>(short_id.value);
  return Dec<SessionRow>::success(r);
}

Dec<QueueRow> queue_row(const Obj& o) {
  const auto qid = req_text(o, "qid", is_qid);
  const auto slot = req_int(o, "slot", kSlotMin, kSlotMax);
  const auto text = req_text(o, "text", is_queue_text);
  const auto intent = req_enum(o, "intent", kIntentNames, 3);
  const auto sid = opt_text(o, "sid", is_sid);
  const auto uncertain = opt_bool(o, "uncertain");  // v0.4.1 (docs/20 U25): absent or null = false
  if (const FieldPath* bad = first_bad(qid, slot, text, intent, sid, uncertain)) return fail_at<QueueRow>(*bad);
  QueueRow r;
  r.qid = fixed<11>(qid.value);
  r.slot = static_cast<uint8_t>(slot.value);
  r.text = fixed<80>(text.value);
  r.intent = static_cast<Intent>(intent.value);
  r.uncertain = uncertain.value.value_or(false);
  r.sid = fixed<8>(sid.value);
  return Dec<QueueRow>::success(r);
}

// x-maxPerKey: the first row that makes one slot exceed `cap` items.
Opt<size_t> first_over_cap(const QueueRow* rows, size_t n, size_t cap) {
  for (size_t i = 0; i < n; ++i) {
    size_t same = 0;
    for (size_t j = 0; j <= i; ++j) same += rows[j].slot == rows[i].slot ? 1 : 0;
    if (same > cap) return some(i);
  }
  return {};
}

// Decodes the array into data.rows[] and data.n in place, or returns the first bad
// field. The array size was checked against the capacity by req_array().
template <class Data, class Fn>
Bad fill_rows(const Obj& o, const char* key, JsonArrayConst arr, Data& data, Fn fn) {
  for (size_t i = 0; i < arr.size(); ++i) {
    const auto item = item_obj(o, key, arr, i);
    if (!item.ok) return some(item.error);
    const auto row = fn(item.value);
    if (!row.ok) return some(row.error);
    data.rows[i] = row.value;
  }
  data.n = static_cast<uint8_t>(arr.size());
  return {};
}

FieldPath row_field(const Obj& o, const char* key, size_t i, const char* field) {
  return path_key(path_index(path_key(o.base, key), i), field);
}

}  // namespace

Bad decode_hello(const Obj& o, Body& out) {
  const auto role = req_enum(o, "role", kRoleNames, 2);
  if (!role.ok) return some(role.error);
  return role.value == 0 ? decode_host_hello(o, out) : decode_device_hello(o, out);
}

Bad decode_time_set(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto epoch = req_int(o, "epoch_ms", limits::kEpochMsMin, limits::kJsonSafeIntMax);
  const auto tz = req_int(o, "tz_min", limits::kTzMinMin, limits::kTzMinMax);
  if (const FieldPath* bad = first_bad(epoch, tz)) return some(*bad);
  TimeData& t = out.emplace<TimeData>();
  t.epoch_ms = epoch.value;
  t.tz_min = static_cast<int16_t>(tz.value);
  return {};
}

Bad decode_projects(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto rev = req_int(o, "rev", 0, limits::kU32Max);
  const auto arr = req_array(o, "projects", 0, limits::kSnapshotProjectsMax);
  const auto other = req_obj(o, "other");
  if (const FieldPath* bad = first_bad(rev, arr, other)) return some(*bad);
  const auto run = req_int(other.value, "run", 0, limits::kCountMax);
  const auto n = req_int(other.value, "n", 0, limits::kCountMax);
  if (const FieldPath* bad = first_bad(run, n)) return some(*bad);
  ProjectsData& d = out.emplace<ProjectsData>();
  d.rev = static_cast<uint32_t>(rev.value);
  d.other.run = static_cast<uint16_t>(run.value);
  d.other.n = static_cast<uint16_t>(n.value);
  const Bad rows = fill_rows(o, "projects", arr.value, d, project_row);
  if (rows.has) return rows;
  const auto dup = first_duplicate(d.rows, d.n, [](const ProjectRow& r) { return r.slot; });
  if (dup.has) return some(row_field(o, "projects", dup.value, "slot"));
  return {};
}

Bad decode_sessions(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto rev = req_int(o, "rev", 0, limits::kU32Max);
  const auto total = req_int(o, "total", 0, limits::kCountMax);
  const auto arr = req_array(o, "sessions", 0, limits::kSnapshotSessionsMax);
  if (const FieldPath* bad = first_bad(rev, total, arr)) return some(*bad);
  SessionsData& d = out.emplace<SessionsData>();
  d.rev = static_cast<uint32_t>(rev.value);
  d.total = static_cast<uint16_t>(total.value);
  const Bad rows = fill_rows(o, "sessions", arr.value, d, session_row);
  if (rows.has) return rows;
  const auto dup = first_duplicate(d.rows, d.n, [](const SessionRow& r) { return r.sid; });
  if (dup.has) return some(row_field(o, "sessions", dup.value, "sid"));
  return {};
}

Bad decode_queue(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto rev = req_int(o, "rev", 0, limits::kU32Max);
  const auto arr = req_array(o, "items", 0, limits::kSnapshotQueueMax);
  if (const FieldPath* bad = first_bad(rev, arr)) return some(*bad);
  QueueData& d = out.emplace<QueueData>();
  d.rev = static_cast<uint32_t>(rev.value);
  const Bad rows = fill_rows(o, "items", arr.value, d, queue_row);
  if (rows.has) return rows;
  const auto dup = first_duplicate(d.rows, d.n, [](const QueueRow& r) { return r.qid; });
  if (dup.has) return some(row_field(o, "items", dup.value, "qid"));
  const auto over = first_over_cap(d.rows, d.n, limits::kSnapshotQueuePerProjectMax);
  if (over.has) return some(row_field(o, "items", over.value, "slot"));
  return {};
}

Bad decode_outbox_ack(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto re = req_text(o, "re", is_monoid_text);
  const auto status = req_enum(o, "status", kAckStatusNames, 3);
  const auto code = opt_err(o, "code");
  if (const FieldPath* bad = first_bad(re, status, code)) return some(*bad);
  if (status.value == static_cast<uint8_t>(AckStatus::Rejected) && !code.value.has) return some(path_key(o.base, "code"));
  OutboxAck& a = out.emplace<OutboxAck>();
  a.status = static_cast<AckStatus>(status.value);
  a.code = code.value;
  return {};
}

Bad decode_agent_result(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto ok = req_bool(o, "ok");
  const auto code = opt_err(o, "code");
  const auto name = opt_text(o, "name", is_session_name);
  const auto short_id = opt_text(o, "short", is_short);
  if (const FieldPath* bad = first_bad(ok, code, name, short_id)) return some(*bad);
  if (!ok.value && !code.value.has) return some(path_key(o.base, "code"));
  AgentResult& r = out.emplace<AgentResult>();
  r.ok = ok.value;
  r.code = code.value;
  r.name = fixed<32>(name.value);
  r.short_id = fixed<16>(short_id.value);
  return {};
}

}  // namespace proto
}  // namespace cpa
