#include "rec_meta.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

#include "codec_internal.h"
#include "wav_header.h"

namespace cpa {
namespace rec {
namespace {

using proto::Bad;
using proto::Obj;
using proto::first_bad;

const char* const kModes[2] = {"memo", "meeting"};
const char* const kConsent[1] = {"ack"};

// Appends printf output to [at, cap) of buf; returns the new end (clamped at cap).
__attribute__((format(printf, 4, 5))) size_t put(char* buf, size_t cap, size_t at, const char* fmt, ...) {
  if (at >= cap) return cap;
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buf + at, cap - at, fmt, args);
  va_end(args);
  return n < 0 ? cap : at + static_cast<size_t>(n);
}

size_t put_at(char* b, size_t cap, size_t at, const Opt<proto::RecAt>& a) {
  if (!a.has) return put(b, cap, at, ",\"at\":null");
  at = put(b, cap, at, ",\"at\":{\"up_ms\":%" PRIu32, a.value.up_ms);
  if (a.value.wall_ms.has) return put(b, cap, at, ",\"wall_ms\":%" PRId64 "}", a.value.wall_ms.value);
  return put(b, cap, at, ",\"wall_ms\":null}");
}

size_t put_tail(char* b, size_t cap, size_t at, const RecMeta& m) {
  if (m.project.has) at = put(b, cap, at, ",\"project\":\"%s\"", m.project.value.c_str());
  if (!m.project.has && m.slot > 0) at = put(b, cap, at, ",\"slot\":%u", static_cast<unsigned>(m.slot));
  for (uint8_t i = 0; i < m.n_stars; ++i) {
    at = put(b, cap, at, "%s%" PRIu32, i == 0 ? ",\"stars\":[" : ",", m.stars[i]);
  }
  if (m.n_stars > 0) at = put(b, cap, at, "]");
  if (m.lead_ms.has) at = put(b, cap, at, ",\"lead_ms\":%u", static_cast<unsigned>(m.lead_ms.value));
  if (m.consent) at = put(b, cap, at, ",\"consent\":\"ack\"");
  if (m.rcv) at = put(b, cap, at, ",\"rcv\":true");
  return put(b, cap, at, ",\"fw\":\"%s\"}", m.fw.c_str());
}

size_t encode_into(const RecMeta& m, char* b, size_t cap) {
  size_t at = put(b, cap, 0, "{\"v\":1,\"id\":\"%s\",\"rec\":\"%s\",\"seg\":%u,\"last\":%s", mono_format(m.id).c_str(),
                  mono_format(m.rec).c_str(), static_cast<unsigned>(m.seg), m.last ? "true" : "false");
  if (m.next.has) at = put(b, cap, at, ",\"next\":\"%s\"", mono_format(m.next.value).c_str());
  at = put(b, cap, at, ",\"mode\":\"%s\",\"bytes\":%" PRIu32, kModes[static_cast<uint8_t>(m.mode) & 1], m.bytes);
  if (m.sha256.has) at = put(b, cap, at, ",\"sha256\":\"%s\"", m.sha256.value.c_str());
  at = put_at(b, cap, at, m.at);
  at = put_tail(b, cap, at, m);
  return at < cap ? at : 0;  // never a cut-off .M: it would not parse anyway
}

Opt<MonoId> mono_of(const Opt<JsonString>& s) {
  return s.has ? mono_parse(s.value.c_str(), s.value.size()) : Opt<MonoId>{};
}

__attribute__((noinline)) Bad meta_head(const Obj& o, RecMeta& m) {
  const auto rec = proto::req_text(o, "rec", proto::is_monoid_text);
  const auto seg = proto::req_int(o, "seg", 0, limits::kFileSegMax);
  const auto last = proto::req_bool(o, "last");
  const auto next = proto::opt_text(o, "next", proto::is_monoid_text);
  const auto mode = proto::req_enum(o, "mode", kModes, 2);
  const auto bytes = proto::req_int(o, "bytes", 0, limits::kU32Max);
  const auto sha = proto::opt_text(o, "sha256", proto::is_sha256);
  if (const auto* bad = first_bad(rec, seg, last, next, mode, bytes, sha)) return some(*bad);
  const Opt<MonoId> r = mono_parse(rec.value.c_str(), rec.value.size());
  const Opt<MonoId> nx = mono_of(next.value);
  if (!r.has || (next.value.has && !nx.has)) return some(proto::FieldPath::from("rec"));
  m.rec = r.value;
  m.seg = static_cast<uint16_t>(seg.value);
  m.last = last.value;
  m.next = nx;
  m.mode = static_cast<RecMode>(mode.value);
  m.bytes = static_cast<uint32_t>(bytes.value);
  m.sha256 = proto::fixed<64>(sha.value);
  return {};
}

Bad meta_at(const Obj& o, RecMeta& m) {
  if (o.o["at"].isNull()) return {};
  const auto at = proto::req_obj(o, "at");
  if (!at.ok) return some(at.error);
  const auto up = proto::req_int(at.value, "up_ms", 0, limits::kU32Max);
  const auto wall = proto::opt_int(at.value, "wall_ms", limits::kEpochMsMin, limits::kJsonSafeIntMax);
  if (const auto* bad = first_bad(up, wall)) return some(*bad);
  proto::RecAt a;
  a.up_ms = static_cast<uint32_t>(up.value);
  a.wall_ms = wall.value;
  m.at = some(a);
  return {};
}

Bad meta_stars(const Obj& o, RecMeta& m) {
  if (o.o["stars"].isNull()) return {};
  const auto arr = proto::req_array(o, "stars", 0, kStarsMax);
  if (!arr.ok) return some(arr.error);
  for (size_t i = 0; i < arr.value.size(); ++i) {
    const Opt<int64_t> v = proto::as_integer(arr.value[i]);
    if (!v.has || v.value < 0 || v.value >= limits::kSegMsMax) return some(proto::FieldPath::from("stars"));
    m.stars[i] = static_cast<uint32_t>(v.value);
  }
  m.n_stars = static_cast<uint8_t>(arr.value.size());
  return {};
}

__attribute__((noinline)) Bad meta_tail(const Obj& o, RecMeta& m) {
  const auto project = proto::opt_text(o, "project", proto::is_project_key);
  const auto slot = proto::opt_int(o, "slot", kSlotMin, kSlotMax);
  const auto lead = proto::opt_int(o, "lead_ms", 0, limits::kLeadMsMax);
  const auto consent = proto::opt_enum(o, "consent", kConsent, 1);
  const auto rcv = proto::opt_bool(o, "rcv");
  const auto fw = proto::opt_text(o, "fw", proto::is_fw);
  if (const auto* bad = first_bad(project, slot, lead, consent, rcv, fw)) return some(*bad);
  m.project = proto::fixed<12>(project.value);
  m.slot = static_cast<uint8_t>(slot.value.value_or(0));
  if (lead.value.has) m.lead_ms = some(static_cast<uint16_t>(lead.value.value));
  m.consent = consent.value.has;
  m.rcv = rcv.value.value_or(false);
  if (fw.value.has) m.fw = proto::fixed<31>(fw.value.value);
  const Bad at = meta_at(o, m);
  return at.has ? at : meta_stars(o, m);
}

bool decode_object(JsonObjectConst root, const MonoId& file_id, RecMeta& m) {
  Obj o;
  o.o = root;
  const auto v = proto::req_int(o, "v", 1, 1);
  const auto id = proto::req_text(o, "id", proto::is_monoid_text);
  if (!v.ok || !id.ok) return false;
  const Opt<MonoId> parsed = mono_parse(id.value.c_str(), id.value.size());
  if (!parsed.has || parsed.value != file_id) return false;
  m.id = parsed.value;
  return !meta_head(o, m).has && !meta_tail(o, m).has;
}

}  // namespace

MetaText rec_meta_encode(const RecMeta& m) {
  return MetaText::build([&](char* buf, size_t cap) { return encode_into(m, buf, cap); });
}

bool rec_meta_decode_into(const char* s, size_t n, const MonoId& file_id, RecMeta& out) {
  if (n == 0 || n > kMetaMax) return false;
  JsonDocument doc;
  if (deserializeJson(doc, s, n) || !doc.is<JsonObjectConst>()) return false;
  out = RecMeta{};
  return decode_object(doc.as<JsonObjectConst>(), file_id, out);
}

Opt<RecMeta> rec_meta_decode(const char* s, size_t n, const MonoId& file_id) {
  RecMeta m;
  if (!rec_meta_decode_into(s, n, file_id, m)) return {};
  return some(m);
}

proto::FileItem rec_item_of(const RecMeta& m, uint32_t file_size) {
  proto::FileItem it;
  const bool exact = file_size == kHeaderBytes + m.bytes;
  it.id = mono_format(m.id);
  it.size = file_size;
  if (exact && !m.rcv) it.sha256 = m.sha256;
  it.rec = mono_format(m.rec);
  it.seg = m.seg;
  it.last = m.last;
  it.mode = m.mode;
  it.dur_ms = dur_ms_of(even_data(file_size));
  it.at = m.at;
  it.project = m.project;
  if (!m.project.has && m.slot > 0) it.slot = some(m.slot);
  it.has_stars = m.n_stars > 0;
  it.n_stars = m.n_stars;
  for (uint8_t i = 0; i < m.n_stars; ++i) it.stars[i] = m.stars[i];
  it.lead_ms = m.lead_ms;
  it.consent = m.consent;
  if (m.rcv || !exact) it.rcv = some(true);
  return it;
}

}  // namespace rec
}  // namespace cpa
