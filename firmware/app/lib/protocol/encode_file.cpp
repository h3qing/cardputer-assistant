// Encoders of the v0.3 recording transfer (docs/11 §5.2-§5.3). file.data has its own
// line builder: the 1840-character base64 is written straight into the line, so no
// second buffer of that size is needed on the loop stack or in static RAM.
#include <cinttypes>
#include <cstdio>

#include "b64.h"
#include "codec_internal.h"

namespace cpa {
namespace proto {
namespace {

void put_text(JsonObject out, const char* key, const char* s, size_t n) { out[key] = JsonString(s, n); }

void put_at(JsonObject item, const Opt<RecAt>& at) {
  if (!at.has) {
    item["at"] = nullptr;
    return;
  }
  JsonObject a = item["at"].to<JsonObject>();
  a["up_ms"] = at.value.up_ms;
  if (at.value.wall_ms.has) {
    a["wall_ms"] = at.value.wall_ms.value;
  } else {
    a["wall_ms"] = nullptr;
  }
}

void put_item_optional(const FileItem& it, JsonObject o) {
  if (it.project.has) put_text(o, "project", it.project.value.data(), it.project.value.size());
  if (it.slot.has) o["slot"] = it.slot.value;
  if (it.has_stars) {
    JsonArray stars = o["stars"].to<JsonArray>();
    for (uint8_t i = 0; i < it.n_stars; ++i) stars.add(it.stars[i]);
  }
  if (it.lead_ms.has) o["lead_ms"] = it.lead_ms.value;
  if (it.consent) o["consent"] = "ack";
  if (it.rcv.has) o["rcv"] = it.rcv.value;
  if (it.dest.has) put_text(o, "dest", it.dest.value.data(), it.dest.value.size());  // v0.4.1
}

void put_item(const FileItem& it, JsonObject o) {
  put_text(o, "id", it.id.data(), it.id.size());
  o["kind"] = "rec";
  o["size"] = it.size;
  if (it.sha256.has) put_text(o, "sha256", it.sha256.value.data(), it.sha256.value.size());
  put_text(o, "rec", it.rec.data(), it.rec.size());
  o["seg"] = it.seg;
  o["last"] = it.last;
  o["mode"] = wire_name(kRecModeNames, it.mode);
  o["dur_ms"] = it.dur_ms;
  put_at(o, it.at);
  put_item_optional(it, o);
}

}  // namespace

void put_files(JsonObject out, const Opt<FilesInfo>& files) {
  if (!files.has) return;
  JsonObject f = out["files"].to<JsonObject>();
  f["n"] = files.value.n;
  f["rev"] = files.value.rev;
  f["live"] = files.value.live;
}

void encode_file(const Body& body, JsonObject out) {
  if (const auto* l = std::get_if<FileList>(&body)) {
    if (l->after.has) put_text(out, "after", l->after.value.data(), l->after.value.size());
    if (l->max.has) out["max"] = l->max.value;
    return;
  }
  if (const auto* items = std::get_if<FileItems>(&body)) {
    JsonArray arr = out["items"].to<JsonArray>();
    for (uint8_t i = 0; i < items->n; ++i) put_item(items->items[i], arr.add<JsonObject>());
    out["more"] = items->more;
    return;
  }
  if (const auto* g = std::get_if<FileGet>(&body)) {
    put_text(out, "fid", g->fid.data(), g->fid.size());
    out["off"] = g->off;
    out["len"] = g->len;
    return;
  }
  if (const auto* d = std::get_if<FileDel>(&body)) {
    put_text(out, "fid", d->fid.data(), d->fid.size());
    out["size"] = d->size;
    if (d->sha256.has) put_text(out, "sha256", d->sha256.value.data(), d->sha256.value.size());
  }
}

#if CPA_TEST_HOOKS
void encode_debug_rec(const Body& body, JsonObject out) {
  if (const auto* r = std::get_if<DebugRec>(&body)) out["seg_ms"] = r->seg_ms;
}

void encode_debug_mic(const Body& body, JsonObject out) {
  const auto* m = std::get_if<DebugMic>(&body);
  if (!m) return;
  out["pga"] = m->pga;
  out["vol"] = m->vol;
  out["mag"] = m->mag;
}

void encode_debug_codec(const Body& body, JsonObject out) {
  if (const auto* c = std::get_if<DebugCodec>(&body)) out["warm"] = c->warm;
}

void put_debug_rec_state(JsonObject out, const Opt<DebugRecState>& rec) {
  if (!rec.has) return;
  JsonObject r = out["rec"].to<JsonObject>();
  r["live"] = rec.value.live;
  if (rec.value.mode.has) {
    r["mode"] = wire_name(kRecModeNames, rec.value.mode.value);
  } else {
    r["mode"] = nullptr;
  }
  if (rec.value.seg.has) {
    r["seg"] = rec.value.seg.value;
  } else {
    r["seg"] = nullptr;
  }
  r["files_n"] = rec.value.files_n;
  r["files_rev"] = rec.value.files_rev;
  if (rec.value.ring_high.has) r["ring_high"] = rec.value.ring_high.value;
  if (rec.value.mic_missing.has) r["mic_missing"] = rec.value.mic_missing.value;
  if (rec.value.rot_ms_max.has) r["rot_ms_max"] = rec.value.rot_ms_max.value;
  if (rec.value.heap_free.has) r["heap_free"] = rec.value.heap_free.value;
  if (rec.value.heap_block.has) r["heap_block"] = rec.value.heap_block.value;
}
#endif

// {"v":0,"t":"file.data","id":..,"re":..,"fid":..,"off":..,"n":..,"size":..,"crc":..[,"b64":".."]}\n
size_t file_data_line(const Message& m, const FileData& d, char* buf, size_t cap) {
  const int head = std::snprintf(buf, cap,
                                 "{\"v\":0,\"t\":\"file.data\",\"id\":\"%s\",\"re\":\"%s\",\"fid\":\"%s\",\"off\":%" PRIu32
                                 ",\"n\":%u,\"size\":%" PRIu32 ",\"crc\":%" PRIu32,
                                 m.id.c_str(), m.re.value.c_str(), d.fid.c_str(), d.off, static_cast<unsigned>(d.n),
                                 d.size, d.crc);
  const size_t b64 = d.n > 0 ? 4 * ((static_cast<size_t>(d.n) + 2) / 3) + 9 : 0;  // ,"b64":"" plus the text
  if (head < 0 || static_cast<size_t>(head) + b64 + 2 > cap - 1) return 0;
  size_t at = static_cast<size_t>(head);
  if (d.n > 0) {
    const char key[] = ",\"b64\":\"";
    for (const char* k = key; *k; ++k) buf[at++] = *k;
    at += b64_encode(d.bytes, d.n, buf + at);
    buf[at++] = '"';
  }
  buf[at++] = '}';
  buf[at++] = '\n';
  return at;
}

}  // namespace proto
}  // namespace cpa
