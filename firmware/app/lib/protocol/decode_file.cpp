// Decoders of the v0.3 recording transfer (docs/11 §5.2-§5.3; protocol/schema/v0
// file.*.schema.json, common.schema.json fileItem / files). The device decodes the
// requests; the replies are decoded by the contract test against the vectors.
#include "b64.h"
#include "codec_internal.h"
#include "mic_gain.h"

namespace cpa {
namespace proto {

const char* const kRecModeNames[2] = {"memo", "meeting"};

namespace {

const char* const kRecKindNames[1] = {"rec"};
const char* const kConsentNames[1] = {"ack"};

constexpr int64_t kSizeMin = limits::kWavHeaderBytes;
constexpr int64_t kDurMax = limits::kSegMsMax;

// A key that must be present but may be null ("at", "rec.mode").
Bad present(const Obj& o, const char* key) { return has_key(o.o, key) ? Bad{} : some(path_key(o.base, key)); }

Bad item_at(const Obj& o, FileItem& it) {
  const Bad missing = present(o, "at");
  if (missing.has || o.o["at"].isNull()) return missing;
  const auto at = req_obj(o, "at");
  if (!at.ok) return some(at.error);
  const auto up = req_int(at.value, "up_ms", 0, limits::kU32Max);
  const auto wall = opt_int(at.value, "wall_ms", limits::kEpochMsMin, limits::kJsonSafeIntMax);
  if (const FieldPath* bad = first_bad(up, wall)) return some(*bad);
  RecAt a;
  a.up_ms = static_cast<uint32_t>(up.value);
  a.wall_ms = wall.value;
  it.at = some(a);
  return {};
}

Bad item_stars(const Obj& o, FileItem& it) {
  if (o.o["stars"].isNull()) return {};
  const auto arr = req_array(o, "stars", 0, kFileMarksMax);
  if (!arr.ok) return some(arr.error);
  for (size_t i = 0; i < arr.value.size(); ++i) {
    const auto v = as_integer(arr.value[i]);
    if (!v.has || v.value < 0 || v.value >= kDurMax) return some(path_index(path_key(o.base, "stars"), i));
    it.stars[i] = static_cast<uint32_t>(v.value);
  }
  it.has_stars = true;
  it.n_stars = static_cast<uint8_t>(arr.value.size());
  return {};
}

// Required fields of a fileItem, in schema order. Split from its optional fields so the
// field results of each half share a smaller frame (stack gate, FRAME_MAX).
__attribute__((noinline)) Bad item_required(const Obj& o, FileItem& it) {
  const auto id = req_text(o, "id", is_monoid_text);
  const auto kind = req_enum(o, "kind", kRecKindNames, 1);
  const auto size = req_int(o, "size", kSizeMin, limits::kFileSizeMax);
  const auto sha = opt_text(o, "sha256", is_sha256);
  const auto rec = req_text(o, "rec", is_monoid_text);
  const auto seg = req_int(o, "seg", 0, limits::kFileSegMax);
  const auto last = req_bool(o, "last");
  const auto mode = req_enum(o, "mode", kRecModeNames, 2);
  const auto dur = req_int(o, "dur_ms", 0, kDurMax);
  if (const FieldPath* bad = first_bad(id, kind, size, sha, rec, seg, last, mode, dur)) return some(*bad);
  it.id = fixed<kMonoIdLen>(id.value);
  it.size = static_cast<uint32_t>(size.value);
  it.sha256 = fixed<64>(sha.value);
  it.rec = fixed<kMonoIdLen>(rec.value);
  it.seg = static_cast<uint16_t>(seg.value);
  it.last = last.value;
  it.mode = static_cast<RecMode>(mode.value);
  it.dur_ms = static_cast<uint32_t>(dur.value);
  return item_at(o, it);
}

__attribute__((noinline)) Bad item_optional(const Obj& o, FileItem& it) {
  const auto project = opt_text(o, "project", is_project_key);
  const auto slot = opt_int(o, "slot", kSlotMin, kSlotMax);
  const auto lead = opt_int(o, "lead_ms", 0, limits::kLeadMsMax);
  const auto consent = opt_enum(o, "consent", kConsentNames, 1);
  const auto rcv = opt_bool(o, "rcv");
  const auto dest = opt_text(o, "dest", is_dest);  // v0.4.1
  if (const FieldPath* bad = first_bad(project, slot, lead, consent, rcv, dest)) return some(*bad);
  it.project = fixed<12>(project.value);
  if (slot.value.has) it.slot = some(static_cast<uint8_t>(slot.value.value));
  if (lead.value.has) it.lead_ms = some(static_cast<uint16_t>(lead.value.value));
  it.consent = consent.value.has;
  it.rcv = rcv.value;
  it.dest = fixed<24>(dest.value);
  return item_stars(o, it);
}

Bad file_item(const Obj& o, FileItem& it) {
  const Bad req = item_required(o, it);
  return req.has ? req : item_optional(o, it);
}

}  // namespace

Dec<Opt<FilesInfo>> opt_files(const Obj& o) {
  if (o.o["files"].isNull()) return Dec<Opt<FilesInfo>>::success({});
  const auto f = req_obj(o, "files");
  if (!f.ok) return fail_at<Opt<FilesInfo>>(f.error);
  const auto n = req_int(f.value, "n", 0, limits::kFilePendingMax);
  const auto rev = req_int(f.value, "rev", 0, limits::kU32Max);
  const auto live = req_bool(f.value, "live");
  if (const FieldPath* bad = first_bad(n, rev, live)) return fail_at<Opt<FilesInfo>>(*bad);
  FilesInfo info;
  info.n = static_cast<uint16_t>(n.value);
  info.rev = static_cast<uint32_t>(rev.value);
  info.live = live.value;
  return Dec<Opt<FilesInfo>>::success(some(info));
}

Bad decode_file_list(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto after = opt_text(o, "after", is_monoid_text);
  const auto max = opt_int(o, "max", 1, limits::kFileListMax);
  if (const FieldPath* bad = first_bad(after, max)) return some(*bad);
  FileList& l = out.emplace<FileList>();
  l.after = fixed<kMonoIdLen>(after.value);
  if (max.value.has) l.max = some(static_cast<uint8_t>(max.value.value));
  return {};
}

Bad decode_file_items(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto arr = req_array(o, "items", 0, limits::kFileListMax);
  const auto more = req_bool(o, "more");
  if (const FieldPath* bad = first_bad(arr, more)) return some(*bad);
  FileItems& items = out.emplace<FileItems>();
  for (size_t i = 0; i < arr.value.size(); ++i) {
    const auto item = item_obj(o, "items", arr.value, i);
    if (!item.ok) return some(item.error);
    const Bad bad = file_item(item.value, items.items[i]);
    if (bad.has) return bad;
  }
  items.n = static_cast<uint8_t>(arr.value.size());
  items.more = more.value;
  return {};
}

Bad decode_file_get(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto fid = req_text(o, "fid", is_monoid_text);
  const auto off = req_int(o, "off", 0, limits::kU32Max);
  const auto len = req_int(o, "len", 1, limits::kFileGetB64Max);
  if (const FieldPath* bad = first_bad(fid, off, len)) return some(*bad);
  FileGet& g = out.emplace<FileGet>();
  g.fid = fixed<kMonoIdLen>(fid.value);
  g.off = static_cast<uint32_t>(off.value);
  g.len = static_cast<uint16_t>(len.value);
  return {};
}

// x-b64Len: b64 present exactly when n > 0, and it decodes to exactly n bytes.
Bad decode_file_data(const Obj& o, Body& out) {
  const auto env = reply_env(o);
  if (env.has) return env;
  const auto fid = req_text(o, "fid", is_monoid_text);
  const auto off = req_int(o, "off", 0, limits::kU32Max);
  const auto n = req_int(o, "n", 0, limits::kFileGetB64Max);
  const auto size = req_int(o, "size", kSizeMin, limits::kFileSizeMax);
  const auto crc = req_int(o, "crc", 0, limits::kU32Max);
  const auto b64 = opt_text(o, "b64", is_b64);
  if (const FieldPath* bad = first_bad(fid, off, n, size, crc, b64)) return some(*bad);
  const FieldPath at_b64 = path_key(o.base, "b64");
  if (b64.value.has != (n.value > 0)) return some(at_b64);
  FileData& d = out.emplace<FileData>();
  if (b64.value.has) {
    const JsonString s = b64.value.value;
    if (b64_decode(s.c_str(), s.size(), d.bytes, sizeof d.bytes) != n.value) return some(at_b64);
  }
  d.fid = fixed<kMonoIdLen>(fid.value);
  d.off = static_cast<uint32_t>(off.value);
  d.n = static_cast<uint16_t>(n.value);
  d.size = static_cast<uint32_t>(size.value);
  d.crc = static_cast<uint32_t>(crc.value);
  return {};
}

Bad decode_file_del(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto fid = req_text(o, "fid", is_monoid_text);
  const auto size = req_int(o, "size", kSizeMin, limits::kFileSizeMax);
  const auto sha = opt_text(o, "sha256", is_sha256);
  if (const FieldPath* bad = first_bad(fid, size, sha)) return some(*bad);
  FileDel& d = out.emplace<FileDel>();
  d.fid = fixed<kMonoIdLen>(fid.value);
  d.size = static_cast<uint32_t>(size.value);
  d.sha256 = fixed<64>(sha.value);
  return {};
}

#if CPA_TEST_HOOKS
Bad decode_debug_rec(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto seg = req_int(o, "seg_ms", limits::kSegMsMin, limits::kSegMsMax);
  if (!seg.ok) return some(seg.error);
  out.emplace<DebugRec>().seg_ms = static_cast<uint32_t>(seg.value);
  return {};
}

// v0.3.1: the microphone gain for the next recordings (pga 0-10, vol 0-255, mag 1-64).
Bad decode_debug_mic(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto pga = req_int(o, "pga", 0, kMicPgaMax);
  const auto vol = req_int(o, "vol", 0, 255);
  const auto mag = req_int(o, "mag", kMicMagMin, kMicMagMax);
  if (const FieldPath* bad = first_bad(pga, vol, mag)) return some(*bad);
  DebugMic& m = out.emplace<DebugMic>();
  m.pga = static_cast<uint8_t>(pga.value);
  m.vol = static_cast<uint8_t>(vol.value);
  m.mag = static_cast<uint8_t>(mag.value);
  return {};
}

// v0.3.2 (issue #31): the codec as a power-on leaves it; warm: the boot's warm-up after it.
Bad decode_debug_codec(const Obj& o, Body& out) {
  const auto env = request_env(o);
  if (env.has) return env;
  const auto warm = req_bool(o, "warm");
  if (!warm.ok) return some(warm.error);
  out.emplace<DebugCodec>().warm = warm.value;
  return {};
}

namespace {

template <class T>
Opt<T> narrow(const Opt<int64_t>& v) {
  return v.has ? some(static_cast<T>(v.value)) : Opt<T>{};
}

// The optional health counters of debug.state.rec (absent or null: not measured).
Bad rec_health(const Obj& ro, DebugRecState& s) {
  const auto ring = opt_int(ro, "ring_high", 0, limits::kCountMax);
  const auto missing = opt_int(ro, "mic_missing", 0, limits::kU32Max);
  const auto rot = opt_int(ro, "rot_ms_max", 0, limits::kU32Max);
  const auto heap = opt_int(ro, "heap_free", 0, limits::kU32Max);
  const auto block = opt_int(ro, "heap_block", 0, limits::kU32Max);
  if (const FieldPath* bad = first_bad(ring, missing, rot, heap, block)) return some(*bad);
  s.ring_high = narrow<uint16_t>(ring.value);
  s.mic_missing = narrow<uint32_t>(missing.value);
  s.rot_ms_max = narrow<uint32_t>(rot.value);
  s.heap_free = narrow<uint32_t>(heap.value);
  s.heap_block = narrow<uint32_t>(block.value);
  return {};
}

}  // namespace

Bad decode_debug_rec_state(const Obj& o, Opt<DebugRecState>& out) {
  if (o.o["rec"].isNull()) return {};
  const auto r = req_obj(o, "rec");
  if (!r.ok) return some(r.error);
  const Obj& ro = r.value;
  const auto live = req_bool(ro, "live");
  const Bad mode_key = present(ro, "mode");
  const auto mode = opt_enum(ro, "mode", kRecModeNames, 2);
  const Bad seg_key = present(ro, "seg");
  const auto seg = opt_int(ro, "seg", 0, limits::kFileSegMax);
  const auto n = req_int(ro, "files_n", 0, limits::kFilePendingMax);
  const auto rev = req_int(ro, "files_rev", 0, limits::kU32Max);
  if (!live.ok) return some(live.error);
  if (mode_key.has) return mode_key;
  if (const FieldPath* bad = first_bad(mode)) return some(*bad);
  if (seg_key.has) return seg_key;
  if (const FieldPath* bad = first_bad(seg, n, rev)) return some(*bad);
  DebugRecState s;
  s.live = live.value;
  if (mode.value.has) s.mode = some(static_cast<RecMode>(mode.value.value));
  if (seg.value.has) s.seg = some(static_cast<uint16_t>(seg.value.value));
  s.files_n = static_cast<uint16_t>(n.value);
  s.files_rev = static_cast<uint32_t>(rev.value);
  const Bad health = rec_health(ro, s);
  if (health.has) return health;
  out = some(s);
  return {};
}
#endif

}  // namespace proto
}  // namespace cpa
