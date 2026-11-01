// Test support: read protocol/ files, decode line_b64, build canonical lines and
// compare JSON semantically. Host-only code (std::string is fine here).
#pragma once

#include <ArduinoJson.h>
#include <dirent.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef CPA_PROTOCOL_DIR
#error "CPA_PROTOCOL_DIR must point at the repository's protocol/ directory"
#endif

namespace testsupport {

inline std::string protocol_path(const std::string& rel) { return std::string(CPA_PROTOCOL_DIR) + "/" + rel; }

inline std::string read_file(const std::string& path) {
  std::string out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return out;
  char buf[4096];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

inline std::vector<std::string> list_dir(const std::string& path, const std::string& suffix) {
  std::vector<std::string> names;
  DIR* d = opendir(path.c_str());
  if (!d) return names;
  while (dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      names.push_back(name);
    }
  }
  closedir(d);
  std::sort(names.begin(), names.end());
  return names;
}

inline bool load_json(const std::string& path, JsonDocument& doc) {
  const std::string text = read_file(path);
  return !text.empty() && !deserializeJson(doc, text);
}

inline std::string b64_decode(const char* in) {
  static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  uint32_t acc = 0;
  int bits = 0;
  for (const char* p = in; *p && *p != '='; ++p) {
    const auto pos = alphabet.find(*p);
    if (pos == std::string::npos) continue;
    acc = (acc << 6) | static_cast<uint32_t>(pos);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((acc >> bits) & 0xFF));
    }
  }
  return out;
}

// Python's json.dumps(msg, ensure_ascii=False, separators=(",", ":")): ArduinoJson's
// own serializer rounds doubles, so floats use the shortest round-trip form here.
inline void py_string(JsonString s, std::string& out) {
  out += '"';
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s.c_str()[i]);
    const char* esc = c == '"' ? "\\\"" : c == '\\' ? "\\\\" : c == '\n' ? "\\n" : c == '\r' ? "\\r"
                    : c == '\t' ? "\\t" : c == '\b' ? "\\b" : c == '\f' ? "\\f" : nullptr;
    if (esc) {
      out += esc;
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
  out += '"';
}

inline std::string py_double(double d) {
  char buf[40];
  for (int p = 1; p <= 17; ++p) {
    std::snprintf(buf, sizeof buf, "%.*g", p, d);
    if (std::strtod(buf, nullptr) == d) break;
  }
  return buf;
}

inline void py_dumps(JsonVariantConst v, std::string& out) {
  if (v.isNull()) {
    out += "null";
  } else if (v.is<bool>()) {
    out += v.as<bool>() ? "true" : "false";
  } else if (v.is<int64_t>()) {
    out += std::to_string(v.as<int64_t>());
  } else if (v.is<double>()) {
    out += py_double(v.as<double>());
  } else if (v.is<JsonString>()) {
    py_string(v.as<JsonString>(), out);
  } else if (v.is<JsonArrayConst>()) {
    out += '[';
    bool first = true;
    for (JsonVariantConst item : v.as<JsonArrayConst>()) {
      out += first ? "" : ",";
      first = false;
      py_dumps(item, out);
    }
    out += ']';
  } else {
    out += '{';
    bool first = true;
    for (JsonPairConst kv : v.as<JsonObjectConst>()) {
      out += first ? "" : ",";
      first = false;
      py_string(kv.key(), out);
      out += ':';
      py_dumps(kv.value(), out);
    }
    out += '}';
  }
}

inline std::string canonical_line(JsonVariantConst msg) {
  std::string out;
  py_dumps(msg, out);
  return out + "\n";
}

// Raw bytes of a vector case: line_b64 when present, else the canonical line of msg.
inline std::string case_line(JsonObjectConst c) {
  if (c["line_b64"].is<const char*>()) return b64_decode(c["line_b64"]);
  return canonical_line(c["msg"]);
}

inline bool is_absent(JsonVariantConst v) { return v.isNull(); }

inline bool starts_with(JsonString s, const char* prefix) {
  const std::string str(s.c_str(), s.size());
  return str.rfind(prefix, 0) == 0;
}

// Semantic equality: null == absent; keys starting with "x_" (unknown future fields
// in the vectors) are ignored, as receivers ignore unknown fields (05 §6.2).
inline bool json_equal(JsonVariantConst want, JsonVariantConst got, std::string& where);

inline bool object_equal(JsonObjectConst want, JsonObjectConst got, std::string& where) {
  for (JsonPairConst kv : want) {
    if (starts_with(kv.key(), "x_")) continue;
    if (!json_equal(kv.value(), got[kv.key()], where)) {
      where = std::string(kv.key().c_str()) + "." + where;
      return false;
    }
  }
  for (JsonPairConst kv : got) {
    if (is_absent(kv.value()) || !is_absent(want[kv.key()])) continue;
    where = std::string(kv.key().c_str()) + " (unexpected)";
    return false;
  }
  return true;
}

inline bool json_equal(JsonVariantConst want, JsonVariantConst got, std::string& where) {
  if (is_absent(want) || is_absent(got)) return is_absent(want) == is_absent(got);
  if (want.is<JsonObjectConst>()) return got.is<JsonObjectConst>() && object_equal(want, got, where);
  if (want.is<JsonArrayConst>()) {
    JsonArrayConst a = want, b = got;
    if (!got.is<JsonArrayConst>() || a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (!json_equal(a[i], b[i], where)) {
        where = "[" + std::to_string(i) + "]" + where;
        return false;
      }
    }
    return true;
  }
  if (want.is<bool>()) return got.is<bool>() && want.as<bool>() == got.as<bool>();
  if (want.is<int64_t>()) return got.is<int64_t>() && want.as<int64_t>() == got.as<int64_t>();
  if (want.is<JsonString>()) {
    JsonString a = want, b = got;
    return got.is<JsonString>() && a.size() == b.size() && std::equal(a.c_str(), a.c_str() + a.size(), b.c_str());
  }
  return false;
}

}  // namespace testsupport
