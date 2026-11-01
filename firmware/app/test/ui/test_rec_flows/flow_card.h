// The card a recording flow starts with (test_rec_flows): the segments of the first
// files.n items the flow lists, each a .W with the bytes of every file.data the flow shows
// for it (zeros elsewhere) and a .M carrying the item's fields. The flows' ids carry
// device-valid epochs (05 §5.9; protocol/tools/test_file_vectors.py), so they are used as is.
#pragma once

#include <ArduinoJson.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "core/support/fake_rec_file_port.h"
#include "core/support/vectors.h"
#include "rec_meta.h"
#include "rec_paths.h"
#include "wav_header.h"

namespace flowcard {

using namespace cpa;

// The items of the device's file.items lines, in order of first appearance.
inline std::vector<JsonObjectConst> flow_items(JsonArrayConst steps) {
  std::vector<JsonObjectConst> out;
  std::set<std::string> seen;
  for (JsonObjectConst s : steps) {
    if (s["from"] != "device" || s["msg"]["t"] != "file.items") continue;
    for (JsonObjectConst it : s["msg"]["items"].as<JsonArrayConst>()) {
      if (seen.insert(it["id"].as<std::string>()).second) out.push_back(it);
    }
  }
  return out;
}

// files.n of the first hello or pong the device sends: the segments on the card at t=0.
inline size_t initial_files(JsonArrayConst steps) {
  for (JsonObjectConst s : steps) {
    if (s["from"] == "device" && s["msg"]["files"].is<JsonObjectConst>()) return s["msg"]["files"]["n"];
  }
  return 0;
}

// The whole .W of an item: our header, then the bytes the flow's file.data lines show.
inline std::string wav_of(JsonObjectConst item, JsonArrayConst steps) {
  const uint32_t size = item["size"];
  std::string w(size, '\0');
  const rec::WavHeader h = rec::wav_header(size - rec::kHeaderBytes);
  w.replace(0, rec::kHeaderBytes, reinterpret_cast<const char*>(h.b), rec::kHeaderBytes);
  for (JsonObjectConst s : steps) {
    JsonObjectConst m = s["msg"];
    if (s["from"] != "device" || m["t"] != "file.data" || m["fid"] != item["id"]) continue;
    const std::string bytes = testsupport::b64_decode(m["b64"]);
    w.replace(m["off"].as<uint32_t>(), bytes.size(), bytes);
  }
  return w;
}

inline MonoId id_of(JsonVariantConst v) {
  const std::string s = v.as<std::string>();
  return mono_parse(s.c_str(), s.size()).value;
}

inline rec::RecMeta meta_of(JsonObjectConst it) {
  rec::RecMeta m;
  m.id = id_of(it["id"]);
  m.rec = id_of(it["rec"]);
  m.seg = it["seg"];
  m.last = it["last"];
  m.mode = it["mode"] == "memo" ? RecMode::Memo : RecMode::Meeting;
  m.bytes = it["size"].as<uint32_t>() - rec::kHeaderBytes;
  m.sha256 = some(Sha256Hex::from(it["sha256"].as<const char*>()));
  proto::RecAt at;
  at.up_ms = it["at"]["up_ms"];
  if (!it["at"]["wall_ms"].isNull()) at.wall_ms = some(it["at"]["wall_ms"].as<int64_t>());
  m.at = some(at);
  if (it["project"].is<const char*>()) m.project = some(ProjectKey::from(it["project"].as<const char*>()));
  m.slot = it["slot"] | 0;
  if (!it["lead_ms"].isNull()) m.lead_ms = some(it["lead_ms"].as<uint16_t>());
  m.consent = it["consent"] == "ack";
  for (JsonVariantConst star : it["stars"].as<JsonArrayConst>()) m.stars[m.n_stars++] = star.as<uint32_t>();
  m.fw = FixedStr<31>::from("cpa-fw/0.3.0");
  return m;
}

inline void seed(testsupport::FakeRecFilePort& fs, JsonObjectConst item, JsonArrayConst steps) {
  const MonoId id = id_of(item["id"]);
  fs.put(rec::rec_wav_path("/cpa", id).c_str(), wav_of(item, steps));
  const rec::MetaText meta = rec::rec_meta_encode(meta_of(item));
  fs.put(rec::rec_meta_path("/cpa", id).c_str(), std::string(meta.c_str(), meta.size()));
}

}  // namespace flowcard
