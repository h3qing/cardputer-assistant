// Receiver pipeline (protocol/README.md "Receiver decision order", tools/wire.py):
//   1 drop \r; > line_max bytes incl. \n -> E_TOO_LARGE   2 not '{' -> noise
//   3 not a JSON object -> E_PARSE   4 v / t / id / re envelope   5 unknown t
//   6 invalid UTF-8 anywhere -> E_BAD_FIELD <path>   7 type schema -> E_BAD_FIELD <path>
#include "codec.h"

#include "codec_internal.h"

namespace cpa {
namespace proto {
namespace {

constexpr uint8_t kNestingLimit = 16;

// Feeds ArduinoJson the bytes of [p, end) without '\r' and remembers how far it got,
// so trailing bytes after the root object can be rejected like json.loads() does.
class CrSkippingReader {
 public:
  CrSkippingReader(const char* p, const char* end) : p_(p), end_(end) {}

  int read() {
    while (p_ < end_ && *p_ == '\r') ++p_;
    return p_ < end_ ? static_cast<unsigned char>(*p_++) : -1;
  }

  size_t readBytes(char* buf, size_t n) {
    size_t got = 0;
    for (int c = 0; got < n && (c = read()) >= 0;) buf[got++] = static_cast<char>(c);
    return got;
  }

  bool only_whitespace_left() const {
    for (const char* q = p_; q < end_; ++q) {
      if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') return false;
    }
    return true;
  }

