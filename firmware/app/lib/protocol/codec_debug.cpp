// debug.* (test firmware only, -DCPA_TEST_HOOKS=1; protocol/README "contract additions" 9).
// Compiled out of release builds so no debug.* name reaches firmware.bin.
#if CPA_TEST_HOOKS

#include "codec_internal.h"
#include "key_names.h"

namespace cpa {
namespace proto {
namespace {

Dec<DebugProject> debug_project(const Obj& o) {
  const auto slot = req_int(o, "slot", kSlotMin, kSlotMax);
  const auto run = req_int(o, "run", 0, limits::kCountMax);
  const auto need = req_int(o, "need", 0, limits::kCountMax);
  const auto queued = req_int(o, "queued", 0, limits::kCountMax);
  if (const FieldPath* bad = first_bad(slot, run, need, queued)) return fail_at<DebugProject>(*bad);
  DebugProject p;
  p.slot = static_cast<uint8_t>(slot.value);
  p.run = static_cast<uint16_t>(run.value);
  p.need = static_cast<uint16_t>(need.value);
  p.queued = static_cast<uint16_t>(queued.value);
  return Dec<DebugProject>::success(p);
}

// Decodes the projects array into s.projects[] and s.n in place (slots unique), or
// returns the first bad field.
Bad fill_projects(const Obj& o, JsonArrayConst arr, DebugState& s) {
  for (size_t i = 0; i < arr.size(); ++i) {
    const auto item = item_obj(o, "projects", arr, i);
    const auto p = item.ok ? debug_project(item.value) : fail_at<DebugProject>(item.error);
    if (!p.ok) return some(p.error);
    for (size_t j = 0; j < i; ++j) {
      if (s.projects[j].slot == p.value.slot) return some(path_key(item.value.base, "slot"));
    }
    s.projects[i] = p.value;
  }
  s.n = static_cast<uint8_t>(arr.size());
  return {};
}

// The summary's fields up to key_ms (schema order), written into s. Split from
// debug_summary() so that fewer field results share one frame on the loop stack.
Bad debug_view(const Obj& o, DebugState& s) {
  const auto up = req_int(o, "up_ms", 0, limits::kU32Max);
  const auto link = req_enum(o, "link", kLinkModeNames, 3);
  const auto mod = req_text(o, "mod", is_module_id);
  const auto overlay = req_enum(o, "overlay", kOverlayNames, 3);
  const auto overlay_ms = opt_int(o, "overlay_ms", 0, limits::kU32Max);
  const auto key_ms = opt_int(o, "key_ms", 0, limits::kU32Max);
  if (const FieldPath* bad = first_bad(up, link, mod, overlay, overlay_ms, key_ms)) return some(*bad);
  s.up_ms = static_cast<uint32_t>(up.value);
  s.link = static_cast<LinkMode>(link.value);
  s.mod = fixed<16>(mod.value);
  s.overlay = static_cast<Overlay>(overlay.value);
  s.overlay_ms = overlay_ms.value.has ? some(static_cast<uint32_t>(overlay_ms.value.value)) : Opt<uint32_t>{};
  s.key_ms = key_ms.value.has ? some(static_cast<uint32_t>(key_ms.value.value)) : Opt<uint32_t>{};
  return {};
}

// debug.lang and debug.state.ui.lang (v0.4.3, docs/21 §7), by Lang.
constexpr const char* kLangCodes[] = {"zh", "en"};

// v0.4.0 debug.state.ui (docs/13 §12.1): absent or null before v0.4.0; lang (v0.4.3) absent
// before v0.4.3.
Bad decode_debug_ui(const Obj& o, Opt<DebugUi>& out) {
  if (o.o["ui"].isNull()) return {};
  const auto u = req_obj(o, "ui");
  if (!u.ok) return some(u.error);
  const auto menu = req_bool(u.value, "menu");
  const auto locked = req_bool(u.value, "locked");
  const auto lang = opt_enum(u.value, "lang", kLangCodes, 2);
  if (const FieldPath* bad = first_bad(menu, locked, lang)) return some(*bad);
  DebugUi ui{menu.value, locked.value, {}};
  if (lang.value.has) ui.lang = some(static_cast<Lang>(lang.value.value));
  out = some(ui);
  return {};
}

void put_debug_ui(JsonObject out, const Opt<DebugUi>& ui) {
  if (!ui.has) return;
  JsonObject u = out["ui"].to<JsonObject>();
  u["menu"] = ui.value.menu;
  u["locked"] = ui.value.locked;
  if (ui.value.lang.has) u["lang"] = wire_name(kLangCodes, ui.value.lang.value);
}

// debug.state.diag (gh #21): one table for both directions, in schema order, so the
// names and the order cannot drift apart. Every field is required.
struct DiagField {
  const char* name;
  uint32_t DebugDiag::*field;
  int64_t max;
};

constexpr DiagField kDiagFields[] = {
    {"heap_free", &DebugDiag::heap_free, limits::kU32Max},
    {"heap_min", &DebugDiag::heap_min, limits::kU32Max},
    {"heap_block", &DebugDiag::heap_block, limits::kU32Max},
    {"stack_left", &DebugDiag::stack_left, limits::kU32Max},
    {"audio_stack_left", &DebugDiag::audio_stack_left, limits::kU32Max},
    {"loop_n", &DebugDiag::loop_n, limits::kU32Max},
    {"loop_max_us", &DebugDiag::loop_max_us, limits::kU32Max},
    {"loop_win_max_us", &DebugDiag::loop_win_max_us, limits::kU32Max},
    {"gap_max_us", &DebugDiag::gap_max_us, limits::kU32Max},
    {"gap_win_max_us", &DebugDiag::gap_win_max_us, limits::kU32Max},
    {"loop_over_20", &DebugDiag::loop_over_20, limits::kU32Max},
    {"loop_over_50", &DebugDiag::loop_over_50, limits::kU32Max},
    {"loop_over_100", &DebugDiag::loop_over_100, limits::kU32Max},
    {"loop_over_500", &DebugDiag::loop_over_500, limits::kU32Max},
    {"ghost_n", &DebugDiag::ghost_n, limits::kCountMax},
    {"kb_reinit_n", &DebugDiag::kb_reinit_n, limits::kCountMax},
};

// Absent or null: a build without diag.
Bad decode_debug_diag(const Obj& o, Opt<DebugDiag>& out) {
  if (o.o["diag"].isNull()) return {};
  const auto d = req_obj(o, "diag");
  if (!d.ok) return some(d.error);
  DebugDiag g;
  for (const DiagField& f : kDiagFields) {
    const auto v = req_int(d.value, f.name, 0, f.max);
    if (!v.ok) return some(v.error);
    g.*f.field = static_cast<uint32_t>(v.value);
  }
  out = some(g);
  return {};
}

void put_debug_diag(JsonObject out, const Opt<DebugDiag>& diag) {
  if (!diag.has) return;
  JsonObject d = out["diag"].to<JsonObject>();
  for (const DiagField& f : kDiagFields) d[f.name] = diag.value.*f.field;
}

Bad debug_summary(const Obj& o, Body& out) {
  DebugState& s = out.emplace<DebugState>();
  const Bad view = debug_view(o, s);
  if (view.has) return view;
  const auto outbox = req_obj(o, "outbox");
  const auto revs = req_revs(o, "revs");
  const auto need = req_int(o, "need", 0, limits::kCountMax);
  const auto arr = req_array(o, "projects", 0, limits::kSnapshotProjectsMax);
  if (const FieldPath* bad = first_bad(outbox, revs, need, arr)) return some(*bad);
  const auto pending = req_int(outbox.value, "pending", 0, limits::kCountMax);
  const auto inflight = req_int(outbox.value, "inflight", 0, limits::kCountMax);
  const auto bad_n = req_int(outbox.value, "bad", 0, limits::kCountMax);
  if (const FieldPath* bad = first_bad(pending, inflight, bad_n)) return some(*bad);
  s.summary = true;
  s.outbox_pending = static_cast<uint16_t>(pending.value);
  s.outbox_inflight = static_cast<uint16_t>(inflight.value);
  s.outbox_bad = static_cast<uint16_t>(bad_n.value);
  s.revs = revs.value;
  s.need = static_cast<uint16_t>(need.value);
  const Bad projects = fill_projects(o, arr.value, s);
  if (projects.has) return projects;
  const Bad rec = decode_debug_rec_state(o, s.rec);
  if (rec.has) return rec;
  const Bad ui = decode_debug_ui(o, s.ui);
  return ui.has ? ui : decode_debug_diag(o, s.diag);
}

void encode_summary(const DebugState& s, JsonObject out) {
  out["up_ms"] = s.up_ms;
  out["link"] = wire_name(kLinkModeNames, s.link);
  out["mod"] = s.mod.c_str();
  out["overlay"] = wire_name(kOverlayNames, s.overlay);
  if (s.overlay_ms.has) out["overlay_ms"] = s.overlay_ms.value;
  if (s.key_ms.has) out["key_ms"] = s.key_ms.value;
  JsonObject ob = out["outbox"].to<JsonObject>();
  ob["pending"] = s.outbox_pending;
  ob["inflight"] = s.outbox_inflight;
  ob["bad"] = s.outbox_bad;
  JsonObject revs = out["revs"].to<JsonObject>();
  revs["projects"] = s.revs.projects;
  revs["sessions"] = s.revs.sessions;
  revs["queue"] = s.revs.queue;
  out["need"] = s.need;
  JsonArray arr = out["projects"].to<JsonArray>();
  for (uint8_t i = 0; i < s.n; ++i) {
    JsonObject p = arr.add<JsonObject>();
    p["slot"] = s.projects[i].slot;
    p["run"] = s.projects[i].run;
    p["need"] = s.projects[i].need;
    p["queued"] = s.projects[i].queued;
  }
  put_debug_rec_state(out, s.rec);
  put_debug_ui(out, s.ui);
  put_debug_diag(out, s.diag);
}

}  // namespace

Bad decode_debug_key(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto arr = req_array(o, "keys", 1, 3);
  const auto hold = opt_int(o, "hold_ms", 0, 5000);
  if (const FieldPath* bad = first_bad(arr, hold)) return some(*bad);
  DebugKey& k = out.emplace<DebugKey>();
  for (size_t i = 0; i < arr.value.size(); ++i) {
    const JsonVariantConst v = arr.value[i];
    const JsonString s = v.as<JsonString>();
    const auto idx = v.is<JsonString>() ? key_index(s.c_str(), s.size()) : Opt<uint8_t>{};
    if (!idx.has) return some(path_index(path_key(o.base, "keys"), i));
    for (size_t j = 0; j < i; ++j) {
      if (k.keys[j] == idx.value) return some(path_key(o.base, "keys"));  // uniqueItems
    }
    k.keys[i] = idx.value;
  }
  k.n = static_cast<uint8_t>(arr.value.size());
  k.hold_ms = hold.value.has ? some(static_cast<uint16_t>(hold.value.value)) : Opt<uint16_t>{};
  return {};
}

Bad decode_debug_lang(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto lang = req_enum(o, "lang", kLangCodes, 2);
  if (!lang.ok) return some(lang.error);
  out.emplace<DebugLang>().lang = static_cast<Lang>(lang.value);
  return {};
}

Bad decode_debug_state(const Obj& o, Body& out) {
  if (has_key(o.o, "re")) return debug_summary(o, out);
  out.emplace<DebugState>();
  return {};
}

void encode_debug(const Body& body, JsonObject out) {
  if (const auto* s = std::get_if<DebugState>(&body)) {
    if (s->summary) encode_summary(*s, out);
    return;
  }
  if (const auto* l = std::get_if<DebugLang>(&body)) {
    out["lang"] = wire_name(kLangCodes, l->lang);
    return;
  }
  const auto* k = std::get_if<DebugKey>(&body);
  if (!k) return;
  JsonArray keys = out["keys"].to<JsonArray>();
  for (uint8_t i = 0; i < k->n; ++i) keys.add(key_name(k->keys[i]));
  if (k->hold_ms.has) out["hold_ms"] = k->hold_ms.value;
}

}  // namespace proto
}  // namespace cpa

#endif  // CPA_TEST_HOOKS
