// Codec of the v0.4.1 recordings app (docs/13 §8.1-§8.4, §12.2; protocol/schema/v0
// rec.*.schema.json, common.schema.json recRow). The device's recordings app sends
// rec.list / rec.act and reads rec.items. rec.items is decoded in place into the
// message's RecPage, so it needs no buffer of its own.
#include "codec_internal.h"

namespace cpa {
namespace proto {
namespace {

const char* const kRecFilterNames[4] = {"all", "starred", "memo", "meeting"};
const char* const kRecOpNames[4] = {"star", "unstar", "tag", "hide"};
const char* const kRecStateNames[9] = {"on_device", "pulling",    "archived", "processing", "blocked",
                                       "done",      "transcript", "failed",   "stale"};

template <class T>
Opt<T> narrow(const Opt<int64_t>& v) {
  return v.has ? some(static_cast<T>(v.value)) : Opt<T>{};
}

// One recRow: rec, mode and st are required; the rest may be absent or null (unknown).
Bad rec_row(const Obj& o, RecRow& r) {
  const auto rec = req_text(o, "rec", is_monoid_text);
  const auto mode = req_enum(o, "mode", kRecModeNames, 2);
  const auto st = req_enum(o, "st", kRecStateNames, 9);
  const auto wall = opt_int(o, "wall_ms", limits::kEpochMsMin, limits::kJsonSafeIntMax);
  const auto dur = opt_int(o, "dur_ms", 0, limits::kU32Max);
  const auto project = opt_text(o, "project", is_project_key);
  const auto starred = opt_bool(o, "starred");
  const auto marks = opt_int(o, "marks", 0, limits::kCountMax);
  const auto title = opt_text(o, "title", is_rec_title);
  const auto todo = opt_int(o, "todo_n", 0, limits::kCountMax);
  if (const FieldPath* bad = first_bad(rec, mode, st, wall, dur, project, starred, marks, title, todo)) return some(*bad);
  r.rec = fixed<kMonoIdLen>(rec.value);
  r.mode = static_cast<RecMode>(mode.value);
  r.st = static_cast<RecState>(st.value);
  r.wall_ms = wall.value;
  r.dur_ms = narrow<uint32_t>(dur.value);
  r.project = fixed<12>(project.value);
  r.starred = starred.value;
  r.marks = narrow<uint16_t>(marks.value);
  r.title = fixed<48>(title.value);
  r.todo_n = narrow<uint16_t>(todo.value);
  return {};
}

template <size_t N>
void put_opt(JsonObject out, const char* key, const Opt<FixedStr<N>>& s) {
  if (s.has) out[key] = JsonString(s.value.data(), s.value.size());
}

template <class T>
void put_opt(JsonObject out, const char* key, const Opt<T>& v) {
  if (v.has) out[key] = v.value;
}

void put_row(const RecRow& r, JsonObject o) {
  o["rec"] = JsonString(r.rec.data(), r.rec.size());
  o["mode"] = wire_name(kRecModeNames, r.mode);
  o["st"] = wire_name(kRecStateNames, r.st);
  put_opt(o, "wall_ms", r.wall_ms);
  put_opt(o, "dur_ms", r.dur_ms);
  put_opt(o, "project", r.project);
  put_opt(o, "starred", r.starred);
  put_opt(o, "marks", r.marks);
  put_opt(o, "title", r.title);
  put_opt(o, "todo_n", r.todo_n);
}

// Only tag carries a project, and there the key is required: no project is sent as null.
void encode_act(const RecAct& a, JsonObject out) {
  out["rec"] = JsonString(a.rec.data(), a.rec.size());
  out["op"] = wire_name(kRecOpNames, a.op);
  if (a.op != RecOp::Tag) return;
  if (a.project.has) return put_opt(out, "project", a.project);
  out["project"] = nullptr;
}

}  // namespace

Bad decode_rec_list(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto filter = req_enum(o, "filter", kRecFilterNames, 4);
  const auto max = req_int(o, "max", 1, limits::kRecListMax);
  const auto cursor = opt_text(o, "cursor", is_rec_cursor);
  if (const FieldPath* bad = first_bad(filter, max, cursor)) return some(*bad);
  RecList& l = out.emplace<RecList>();
  l.filter = static_cast<RecFilter>(filter.value);
  l.max = static_cast<uint8_t>(max.value);
  l.cursor = fixed<32>(cursor.value);
  return {};
}

// x-uniqueBy rec: a session is listed once per page.
Bad decode_rec_items(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto arr = req_array(o, "items", 0, limits::kRecListMax);
  const auto next = opt_text(o, "next", is_rec_cursor);
  if (const FieldPath* bad = first_bad(arr, next)) return some(*bad);
  RecPage& p = out.emplace<RecPage>();
  for (size_t i = 0; i < arr.value.size(); ++i) {
    const auto item = item_obj(o, "items", arr.value, i);
    if (!item.ok) return some(item.error);
    const Bad bad = rec_row(item.value, p.rows[i]);
    if (bad.has) return bad;
  }
  p.n = static_cast<uint8_t>(arr.value.size());
  p.next = fixed<32>(next.value);
  const auto dup = first_duplicate(p.rows, p.n, [](const RecRow& r) { return r.rec; });
  if (dup.has) return some(path_key(path_index(path_key(o.base, "items"), dup.value), "rec"));
  return {};
}

// project goes with tag only: required there (a key, or null for none), absent or null
// for the other ops (the schema's if/then/else).
Bad decode_rec_act(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto rec = req_text(o, "rec", is_monoid_text);
  const auto op = req_enum(o, "op", kRecOpNames, 4);
  const auto project = opt_text(o, "project", is_project_key);
  if (const FieldPath* bad = first_bad(rec, op, project)) return some(*bad);
  const bool tag = op.value == static_cast<uint8_t>(RecOp::Tag);
  if (tag ? !has_key(o.o, "project") : project.value.has) return some(path_key(o.base, "project"));
  RecAct& a = out.emplace<RecAct>();
  a.rec = fixed<kMonoIdLen>(rec.value);
  a.op = static_cast<RecOp>(op.value);
  a.project = fixed<12>(project.value);
  return {};
}

void encode_rec(const Body& body, JsonObject out) {
  if (const auto* l = std::get_if<RecList>(&body)) {
    out["filter"] = wire_name(kRecFilterNames, l->filter);
    out["max"] = l->max;
    return put_opt(out, "cursor", l->cursor);
  }
  if (const auto* p = std::get_if<RecPage>(&body)) {
    JsonArray items = out["items"].to<JsonArray>();
    for (uint8_t i = 0; i < p->n; ++i) put_row(p->rows[i], items.add<JsonObject>());
    return put_opt(out, "next", p->next);
  }
  if (const auto* a = std::get_if<RecAct>(&body)) encode_act(*a, out);
}

}  // namespace proto
}  // namespace cpa