 private:
  const char* p_;
  const char* end_;
};

// The outcome of steps 1-7 without the message (small: returned by value).
struct Verdict {
  Outcome outcome = Outcome::Error;
  ErrCode code = ErrCode::None;
  FieldPath field;
};

Verdict verdict(Outcome outcome, ErrCode code) {
  Verdict v;
  v.outcome = outcome;
  v.code = code;
  return v;
}

Verdict bad_field(const FieldPath& field) {
  Verdict v = verdict(Outcome::Error, ErrCode::BadField);
  v.field = field;
  return v;
}

size_t length_without_cr(const char* raw, size_t n) {
  size_t len = 0;
  for (size_t i = 0; i < n; ++i) len += raw[i] != '\r' ? 1 : 0;
  return len;
}

// End of the body: the last non-\r byte is dropped when it is the line's '\n'.
const char* body_end(const char* raw, size_t n) {
  size_t i = n;
  while (i > 0 && raw[i - 1] == '\r') --i;
  return (i > 0 && raw[i - 1] == '\n') ? raw + i - 1 : raw + n;
}

bool starts_with_brace(const char* p, const char* end) {
  while (p < end && *p == '\r') ++p;
  return p < end && *p == '{';
}

bool text_field(JsonVariantConst v, TextRule rule) {
  if (!v.is<JsonString>()) return false;
  const JsonString s = v.as<JsonString>();
  return rule(s.c_str(), s.size());
}

// Step 4: v, t, id, re. Returns the failing outcome, if any.
Opt<Verdict> check_envelope(JsonObjectConst o) {
  const auto v = o["v"];
  if (!v.is<int64_t>() || v.is<bool>()) return some(bad_field(FieldPath::from("v")));
  if (v.as<int64_t>() != limits::kProtocolVersion) return some(verdict(Outcome::Error, ErrCode::Version));
  if (!text_field(o["t"], is_type)) return some(bad_field(FieldPath::from("t")));
  if (!text_field(o["id"], is_id)) return some(bad_field(FieldPath::from("id")));
  if (has_key(o, "re") && !text_field(o["re"], is_id)) return some(bad_field(FieldPath::from("re")));
  return {};
}

// True when every string (key or value) in v is valid UTF-8. The only recursive step
// of the walk, so its frame is kept small (up to 16 nesting levels deep).
bool utf8_clean(JsonVariantConst v) {
  if (v.is<JsonString>()) {
    const JsonString s = v.as<JsonString>();
    return utf8_valid(s.c_str(), s.size());
  }
  if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst item : v.as<JsonArrayConst>()) {
      if (!utf8_clean(item)) return false;
    }
    return true;
  }
  if (!v.is<JsonObjectConst>()) return true;
  for (JsonPairConst kv : v.as<JsonObjectConst>()) {
    if (!utf8_valid(kv.key().c_str(), kv.key().size()) || !utf8_clean(kv.value())) return false;
  }
  return true;
}

struct Descent {
  JsonVariantConst node;
  FieldPath at;
  bool stop = false;  // `at` names a bad key
};

// From an unclean container to its first bad key or first unclean child.
Descent descend(JsonVariantConst v, const FieldPath& at) {
  size_t i = 0;
  for (JsonVariantConst item : v.as<JsonArrayConst>()) {
    if (!utf8_clean(item)) return Descent{item, path_index(at, i), false};
    ++i;
  }
  for (JsonPairConst kv : v.as<JsonObjectConst>()) {
    const JsonString key = kv.key();
    const bool bad_key = !utf8_valid(key.c_str(), key.size());
    if (bad_key || !utf8_clean(kv.value())) return Descent{kv.value(), path_key(at, key.c_str(), key.size()), bad_key};
  }
  return Descent{v, at, true};  // unreachable for an unclean container
}

// Step 6: the path of the first string (value or key) that is not valid UTF-8, depth
// first. Walks down iteratively so that only utf8_clean() recurses.
Opt<FieldPath> bad_utf8(JsonObjectConst root) {
  if (utf8_clean(root)) return {};
  Descent d{root, FieldPath{}, false};
  while (!d.stop && !d.node.is<JsonString>()) d = descend(d.node, d.at);
  return some(d.at);
}

Bad decode_ping(const Obj& o, Body& out) { return decode_empty_request<Ping>(o, out); }
#if CPA_TEST_HOOKS
Bad decode_debug_reboot(const Obj& o, Body& out) { return decode_empty_request<DebugReboot>(o, out); }
#endif

using Decoder = Bad (*)(const Obj&, Body&);

// Indexed by MsgType.
constexpr Decoder kDecoders[kMsgTypeCount] = {
    decode_hello,        decode_ping,         decode_pong,         decode_ack,         decode_error,
    decode_time_set,     decode_projects,     decode_sessions,     decode_queue,       decode_outbox_item,
    decode_outbox_ack,   decode_agent_spawn,  decode_agent_reply,  decode_agent_result, decode_queue_done,
    decode_inbox_dismiss, decode_file_list,   decode_file_items,  decode_file_get,    decode_file_data,
    decode_file_del,     decode_rec_list,     decode_rec_items,    decode_rec_act,      decode_dests_snapshot,
#if CPA_TEST_HOOKS
    decode_debug_key,    decode_debug_state,  decode_debug_reboot, decode_debug_rec,    decode_debug_mic,
    decode_debug_codec,  decode_debug_lang,
#endif
};

// Steps 4-7 on a parsed object; a valid message is written into msg.
Verdict classify(JsonObjectConst root, Message& msg) {
  const auto envelope = check_envelope(root);
  if (envelope.has) return envelope.value;
  const JsonString t = root["t"].as<JsonString>();
  const auto type = type_parse(t.c_str(), t.size());
  const bool is_reply = has_key(root, "re");
  if (!type.has) return verdict(is_reply ? Outcome::Ignored : Outcome::Error, ErrCode::UnknownType);
  const auto utf8 = bad_utf8(root);
  if (utf8.has) return bad_field(utf8.value);
  Obj o;
  o.o = root;
  const Bad body = kDecoders[static_cast<uint8_t>(type.value)](o, msg.body);
  if (body.has) return bad_field(body.value);
  msg.id = fixed<24>(root["id"].as<JsonString>());
  if (is_reply) msg.re = some(fixed<24>(root["re"].as<JsonString>()));
  return verdict(Outcome::Ok, ErrCode::None);
}

bool answerable(const Verdict& v) {
  if (v.outcome == Outcome::Ok) return true;
  return v.outcome == Outcome::Error &&
         (v.code == ErrCode::Version || v.code == ErrCode::UnknownType || v.code == ErrCode::BadField);
}

// The id the receiver must answer exactly once: a request with a valid id.
Opt<MsgId> reply_target(JsonObjectConst root, const Verdict& v) {
  const JsonString id = root["id"].as<JsonString>();
  const bool id_ok = root["id"].is<JsonString>() && is_id(id.c_str(), id.size());
  if (!answerable(v) || has_key(root, "re") || !id_ok) return {};
  return some(fixed<24>(id));
}

// Steps 1-7 of one line into `out` (msg and reply_to); returns the verdict.
Verdict read_line(JsonDocument& doc, const char* raw, size_t n, Decoded& out) {
  if (length_without_cr(raw, n) > limits::kLineMax) return verdict(Outcome::Error, ErrCode::TooLarge);
  const char* end = body_end(raw, n);
  if (!starts_with_brace(raw, end)) return verdict(Outcome::Noise, ErrCode::None);
  CrSkippingReader reader(raw, end);
  const auto err = deserializeJson(doc, reader, DeserializationOption::NestingLimit(kNestingLimit));
  if (err || !reader.only_whitespace_left() || !doc.is<JsonObjectConst>()) {
    return verdict(Outcome::Error, ErrCode::Parse);
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  const Verdict v = classify(root, out.msg);
  out.reply_to = reply_target(root, v);
  return v;
}

// file.data (docs/11 §5.4): built without the JSON document, base64 straight into the line.
Result<Line, ErrCode> encode_data_line(const Message& m, const FileData& d) {
  Result<Line, ErrCode> out;
  out.value = Line::build([&](char* buf, size_t cap) { return file_data_line(m, d, buf, cap); });
  out.ok = !out.value.empty();
  if (!out.ok) out.error = ErrCode::TooLarge;
  return out;
}

}  // namespace

const Decoded& Codec::decode(const char* raw, size_t n) {
  last_.msg.id = MsgId{};
  last_.msg.re = Opt<MsgId>{};
  last_.reply_to = Opt<MsgId>{};
  const Verdict v = read_line(doc_, raw, n, last_);
  last_.outcome = v.outcome;
  last_.code = v.code;
  last_.field = v.field;
  return last_;
}

Result<Line, ErrCode> Codec::encode(const Message& m) {
  if (const auto* d = std::get_if<FileData>(&m.body)) {
    if (m.re.has) return encode_data_line(m, *d);
  }
  doc_.clear();
  JsonObject o = doc_.to<JsonObject>();
  o["v"] = limits::kProtocolVersion;
  o["t"] = type_name(msg_type(m));
  o["id"] = m.id.c_str();
  if (m.re.has) o["re"] = m.re.value.c_str();
  encode_payload(m.body, o);
  if (doc_.overflowed()) return Result<Line, ErrCode>::failure(ErrCode::Internal);
  const size_t len = measureJson(doc_);
  if (len + 1 > limits::kLineMax) return Result<Line, ErrCode>::failure(ErrCode::TooLarge);
  Result<Line, ErrCode> out;
  out.ok = true;
  out.value = Line::build([this](char* buf, size_t cap) {
    const size_t n = serializeJson(doc_, buf, cap - 1);
    buf[n] = '\n';
    return n + 1;
  });
  return out;
}

}  // namespace proto
}  // namespace cpa
