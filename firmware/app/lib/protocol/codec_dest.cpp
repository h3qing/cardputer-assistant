// Codec of the v0.5 capture destinations (docs/14 §7.2, protocol/schema/v0
// dests.snapshot.schema.json, protocol/README item 24). Decoded in place like rec.items;
// the device does not act on it before F5 (docs/25 §4.6).
#include "codec_internal.h"

namespace cpa {
namespace proto {
namespace {

// One row: id, slot, label and kind are required; key and fl may be absent or null.
Bad dest_row(const Obj& o, DestRow& r) {
  const auto id = req_text(o, "id", is_dest_row_id);
  const auto slot = req_int(o, "slot", 1, kDestRowsMax);
  const auto label = req_text(o, "label", is_dest_label);
  const auto kind = req_text(o, "kind", is_dest_kind);
  const auto key = opt_text(o, "key", is_dest_key);
  const auto fl = opt_text(o, "fl", is_dest_flags);
  if (const FieldPath* bad = first_bad(id, slot, label, kind, key, fl)) return some(*bad);
  r.id = fixed<20>(id.value);
  r.slot = static_cast<uint8_t>(slot.value);
  r.label = fixed<24>(label.value);
  r.kind = fixed<7>(kind.value);
  r.key = fixed<1>(key.value);
  r.fl = fixed<8>(fl.value);
  return {};
}

template <size_t N>
void put_str(JsonObject out, const char* key, const FixedStr<N>& s) {
  out[key] = JsonString(s.data(), s.size());
}

void put_row(const DestRow& r, JsonObject o) {
  put_str(o, "id", r.id);
  o["slot"] = r.slot;
  put_str(o, "label", r.label);
  put_str(o, "kind", r.kind);
  if (r.key.has) put_str(o, "key", r.key.value);
  if (r.fl.has) put_str(o, "fl", r.fl.value);
}

}  // namespace

// x-uniqueBy slot: two rows never share a place in the picker.
Bad decode_dests_snapshot(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto rev = req_int(o, "rev", 0, limits::kU32Max);
  const auto arr = req_array(o, "dests", 0, kDestRowsMax);
  if (const FieldPath* bad = first_bad(rev, arr)) return some(*bad);
  DestsSnapshot& s = out.emplace<DestsSnapshot>();
  for (size_t i = 0; i < arr.value.size(); ++i) {
    const auto item = item_obj(o, "dests", arr.value, i);
    if (!item.ok) return some(item.error);
    const Bad bad = dest_row(item.value, s.rows[i]);
    if (bad.has) return bad;
  }
  s.rev = static_cast<uint32_t>(rev.value);
  s.n = static_cast<uint8_t>(arr.value.size());
  const auto dup = first_duplicate(s.rows, s.n, [](const DestRow& r) { return r.slot; });
  if (dup.has) return some(path_key(path_index(path_key(o.base, "dests"), dup.value), "slot"));
  return {};
}

void encode_dest(const Body& body, JsonObject out) {
  const auto* s = std::get_if<DestsSnapshot>(&body);
  if (!s) return;
  out["rev"] = s->rev;
  JsonArray rows = out["dests"].to<JsonArray>();
  for (uint8_t i = 0; i < s->n; ++i) put_row(s->rows[i], rows.add<JsonObject>());
}

}  // namespace proto
}  // namespace cpa
