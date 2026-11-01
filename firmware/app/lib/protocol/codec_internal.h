// Internal to the codec: JSON field readers that report the JSON path of the first
// bad field (E_BAD_FIELD), wire names of enums, and the per-type decoders/encoders.
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "codec.h"
#include "protocol_limits.h"
#include "text_rules.h"

namespace cpa {
namespace proto {

template <class T>
using Dec = Result<T, FieldPath>;

// The first bad field, if any. Decoders fill their output in place and return this.
using Bad = Opt<FieldPath>;

struct Obj {
  JsonObjectConst o;
  FieldPath base;
};

using TextRule = bool (*)(const char*, size_t);

// ---- paths and generic helpers ----------------------------------------------------
FieldPath path_key(const FieldPath& base, const char* key, size_t n);
FieldPath path_key(const FieldPath& base, const char* key);
FieldPath path_index(const FieldPath& base, size_t i);
bool has_key(JsonObjectConst o, const char* key);
Opt<int64_t> as_integer(JsonVariantConst v);  // JSON Schema "integer": 1.0 counts

template <class T>
Dec<T> fail_at(const FieldPath& f) {
  return Dec<T>::failure(f);
}

template <class T>
Dec<T> fail_key(const Obj& o, const char* key) {
  return Dec<T>::failure(path_key(o.base, key));
}

// The first failure among already computed field results (all reads are pure).
template <class... Ds>
const FieldPath* first_bad(const Ds&... ds) {
  const FieldPath* bad = nullptr;
  ((bad = (bad || ds.ok) ? bad : &ds.error), ...);
  return bad;
}

template <size_t N>
FixedStr<N> fixed(JsonString s) {
  return FixedStr<N>::from(s.c_str(), s.size());
}

template <size_t N>
Opt<FixedStr<N>> fixed(const Opt<JsonString>& s) {
  return s.has ? some(fixed<N>(s.value)) : Opt<FixedStr<N>>{};
}

// place(dst, s) is dst = fixed<N>(s) built directly in dst's storage (C++17 guaranteed
// elision): a plain assignment first puts a FixedStr<N> temporary on the stack, 504 B
// for user text. FixedStr is trivially destructible, so reusing its storage is defined.
template <size_t N>
void place(FixedStr<N>& dst, JsonString s) {
  static_assert(std::is_trivially_destructible<FixedStr<N>>::value, "storage reuse needs a trivial destructor");
  ::new (static_cast<void*>(&dst)) FixedStr<N>(FixedStr<N>::from(s.c_str(), s.size()));
}

// x-uniqueBy: the first row whose key repeats an earlier row's key.
template <class Row, class Key>
Opt<size_t> first_duplicate(const Row* rows, size_t n, Key key) {
  for (size_t i = 1; i < n; ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (key(rows[j]) == key(rows[i])) return some(i);
    }
  }
  return {};
}

// ---- field readers (missing and null mean "absent") ----------------------------------
Dec<int64_t> req_int(const Obj& o, const char* key, int64_t lo, int64_t hi);
Dec<Opt<int64_t>> opt_int(const Obj& o, const char* key, int64_t lo, int64_t hi);
Dec<bool> req_bool(const Obj& o, const char* key);
Dec<Opt<bool>> opt_bool(const Obj& o, const char* key);
Dec<JsonString> req_text(const Obj& o, const char* key, TextRule rule);
Dec<Opt<JsonString>> opt_text(const Obj& o, const char* key, TextRule rule);
Dec<uint8_t> req_enum(const Obj& o, const char* key, const char* const* names, uint8_t count);
Dec<Opt<uint8_t>> opt_enum(const Obj& o, const char* key, const char* const* names, uint8_t count);
Dec<Opt<ErrCode>> opt_err(const Obj& o, const char* key);
Dec<Obj> req_obj(const Obj& o, const char* key);
Dec<JsonArrayConst> req_array(const Obj& o, const char* key, size_t min_items, size_t max_items);
Dec<Revs> req_revs(const Obj& o, const char* key);
Dec<Obj> item_obj(const Obj& o, const char* key, JsonArrayConst arr, size_t i);

// Envelope rules of the schemas: requests must not carry re, replies must.
Opt<FieldPath> request_env(const Obj& o);
Opt<FieldPath> reply_env(const Obj& o);

// ---- wire names --------------------------------------------------------------------
extern const char* const kAgentStatusNames[3];
extern const char* const kSessionKindNames[2];
extern const char* const kSessionStateNames[5];
extern const char* const kIntentNames[3];
extern const char* const kItemKindNames[2];
extern const char* const kAckStatusNames[3];
extern const char* const kLinkModeNames[3];
extern const char* const kOverlayNames[3];
extern const char* const kStoreKindNames[3];
extern const char* const kRecModeNames[2];

template <class E, size_t N>
const char* wire_name(const char* const (&names)[N], E value) {
  const auto i = static_cast<size_t>(value);
  return i < N ? names[i] : "";
}

// ---- per-type decoders (root object; envelope already checked) ---------------------
// Each writes the message into `out` (variant emplace: no copy on the stack) and returns
// the first bad field; on failure `out` holds a partial value that must not be used.
Bad decode_hello(const Obj& o, Body& out);
Bad decode_time_set(const Obj& o, Body& out);
Bad decode_projects(const Obj& o, Body& out);
Bad decode_sessions(const Obj& o, Body& out);
Bad decode_queue(const Obj& o, Body& out);
Bad decode_outbox_ack(const Obj& o, Body& out);
Bad decode_agent_result(const Obj& o, Body& out);
Bad decode_pong(const Obj& o, Body& out);
Bad decode_ack(const Obj& o, Body& out);
Bad decode_error(const Obj& o, Body& out);
Bad decode_outbox_item(const Obj& o, Body& out);
Bad decode_agent_spawn(const Obj& o, Body& out);
Bad decode_agent_reply(const Obj& o, Body& out);
Bad decode_queue_done(const Obj& o, Body& out);
Bad decode_inbox_dismiss(const Obj& o, Body& out);
// v0.3 recording transfer (decode_file.cpp, encode_file.cpp).
Bad decode_file_list(const Obj& o, Body& out);
Bad decode_file_items(const Obj& o, Body& out);
Bad decode_file_get(const Obj& o, Body& out);
Bad decode_file_data(const Obj& o, Body& out);
Bad decode_file_del(const Obj& o, Body& out);
Dec<Opt<FilesInfo>> opt_files(const Obj& o);  // hello.files / pong.files
void put_files(JsonObject out, const Opt<FilesInfo>& files);
void encode_file(const Body& body, JsonObject out);
// The whole file.data line with '\n' into buf; its length, or 0 when it does not fit.
size_t file_data_line(const Message& m, const FileData& d, char* buf, size_t cap);
// v0.4.1 recordings app (codec_rec.cpp).
Bad decode_rec_list(const Obj& o, Body& out);
Bad decode_rec_items(const Obj& o, Body& out);
Bad decode_rec_act(const Obj& o, Body& out);
void encode_rec(const Body& body, JsonObject out);
// v0.5 capture destinations (codec_dest.cpp).
Bad decode_dests_snapshot(const Obj& o, Body& out);
void encode_dest(const Body& body, JsonObject out);
#if CPA_TEST_HOOKS
Bad decode_debug_key(const Obj& o, Body& out);
Bad decode_debug_state(const Obj& o, Body& out);
Bad decode_debug_rec(const Obj& o, Body& out);
Bad decode_debug_mic(const Obj& o, Body& out);
Bad decode_debug_codec(const Obj& o, Body& out);
Bad decode_debug_lang(const Obj& o, Body& out);
Bad decode_debug_rec_state(const Obj& o, Opt<DebugRecState>& out);
void encode_debug(const Body& body, JsonObject out);
void encode_debug_rec(const Body& body, JsonObject out);
void encode_debug_mic(const Body& body, JsonObject out);
void encode_debug_codec(const Body& body, JsonObject out);
void put_debug_rec_state(JsonObject out, const Opt<DebugRecState>& rec);
#endif

// A request without payload fields (ping, debug.reboot).
template <class T>
Bad decode_empty_request(const Obj& o, Body& out) {
  const Bad env = request_env(o);
  if (!env.has) out.emplace<T>();
  return env;
}

// ---- per-type encoders (write payload fields into the root object) -----------------
void encode_payload(const Body& body, JsonObject out);

}  // namespace proto
}  // namespace cpa
