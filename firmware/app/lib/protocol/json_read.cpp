// JSON field readers for the codec: each returns the value or the path of the bad
// field, following the JSON Schema semantics of protocol/schema/v0.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "codec_internal.h"
#include "errors.h"

namespace cpa {
namespace proto {

const char* const kAgentStatusNames[3] = {"ok", "missing", "error"};
const char* const kSessionKindNames[2] = {"tty", "bg"};
const char* const kSessionStateNames[5] = {"busy", "shell", "idle", "waiting", "unknown"};
const char* const kIntentNames[3] = {"todo", "spawn", "feedback"};
const char* const kItemKindNames[2] = {"note", "queue"};
const char* const kAckStatusNames[3] = {"stored", "duplicate", "rejected"};
const char* const kLinkModeNames[3] = {"Detached", "Attached", "Ready"};
const char* const kOverlayNames[3] = {"none", "compose", "confirm"};
const char* const kStoreKindNames[3] = {"sd", "int", "none"};

FieldPath path_key(const FieldPath& base, const char* key, size_t n) {
  const FieldPath head = base.empty() ? FieldPath{} : base.append(".");
  return head.append(key, n);
}

FieldPath path_key(const FieldPath& base, const char* key) { return path_key(base, key, std::strlen(key)); }

FieldPath path_index(const FieldPath& base, size_t i) {
  char buf[16];
  const int n = std::snprintf(buf, sizeof buf, "[%u]", static_cast<unsigned>(i));
  return base.append(buf, n > 0 ? static_cast<size_t>(n) : 0);
}

bool has_key(JsonObjectConst o, const char* key) {
  for (JsonPairConst kv : o) {
    if (std::strcmp(kv.key().c_str(), key) == 0) return true;
  }
  return false;
}

Opt<int64_t> as_integer(JsonVariantConst v) {
  if (v.is<bool>()) return {};
  if (v.is<int64_t>()) return some(v.as<int64_t>());
  if (!v.is<double>()) return {};
  const double d = v.as<double>();
  const double limit = static_cast<double>(limits::kJsonSafeIntMax);
  if (!std::isfinite(d) || std::floor(d) != d || d > limit || d < -limit) return {};
  return some(static_cast<int64_t>(d));
}

Dec<Opt<int64_t>> opt_int(const Obj& o, const char* key, int64_t lo, int64_t hi) {
  const JsonVariantConst v = o.o[key];
  if (v.isNull()) return Dec<Opt<int64_t>>::success({});
  const auto i = as_integer(v);
  if (!i.has || i.value < lo || i.value > hi) return fail_key<Opt<int64_t>>(o, key);
  return Dec<Opt<int64_t>>::success(i);
}

Dec<int64_t> req_int(const Obj& o, const char* key, int64_t lo, int64_t hi) {
  const auto v = opt_int(o, key, lo, hi);
  if (!v.ok || !v.value.has) return fail_key<int64_t>(o, key);
  return Dec<int64_t>::success(v.value.value);
}

Dec<Opt<bool>> opt_bool(const Obj& o, const char* key) {
  const JsonVariantConst v = o.o[key];
  if (v.isNull()) return Dec<Opt<bool>>::success({});
  if (!v.is<bool>()) return fail_key<Opt<bool>>(o, key);
  return Dec<Opt<bool>>::success(some(v.as<bool>()));
}

Dec<bool> req_bool(const Obj& o, const char* key) {
  const auto v = opt_bool(o, key);
  if (!v.ok || !v.value.has) return fail_key<bool>(o, key);
  return Dec<bool>::success(v.value.value);
}

Dec<Opt<JsonString>> opt_text(const Obj& o, const char* key, TextRule rule) {
  const JsonVariantConst v = o.o[key];
  if (v.isNull()) return Dec<Opt<JsonString>>::success({});
  if (!v.is<JsonString>()) return fail_key<Opt<JsonString>>(o, key);
  const JsonString s = v.as<JsonString>();
  if (!rule(s.c_str(), s.size())) return fail_key<Opt<JsonString>>(o, key);
  return Dec<Opt<JsonString>>::success(some(s));
}

Dec<JsonString> req_text(const Obj& o, const char* key, TextRule rule) {
  const auto v = opt_text(o, key, rule);
  if (!v.ok || !v.value.has) return fail_key<JsonString>(o, key);
  return Dec<JsonString>::success(v.value.value);
}

Dec<uint8_t> req_enum(const Obj& o, const char* key, const char* const* names, uint8_t count) {
  const JsonVariantConst v = o.o[key];
  if (!v.is<JsonString>()) return fail_key<uint8_t>(o, key);
  const JsonString s = v.as<JsonString>();
  for (uint8_t i = 0; i < count; ++i) {
    if (std::strlen(names[i]) == s.size() && std::memcmp(names[i], s.c_str(), s.size()) == 0) {
      return Dec<uint8_t>::success(i);
    }
  }
  return fail_key<uint8_t>(o, key);
}

Dec<Opt<uint8_t>> opt_enum(const Obj& o, const char* key, const char* const* names, uint8_t count) {
  if (o.o[key].isNull()) return Dec<Opt<uint8_t>>::success({});
  const auto v = req_enum(o, key, names, count);
  if (!v.ok) return fail_at<Opt<uint8_t>>(v.error);
  return Dec<Opt<uint8_t>>::success(some(v.value));
}

Dec<Opt<ErrCode>> opt_err(const Obj& o, const char* key) {
  const JsonVariantConst v = o.o[key];
  if (v.isNull()) return Dec<Opt<ErrCode>>::success({});
  const JsonString s = v.as<JsonString>();
  const auto code = v.is<JsonString>() ? err_parse(s.c_str(), s.size()) : Opt<ErrCode>{};
  if (!code.has) return fail_key<Opt<ErrCode>>(o, key);
  return Dec<Opt<ErrCode>>::success(code);
}

Dec<Obj> req_obj(const Obj& o, const char* key) {
  const JsonVariantConst v = o.o[key];
  if (!v.is<JsonObjectConst>()) return fail_key<Obj>(o, key);
  Obj child;
  child.o = v.as<JsonObjectConst>();
  child.base = path_key(o.base, key);
  return Dec<Obj>::success(child);
}

Dec<JsonArrayConst> req_array(const Obj& o, const char* key, size_t min_items, size_t max_items) {
  const JsonVariantConst v = o.o[key];
  if (!v.is<JsonArrayConst>()) return fail_key<JsonArrayConst>(o, key);
  const JsonArrayConst a = v.as<JsonArrayConst>();
  if (a.size() < min_items || a.size() > max_items) return fail_key<JsonArrayConst>(o, key);
  return Dec<JsonArrayConst>::success(a);
}

Dec<Obj> item_obj(const Obj& o, const char* key, JsonArrayConst arr, size_t i) {
  const FieldPath at = path_index(path_key(o.base, key), i);
  if (!arr[i].is<JsonObjectConst>()) return fail_at<Obj>(at);
  Obj child;
  child.o = arr[i].as<JsonObjectConst>();
  child.base = at;
  return Dec<Obj>::success(child);
}

Dec<Revs> req_revs(const Obj& o, const char* key) {
  const auto r = req_obj(o, key);
  if (!r.ok) return fail_at<Revs>(r.error);
  const auto p = req_int(r.value, "projects", 0, limits::kU32Max);
  const auto s = req_int(r.value, "sessions", 0, limits::kU32Max);
  const auto q = req_int(r.value, "queue", 0, limits::kU32Max);
  if (const FieldPath* bad = first_bad(p, s, q)) return fail_at<Revs>(*bad);
  Revs revs;
  revs.projects = static_cast<uint32_t>(p.value);
  revs.sessions = static_cast<uint32_t>(s.value);
  revs.queue = static_cast<uint32_t>(q.value);
  return Dec<Revs>::success(revs);
}

Opt<FieldPath> request_env(const Obj& o) {
  return has_key(o.o, "re") ? some(path_key(o.base, "re")) : Opt<FieldPath>{};
}

Opt<FieldPath> reply_env(const Obj& o) {
  return has_key(o.o, "re") ? Opt<FieldPath>{} : some(path_key(o.base, "re"));
}

}  // namespace proto
}  // namespace cpa
